#include "pdvzip.h"

namespace {

// ============================================================================
// Internal: Resize image by 1 pixel in each dimension
// ============================================================================

void resizeImage(vBytes& image_file_vec) {

	constexpr unsigned
		MIN_DIMENSION       = 68,
		DIMENSION_REDUCTION = 1;

	constexpr double SAMPLING_OFFSET = 0.5;

	// Decode preserving original color type (no conversion to RGBA).
	lodepng::State decode_state;
	decode_state.decoder.color_convert = 0;

	vBytes pixels;
	unsigned
		width  = 0,
		height = 0,
		error  = lodepng::decode(pixels, width, height, decode_state, image_file_vec);

	if (error) {
		throw std::runtime_error(std::format("LodePNG decode error: {}", error));
	}

	if (width <= MIN_DIMENSION || height <= MIN_DIMENSION) {
		throw std::runtime_error("Image dimensions too small to reduce.");
	}

	const auto& raw = decode_state.info_raw;

	const unsigned
		channels = lodepng_get_channels(&raw),
		bitdepth = raw.bitdepth;

	const bool is_palette = (raw.colortype == LCT_PALETTE);

	if (channels == 0) {
		throw std::runtime_error("Image Error: Decoded image reports 0 channels.");
	}

	// Sub-byte palette images (1/2/4-bit) pack multiple indices per byte.
	// Bilinear interpolation doesn't apply to palette indices, and handling
	// sub-byte packing adds significant complexity for a 1px reduction.
	// Force these through lodepng's conversion to 8-bit palette first.
	if (is_palette && bitdepth < 8) {
		lodepng::State redecode_state;
		redecode_state.decoder.color_convert = 1;
		redecode_state.info_raw.colortype = LCT_PALETTE;
		redecode_state.info_raw.bitdepth = 8;

		// Copy the palette into info_raw so lodepng can convert.
		const auto& src_color = decode_state.info_png.color;
		constexpr std::size_t RGBA_COMPONENTS = 4;
		for (std::size_t i = 0; i < src_color.palettesize; ++i) {
			const Byte* p = &src_color.palette[i * RGBA_COMPONENTS];
			lodepng_palette_add(&redecode_state.info_raw, p[0], p[1], p[2], p[3]);
		}

		pixels.clear();
		error = lodepng::decode(pixels, width, height, redecode_state, image_file_vec);
		if (error) {
			throw std::runtime_error(std::format("LodePNG re-decode error: {}", error));
		}
		// From here on, treat as 8-bit palette (1 byte per pixel index).
	}

	const unsigned
		new_width  = width - DIMENSION_REDUCTION,
		new_height = height - DIMENSION_REDUCTION;

	const double
		x_ratio = static_cast<double>(width)  / new_width,
		y_ratio = static_cast<double>(height) / new_height;

	// For palette images: 1 byte per pixel (index).
	// For truecolor/grey: channels bytes per pixel.
	const std::size_t bytes_per_pixel = is_palette ? 1 : channels;
	vBytes resized(static_cast<std::size_t>(new_width) * new_height * bytes_per_pixel);

	for (unsigned y = 0; y < new_height; ++y) {
		for (unsigned x = 0; x < new_width; ++x) {
			const double
				src_x = std::clamp(
					(x + SAMPLING_OFFSET) * x_ratio - SAMPLING_OFFSET,
					0.0, static_cast<double>(width - 1)),
				src_y = std::clamp(
					(y + SAMPLING_OFFSET) * y_ratio - SAMPLING_OFFSET,
					0.0, static_cast<double>(height - 1));

			if (is_palette) {
				// Nearest-neighbor for palette images (indices can't be interpolated).
				const unsigned
					ix = static_cast<unsigned>(std::round(src_x)),
					iy = static_cast<unsigned>(std::round(src_y));
				resized[y * new_width + x] = pixels[iy * width + ix];
			} else {
				// Bilinear interpolation for truecolor/greyscale images.
				const unsigned
					x0 = static_cast<unsigned>(src_x),
					y0 = static_cast<unsigned>(src_y),
					x1 = std::min(x0 + 1, width  - 1),
					y1 = std::min(y0 + 1, height - 1);

				const double
					dx = src_x - x0,
					dy = src_y - y0;

				for (unsigned c = 0; c < channels; ++c) {
					const double value =
						(1 - dx) * (1 - dy) * pixels[channels * (y0 * width + x0) + c] +
						(1 - dx) *      dy  * pixels[channels * (y1 * width + x0) + c] +
						     dx  * (1 - dy) * pixels[channels * (y0 * width + x1) + c] +
						     dx  *      dy  * pixels[channels * (y1 * width + x1) + c];
					resized[channels * (y * new_width + x) + c] =
						static_cast<Byte>(std::clamp(std::round(value), 0.0, 255.0));
				}
			}
		}
	}

	// Re-encode with the same color type and palette.
	lodepng::State encode_state;
	const auto& png_color = decode_state.info_png.color;

	// For sub-byte palettes we decoded to 8-bit, so encode as 8-bit palette.
	const unsigned encode_bitdepth = (is_palette && bitdepth < 8) ? 8 : png_color.bitdepth;

	encode_state.info_raw.colortype       = png_color.colortype;
	encode_state.info_raw.bitdepth        = encode_bitdepth;
	encode_state.info_png.color.colortype = png_color.colortype;
	encode_state.info_png.color.bitdepth  = encode_bitdepth;
	encode_state.encoder.auto_convert     = 0;

	if (is_palette) {
		constexpr std::size_t RGBA_COMPONENTS = 4;
		for (std::size_t i = 0; i < png_color.palettesize; ++i) {
			const Byte* p = &png_color.palette[i * RGBA_COMPONENTS];
			lodepng_palette_add(&encode_state.info_png.color, p[0], p[1], p[2], p[3]);
			lodepng_palette_add(&encode_state.info_raw, p[0], p[1], p[2], p[3]);
		}
	}

	vBytes output;
	error = lodepng::encode(output, resized, new_width, new_height, encode_state);
	if (error) {
		throw std::runtime_error(std::format("LodePNG encode error: {}", error));
	}

	image_file_vec = std::move(output);
}

// ============================================================================
// Internal: Convert truecolor image to indexed palette
// ============================================================================

void convertToPalette(vBytes& image_file_vec, const vBytes& image, unsigned width, unsigned height, const LodePNGColorStats& stats, LodePNGColorType raw_color_type) {

	constexpr Byte
		PALETTE_BIT_DEPTH = 8,
		ALPHA_OPAQUE      = 255;

	constexpr std::size_t
		RGBA_COMPONENTS  = 4,
		RGB_COMPONENTS   = 3,
		MAX_PALETTE_SIZE = 256;

	// Validate color type — this function only handles RGB and RGBA input.
	if (raw_color_type != LCT_RGB && raw_color_type != LCT_RGBA) {
		throw std::runtime_error(std::format(
			"convertToPalette: Unsupported color type {}. Expected RGB or RGBA.",
			static_cast<unsigned>(raw_color_type)));
	}

	const std::size_t palette_size = stats.numcolors;

	if (palette_size == 0) {
		throw std::runtime_error("convertToPalette: Palette is empty.");
	}
	if (palette_size > MAX_PALETTE_SIZE) {
		throw std::runtime_error(std::format(
			"convertToPalette: Palette has {} colors, exceeds maximum of {}.",
			palette_size, MAX_PALETTE_SIZE));
	}

	const std::size_t channels =
		(raw_color_type == LCT_RGBA) ? RGBA_COMPONENTS : RGB_COMPONENTS;

	// Build a lookup from RGBA key -> palette index.
	std::unordered_map<uint32_t, Byte> color_to_index;
	color_to_index.reserve(palette_size);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		const uint32_t key =
			(static_cast<uint32_t>(src[0]) << 24) |
			(static_cast<uint32_t>(src[1]) << 16) |
			(static_cast<uint32_t>(src[2]) << 8)  |
			 static_cast<uint32_t>(src[3]);
		color_to_index[key] = static_cast<Byte>(i);
	}

