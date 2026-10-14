// Regression tests for transparency-preserving PNG resampling (review issue 6).
// From the source directory:
/*
g++ -std=c++23 -O1 -I. -DLODEPNG_NO_COMPILE_DISK \
  -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CRC \
  tests/image_transparency_tests.cpp image_processing.cpp image_resize.cpp \
  binary_utils.cpp crc32.cpp lodepng/lodepng.cpp -lz \
  -o /tmp/pdvzip-image-transparency-tests
*/

#include "image_processing_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

void testHalfCoverage(LodePNGColorType mode) {
	const vBytes pixels = alternatingPixels(136, 68, mode,
		Pixel{0, 0, 0, 0}, Pixel{255, 255, 255, 255});
	vBytes png = encodeTruecolor(pixels, 136, 68, mode, mode == LCT_RGB);
	image_processing_internal::resizeImage(png, 68, 68);
	const Decoded image(png);
	requireSolid(image, Pixel{255, 255, 255, 128});
	require(image.state.info_png.color.colortype == LCT_RGBA,
		"fractional coverage requires RGBA output");
	require(!image.state.info_png.color.key_defined, "RGBA output retains an obsolete color key");
}

void testPartialAlpha() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{255, 0, 0, 64}, Pixel{0, 0, 255, 192}), 136, 68, LCT_RGBA);
	image_processing_internal::resizeImage(png, 68, 68);
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
	image_processing_internal::resizeImage(png, 68, 68);
	// Four equal spatial weights give alpha 96 and visible RGB proportions 1:2:3.
	requireSolid(Decoded(png), Pixel{43, 85, 128, 96});
}

void testTransparentHiddenColors() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{255, 17, 0, 0}, Pixel{0, 93, 255, 0}), 136, 68, LCT_RGBA);
	image_processing_internal::resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{0, 0, 0, 0});
}

void testColoredTransparencyKey() {
	constexpr Pixel key{12, 34, 56, 0};
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGB,
		key, Pixel{12, 90, 56, 255}), 136, 68, LCT_RGB, true, key);
	image_processing_internal::resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{12, 90, 56, 128});
}

void testOpaqueRgb() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGB,
		Pixel{0, 0, 0, 255}, Pixel{255, 255, 255, 255}), 136, 68, LCT_RGB);
	image_processing_internal::resizeImage(png, 68, 68);
	const Decoded image(png);
	requireSolid(image, Pixel{128, 128, 128, 255});
	require(image.state.info_png.color.colortype == LCT_RGB,
		"opaque RGB output unnecessarily changes color type");
}

void testOpaqueRgba() {
	vBytes png = encodeTruecolor(alternatingPixels(136, 68, LCT_RGBA,
		Pixel{0, 0, 0, 255}, Pixel{255, 255, 255, 255}), 136, 68, LCT_RGBA);
	image_processing_internal::resizeImage(png, 68, 68);
	requireSolid(Decoded(png), Pixel{128, 128, 128, 255});
}

void testPaletteTransparency() {
	lodepng::State state;
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
	image_processing_internal::resizeImage(png, 68, 68);
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
	require(readValueAt(png, 29, 4) == lodepng_crc32(png.data() + 12, 17),
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
	return failures ? 1 : 0;
}
