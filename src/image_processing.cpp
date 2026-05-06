#include "image_processing_internal.h"

using image_processing_internal::copyPalette;
using image_processing_internal::resizeImage;
using image_processing_internal::throwLodepngError;

namespace {

constexpr std::size_t RGBA_COMPONENTS = image_processing_internal::RGBA_COMPONENTS;

[[nodiscard]] constexpr std::uint32_t pngChunkType(char a, char b, char c, char d) {
	return (static_cast<std::uint32_t>(static_cast<Byte>(a)) << 24)
		| (static_cast<std::uint32_t>(static_cast<Byte>(b)) << 16)
		| (static_cast<std::uint32_t>(static_cast<Byte>(c)) << 8)
		| static_cast<std::uint32_t>(static_cast<Byte>(d));
}

[[nodiscard]] constexpr std::uint32_t packRgbaKey(Byte red, Byte green, Byte blue, Byte alpha) {
	return (static_cast<std::uint32_t>(red) << 24) | (static_cast<std::uint32_t>(green) << 16)
		| (static_cast<std::uint32_t>(blue) << 8) | static_cast<std::uint32_t>(alpha);
}

struct PngIhdr {
	std::size_t width;
	std::size_t height;
	Byte bit_depth;
	Byte color_type;
};

class PaletteIndexTable {
	static constexpr std::size_t TABLE_SIZE = 512;
	static constexpr std::size_t TABLE_MASK = TABLE_SIZE - 1;

	std::array<std::uint32_t, TABLE_SIZE> keys_{};
	std::array<Byte, TABLE_SIZE> values_{};
	std::array<bool, TABLE_SIZE> occupied_{};

	[[nodiscard]] static constexpr std::size_t hash(std::uint32_t key) {
		key ^= key >> 16;
		key *= 0x7feb352dU;
		key ^= key >> 15;
		key *= 0x846ca68bU;
		key ^= key >> 16;
		return static_cast<std::size_t>(key) & TABLE_MASK;
	}

public:
	void insertIfAbsent(std::uint32_t key, Byte value) {
		std::size_t slot = hash(key);
		for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
			if (!occupied_[slot]) {
				occupied_[slot] = true;
				keys_[slot] = key;
				values_[slot] = value;
				return;
			}
			if (keys_[slot] == key) {
				return;
			}
			slot = (slot + 1) & TABLE_MASK;
		}
		throw std::runtime_error("convertToPalette: Palette lookup table is full.");
	}

	[[nodiscard]] bool find(std::uint32_t key, Byte& value) const {
		std::size_t slot = hash(key);
		for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
			if (!occupied_[slot]) {
				return false;
			}
			if (keys_[slot] == key) {
				value = values_[slot];
				return true;
			}
			slot = (slot + 1) & TABLE_MASK;
		}
		return false;
	}
};

[[nodiscard]] PngIhdr readPngIhdr(std::span<const Byte> png_data) {
	constexpr auto PNG_SIG = std::to_array<Byte>({
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
	});

	constexpr std::size_t
		MIN_IHDR_TOTAL_SIZE = 33,
		IHDR_LENGTH_INDEX   = 8,
		IHDR_NAME_INDEX     = 12,
		WIDTH_INDEX         = 16,
		HEIGHT_INDEX        = 20,
		BIT_DEPTH_INDEX     = 24,
		COLOR_TYPE_INDEX    = 25;

	if (png_data.size() < MIN_IHDR_TOTAL_SIZE) {
		throw std::runtime_error("PNG Error: File too small to contain a valid IHDR chunk.");
	}

	if (!std::equal(PNG_SIG.begin(), PNG_SIG.end(), png_data.begin())) {
		throw std::runtime_error("PNG Error: Invalid signature.");
	}

	if (readValueAt(png_data, IHDR_NAME_INDEX, 4) != pngChunkType('I', 'H', 'D', 'R')) {
		throw std::runtime_error("PNG Error: First chunk is not IHDR.");
	}
	if (readValueAt(png_data, IHDR_LENGTH_INDEX, 4) != 13) {
		throw std::runtime_error("PNG Error: IHDR chunk length is invalid.");
	}

	return PngIhdr{
		.width      = readValueAt(png_data, WIDTH_INDEX, 4),
		.height     = readValueAt(png_data, HEIGHT_INDEX, 4),
		.bit_depth  = png_data[BIT_DEPTH_INDEX],
		.color_type = png_data[COLOR_TYPE_INDEX]
	};
}

