// LodePNG's zlib hooks, backed by the system zlib that pdvzip already links
// for archive verification. LodePNG's own deflate/inflate is not compiled
// (LODEPNG_NO_COMPILE_ZLIB), so every lodepng::State must be configured here.

#include "image_processing_internal.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace {

// LodePNG error codes. Without LodePNG's own zlib compiled in, these are
// passed through unchanged to the caller (and to its error text).
constexpr unsigned LODEPNG_ALLOC_FAILED = 83;
constexpr unsigned LODEPNG_OVER_MAX_OUTPUT_SIZE = 109;
constexpr unsigned LODEPNG_BAD_HEADER_CHECK = 24;
constexpr unsigned LODEPNG_BAD_METHOD_OR_WINDOW = 25;
constexpr unsigned LODEPNG_PRESET_DICTIONARY = 26;
constexpr unsigned LODEPNG_BAD_ADLER32 = 58;
constexpr unsigned LODEPNG_INFLATE_FAILED = 110;
constexpr unsigned LODEPNG_DEFLATE_FAILED = 111;
constexpr std::size_t MIN_GROWTH = 64 * 1024;

// zlib's defaults (level 6). Against LodePNG's built-in deflate this makes
// re-encoded covers smaller in total at about the same speed; level 9 saved
// only a further ~0.2% and was up to four times slower on photographic covers.
constexpr int PNG_ZLIB_LEVEL = Z_DEFAULT_COMPRESSION;
constexpr int PNG_ZLIB_MEM_LEVEL = 8;
constexpr int PNG_ZLIB_STRATEGY = Z_DEFAULT_STRATEGY;

// zlib counts in uInt; feed larger buffers in pieces.
[[nodiscard]] uInt zlibChunk(std::size_t remaining) {
	return static_cast<uInt>(std::min<std::size_t>(remaining, std::numeric_limits<uInt>::max()));
}

// The LodePNG code for an inflate failure, so the common cases keep the
// messages LodePNG's own decoder gave. zlib's messages for these have not
// changed since zlib 1.0 (and zlib-ng keeps them).
[[nodiscard]] unsigned inflateError(int status, const z_stream& stream) {
	if (status == Z_NEED_DICT) return LODEPNG_PRESET_DICTIONARY;
	if (status == Z_DATA_ERROR && stream.msg) {
		const std::string_view message = stream.msg;
		if (message == "incorrect data check") return LODEPNG_BAD_ADLER32;
		if (message == "incorrect header check") return LODEPNG_BAD_HEADER_CHECK;
		if (message == "unknown compression method" || message == "invalid window size") {
			return LODEPNG_BAD_METHOD_OR_WINDOW;
		}
	}
	return LODEPNG_INFLATE_FAILED;
}

