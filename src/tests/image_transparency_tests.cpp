// Regression tests for transparency-preserving PNG resampling (review issue 6).
// From the source directory:
/*
g++ -std=c++23 -O1 -I. -DLODEPNG_NO_COMPILE_DISK \
  -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CRC \
  -DLODEPNG_NO_COMPILE_ZLIB \
  tests/image_transparency_tests.cpp image_processing.cpp image_resize.cpp \
  png_zlib.cpp binary_utils.cpp crc32.cpp lodepng/lodepng.cpp -lz \
  -o /tmp/pdvzip-image-transparency-tests
*/

#include "image_processing_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <zlib.h>

namespace {

using Pixel = std::array<Byte, 4>;

void require(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

void checkLodepng(unsigned error) {
	if (error) throw std::runtime_error(lodepng_error_text(error));
}

vBytes encodeTruecolor(const vBytes& pixels, unsigned width, unsigned height,
	LodePNGColorType mode, bool transparent_key = false, Pixel key = {}) {
	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	state.encoder.auto_convert = 0;
	for (LodePNGColorMode* color : {&state.info_raw, &state.info_png.color}) {
		color->colortype = mode;
		color->bitdepth = 8;
		color->key_defined = transparent_key;
		color->key_r = key[0];
		color->key_g = key[1];
		color->key_b = key[2];
	}
	vBytes png;
	checkLodepng(lodepng::encode(png, pixels, width, height, state));
	return png;
}

vBytes alternatingPixels(unsigned width, unsigned height, LodePNGColorType mode,
	Pixel left, Pixel right) {
	const unsigned channels = mode == LCT_RGBA ? 4 : 3;
	vBytes pixels(static_cast<std::size_t>(width) * height * channels);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) {
			const Pixel& pixel = x % 2 ? right : left;
			const auto start = pixels.begin() + (static_cast<std::size_t>(y) * width + x) * channels;
			std::copy_n(pixel.begin(), channels, start);
		}
	}
	return pixels;
}

struct Decoded {
	vBytes pixels;
	unsigned width{};
	unsigned height{};
	lodepng::State state;

	explicit Decoded(const vBytes& png) {
		image_processing_internal::useSystemZlib(state);
		checkLodepng(lodepng::decode(pixels, width, height, state, png));
	}
};

void requireSolid(const Decoded& image, Pixel expected) {
	require(image.width == 68 && image.height == 68, "unexpected resized dimensions");
	for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
		require(std::equal(expected.begin(), expected.end(), image.pixels.begin() + offset),
			"resized pixel differs from analytic alpha-weighted result");
	}
}

// Decode a PNG in its own color type and resize it, as optimizeImage() does
// with the pixels it has already decoded.
void resizeImage(vBytes& png, unsigned new_width, unsigned new_height) {
	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	state.decoder.color_convert = 0;
	image_processing_internal::configurePngInflateLimit(state, png);
	vBytes pixels;
	unsigned width = 0;
	unsigned height = 0;
	checkLodepng(lodepng::decode(pixels, width, height, state, png));
	image_processing_internal::resizeDecodedImage(png, std::move(pixels), width, height,
		state.info_raw, state.info_png.color, new_width, new_height);
}

void testHalfCoverage(LodePNGColorType mode) {
	const vBytes pixels = alternatingPixels(136, 68, mode,
		Pixel{0, 0, 0, 0}, Pixel{255, 255, 255, 255});
	vBytes png = encodeTruecolor(pixels, 136, 68, mode, mode == LCT_RGB);
	resizeImage(png, 68, 68);
	const Decoded image(png);
	requireSolid(image, Pixel{255, 255, 255, 128});
	require(image.state.info_png.color.colortype == LCT_RGBA,
		"fractional coverage requires RGBA output");
	require(!image.state.info_png.color.key_defined, "RGBA output retains an obsolete color key");
}

