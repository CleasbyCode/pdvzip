#include "pdvzip.h"

#include <array>
#include <cstddef>
#include <cstdint>

#if !defined(PDVZIP_CRC32_DISABLE_PCLMUL) &&                              \
    (defined(__x86_64__) || defined(__i386__)) &&                         \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <immintrin.h>
#define PDVZIP_HAS_X86_PCLMUL 1
#else
#define PDVZIP_HAS_X86_PCLMUL 0
#endif

namespace {

constexpr std::uint32_t CRC32_POLY = 0xEDB88320U;
constexpr std::uint32_t CRC32_INITIAL = 0xFFFFFFFFU;
constexpr std::size_t CRC32_PCLMUL_MIN_BYTES = 64;

[[nodiscard]] constexpr std::uint32_t makeCrcTableEntry(std::uint32_t value) {
	for (int bit = 0; bit < 8; ++bit) {
		value = (value & 1U) != 0U ? (value >> 1U) ^ CRC32_POLY : value >> 1U;
	}
	return value;
}

[[nodiscard]] constexpr auto makeCrcTables() {
	std::array<std::array<std::uint32_t, 256>, 8> tables{};
	for (std::uint32_t i = 0; i < 256; ++i) {
		tables[0][i] = makeCrcTableEntry(i);
	}
	for (std::size_t table = 1; table < tables.size(); ++table) {
		for (std::size_t i = 0; i < tables[table].size(); ++i) {
			const std::uint32_t previous = tables[table - 1][i];
			tables[table][i] = (previous >> 8U) ^ tables[0][previous & 0xFFU];
		}
	}
	return tables;
}

alignas(64) constexpr auto CRC_TABLES = makeCrcTables();

[[nodiscard]] std::uint32_t crc32UpdateScalar(std::uint32_t crc,
                                              const unsigned char *data,
                                              std::size_t length) noexcept {
	while (length >= 8) {
		crc = CRC_TABLES[7][(data[0] ^ (crc & 0xFFU))] ^
		      CRC_TABLES[6][(data[1] ^ ((crc >> 8U) & 0xFFU))] ^
		      CRC_TABLES[5][(data[2] ^ ((crc >> 16U) & 0xFFU))] ^
		      CRC_TABLES[4][(data[3] ^ ((crc >> 24U) & 0xFFU))] ^
		      CRC_TABLES[3][data[4]] ^
		      CRC_TABLES[2][data[5]] ^
		      CRC_TABLES[1][data[6]] ^
		      CRC_TABLES[0][data[7]];
		data += 8;
		length -= 8;
	}

	while (length-- != 0) {
		crc = CRC_TABLES[0][(crc ^ *data++) & 0xFFU] ^ (crc >> 8U);
	}
	return crc;
}

#if PDVZIP_HAS_X86_PCLMUL

[[nodiscard]] bool cpuHasPclmul() noexcept {
	unsigned int eax = 0;
	unsigned int ebx = 0;
	unsigned int ecx = 0;
	unsigned int edx = 0;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
		return false;
	}
	constexpr unsigned int SSE2_BIT = 1U << 26U;
	constexpr unsigned int PCLMULQDQ_BIT = 1U << 1U;
	return (edx & SSE2_BIT) != 0U && (ecx & PCLMULQDQ_BIT) != 0U;
}

[[nodiscard]] __attribute__((target("sse2,pclmul"))) inline __m128i
loadBlock128(const unsigned char *data) noexcept {
	return _mm_loadu_si128(reinterpret_cast<const __m128i *>(data));
}

[[nodiscard]] __attribute__((target("sse2,pclmul"))) inline __m128i
foldAcross128Bits(__m128i state, __m128i constants) noexcept {
	const __m128i folded_low = _mm_clmulepi64_si128(state, constants, 0x00);
	const __m128i folded_high = _mm_clmulepi64_si128(state, constants, 0x11);
	return _mm_xor_si128(folded_low, folded_high);
}