void validateInputPngForDecode(const PngIhdr& ihdr) {
	constexpr std::size_t
		MIN_DIMENSION = 68,
		MAX_DIMENSION = 4096;

	const bool supported_color_type =
		ihdr.color_type == INDEXED_PLTE
		|| ihdr.color_type == TRUECOLOR_RGB
		|| ihdr.color_type == TRUECOLOR_RGBA;
	if (!supported_color_type) {
		throw std::runtime_error("Image File Error: Unsupported PNG color type.");
	}

	const bool supported_bit_depth =
		(ihdr.color_type == INDEXED_PLTE && (ihdr.bit_depth == 1 || ihdr.bit_depth == 2 || ihdr.bit_depth == 4 || ihdr.bit_depth == 8))
		|| ((ihdr.color_type == TRUECOLOR_RGB || ihdr.color_type == TRUECOLOR_RGBA) && ihdr.bit_depth == 8);
	if (!supported_bit_depth) {
		throw std::runtime_error("Image File Error: Unsupported PNG bit depth.");
	}

	if (ihdr.width < MIN_DIMENSION || ihdr.height < MIN_DIMENSION) {
		throw std::runtime_error("Image File Error: Cover image dimensions are too small.");
	}
	if (ihdr.width > MAX_DIMENSION || ihdr.height > MAX_DIMENSION) {
		throw std::runtime_error("Image File Error: Cover image dimensions exceed the supported limit.");
	}
}

// ============================================================================
// Internal: Convert truecolor image to indexed palette
// ============================================================================

void convertToPalette(vBytes& image_file_vec, const vBytes& image, unsigned width, unsigned height, const LodePNGColorStats& stats, LodePNGColorType raw_color_type) {

	constexpr Byte
		PALETTE_BIT_DEPTH = 8,
		ALPHA_OPAQUE      = 255;

	constexpr std::size_t
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

	PaletteIndexTable color_to_index;

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		color_to_index.insertIfAbsent(packRgbaKey(src[0], src[1], src[2], src[3]), static_cast<Byte>(i));
	}

	// Map each pixel to its palette index.
	const std::size_t pixel_count = checkedMultiply(
		static_cast<std::size_t>(width),
		static_cast<std::size_t>(height),
		"Image Error: Indexed image dimensions overflow.");
	const std::size_t image_span = checkedMultiply(
		pixel_count,
		channels,
		"Image Error: Indexed image byte span overflow.");
	if (image.size() < image_span) {
		throw std::runtime_error("Image Error: Decoded image buffer is truncated.");
	}
	vBytes indexed_image(pixel_count);

	const Byte* pixel = image.data();
	for (std::size_t i = 0; i < pixel_count; ++i) {
		const std::uint32_t key = packRgbaKey(
			pixel[0],
			pixel[1],
			pixel[2],
			(channels == RGBA_COMPONENTS) ? pixel[3] : ALPHA_OPAQUE
		);

		Byte palette_index = 0;
		if (!color_to_index.find(key, palette_index)) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i, key));
		}
		indexed_image[i] = palette_index;
		pixel += channels;
	}

	// Encode as 8-bit palette PNG.
	lodepng::State encode_state;
	encode_state.info_raw.colortype       = LCT_PALETTE;
	encode_state.info_raw.bitdepth        = PALETTE_BIT_DEPTH;
	encode_state.info_png.color.colortype = LCT_PALETTE;
	encode_state.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
	encode_state.encoder.auto_convert     = 0;

	copyPalette(stats.palette, palette_size, encode_state.info_png.color);
	copyPalette(stats.palette, palette_size, encode_state.info_raw);

	vBytes output;
	unsigned error = lodepng::encode(output, indexed_image.data(), width, height, encode_state);
	throwLodepngError("LodePNG encode error", error);
	image_file_vec = std::move(output);
}

// ============================================================================
// Internal: Strip non-essential chunks, keeping only IHDR, PLTE, tRNS, IDAT, IEND
// ============================================================================

