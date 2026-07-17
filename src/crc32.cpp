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

[[nodiscard]] std::uint32_t crc32Scalar(const unsigned char *data,
                                        std::size_t length) noexcept {
	return crc32UpdateScalar(CRC32_INITIAL, data, length) ^ CRC32_INITIAL;
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

[[nodiscard]] __attribute__((target("sse2,pclmul"))) std::uint32_t
crc32Pclmul(const unsigned char *data, std::size_t length) noexcept {
	if (length < CRC32_PCLMUL_MIN_BYTES) {
		return crc32Scalar(data, length);
	}

	const std::size_t folded_length = length & ~std::size_t{15};
	__m128i state =
	    _mm_xor_si128(loadBlock128(data), _mm_set_epi64x(0, CRC32_INITIAL));

	// Constants generated for reflected IEEE CRC-32.  The vector order is
	// chosen so imm8 0x00 multiplies low64 by HI64_TERMS and imm8 0x11
	// multiplies high64 by LO64_TERMS.
	const __m128i fold128_constants =
	    _mm_set_epi64x(0x00000000CCAA009EULL, 0x00000000AE689191ULL);

	for (std::size_t offset = 16; offset < folded_length; offset += 16) {
		state = _mm_xor_si128(foldAcross128Bits(state, fold128_constants),
		                      loadBlock128(data + offset));
	}

	// Reduce the 128-bit folded state to a 32-bit CRC. Rather than a Barrett
	// reduction, we exploit the folding invariant: the running state is
	// congruent (mod the CRC polynomial) to the data consumed so far, so the
	// CRC of those 16 bytes equals the CRC of that data. We therefore store the
	// state and run the scalar table CRC over it (init 0, no final inversion).
	// Correct because the initial inversion was already injected into the first
	// dword above; the final inversion is applied once at the end. Slower than
	// Barrett but only 16 bytes, so the cost is negligible.
	alignas(16) std::array<unsigned char, 16> folded{};
	_mm_store_si128(reinterpret_cast<__m128i*>(folded.data()), state);

	std::uint32_t crc = crc32UpdateScalar(0, folded.data(), folded.size());
	crc = crc32UpdateScalar(crc, data + folded_length, length - folded_length);
	return crc ^ CRC32_INITIAL;
}

#endif

using Crc32Impl = std::uint32_t (*)(const unsigned char *, std::size_t) noexcept;

[[nodiscard]] Crc32Impl resolveCrc32Impl() noexcept {
#if PDVZIP_HAS_X86_PCLMUL
	if (cpuHasPclmul()) {
		return crc32Pclmul;
	}
#endif
	return crc32Scalar;
}

} // namespace

unsigned lodepng_crc32(const unsigned char *data, std::size_t length) {
	static const Crc32Impl impl = resolveCrc32Impl();
	return impl(data, length);
}