// Continues a CRC from `state`, the running register (the CRC so far,
// inverted). Folds four 128-bit lanes per 64-byte block so the multiplies of
// one block do not wait on each other: one lane is bound by PCLMULQDQ latency.
[[nodiscard]] __attribute__((target("sse2,pclmul"))) std::uint32_t
crc32UpdatePclmul(std::uint32_t state, const unsigned char *data,
                  std::size_t length) noexcept {
	if (length < CRC32_PCLMUL_MIN_BYTES) {
		return crc32UpdateScalar(state, data, length);
	}

	// Constants for reflected IEEE CRC-32: folding a lane forward by D bits
	// multiplies its low 64 bits by x^(D+31) and its high 64 bits by
	// x^(D-33), modulo the polynomial, bit-reflected. imm8 0x00 multiplies
	// the low halves and 0x11 the high halves, so each constant vector is
	// set as (high: x^(D-33), low: x^(D+31)).
	const __m128i fold512_constants =  // D = 512: four lanes, 64 bytes ahead
	    _mm_set_epi64x(0x000000001D9513D7ULL, 0x000000008F352D95ULL);
	const __m128i fold128_constants =  // D = 128: one lane, 16 bytes ahead
	    _mm_set_epi64x(0x00000000CCAA009EULL, 0x00000000AE689191ULL);

	// The running register enters as the first four bytes' XOR mask.
	__m128i lane0 = _mm_xor_si128(loadBlock128(data), _mm_cvtsi32_si128(static_cast<int>(state)));
	__m128i lane1 = loadBlock128(data + 16);
	__m128i lane2 = loadBlock128(data + 32);
	__m128i lane3 = loadBlock128(data + 48);
	std::size_t offset = 64;
	for (; length - offset >= 64; offset += 64) {
		lane0 = _mm_xor_si128(foldAcross128Bits(lane0, fold512_constants), loadBlock128(data + offset));
		lane1 = _mm_xor_si128(foldAcross128Bits(lane1, fold512_constants), loadBlock128(data + offset + 16));
		lane2 = _mm_xor_si128(foldAcross128Bits(lane2, fold512_constants), loadBlock128(data + offset + 32));
		lane3 = _mm_xor_si128(foldAcross128Bits(lane3, fold512_constants), loadBlock128(data + offset + 48));
	}

	// Merge the lanes in data order, then fold any remaining 16-byte blocks.
	__m128i folded_state = _mm_xor_si128(foldAcross128Bits(lane0, fold128_constants), lane1);
	folded_state = _mm_xor_si128(foldAcross128Bits(folded_state, fold128_constants), lane2);
	folded_state = _mm_xor_si128(foldAcross128Bits(folded_state, fold128_constants), lane3);
	for (; length - offset >= 16; offset += 16) {
		folded_state = _mm_xor_si128(foldAcross128Bits(folded_state, fold128_constants),
		                             loadBlock128(data + offset));
	}

	// Reduce the 128-bit folded state to a 32-bit CRC. Rather than a Barrett
	// reduction, we exploit the folding invariant: the folded state is
	// congruent (mod the CRC polynomial) to the data consumed so far, with the
	// running register already injected into its first dword, so the CRC of
	// those 16 bytes from a zero register equals the register after that data.
	// Slower than Barrett but only 16 bytes, so the cost is negligible.
	alignas(16) std::array<unsigned char, 16> folded{};
	_mm_store_si128(reinterpret_cast<__m128i*>(folded.data()), folded_state);

	const std::uint32_t folded_register = crc32UpdateScalar(0, folded.data(), folded.size());
	return crc32UpdateScalar(folded_register, data + offset, length - offset);
}

#endif

// Every implementation takes and returns the running register.
using Crc32Impl = std::uint32_t (*)(std::uint32_t, const unsigned char *, std::size_t) noexcept;

[[nodiscard]] Crc32Impl resolveCrc32Impl() noexcept {
#if PDVZIP_HAS_X86_PCLMUL
	if (cpuHasPclmul()) {
		return crc32UpdatePclmul;
	}
#endif
	return crc32UpdateScalar;
}

} // namespace

std::uint32_t crc32Update(std::uint32_t crc, std::span<const Byte> data) noexcept {
	static const Crc32Impl impl = resolveCrc32Impl();
	return impl(crc ^ CRC32_INITIAL, data.data(), data.size()) ^ CRC32_INITIAL;
}

unsigned lodepng_crc32(const unsigned char *data, std::size_t length) {
	return crc32Update(0, std::span<const Byte>(data, length));
}