void stripAndCopyChunks(vBytes& image_file_vec, Byte color_type) {

	constexpr std::uint32_t
		IHDR_TYPE = pngChunkType('I', 'H', 'D', 'R'),
		PLTE_TYPE = pngChunkType('P', 'L', 'T', 'E'),
		TRNS_TYPE = pngChunkType('t', 'R', 'N', 'S'),
		IDAT_TYPE = pngChunkType('I', 'D', 'A', 'T'),
		IEND_TYPE = pngChunkType('I', 'E', 'N', 'D');

	constexpr std::size_t
		PNG_SIGNATURE_SIZE       = 8,
		CHUNK_OVERHEAD           = 12,  // length(4) + name(4) + crc(4)
		LENGTH_FIELD_SIZE        = 4,
		TYPE_FIELD_SIZE          = 4;

	// In-place memmove compaction: walk chunks once, shifting kept chunks toward
	// the front. Avoids allocating a second image-sized buffer.
	std::size_t read_pos = PNG_SIGNATURE_SIZE;
	std::size_t write_pos = PNG_SIGNATURE_SIZE;
	bool saw_idat = false;
	bool saw_iend = false;

	while (read_pos < image_file_vec.size()) {
		if (CHUNK_OVERHEAD > image_file_vec.size() - read_pos) {
			throw std::runtime_error("PNG Error: Truncated chunk header.");
		}

		const std::size_t data_length = readValueAt(image_file_vec, read_pos, LENGTH_FIELD_SIZE);
		if (data_length > image_file_vec.size() - read_pos - CHUNK_OVERHEAD) {
			throw std::runtime_error(std::format(
				"PNG Error: Chunk at offset 0x{:X} exceeds file size.",
				read_pos));
		}

		const std::size_t name_index = read_pos + LENGTH_FIELD_SIZE;
		const std::uint32_t chunk_type = static_cast<std::uint32_t>(
			readValueAt(image_file_vec, name_index, TYPE_FIELD_SIZE));
		const bool is_idat = chunk_type == IDAT_TYPE;
		const bool is_iend = chunk_type == IEND_TYPE;
		const bool keep_chunk = chunk_type == IHDR_TYPE || is_idat || is_iend || chunk_type == TRNS_TYPE
			|| (color_type == INDEXED_PLTE && chunk_type == PLTE_TYPE);
		const std::size_t total_chunk_size = CHUNK_OVERHEAD + data_length;

		if (keep_chunk) {
			if (write_pos != read_pos) {
				std::memmove(
					image_file_vec.data() + write_pos,
					image_file_vec.data() + read_pos,
					total_chunk_size);
			}
			write_pos += total_chunk_size;
		}

		saw_idat = saw_idat || is_idat;
		read_pos += total_chunk_size;

		if (is_iend) {
			saw_iend = true;
			break;
		}
	}

	if (!saw_idat) {
		throw std::runtime_error("PNG Error: No IDAT chunk found.");
	}
	if (!saw_iend) {
		throw std::runtime_error("PNG Error: Missing IEND chunk.");
	}

	image_file_vec.resize(write_pos);
}

} // anonymous namespace

// ============================================================================
// Public: Optimize image for polyglot embedding
// ============================================================================