void testPartialAlpha() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{255, 0, 0, 64}, Pixel{0, 0, 255, 192}), 136, 68, LCT_RGBA);
	resizeImage(png, 68, 68);
	// Equal spatial weights give alpha 128; the visible red:blue ratio is 1:3.
	requireSolid(Decoded(png), Pixel{64, 0, 191, 128});
}

void testTwoDimensionalAlpha() {
	constexpr std::array<Pixel, 4> tile{
		Pixel{0, 0, 0, 0}, Pixel{255, 0, 0, 64},
		Pixel{0, 255, 0, 128}, Pixel{0, 0, 255, 192}};
	vBytes pixels(136 * 136 * 4);
	for (unsigned y = 0; y < 136; ++y) {
		for (unsigned x = 0; x < 136; ++x) {
			const Pixel& pixel = tile[(y % 2) * 2 + x % 2];
			std::copy(pixel.begin(), pixel.end(), pixels.begin() + (y * 136 + x) * 4);
		}
	}
	vBytes png = encodeTruecolor(pixels, 136, 136, LCT_RGBA);
	resizeImage(png, 68, 68);
	// Four equal spatial weights give alpha 96 and visible RGB proportions 1:2:3.
	requireSolid(Decoded(png), Pixel{43, 85, 128, 96});
}

void testTransparentHiddenColors() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{255, 17, 0, 0}, Pixel{0, 93, 255, 0}), 136, 68, LCT_RGBA);
	resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{0, 0, 0, 0});
}

void testColoredTransparencyKey() {
	constexpr Pixel key{12, 34, 56, 0};
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGB,
		key, Pixel{12, 90, 56, 255}), 136, 68, LCT_RGB, true, key);
	resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{12, 90, 56, 128});
}

void testOpaqueRgb() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGB,
		Pixel{0, 0, 0, 255}, Pixel{255, 255, 255, 255}), 136, 68, LCT_RGB);
	resizeImage(png, 68, 68);
	const Decoded image(png);
	requireSolid(image, Pixel{128, 128, 128, 255});
	require(image.state.info_png.color.colortype == LCT_RGB,
		"opaque RGB output unnecessarily changes color type");
}

void testOpaqueRgba() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{0, 0, 0, 255}, Pixel{255, 255, 255, 255}), 136, 68, LCT_RGBA);
	resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{128, 128, 128, 255});
}

void testPaletteTransparency() {
	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	state.encoder.auto_convert = 0;
	state.info_raw.colortype = LCT_PALETTE;
	state.info_raw.bitdepth = 8;
	state.info_png.color.colortype = LCT_PALETTE;
	state.info_png.color.bitdepth = 1;
	for (LodePNGColorMode* color : {&state.info_raw, &state.info_png.color}) {
		checkLodepng(lodepng_palette_add(color, 255, 0, 0, 0));
		checkLodepng(lodepng_palette_add(color, 0, 255, 0, 255));
	}
	vBytes pixels(136 * 68);
	constexpr std::array<Byte, 4> pattern{1, 0, 0, 1};
	for (unsigned y = 0; y < 68; ++y) {
		for (unsigned x = 0; x < 136; ++x) pixels[y * 136 + x] = pattern[x % 4];
	}
	vBytes png;
	checkLodepng(lodepng::encode(png, pixels, 136, 68, state));
	resizeImage(png, 68, 68);
	const Decoded image(png);
	require(image.state.info_png.color.colortype == LCT_PALETTE,
		"indexed image was promoted instead of using nearest-neighbor sampling");
	require(image.state.info_png.color.bitdepth == 1, "indexed bit depth changed");
	require(image.width == 68 && image.height == 68, "indexed resize dimensions changed");
	for (unsigned y = 0; y < 68; ++y) {
		for (unsigned x = 0; x < 68; ++x) {
			const Pixel expected = x % 2 ? Pixel{0, 255, 0, 255} : Pixel{255, 0, 0, 0};
			require(std::equal(expected.begin(), expected.end(), image.pixels.begin() + (y * 68 + x) * 4),
				"indexed nearest-neighbor color or transparency changed");
		}
	}
}