	// Map each pixel to its palette index.
	const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
	vBytes indexed_image(pixel_count);

	for (std::size_t i = 0; i < pixel_count; ++i) {
		const std::size_t offset = i * channels;
		const uint32_t key =
			(static_cast<uint32_t>(image[offset])     << 24) |
			(static_cast<uint32_t>(image[offset + 1]) << 16) |
			(static_cast<uint32_t>(image[offset + 2]) << 8)  |
			((channels == RGBA_COMPONENTS)
				? static_cast<uint32_t>(image[offset + 3])
				: ALPHA_OPAQUE);

		auto it = color_to_index.find(key);
		if (it == color_to_index.end()) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i, key));
		}
		indexed_image[i] = it->second;
	}

	// Encode as 8-bit palette PNG.
	lodepng::State encode_state;
	encode_state.info_raw.colortype       = LCT_PALETTE;
	encode_state.info_raw.bitdepth        = PALETTE_BIT_DEPTH;
	encode_state.info_png.color.colortype = LCT_PALETTE;
	encode_state.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
	encode_state.encoder.auto_convert     = 0;

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* p = &stats.palette[i * RGBA_COMPONENTS];
		lodepng_palette_add(&encode_state.info_png.color, p[0], p[1], p[2], p[3]);
		lodepng_palette_add(&encode_state.info_raw, p[0], p[1], p[2], p[3]);
	}

	vBytes output;
	unsigned error = lodepng::encode(output, indexed_image.data(), width, height, encode_state);
	if (error) {
		throw std::runtime_error(std::format("LodePNG encode error: {}", error));
	}
	image_file_vec = std::move(output);
}