// Appends to *out, as LodePNG's built-in decoder does. settings->max_output_size
// (nonzero for every PNG decode) bounds the total: the buffer is sized once to
// one byte past the limit, so a decompression bomb stops there and is reported
// as too large (109) rather than inflated in full.
unsigned inflateWithSystemZlib(
	unsigned char** out,
	std::size_t* outsize,
	const unsigned char* in,
	std::size_t insize,
	const LodePNGDecompressSettings* settings) {

	const std::size_t limit = settings->max_output_size;
	unsigned char* buffer = *out;
	std::size_t size = *outsize;
	std::size_t capacity = size;
	const auto hand_back = [&](unsigned result) {
		*out = buffer;
		*outsize = size;
		return result;
	};
	z_stream stream{};
	if (inflateInit(&stream) != Z_OK) return hand_back(LODEPNG_ALLOC_FAILED);
	struct EndGuard {
		z_stream& stream;
		~EndGuard() { inflateEnd(&stream); }
	} end_guard{stream};

	const unsigned char* next_in = in;
	std::size_t remaining_in = insize;
	for (;;) {
		if (stream.avail_in == 0 && remaining_in != 0) {
			stream.next_in = const_cast<Bytef*>(next_in);
			stream.avail_in = zlibChunk(remaining_in);
			next_in += stream.avail_in;
			remaining_in -= stream.avail_in;
		}
		if (size == capacity) {
			// With a limit, one allocation holds every valid output plus the
			// single byte that proves an oversized one.
			std::size_t wanted = 0;
			if (limit) {
				// Full past the limit (including existing output that already
				// was): refused, never grown again.
				if (capacity > limit) return hand_back(LODEPNG_OVER_MAX_OUTPUT_SIZE);
				if (limit == std::numeric_limits<std::size_t>::max()) return hand_back(LODEPNG_ALLOC_FAILED);
				wanted = limit + 1;
			} else if (capacity > std::numeric_limits<std::size_t>::max() / 2) {
				return hand_back(LODEPNG_ALLOC_FAILED);
			} else {
				wanted = std::max(capacity * 2, MIN_GROWTH);
			}
			auto* grown = static_cast<unsigned char*>(std::realloc(buffer, wanted));
			if (!grown) return hand_back(LODEPNG_ALLOC_FAILED);
			buffer = grown;
			capacity = wanted;
		}

		stream.next_out = buffer + size;
		stream.avail_out = zlibChunk(capacity - size);
		const uInt offered = stream.avail_out;
		const int status = inflate(&stream, Z_NO_FLUSH);
		size += offered - stream.avail_out;

		if (limit && size > limit) return hand_back(LODEPNG_OVER_MAX_OUTPUT_SIZE);
		if (status == Z_STREAM_END) break;
		if (status == Z_BUF_ERROR && stream.avail_in == 0 && remaining_in == 0) {
			return hand_back(LODEPNG_INFLATE_FAILED);  // Truncated stream.
		}
		if (status != Z_OK && status != Z_BUF_ERROR) {
			return hand_back(inflateError(status, stream));
		}
	}

	// LodePNG's built-in decoder read the Adler-32 from the last four input
	// bytes, so data after the stream was always refused; keep that, with a
	// clearer message (a pdvzip output given as the cover ends up here).
	if (stream.avail_in != 0 || remaining_in != 0) {
		return hand_back(image_processing_internal::PNG_ZLIB_TRAILING_DATA_ERROR);
	}
	return hand_back(0);
}

unsigned deflateWithSystemZlib(
	unsigned char** out,
	std::size_t* outsize,
	const unsigned char* in,
	std::size_t insize,
	const LodePNGCompressSettings*) {

	*out = nullptr;
	*outsize = 0;

	z_stream stream{};
	if (deflateInit2(&stream, PNG_ZLIB_LEVEL, Z_DEFLATED, MAX_WBITS,
			PNG_ZLIB_MEM_LEVEL, PNG_ZLIB_STRATEGY) != Z_OK) {
		return LODEPNG_ALLOC_FAILED;
	}
	struct EndGuard {
		z_stream& stream;
		~EndGuard() { deflateEnd(&stream); }
	} end_guard{stream};

	// deflateBound covers the whole stream, so the output never needs to grow.
	const std::size_t bound = deflateBound(&stream, static_cast<uLong>(insize));
	auto* buffer = static_cast<unsigned char*>(std::malloc(bound));
	if (!buffer) return LODEPNG_ALLOC_FAILED;

	const unsigned char* next_in = in;
	std::size_t remaining_in = insize;
	std::size_t size = 0;
	int status = Z_OK;
	do {
		if (stream.avail_in == 0 && remaining_in != 0) {
			stream.next_in = const_cast<Bytef*>(next_in);
			stream.avail_in = zlibChunk(remaining_in);
			next_in += stream.avail_in;
			remaining_in -= stream.avail_in;
		}
		stream.next_out = buffer + size;
		stream.avail_out = zlibChunk(bound - size);
		const uInt offered = stream.avail_out;
		status = deflate(&stream, remaining_in == 0 ? Z_FINISH : Z_NO_FLUSH);
		size += offered - stream.avail_out;
	} while (status == Z_OK);

	if (status != Z_STREAM_END) {
		std::free(buffer);
		return LODEPNG_DEFLATE_FAILED;
	}
	*out = buffer;
	*outsize = size;
	return 0;
}

}  // namespace

namespace image_processing_internal {

void useSystemZlib(lodepng::State& state) {
	state.decoder.zlibsettings.custom_zlib = inflateWithSystemZlib;
	state.encoder.zlibsettings.custom_zlib = deflateWithSystemZlib;
}

}  // namespace image_processing_internal