void requireLinuxSafeIhdr(const vBytes& png) {
	for (const auto& [first, last] : {std::pair{16, 24}, std::pair{29, 33}}) {
		for (int i = first; i < last; ++i) {
			require(!isLinuxProblemMetacharacter(png[i]), "optimized IHDR contains a shell metacharacter");
		}
	}
	require(readBe32(png, 29) == lodepng_crc32(png.data() + 12, 17),
		"optimized IHDR checksum is invalid");
}

vBytes optimizationFixture(unsigned width, LodePNGColorType mode) {
	constexpr unsigned height = 68;
	vBytes pixels = alternatingPixels(width, height, mode,
		Pixel{0, 0, 0, 0}, Pixel{255, 255, 255, 255});
	const unsigned channels = mode == LCT_RGBA ? 4 : 3;
	// More than 256 distinct opaque colors prevent the optimizer from converting
	// this fixture to a palette. They cannot affect the first row's interpolation.
	for (unsigned x = 0; x < width; ++x) {
		const auto offset = (static_cast<std::size_t>(height - 1) * width + x) * channels;
		pixels[offset] = static_cast<Byte>(x);
		pixels[offset + 1] = static_cast<Byte>(x >> 8);
		pixels[offset + 2] = 127;
		if (channels == 4) pixels[offset + 3] = 255;
	}
	return encodeTruecolor(pixels, width, height, mode, mode == LCT_RGB);
}

void testOptimization(unsigned width, LodePNGColorType mode) {
	vBytes png = optimizationFixture(width, mode);
	optimizeImage(png);
	const Decoded image(png);
	require(image.width >= 68 && image.width < width && image.height == 68,
		"unsafe image was not resized within the valid dimensions");
	require(image.state.info_png.color.colortype == LCT_RGBA,
		"optimized truecolor transparency is not represented as RGBA");
	requireLinuxSafeIhdr(png);
	bool has_partial_alpha = false;
	for (unsigned x = 0; x < image.width; ++x) {
		const Byte* pixel = image.pixels.data() + x * 4;
		has_partial_alpha |= pixel[3] > 0 && pixel[3] < 255;
		const Byte expected_rgb = pixel[3] == 0 ? 0 : 255;
		require(pixel[0] == expected_rgb && pixel[1] == expected_rgb && pixel[2] == expected_rgb,
			"optimized transparency introduces a dark fringe or noncanonical transparent pixel");
	}
	require(has_partial_alpha, "optimization discarded fractional transparency coverage");
}

bool safeHeader(unsigned width, LodePNGColorType mode) {
	// Encoding a real, small-height PNG gives independent CRC and layout checks
	// without reproducing the optimizer's candidate-header construction.
	const vBytes png = encodeTruecolor(vBytes(static_cast<std::size_t>(width) * 68 * 3),
		width, 68, LCT_RGB);
	vBytes candidate(png.begin(), png.begin() + 33);
	candidate[25] = static_cast<Byte>(mode);
	const std::uint32_t crc = lodepng_crc32(candidate.data() + 12, 17);
	for (unsigned i = 0; i < 4; ++i) candidate[29 + i] = static_cast<Byte>(crc >> (24 - i * 8));
	try {
		requireLinuxSafeIhdr(candidate);
		return true;
	} catch (const std::runtime_error&) {
		return false;
	}
}

