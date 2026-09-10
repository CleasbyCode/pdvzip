// Regression tests for native indexed PNG validation (review issue 8).
// From the source directory:
/*
g++ -std=c++23 -O1 -I. -DLODEPNG_NO_COMPILE_DISK \
  -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CRC \
  tests/indexed_image_tests.cpp image_processing.cpp image_resize.cpp \
  binary_utils.cpp crc32.cpp lodepng/lodepng.cpp -lz \
  -o /tmp/pdvzip-indexed-image-tests
*/

#include "image_processing_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <zlib.h>

namespace {

using Pixel = std::array<Byte, 4>;

void require(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

void checkLodepng(unsigned error) {
	if (error) throw std::runtime_error(lodepng_error_text(error));
}

void append32(vBytes& bytes, std::uint32_t value) {
	for (unsigned shift : {24, 16, 8, 0}) bytes.push_back(static_cast<Byte>(value >> shift));
}

void appendChunk(vBytes& png, std::string_view type, const vBytes& data) {
	require(type.size() == 4, "invalid fixture chunk name");
	append32(png, static_cast<std::uint32_t>(data.size()));
	const std::size_t crc_start = png.size();
	png.insert(png.end(), type.begin(), type.end());
	png.insert(png.end(), data.begin(), data.end());
	append32(png, lodepng_crc32(png.data() + crc_start, data.size() + 4));
}

vBytes replaceChunk(const vBytes& png, std::string_view type, const vBytes* replacement) {
	vBytes result(png.begin(), png.begin() + 8);
	bool replaced = false;
	for (std::size_t offset = 8; offset < png.size();) {
		const std::size_t size = readValueAt(png, offset, 4) + 12;
		require(size <= png.size() - offset, "fixture contains a truncated chunk");
		const std::string_view chunk_type(reinterpret_cast<const char*>(png.data() + offset + 4), 4);
		if (chunk_type == type) {
			if (!replaced && replacement) appendChunk(result, type, *replacement);
			replaced = true;
		} else {
			result.insert(result.end(), png.begin() + offset, png.begin() + offset + size);
		}
		offset += size;
	}
	require(replaced, "fixture chunk to replace is absent");
	return result;
}

bool linuxSafeHeader(const vBytes& png) {
	for (const auto& [first, last] : {std::pair{16, 24}, std::pair{29, 33}}) {
		for (int index = first; index < last; ++index) {
			if (isLinuxProblemMetacharacter(png[index])) return false;
		}
	}
	return true;
}

vBytes header(unsigned width, unsigned height, unsigned depth, unsigned interlace,
	LodePNGColorType color = LCT_PALETTE) {
	vBytes png{137, 80, 78, 71, 13, 10, 26, 10};
	vBytes ihdr;
	append32(ihdr, width);
	append32(ihdr, height);
	ihdr.insert(ihdr.end(), {static_cast<Byte>(depth), static_cast<Byte>(color),
		0, 0, static_cast<Byte>(interlace)});
	appendChunk(png, "IHDR", ihdr);
	return png;
}

unsigned safeHeight(unsigned width, unsigned depth, unsigned interlace) {
	for (unsigned height = 69; height < 200; ++height) {
		if (linuxSafeHeader(header(width, height, depth, interlace))) return height;
	}
	throw std::runtime_error("no safe fixture dimensions found");
}

Pixel paletteColor(unsigned index) {
	constexpr std::array<Byte, 4> alpha{0, 255, 64, 128};
	return {static_cast<Byte>(index), static_cast<Byte>(index * 37 + 11),
		static_cast<Byte>(index * 13 + 19), alpha[index % alpha.size()]};
}

Byte paletteIndex(unsigned x, unsigned y, unsigned depth) {
	return static_cast<Byte>((x * 5 + y * 7) % (1U << depth));
}

vBytes indexedPng(unsigned width, unsigned height, unsigned depth, unsigned interlace) {
	lodepng::State state;
	state.encoder.auto_convert = 0;
	state.info_png.interlace_method = interlace;
	state.info_raw.colortype = LCT_PALETTE;
	state.info_raw.bitdepth = 8;
	state.info_png.color.colortype = LCT_PALETTE;
	state.info_png.color.bitdepth = depth;
	for (unsigned index = 0; index < (1U << depth); ++index) {
		const Pixel color = paletteColor(index);
		for (LodePNGColorMode* mode : {&state.info_raw, &state.info_png.color}) {
			checkLodepng(lodepng_palette_add(mode, color[0], color[1], color[2], color[3]));
		}
	}
	vBytes indices(static_cast<std::size_t>(width) * height);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) indices[y * width + x] = paletteIndex(x, y, depth);
	}
	vBytes png;
	checkLodepng(lodepng::encode(png, indices, width, height, state));
	return png;
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

void requirePalette(const Decoded& image, unsigned depth) {
	const LodePNGColorMode& mode = image.state.info_png.color;
	require(mode.colortype == LCT_PALETTE && mode.bitdepth == depth,
		"indexed color type or bit depth changed");
	require(mode.palettesize == (1U << depth), "palette size changed");
	for (unsigned index = 0; index < mode.palettesize; ++index) {
		const Pixel expected = paletteColor(index);
		require(std::equal(expected.begin(), expected.end(), mode.palette + index * 4),
			"palette RGB or tRNS alpha value changed");
	}
}

void testPreserved(unsigned depth, unsigned interlace) {
	// An odd width exercises packed scanline padding at every sub-byte depth.
	constexpr unsigned width = 69;
	const unsigned height = safeHeight(width, depth, interlace);
	const vBytes original = indexedPng(width, height, depth, interlace);
	require(linuxSafeHeader(original), "preservation fixture needs an unexpected resize");
	vBytes png = original;
	// Insert an ancillary chunk to ensure the native validation path still strips it.
	vBytes ancillary;
	appendChunk(ancillary, "tEXt", vBytes{'n', 'o', 't', 'e', 0, 't', 'e', 's', 't'});
	png.insert(png.begin() + 33, ancillary.begin(), ancillary.end());
	optimizeImage(png);
	require(png == original, "optimization changed an already-safe indexed encoding");
	const Decoded image(png);
	require(image.width == width && image.height == height, "indexed dimensions changed");
	require(image.state.info_png.interlace_method == interlace, "interlace mode changed");
	requirePalette(image, depth);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) {
			const Pixel expected = paletteColor(paletteIndex(x, y, depth));
			require(std::equal(expected.begin(), expected.end(), image.pixels.begin() + (y * width + x) * 4),
				"decoded indexed pixel changed");
		}
	}
}