void optimizeImage(vBytes& image_file_vec) {
	constexpr uint16_t MIN_RGB_COLORS = 257;

	validateInputPngForDecode(readPngIhdr(image_file_vec));

	lodepng::State state;

	vBytes image;
	unsigned
		width = 0, height = 0,
		error = lodepng::decode(image, width, height, state, image_file_vec);
	throwLodepngError("LodePNG decode error", error, true);

	LodePNGColorStats stats;
	lodepng_color_stats_init(&stats);
	stats.allow_palette   = 1;
	stats.allow_greyscale = 0;
	error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
	throwLodepngError("LodePNG stats error", error);

	const Byte input_color_type = static_cast<Byte>(state.info_png.color.colortype);
	const bool can_palettize = (input_color_type == TRUECOLOR_RGB || input_color_type == TRUECOLOR_RGBA)
		&& (stats.numcolors < MIN_RGB_COLORS);

	if (can_palettize) {
		convertToPalette(image_file_vec, image, width, height, stats, state.info_raw.colortype);
	} else {
		stripAndCopyChunks(image_file_vec, input_color_type);
	}

	// Problem metacharacters can appear in IHDR width/height bytes or the IHDR CRC
	// and break the Linux extraction script. Compute safe dimensions analytically
	// over a 17-byte scratch buffer, then resize at most once — far cheaper than
	// repeatedly decoding and re-encoding the whole PNG.
	// Checked ranges: width+height at 0x10..0x17 and CRC at 0x1D..0x20.

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
		if (image_file_vec.size() < CRC_END) {
			throw std::runtime_error("PNG Error: IHDR chunk is truncated after optimization.");
		}
		auto range_has = [&](std::size_t start, std::size_t end) {
			const auto bytes = std::span<const Byte>(image_file_vec).subspan(start, end - start);
			return std::ranges::any_of(bytes, isLinuxProblemMetacharacter);
		};
		return range_has(WIDTH_START, HEIGHT_END) || range_has(CRC_START, CRC_END);
	};

	// Predicts whether an IHDR with the given dims and fixed fields will be
	// metacharacter-safe, without touching the encoded image.
	auto candidateIsSafe = [&](unsigned w, unsigned h, Byte bit_depth, Byte color_type) {
		Byte ihdr[17] = {
			'I', 'H', 'D', 'R',
			static_cast<Byte>((w >> 24) & 0xFF), static_cast<Byte>((w >> 16) & 0xFF),
			static_cast<Byte>((w >> 8) & 0xFF),  static_cast<Byte>(w & 0xFF),
			static_cast<Byte>((h >> 24) & 0xFF), static_cast<Byte>((h >> 16) & 0xFF),
			static_cast<Byte>((h >> 8) & 0xFF),  static_cast<Byte>(h & 0xFF),
			bit_depth, color_type,
			0, 0, 0  // compression, filter, interlace — lodepng default is 0 for all.
		};
		const auto dimension_bytes = std::span<const Byte>(ihdr).subspan(4, 8);
		if (std::ranges::any_of(dimension_bytes, isLinuxProblemMetacharacter)) {
			return false;
		}
		const uint32_t crc = lodepng_crc32(ihdr, sizeof(ihdr));
		const Byte crc_bytes[4] = {
			static_cast<Byte>((crc >> 24) & 0xFF),
			static_cast<Byte>((crc >> 16) & 0xFF),
			static_cast<Byte>((crc >> 8) & 0xFF),
			static_cast<Byte>(crc & 0xFF),
		};
		return !std::ranges::any_of(crc_bytes, isLinuxProblemMetacharacter);
	};

	if (hasProblemCharacter()) {
		const PngIhdr current = readPngIhdr(image_file_vec);

		std::optional<std::pair<unsigned, unsigned>> target;
		for (unsigned delta = 1; delta <= MAX_RESIZE_ITERATIONS; ++delta) {
			if (current.width < MIN_DIMS + delta || current.height < MIN_DIMS + delta) {
				break;
			}
			const unsigned w = static_cast<unsigned>(current.width)  - delta;
			const unsigned h = static_cast<unsigned>(current.height) - delta;
			if (candidateIsSafe(w, h, current.bit_depth, current.color_type)) {
				target = std::make_pair(w, h);
				break;
			}
		}

		if (!target) {
			throw std::runtime_error(
				"Image Error: Could not eliminate problem characters from IHDR "
				"within the resize iteration limit.");
		}

		resizeImage(image_file_vec, target->first, target->second);

		// Sanity-check that the encoder produced the IHDR we predicted.
		if (hasProblemCharacter()) {
			throw std::runtime_error(
				"Image Error: Post-resize IHDR still contains problem characters. "
				"Encoder produced an unexpected IHDR layout.");
		}
	}

	const PngIhdr final_ihdr = readPngIhdr(image_file_vec);
	const Byte final_color_type = final_ihdr.color_type;
	const std::size_t final_width = final_ihdr.width;
	const std::size_t final_height = final_ihdr.height;

	const std::size_t max_dims =
		(final_color_type == INDEXED_PLTE) ? MAX_PLTE_DIMS
		: (final_color_type == TRUECOLOR_RGB || final_color_type == TRUECOLOR_RGBA) ? MAX_RGB_DIMS
		: 0;
	const bool has_valid_dimensions = max_dims != 0
		&& final_width >= MIN_DIMS && final_width <= max_dims
		&& final_height >= MIN_DIMS && final_height <= max_dims;

	if (!has_valid_dimensions) {
		if (max_dims == 0) {
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