// Covers with up to 256 colors (alpha included; an RGB color key counts as
// transparent) become an indexed PNG whose palette lists the colors in order of
// first appearance. A 257th color keeps the cover truecolor.
void testPaletteConversionLimit(LodePNGColorType mode, bool transparent_key, unsigned colors) {
	unsigned width = 68;
	while (!safeHeader(width, LCT_PALETTE) || !safeHeader(width, mode)) ++width;
	constexpr unsigned height = 68;
	constexpr unsigned KEY_INDEX = 3;
	const auto color = [mode](unsigned i) {
		return Pixel{ static_cast<Byte>(i & 0xFF), static_cast<Byte>(i >> 8), static_cast<Byte>(i * 7),
			static_cast<Byte>(mode == LCT_RGBA && i % 5 == 0 ? 128 : 255) };
	};
	const unsigned channels = mode == LCT_RGBA ? 4 : 3;
	vBytes pixels(static_cast<std::size_t>(width) * height * channels);
	for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
		const Pixel pixel = color(static_cast<unsigned>(i % colors));
		std::copy_n(pixel.begin(), channels, pixels.begin() + static_cast<std::ptrdiff_t>(i * channels));
	}
	vBytes png = encodeTruecolor(pixels, width, height, mode, transparent_key, color(KEY_INDEX));
	optimizeImage(png);

	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	state.decoder.color_convert = 0;
	vBytes decoded;
	unsigned decoded_width = 0, decoded_height = 0;
	checkLodepng(lodepng::decode(decoded, decoded_width, decoded_height, state, png));
	require(decoded_width == width && decoded_height == height, "cover with a safe header was resized");
	const LodePNGColorMode& result = state.info_png.color;
	if (colors > 256) {
		require(result.colortype == mode, "cover with 257 colors was not kept truecolor");
		return;
	}
	require(result.colortype == LCT_PALETTE && result.bitdepth == 8 && result.palettesize == colors,
		"cover with at most 256 colors was not converted to an 8-bit palette of those colors");
	for (unsigned i = 0; i < colors; ++i) {
		Pixel expected = color(i);
		if (transparent_key && i == KEY_INDEX) expected[3] = 0;
		require(std::equal(expected.begin(), expected.end(), result.palette + i * 4),
			"palette does not list the colors in order of first appearance");
	}
	for (std::size_t i = 0; i < decoded.size(); ++i) {
		require(decoded[i] == i % colors, "pixel maps to the wrong palette index");
	}
}

void testPromotedHeaderPrediction() {
	// Height 68 permits only horizontal reduction. Select a source whose first
	// RGB-safe width is unsafe for RGBA, exposing use of the old color type when
	// predicting the resized checksum. The bounded search is independent of
	// pixel interpolation and does not dictate the eventual safe target.
	for (unsigned width = 258; width <= 600; ++width) {
		if (safeHeader(width, LCT_RGB)) continue;
		for (unsigned delta = 1; delta <= 200 && width - delta >= 68; ++delta) {
			const unsigned candidate = width - delta;
			if (!safeHeader(candidate, LCT_RGB)) continue;
			if (!safeHeader(candidate, LCT_RGBA)) {
				testOptimization(width, LCT_RGB);
				return;
			}
			break;
		}
	}
	throw std::runtime_error("no color-type-dependent IHDR fixture found within the bounded search");
}

}  // namespace

bool unsafeTruecolorHeader(unsigned width, unsigned height, LodePNGColorType mode) {
	Byte ihdr[17] = {'I', 'H', 'D', 'R'};
	for (unsigned i = 0; i < 4; ++i) {
		ihdr[4 + i] = static_cast<Byte>(width >> (24 - i * 8));
		ihdr[8 + i] = static_cast<Byte>(height >> (24 - i * 8));
	}
	ihdr[12] = 8;
	ihdr[13] = static_cast<Byte>(mode);
	const std::uint32_t crc = lodepng_crc32(ihdr, sizeof(ihdr));
	bool unsafe = false;
	for (unsigned i = 4; i < 12; ++i) unsafe |= isLinuxProblemMetacharacter(ihdr[i]);
	for (unsigned i = 0; i < 4; ++i) unsafe |= isLinuxProblemMetacharacter(static_cast<Byte>(crc >> (24 - i * 8)));
	return unsafe;
}