void testSingleAxisResize(unsigned depth, unsigned interlace) {
	constexpr unsigned width = 68;
	unsigned height = 90;
	while (height < 200 && linuxSafeHeader(header(width, height, depth, interlace))) ++height;
	require(height < 200, "no unsafe fixture height found");
	vBytes png = indexedPng(width, height, depth, interlace);
	require(!linuxSafeHeader(png), "resize fixture has an unexpectedly safe header");
	optimizeImage(png);
	const Decoded image(png);
	require(image.width == width && image.height >= 68 && image.height < height,
		"indexed image was not resized along its available axis");
	require(linuxSafeHeader(png), "resized indexed header remains unsafe");
	requirePalette(image, depth);
	for (unsigned y = 0; y < image.height; ++y) {
		const unsigned source_y = static_cast<unsigned>(std::floor(
			(static_cast<double>(y) + 0.5) * height / image.height));
		for (unsigned x = 0; x < width; ++x) {
			const Pixel expected = paletteColor(paletteIndex(x, source_y, depth));
			require(std::equal(expected.begin(), expected.end(), image.pixels.begin() + (y * width + x) * 4),
				"indexed resize changed nearest-neighbor color or transparency");
		}
	}
}

void requireRejected(vBytes png) {
	try {
		optimizeImage(png);
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("malformed indexed PNG was accepted");
}

vBytes compressed(const vBytes& data) {
	uLongf size = compressBound(data.size());
	vBytes result(size);
	require(compress2(result.data(), &size, data.data(), data.size(), Z_BEST_SPEED) == Z_OK,
		"fixture compression failed");
	result.resize(size);
	return result;
}

void testMalformed(unsigned interlace) {
	const vBytes original = indexedPng(69, safeHeight(69, 1, interlace), 1, interlace);
	const vBytes invalid_deflate{0, 0, 0};
	requireRejected(replaceChunk(original, "IDAT", &invalid_deflate));
	const vBytes oversized = compressed(vBytes(8 * 1024 * 1024, 0));
	requireRejected(replaceChunk(original, "IDAT", &oversized));
	requireRejected(replaceChunk(original, "PLTE", nullptr));
	requireRejected(replaceChunk(original, "IDAT", nullptr));
	requireRejected(replaceChunk(original, "IEND", nullptr));
	const vBytes invalid_iend{0};
	requireRejected(replaceChunk(original, "IEND", &invalid_iend));
	vBytes truncated_iend = original;
	truncated_iend.pop_back();
	requireRejected(std::move(truncated_iend));
}

void testTruecolorConversion(LodePNGColorType mode) {
	constexpr unsigned width = 69;
	const unsigned height = safeHeight(width, 8, 0);
	const unsigned channels = mode == LCT_RGBA ? 4 : 3;
	lodepng::State state;
	state.encoder.auto_convert = 0;
	state.info_raw.colortype = mode;
	state.info_raw.bitdepth = 8;
	state.info_png.color.colortype = mode;
	state.info_png.color.bitdepth = 8;
	vBytes pixels(static_cast<std::size_t>(width) * height * channels);
	vBytes expected(static_cast<std::size_t>(width) * height * 4);
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x) {
			Pixel color = paletteColor(paletteIndex(x, y, 8));
			if (mode == LCT_RGB) color[3] = 255;
			const std::size_t offset = y * width + x;
			std::copy_n(color.begin(), channels, pixels.begin() + offset * channels);
			std::copy(color.begin(), color.end(), expected.begin() + offset * 4);
		}
	}
	vBytes png;
	checkLodepng(lodepng::encode(png, pixels, width, height, state));
	optimizeImage(png);
	const Decoded image(png);
	require(image.width == width && image.height == height, "converted image dimensions changed");
	require(image.state.info_png.color.colortype == LCT_PALETTE,
		"truecolor image with 256 colors no longer converts to a palette");
	require(image.pixels == expected, "truecolor-to-palette conversion changed RGBA pixels");
}

} // namespace

int main() {
	unsigned passed = 0;
	auto run = [&passed](const std::string& name, auto test) {
		try {
			test();
			++passed;
		} catch (const std::exception& error) {
			std::cerr << name << ": " << error.what() << '\n';
			throw;
		}
	};
	try {
		for (unsigned depth : {1, 2, 4, 8}) {
			for (unsigned interlace : {0, 1}) {
				const std::string label = std::to_string(depth) + "-bit, interlace " + std::to_string(interlace);
				run("preserve " + label, [&] { testPreserved(depth, interlace); });
				run("resize " + label, [&] { testSingleAxisResize(depth, interlace); });
			}
		}
		for (unsigned interlace : {0, 1}) {
			run("malformed, interlace " + std::to_string(interlace), [&] { testMalformed(interlace); });
		}
		run("RGB palette conversion", [] { testTruecolorConversion(LCT_RGB); });
		run("RGBA palette conversion", [] { testTruecolorConversion(LCT_RGBA); });
	} catch (const std::exception&) {
		return 1;
	}
	std::cout << "All " << passed << " indexed-image regressions passed.\n";
}
