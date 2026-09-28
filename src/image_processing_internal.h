#pragma once

#include "pdvzip.h"
#include "lodepng/lodepng.h"

#include <array>
#include <format>
#include <stdexcept>

namespace image_processing_internal {

constexpr std::size_t RGBA_COMPONENTS = 4;

inline void throwLodepngError(std::string_view context, unsigned error, bool include_error_text = false) {
	if (!error) return;
	throw std::runtime_error(include_error_text
		? std::format("{} {}: {}", context, error, lodepng_error_text(error))
		: std::format("{}: {}", context, error));
}

inline void copyPalette(const Byte* palette, std::size_t count, LodePNGColorMode& target) {
	if (count > 0 && palette == nullptr) {
		throw std::runtime_error("LodePNG palette setup error: source palette is null");
	}
	for (std::size_t i = 0; i < count; ++i) {
		const Byte* p = &palette[i * RGBA_COMPONENTS];
		throwLodepngError("LodePNG palette setup error", lodepng_palette_add(&target, p[0], p[1], p[2], p[3]));
	}
}

// Return the exact number of bytes represented by the PNG's inflated IDAT
// stream: one filter byte per scanline plus the scanline's byte-padded pixels.
// This deliberately uses the encoded PNG color mode rather than info_raw,
// because color conversion happens only after IDAT has been inflated.
[[nodiscard]] inline std::size_t pngInflatedScanlineSize(std::span<const Byte> png_data) {
	constexpr std::size_t
		MIN_IHDR_SIZE        = 29,
		WIDTH_OFFSET         = 16,
		HEIGHT_OFFSET        = 20,
		BIT_DEPTH_OFFSET     = 24,
		COLOR_TYPE_OFFSET    = 25,
		INTERLACE_OFFSET     = 28;
	constexpr std::string_view OVERFLOW_ERROR =
		"PNG Error: Inflated scanline size overflows the supported address space.";

	if (png_data.size() < MIN_IHDR_SIZE) {
		throw std::runtime_error("PNG Error: IHDR is truncated before decoder limit setup.");
	}

	const std::size_t width = readValueAt(png_data, WIDTH_OFFSET, 4);
	const std::size_t height = readValueAt(png_data, HEIGHT_OFFSET, 4);
	const unsigned bit_depth = png_data[BIT_DEPTH_OFFSET];
	const auto color_type = static_cast<LodePNGColorType>(png_data[COLOR_TYPE_OFFSET]);
	const unsigned interlace_method = png_data[INTERLACE_OFFSET];

	if (width == 0 || height == 0) {
		throw std::runtime_error("PNG Error: Image dimensions must be nonzero.");
	}

	unsigned channels = 0;
	bool valid_bit_depth = false;
	switch (color_type) {
		case LCT_GREY:
			channels = 1;
			valid_bit_depth = bit_depth == 1 || bit_depth == 2 || bit_depth == 4
				|| bit_depth == 8 || bit_depth == 16;
			break;
		case LCT_RGB:
			channels = 3;
			valid_bit_depth = bit_depth == 8 || bit_depth == 16;
			break;
		case LCT_PALETTE:
			channels = 1;
			valid_bit_depth = bit_depth == 1 || bit_depth == 2 || bit_depth == 4
				|| bit_depth == 8;
			break;
		case LCT_GREY_ALPHA:
			channels = 2;
			valid_bit_depth = bit_depth == 8 || bit_depth == 16;
			break;
		case LCT_RGBA:
			channels = 4;
			valid_bit_depth = bit_depth == 8 || bit_depth == 16;
			break;
		default:
			throw std::runtime_error("PNG Error: Unsupported color type during decoder limit setup.");
	}
	if (!valid_bit_depth) {
		throw std::runtime_error("PNG Error: Unsupported bit depth during decoder limit setup.");
	}
	if (interlace_method > 1) {
		throw std::runtime_error("PNG Error: Unsupported interlace method during decoder limit setup.");
	}

	const std::size_t bits_per_pixel = checkedMultiply(
		static_cast<std::size_t>(channels), bit_depth, OVERFLOW_ERROR);

	const auto filteredPassSize = [bits_per_pixel, OVERFLOW_ERROR](
		std::size_t pass_width,
		std::size_t pass_height) {
		if (pass_width == 0 || pass_height == 0) {
			return std::size_t{0};
		}

		// ceil(pass_width * bits_per_pixel / 8), arranged to avoid the
		// potentially overflowing pass_width * bits_per_pixel expression.
		const std::size_t whole_bytes = checkedMultiply(
			pass_width / 8, bits_per_pixel, OVERFLOW_ERROR);
		const std::size_t remaining_bits = checkedMultiply(
			pass_width % 8, bits_per_pixel, OVERFLOW_ERROR);
		const std::size_t partial_byte_count = checkedAdd(
			remaining_bits, 7, OVERFLOW_ERROR) / 8;
		const std::size_t pixel_bytes = checkedAdd(
			whole_bytes, partial_byte_count, OVERFLOW_ERROR);
		const std::size_t filtered_row_bytes = checkedAdd(
			pixel_bytes, 1, OVERFLOW_ERROR);
		return checkedMultiply(pass_height, filtered_row_bytes, OVERFLOW_ERROR);
	};

	if (interlace_method == 0) {
		return filteredPassSize(width, height);
	}

	// Adam7 start coordinates and strides for its seven reduced images.
	constexpr std::array<std::size_t, 7> X_START = {0, 4, 0, 2, 0, 1, 0};
	constexpr std::array<std::size_t, 7> Y_START = {0, 0, 4, 0, 2, 0, 1};
	constexpr std::array<std::size_t, 7> X_STEP  = {8, 8, 4, 4, 2, 2, 1};
	constexpr std::array<std::size_t, 7> Y_STEP  = {8, 8, 8, 4, 4, 2, 2};
	const auto passExtent = [](std::size_t full_size, std::size_t start, std::size_t step) {
		return full_size <= start ? std::size_t{0} : 1 + (full_size - start - 1) / step;
	};

	std::size_t total = 0;
	for (std::size_t pass = 0; pass < X_START.size(); ++pass) {
		const std::size_t pass_width = passExtent(width, X_START[pass], X_STEP[pass]);
		const std::size_t pass_height = passExtent(height, Y_START[pass], Y_STEP[pass]);
		total = checkedAdd(total, filteredPassSize(pass_width, pass_height), OVERFLOW_ERROR);
	}
	return total;
}

inline void configurePngInflateLimit(lodepng::State& state, std::span<const Byte> png_data) {
	state.decoder.zlibsettings.max_output_size = pngInflatedScanlineSize(png_data);
}

void resizeImage(vBytes& image_file_vec, unsigned new_width, unsigned new_height);

}  // namespace image_processing_internal