// A truecolor cover over 900 pixels wide must be refused whether or not its
// header needs a safety resize: shrinking it by a pixel or two to fix the
// header must not slip it under the limit.
void testOversizedTruecolorIsRejectedBeforeResize(LodePNGColorType mode) {
	// One pixel over the limit, with a header that needs a resize whose
	// cheapest fix is cutting that one column: (900, height) is safe, and at
	// equal total change the search prefers it over dropping a row from a
	// shorter-than-wide image.
	constexpr unsigned width = 901;
	unsigned height = 68;
	while (height < 900
		&& !(unsafeTruecolorHeader(width, height, mode) && !unsafeTruecolorHeader(width - 1, height, mode))) {
		++height;
	}
	require(height < 900, "no oversized fixture height found");
	const unsigned channels = mode == LCT_RGBA ? 4 : 3;
	vBytes pixels(static_cast<std::size_t>(width) * height * channels);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) {
			// Over 256 distinct opaque colors, so the cover stays truecolor.
			const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * channels;
			pixels[offset] = static_cast<Byte>(x);
			pixels[offset + 1] = static_cast<Byte>(x >> 8);
			pixels[offset + 2] = static_cast<Byte>(y);
			if (channels == 4) pixels[offset + 3] = 255;
		}
	}
	vBytes png = encodeTruecolor(pixels, width, height, mode);
	try {
		optimizeImage(png);
	} catch (const std::runtime_error& error) {
		require(std::string_view(error.what()).find("Incompatible image") != std::string_view::npos,
			"oversized cover was refused for the wrong reason");
		return;
	}
	throw std::runtime_error("oversized truecolor cover was accepted after a safety resize");
}

// resizeDecodedImage() takes the pixel buffer's own mode, so an RGB cover
// decoded to RGBA must resize to the same bytes as its native RGB decode.
void testRgbaDecodedResizeMatchesNative(bool transparent_key) {
	constexpr unsigned WIDTH = 120, HEIGHT = 100, NEW_WIDTH = 101, NEW_HEIGHT = 87;
	vBytes pixels(static_cast<std::size_t>(WIDTH) * HEIGHT * 3);
	for (std::size_t i = 0; i < pixels.size(); ++i) {
		pixels[i] = static_cast<Byte>((i * 37 + i / 11 * 5) & 0xFF);
	}
	const vBytes png = encodeTruecolor(pixels, WIDTH, HEIGHT, LCT_RGB, transparent_key,
		{ pixels[0], pixels[1], pixels[2], 0 });

	vBytes expected = png;
	resizeImage(expected, NEW_WIDTH, NEW_HEIGHT);

	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	state.decoder.color_convert = 1;  // to the default raw mode, RGBA
	vBytes rgba;
	unsigned width = 0, height = 0;
	checkLodepng(lodepng::decode(rgba, width, height, state, png));
	require(state.info_raw.colortype == LCT_RGBA, "fixture decoded to RGBA");
	vBytes actual;
	image_processing_internal::resizeDecodedImage(actual, std::move(rgba), width, height,
		state.info_raw, state.info_png.color, NEW_WIDTH, NEW_HEIGHT);
	require(actual == expected, "resizing RGBA-decoded pixels differs from resizing the native decode");
}

// ---------------------------------------------------------------------------
// LodePNG's zlib hooks (png_zlib.cpp), called directly.

vBytes zlibCompress(const vBytes& input, int level = Z_BEST_COMPRESSION) {
	uLongf size = compressBound(static_cast<uLong>(input.size()));
	vBytes output(size);
	require(compress2(output.data(), &size, input.data(), static_cast<uLong>(input.size()), level) == Z_OK,
		"zlib compress2 failed");
	output.resize(size);
	return output;
}

struct HookResult {
	unsigned error{};
	vBytes output;
};