// ============================================================================
// Internal: Strip non-essential chunks, keeping only IHDR, PLTE, tRNS, IDAT, IEND
// ============================================================================

void stripAndCopyChunks(vBytes& image_file_vec, Byte color_type) {

	constexpr auto
		PLTE_SIG = std::to_array<Byte>({ 0x50, 0x4C, 0x54, 0x45 }),
		TRNS_SIG = std::to_array<Byte>({ 0x74, 0x52, 0x4E, 0x53 }),
		IDAT_SIG = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });

	constexpr auto IEND_SIG = std::to_array<Byte>({ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 });

	constexpr std::size_t
		PNG_HEADER_AND_IHDR_SIZE = 33,
		CHUNK_OVERHEAD           = 12,  // length(4) + name(4) + crc(4)
		LENGTH_FIELD_SIZE        = 4,
		IEND_CHUNK_SIZE          = 12;

	const std::size_t file_size = image_file_vec.size();

	if (file_size < PNG_HEADER_AND_IHDR_SIZE + IEND_CHUNK_SIZE) {
		throw std::runtime_error("PNG Error: File too small to contain valid PNG structure.");
	}

	// Truncate any trailing data after IEND.
	if (auto pos_opt = searchSig(image_file_vec, IEND_SIG)) {
		const std::size_t end_index = *pos_opt + IEND_SIG.size();
		if (end_index <= file_size) {
			image_file_vec.resize(end_index);
		}
	}

	vBytes cleaned_png;
	cleaned_png.reserve(image_file_vec.size());

	// Copy PNG signature + IHDR chunk.
	cleaned_png.insert(
		cleaned_png.end(),
		image_file_vec.begin(),
		image_file_vec.begin() + PNG_HEADER_AND_IHDR_SIZE);

	// Copy all chunks of a given type.
	auto copy_chunks_of_type = [&](std::span<const Byte> chunk_sig) {
		std::size_t search_pos = PNG_HEADER_AND_IHDR_SIZE;

		while (auto chunk_opt = searchSig(image_file_vec, chunk_sig, search_pos)) {
			const std::size_t name_index = *chunk_opt;

			if (name_index < LENGTH_FIELD_SIZE) {
				throw std::runtime_error("PNG Error: Chunk found before valid length field.");
			}

			const std::size_t
				chunk_start      = name_index - LENGTH_FIELD_SIZE,
				data_length      = getValue(image_file_vec, chunk_start, 4),
				total_chunk_size = data_length + CHUNK_OVERHEAD;

			if (chunk_start + total_chunk_size > image_file_vec.size()) {
				throw std::runtime_error(std::format(
					"PNG Error: Chunk at offset 0x{:X} claims length {} but exceeds file size.",
					chunk_start, data_length));
			}

			cleaned_png.insert(
				cleaned_png.end(),
				image_file_vec.begin() + chunk_start,
				image_file_vec.begin() + chunk_start + total_chunk_size);

			search_pos = chunk_start + total_chunk_size;
		}
	};

	if (color_type == INDEXED_PLTE) {
		copy_chunks_of_type(PLTE_SIG);
	}
	copy_chunks_of_type(TRNS_SIG);
	copy_chunks_of_type(IDAT_SIG);

	// Append IEND chunk.
	cleaned_png.insert(
		cleaned_png.end(),
		image_file_vec.end() - IEND_CHUNK_SIZE,
		image_file_vec.end());

	image_file_vec = std::move(cleaned_png);
}

} // anonymous namespace

// ============================================================================
// Public: Optimize image for polyglot embedding
// ============================================================================