// Runs the inflate hook with LodePNG's calling convention: the output is
// appended to `prefix`, and max_output_size bounds the total.
HookResult inflateHook(const vBytes& input, std::size_t limit, const vBytes& prefix = {}) {
	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	LodePNGDecompressSettings settings = state.decoder.zlibsettings;
	settings.max_output_size = limit;
	require(settings.custom_zlib != nullptr, "inflate hook not installed");

	unsigned char* out = nullptr;
	std::size_t size = prefix.size();
	if (!prefix.empty()) {
		out = static_cast<unsigned char*>(std::malloc(prefix.size()));
		require(out != nullptr, "malloc failed");
		std::memcpy(out, prefix.data(), prefix.size());
	}
	HookResult result;
	result.error = settings.custom_zlib(&out, &size, input.data(), input.size(), &settings);
	if (out) result.output.assign(out, out + size);
	std::free(out);
	return result;
}

vBytes deflateHook(const vBytes& input) {
	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	require(state.encoder.zlibsettings.custom_zlib != nullptr, "deflate hook not installed");
	unsigned char* out = nullptr;
	std::size_t size = 0;
	const unsigned error = state.encoder.zlibsettings.custom_zlib(
		&out, &size, input.data(), input.size(), &state.encoder.zlibsettings);
	vBytes result;
	if (out) result.assign(out, out + size);
	std::free(out);
	require(error == 0, std::format("deflate hook failed with {}", error));
	return result;
}

vBytes hookPayload() {
	vBytes payload(200'000);
	for (std::size_t i = 0; i < payload.size(); ++i) {
		payload[i] = static_cast<Byte>((i % 251) ^ (i / 997));
	}
	return payload;
}

void testSystemZlibRoundTrip() {
	const vBytes payload = hookPayload();

	// The encoder writes a standard zlib stream that plain zlib reads back.
	const vBytes compressed = deflateHook(payload);
	require(compressed.size() < payload.size() / 4, "deflate hook did not compress");
	vBytes check(payload.size());
	uLongf check_size = static_cast<uLongf>(check.size());
	require(uncompress(check.data(), &check_size, compressed.data(), static_cast<uLong>(compressed.size())) == Z_OK
		&& check_size == payload.size() && check == payload, "plain zlib did not read back the deflate hook's stream");

	// Empty input still makes a valid stream.
	require(inflateHook(deflateHook({}), 0).output.empty(), "empty round trip failed");

	// The decoder appends after existing output, and the limit covers the total.
	const vBytes prefix{'p', 'r', 'e'};
	vBytes expected = prefix;
	expected.insert(expected.end(), payload.begin(), payload.end());
	for (const std::size_t limit : {0uz, expected.size()}) {
		const HookResult result = inflateHook(compressed, limit, prefix);
		require(result.error == 0, std::format("inflate hook failed with {} (limit {})", result.error, limit));
		require(result.output == expected, "inflate hook output differs");
	}
	// Streams from another encoder (zlib at level 9, as PIL or libpng write) work too.
	require(inflateHook(zlibCompress(payload), payload.size()).output == payload, "level-9 stream not inflated");
}

void testSystemZlibRefusesBadStreams() {
	constexpr unsigned OVER_MAX_OUTPUT_SIZE = 109;
	constexpr unsigned INFLATE_FAILED = 110;
	const vBytes payload = hookPayload();
	const vBytes compressed = zlibCompress(payload);

	// A stream that inflates past the limit stops one byte over it instead of
	// being inflated in full (a 16 MiB bomb here).
	const vBytes bomb = zlibCompress(vBytes(16 * 1024 * 1024, 0));
	const HookResult bombed = inflateHook(bomb, 1000);
	require(bombed.error == OVER_MAX_OUTPUT_SIZE, std::format("bomb gave error {}", bombed.error));
	require(bombed.output.size() == 1001, std::format("bomb inflated to {} bytes", bombed.output.size()));
	require(inflateHook(compressed, payload.size() - 1).error == OVER_MAX_OUTPUT_SIZE,
		"output one byte over the limit was accepted");
	require(inflateHook(compressed, 5, vBytes(6, 'x')).error == OVER_MAX_OUTPUT_SIZE,
		"existing output over the limit was accepted");

	// LodePNG's own codes where it had one, so users see the same messages.
	const auto refuse = [&](std::string_view label, const vBytes& stream, unsigned expected) {
		const HookResult result = inflateHook(stream, payload.size());
		require(result.error == expected, std::format("{}: error {}, expected {}", label, result.error, expected));
	};
	refuse("empty input", {}, INFLATE_FAILED);
	refuse("truncated stream", vBytes(compressed.begin(), compressed.end() - 1), INFLATE_FAILED);
	vBytes trailing = compressed;
	trailing.push_back(0);
	refuse("data after the stream", trailing, image_processing_internal::PNG_ZLIB_TRAILING_DATA_ERROR);
	vBytes bad_adler = compressed;
	bad_adler.back() ^= 1;
	refuse("bad Adler-32", bad_adler, 58);
	vBytes bad_check = compressed;
	bad_check[1] ^= 1;
	refuse("bad header check", bad_check, 24);
	refuse("bad compression method", vBytes{0x79, 0x18, 0x03, 0x00}, 25);  // CM 9, valid check bits
	vBytes corrupt = compressed;
	corrupt[2] = 0xff;  // reserved block type 3
	refuse("corrupt deflate data", corrupt, INFLATE_FAILED);

	// PNG forbids a preset dictionary.
	z_stream stream{};
	require(deflateInit(&stream, Z_BEST_COMPRESSION) == Z_OK, "deflateInit failed");
	const vBytes dictionary(payload.begin(), payload.begin() + 4096);
	require(deflateSetDictionary(&stream, dictionary.data(), static_cast<uInt>(dictionary.size())) == Z_OK,
		"deflateSetDictionary failed");
	vBytes primed(compressBound(static_cast<uLong>(payload.size())) + 64);
	stream.next_in = const_cast<Byte*>(payload.data());
	stream.avail_in = static_cast<uInt>(payload.size());
	stream.next_out = primed.data();
	stream.avail_out = static_cast<uInt>(primed.size());
	require(deflate(&stream, Z_FINISH) == Z_STREAM_END, "primed deflate failed");
	primed.resize(stream.total_out);
	deflateEnd(&stream);
	refuse("preset dictionary", primed, 26);
}

// A pdvzip output carries its ZIP in an IDAT after the image's zlib stream.
// Given back as a cover it is refused with a message that says so, whether
// the data is in a separate IDAT or appended to the image's own.
void testTrailingIdatDataIsNamed() {
	constexpr unsigned SIZE = 68;
	const vBytes png = encodeTruecolor(alternatingPixels(SIZE, SIZE, LCT_RGB, {10, 20, 30, 255}, {200, 100, 50, 255}),
		SIZE, SIZE, LCT_RGB);
	const auto chunk = [](std::string_view type, std::span<const Byte> data) {
		vBytes out(4);
		writeBe32(out, 0, data.size());
		out.insert(out.end(), type.begin(), type.end());
		out.insert(out.end(), data.begin(), data.end());
		const std::uint32_t crc = lodepng_crc32(out.data() + 4, out.size() - 4);
		out.resize(out.size() + 4);
		writeBe32(out, out.size() - 4, crc);
		return out;
	};
	const std::string_view zip = "PK\x03\x04 zip data";
	const std::span<const Byte> zip_bytes(reinterpret_cast<const Byte*>(zip.data()), zip.size());

	// Separate IDAT before IEND (the last 12 bytes).
	vBytes separate(png.begin(), png.end() - 12);
	const vBytes extra = chunk("IDAT", zip_bytes);
	separate.insert(separate.end(), extra.begin(), extra.end());
	separate.insert(separate.end(), png.end() - 12, png.end());

	// The same bytes appended inside the image's only IDAT, which lodepng
	// writes straight after IHDR (8 + 25 bytes).
	constexpr std::size_t IDAT_START = 33;
	require(std::string_view(reinterpret_cast<const char*>(png.data() + IDAT_START + 4), 4) == "IDAT",
		"fixture IDAT follows IHDR");
	const std::size_t idat_length = readBe32(png, IDAT_START);
	vBytes idat_data(png.begin() + IDAT_START + 8, png.begin() + static_cast<std::ptrdiff_t>(IDAT_START + 8 + idat_length));
	idat_data.insert(idat_data.end(), zip_bytes.begin(), zip_bytes.end());
	vBytes same(png.begin(), png.begin() + IDAT_START);
	const vBytes replaced = chunk("IDAT", idat_data);
	same.insert(same.end(), replaced.begin(), replaced.end());
	same.insert(same.end(), png.begin() + static_cast<std::ptrdiff_t>(IDAT_START + 12 + idat_length), png.end());

	vBytes accepted = png;
	optimizeImage(accepted);
	for (vBytes cover : {separate, same}) {
		try {
			optimizeImage(cover);
		} catch (const std::runtime_error& error) {
			require(std::string_view(error.what()) == "PNG Error: IDAT data continues after the end of its zlib stream. The cover image may already be a pdvzip output; use the original PNG instead.",
				std::format("unexpected message: {}", error.what()));
			continue;
		}
		throw std::runtime_error("cover with data after its zlib stream was accepted");
	}
}

int main() {
	unsigned failures = 0;
	const auto run = [&failures](std::string_view name, const auto& test) {
		try {
			test();
			std::cout << "PASS: " << name << '\n';
		} catch (const std::exception& error) {
			++failures;
			std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
		}
	};
	run("RGB+tRNS half coverage", [] { testHalfCoverage(LCT_RGB); });
	run("RGBA half coverage", [] { testHalfCoverage(LCT_RGBA); });
	run("partial alpha mixing", testPartialAlpha);
	run("two-dimensional alpha mixing", testTwoDimensionalAlpha);
	run("fully transparent hidden colors", testTransparentHiddenColors);
	run("nonzero RGB transparency key", testColoredTransparencyKey);
	run("opaque RGB interpolation", testOpaqueRgb);
	run("opaque RGBA interpolation", testOpaqueRgba);
	run("indexed nearest-neighbor transparency", testPaletteTransparency);
	run("RGB+tRNS optimization", [] { testOptimization(294, LCT_RGB); });
	run("RGBA optimization", [] { testOptimization(294, LCT_RGBA); });
	run("promoted RGBA IHDR prediction", testPromotedHeaderPrediction);
	run("oversized RGB refused before resize", [] { testOversizedTruecolorIsRejectedBeforeResize(LCT_RGB); });
	run("oversized RGBA refused before resize", [] { testOversizedTruecolorIsRejectedBeforeResize(LCT_RGBA); });
	for (const unsigned colors : {1u, 256u, 257u}) {
		run(std::format("RGB palette limit {}", colors), [colors] { testPaletteConversionLimit(LCT_RGB, false, colors); });
		run(std::format("RGB+tRNS palette limit {}", colors), [colors] { testPaletteConversionLimit(LCT_RGB, true, colors); });
		run(std::format("RGBA palette limit {}", colors), [colors] { testPaletteConversionLimit(LCT_RGBA, false, colors); });
	}
	run("RGBA-decoded RGB resize matches native decode", [] { testRgbaDecodedResizeMatchesNative(false); });
	run("RGBA-decoded RGB+tRNS resize matches native decode", [] { testRgbaDecodedResizeMatchesNative(true); });
	run("system zlib round trip", testSystemZlibRoundTrip);
	run("system zlib refuses bad streams", testSystemZlibRefusesBadStreams);
	run("data after the IDAT zlib stream is named", testTrailingIdatDataIsNamed);
	return failures ? 1 : 0;
}