void optimizeImage(vBytes& image_file_vec) {
	constexpr uint16_t MIN_RGB_COLORS = 257;

	lodepng::State state;
	state.decoder.zlibsettings.ignore_adler32 = 1;

	vBytes image;
	unsigned
		width = 0, height = 0,
		error = lodepng::decode(image, width, height, state, image_file_vec);
	if (error) {
		throw std::runtime_error(std::format("LodePNG decode error {}: {}", error, lodepng_error_text(error)));
	}

	LodePNGColorStats stats;
	lodepng_color_stats_init(&stats);
	stats.allow_palette   = 1;
	stats.allow_greyscale = 0;
	error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
	if (error) {
		throw std::runtime_error(std::format("LodePNG stats error: {}", error));
	}

	Byte color_type = static_cast<Byte>(state.info_png.color.colortype);
	const bool is_truecolor = (color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA);
	const bool can_palettize = is_truecolor && (stats.numcolors < MIN_RGB_COLORS);

	if (can_palettize) {
		convertToPalette(image_file_vec, image, width, height, stats, state.info_raw.colortype);
		color_type = INDEXED_PLTE;
	} else {
		stripAndCopyChunks(image_file_vec, color_type);
	}

	// A selection of problem metacharacters may appear within the cover image's
	// width/height dimension fields of the IHDR chunk or within the 4-byte IHDR
	// chunk's CRC field. These characters will break the Linux extraction script.
	// If any are detected, decrease the image dimensions by 1 pixel, repeated
	// if necessary, until no problem characters remain.

	// IHDR layout (offsets from start of file):
	//   0x10..0x13  Width  (4 bytes, big-endian)
	//   0x14..0x17  Height (4 bytes, big-endian)
	//   0x18..0x1C  Bit depth, color type, compression, filter, interlace (fixed, safe)
	//   0x1D..0x20  CRC    (4 bytes)
	//
	// Only width, height, and CRC change on resize.
	// We check just those bytes for problem metacharacters, skipping the fixed fields in between.

	constexpr std::size_t
		WIDTH_START = 0x10,
		HEIGHT_END  = 0x18,  // Exclusive: covers width (0x10..0x13) + height (0x14..0x17).
		CRC_START   = 0x1D,
		CRC_END     = 0x21;  // Exclusive.

	constexpr uint16_t
		MIN_DIMS              = 68,
		MAX_PLTE_DIMS         = 4096,
		MAX_RGB_DIMS          = 900,
		MAX_RESIZE_ITERATIONS = 200;

	auto hasProblemCharacter = [&]() {
		auto check = [&](std::size_t start, std::size_t length) {
			return std::ranges::any_of(
				image_file_vec | std::views::drop(start) | std::views::take(length),
				[&](Byte b) { return std::ranges::contains(LINUX_PROBLEM_METACHARACTERS, b); });
		};
		return check(WIDTH_START, HEIGHT_END - WIDTH_START) || check(CRC_START, CRC_END - CRC_START);
	};

	unsigned iterations = 0;
	while (hasProblemCharacter()) {
		if (++iterations > MAX_RESIZE_ITERATIONS) {
			throw std::runtime_error(
				"Image Error: Could not eliminate problem characters from IHDR "
				"within the resize iteration limit.");
		}
		resizeImage(image_file_vec);
	}

	// After potential resizing, RGBA may have been re-encoded as RGB by lodepng.
	color_type = (color_type == TRUECOLOR_RGBA) ? TRUECOLOR_RGB : color_type;

	const bool hasValidColorType = (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB);

	auto checkDimensions = [&](Byte target_color, uint16_t max_dims) {
		return color_type == target_color
			&& width  >= MIN_DIMS && width  <= max_dims
			&& height >= MIN_DIMS && height <= max_dims;
	};

	const bool hasValidDimensions = checkDimensions(TRUECOLOR_RGB, MAX_RGB_DIMS) || checkDimensions(INDEXED_PLTE, MAX_PLTE_DIMS);

	if (!hasValidColorType || !hasValidDimensions) {
		if (!hasValidColorType) {
			std::print(stderr,
				"\nImage File Error: Color type of cover image is not supported.\n\n"
				"Supported types: PNG-32/24 (Truecolor) or PNG-8 (Indexed-Color).");
		} else {
			std::print(stderr,
				"\nImage File Error: Dimensions of cover image are not within the supported range.\n\n"
				"Supported ranges:\n"
				" - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n"
				" - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]\n");
		}
		throw std::runtime_error("Incompatible image. Aborting.");
	}
}
