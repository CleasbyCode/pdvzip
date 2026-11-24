#include "image_processing_internal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <print>
#include <stdexcept>
#include <utility>

using image_processing_internal::copyPalette;
using image_processing_internal::resizeDecodedImage;
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

constexpr uint16_t
	MIN_RGB_COLORS          = 257,
	MIN_SAFE_DIMENSION      = 68,
	MAX_PLTE_DIMENSION      = 4096,
	MAX_RGB_DIMENSION       = 900,
	// A problem byte in a dimension's high byte makes a whole 256-value band
	// of that dimension unsafe (0x0A and 0x0D are the only such bytes
	// reachable, giving the bands 2560-2815 and 3328-3583 for indexed covers up
	// to 4096). Crossing a band takes a delta of up to 256, so the budget must
	// exceed it; the bands are far enough apart that one crossing is enough.
	MAX_RESIZE_DELTA        = 260;

constexpr std::size_t
	IHDR_WIDTH_START = 0x10,
	IHDR_HEIGHT_END  = 0x18,  // Exclusive: covers width (0x10..0x13) + height (0x14..0x17).
	IHDR_CRC_START   = 0x1D,
	IHDR_CRC_END     = 0x21;  // Exclusive.

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
	// The key's value, inserting next_value for a key not yet present. Returns
	// nothing when the key is new and the table may hold no more entries.
	[[nodiscard]] std::optional<Byte> findOrInsert(std::uint32_t key, std::size_t& count, std::size_t max_count) {
		std::size_t slot = hash(key);
		for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
			if (!occupied_[slot]) {
				if (count == max_count) {
					return std::nullopt;
				}
				occupied_[slot] = true;
				keys_[slot] = key;
				values_[slot] = static_cast<Byte>(count++);
				return values_[slot];
			}
			if (keys_[slot] == key) {
				return values_[slot];
			}
			slot = (slot + 1) & TABLE_MASK;
		}
		throw std::runtime_error("convertToPalette: Palette lookup table is full.");
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

	if (readBe32(png_data, IHDR_NAME_INDEX) != pngChunkType('I', 'H', 'D', 'R')) {
		throw std::runtime_error("PNG Error: First chunk is not IHDR.");
	}
	if (readBe32(png_data, IHDR_LENGTH_INDEX) != 13) {
		throw std::runtime_error("PNG Error: IHDR chunk length is invalid.");
	}

	return PngIhdr{
		.width      = readBe32(png_data, WIDTH_INDEX),
		.height     = readBe32(png_data, HEIGHT_INDEX),
		.bit_depth  = png_data[BIT_DEPTH_INDEX],
		.color_type = png_data[COLOR_TYPE_INDEX]
	};
}

void validateInputPngForDecode(const PngIhdr& ihdr) {
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

	if (ihdr.width < MIN_SAFE_DIMENSION || ihdr.height < MIN_SAFE_DIMENSION) {
		throw std::runtime_error("Image File Error: Cover image dimensions are too small.");
	}
	if (ihdr.width > MAX_PLTE_DIMENSION || ihdr.height > MAX_PLTE_DIMENSION) {
		throw std::runtime_error("Image File Error: Cover image dimensions exceed the supported limit.");
	}
}

// ============================================================================
// Internal: Convert truecolor image to indexed palette
// ============================================================================

// Owns a LodePNG color mode (and so its palette allocation).
class OwnedColorMode {
public:
	OwnedColorMode() { lodepng_color_mode_init(&mode_); }
	~OwnedColorMode() { lodepng_color_mode_cleanup(&mode_); }
	OwnedColorMode(const OwnedColorMode&) = delete;
	OwnedColorMode& operator=(const OwnedColorMode&) = delete;

	[[nodiscard]] LodePNGColorMode& get() { return mode_; }

private:
	LodePNGColorMode mode_;
};

// Map a natively decoded 8-bit RGB or RGBA image to 8-bit palette indices in
// one pass, numbering colors (alpha included; an RGB color key reads as alpha 0,
// as LodePNG's RGBA conversion gives) in order of first appearance. Stops at
// the first color past 256 and returns false: such an image stays truecolor.
// On success the caller encodes the indices, so a safety resize can run on
// them before the only encode.
[[nodiscard]] bool convertToPalette(
	vBytes& indexed_image,
	LodePNGColorMode& palette_mode,
	const vBytes& image,
	unsigned width,
	unsigned height,
	const LodePNGColorMode& pixel_mode) {

	constexpr unsigned PALETTE_BIT_DEPTH = 8;
	constexpr Byte
		ALPHA_OPAQUE      = 255,
		ALPHA_TRANSPARENT = 0;
	constexpr std::size_t
		RGB_COMPONENTS   = 3,
		MAX_PALETTE_SIZE = MIN_RGB_COLORS - 1;

	// Validate color type — this function only handles 8-bit RGB and RGBA input.
	if ((pixel_mode.colortype != LCT_RGB && pixel_mode.colortype != LCT_RGBA) || pixel_mode.bitdepth != 8) {
		throw std::runtime_error(std::format(
			"convertToPalette: Unsupported color type {}. Expected RGB or RGBA.",
			static_cast<unsigned>(pixel_mode.colortype)));
	}
	const bool has_alpha = pixel_mode.colortype == LCT_RGBA;
	const std::size_t channels = has_alpha ? RGBA_COMPONENTS : RGB_COMPONENTS;
	const bool has_color_key = !has_alpha && pixel_mode.key_defined != 0;

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

	PaletteIndexTable color_to_index;
	std::array<Byte, MAX_PALETTE_SIZE * RGBA_COMPONENTS> palette{};
	std::size_t palette_size = 0;

	// Reserve rather than size the index buffer: most photos stop within the
	// first rows, and value-initializing it would touch every page first.
	indexed_image.clear();
	indexed_image.reserve(pixel_count);

	std::uint32_t previous_key = 0;
	Byte previous_index = 0;
	const Byte* pixel = image.data();
	for (std::size_t i = 0; i < pixel_count; ++i, pixel += channels) {
		Byte alpha = has_alpha ? pixel[3] : ALPHA_OPAQUE;
		if (has_color_key && pixel[0] == pixel_mode.key_r && pixel[1] == pixel_mode.key_g
			&& pixel[2] == pixel_mode.key_b) {
			alpha = ALPHA_TRANSPARENT;
		}
		const std::uint32_t key = packRgbaKey(pixel[0], pixel[1], pixel[2], alpha);

		// Runs of one color are common in images with few colors.
		if (i == 0 || key != previous_key) {
			const std::size_t before = palette_size;
			const auto index = color_to_index.findOrInsert(key, palette_size, MAX_PALETTE_SIZE);
			if (!index) {
				return false;
			}
			if (palette_size != before) {
				std::copy_n(std::array{ pixel[0], pixel[1], pixel[2], alpha }.begin(), RGBA_COMPONENTS,
					palette.begin() + static_cast<std::ptrdiff_t>(before * RGBA_COMPONENTS));
			}
			previous_key = key;
			previous_index = *index;
		}
		indexed_image.push_back(previous_index);
	}

	if (palette_size == 0) {
		throw std::runtime_error("convertToPalette: Palette is empty.");
	}

	palette_mode.colortype = LCT_PALETTE;
	palette_mode.bitdepth  = PALETTE_BIT_DEPTH;
	copyPalette(palette.data(), palette_size, palette_mode);
	return true;
}

// Encode 8-bit palette indices as an 8-bit indexed PNG.
void encodePaletteImage(
	vBytes& image_file_vec,
	const vBytes& indexed_image,
	unsigned width,
	unsigned height,
	const LodePNGColorMode& palette_mode) {

	lodepng::State encode_state;
	image_processing_internal::useSystemZlib(encode_state);
	encode_state.encoder.auto_convert = 0;
	throwLodepngError("LodePNG palette setup error",
		lodepng_color_mode_copy(&encode_state.info_raw, &palette_mode));
	throwLodepngError("LodePNG palette setup error",
		lodepng_color_mode_copy(&encode_state.info_png.color, &palette_mode));

	vBytes output;
	const unsigned error = lodepng::encode(output, indexed_image.data(), width, height, encode_state);
	throwLodepngError("LodePNG encode error", error);
	image_file_vec = std::move(output);
}

// ============================================================================
// Internal: Strip non-essential chunks, keeping only IHDR, PLTE, tRNS, IDAT, IEND
// ============================================================================

// Also validates the structure of the PLTE and tRNS chunks it keeps: LodePNG
// tolerates duplicates, misordering and partial palette entries, which would
// otherwise be carried into the output unchanged.
void stripAndCopyChunks(vBytes& image_file_vec, const PngIhdr& ihdr) {
	const Byte color_type = ihdr.color_type;

	constexpr std::uint32_t
		IHDR_TYPE = pngChunkType('I', 'H', 'D', 'R'),
		PLTE_TYPE = pngChunkType('P', 'L', 'T', 'E'),
		TRNS_TYPE = pngChunkType('t', 'R', 'N', 'S'),
		IDAT_TYPE = pngChunkType('I', 'D', 'A', 'T'),
		IEND_TYPE = pngChunkType('I', 'E', 'N', 'D');

	constexpr std::size_t
		PNG_SIGNATURE_SIZE       = 8,
		CHUNK_OVERHEAD           = 12,  // length(4) + name(4) + crc(4)
		LENGTH_FIELD_SIZE        = 4;

	// In-place memmove compaction: walk chunks once, shifting kept chunks toward
	// the front. Avoids allocating a second image-sized buffer.
	std::size_t read_pos = PNG_SIGNATURE_SIZE;
	std::size_t write_pos = PNG_SIGNATURE_SIZE;
	bool saw_idat = false;
	bool saw_iend = false;
	bool saw_plte = false;
	bool saw_trns = false;

	while (read_pos < image_file_vec.size()) {
		if (CHUNK_OVERHEAD > image_file_vec.size() - read_pos) {
			throw std::runtime_error("PNG Error: Truncated chunk header.");
		}

		const std::size_t data_length = readBe32(image_file_vec, read_pos);
		if (data_length > image_file_vec.size() - read_pos - CHUNK_OVERHEAD) {
			throw std::runtime_error(std::format(
				"PNG Error: Chunk at offset 0x{:X} exceeds file size.",
				read_pos));
		}

		const std::size_t name_index = read_pos + LENGTH_FIELD_SIZE;
		const std::uint32_t chunk_type = readBe32(image_file_vec, name_index);
		const bool is_idat = chunk_type == IDAT_TYPE;
		const bool is_iend = chunk_type == IEND_TYPE;
		if (is_iend && data_length != 0) {
			throw std::runtime_error("PNG Error: IEND chunk must have zero length.");
		}
		if (chunk_type == PLTE_TYPE) {
			constexpr std::size_t MAX_PALETTE_ENTRIES = 256;
			const std::size_t max_entries = color_type == INDEXED_PLTE
				? std::min(MAX_PALETTE_ENTRIES, std::size_t{1} << ihdr.bit_depth)
				: MAX_PALETTE_ENTRIES;
			if (saw_plte) {
				throw std::runtime_error("PNG Error: Duplicate PLTE chunk.");
			}
			if (saw_idat || saw_trns) {
				throw std::runtime_error("PNG Error: PLTE must precede tRNS and IDAT.");
			}
			if (data_length == 0 || data_length % 3 != 0 || data_length / 3 > max_entries) {
				throw std::runtime_error("PNG Error: PLTE has an invalid length for the image bit depth.");
			}
			saw_plte = true;
		} else if (chunk_type == TRNS_TYPE) {
			if (saw_trns) {
				throw std::runtime_error("PNG Error: Duplicate tRNS chunk.");
			}
			if (saw_idat) {
				throw std::runtime_error("PNG Error: tRNS must precede IDAT.");
			}
			saw_trns = true;
		} else if (is_idat && color_type == INDEXED_PLTE && !saw_plte) {
			throw std::runtime_error("PNG Error: Indexed image is missing PLTE before IDAT.");
		}
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

[[nodiscard]] bool pngRangeHasProblemCharacter(std::span<const Byte> png_data, std::size_t start, std::size_t end) {
	const auto bytes = png_data.subspan(start, end - start);
	return std::ranges::any_of(bytes, isLinuxProblemMetacharacter);
}

[[nodiscard]] bool ihdrHasLinuxProblemCharacter(std::span<const Byte> png_data) {
	if (png_data.size() < IHDR_CRC_END) {
		throw std::runtime_error("PNG Error: IHDR chunk is truncated after optimization.");
	}

	return pngRangeHasProblemCharacter(png_data, IHDR_WIDTH_START, IHDR_HEIGHT_END)
		|| pngRangeHasProblemCharacter(png_data, IHDR_CRC_START, IHDR_CRC_END);
}

[[nodiscard]] bool candidateIhdrIsLinuxSafe(unsigned width, unsigned height, Byte bit_depth, Byte color_type) {
	Byte ihdr[17] = {
		'I', 'H', 'D', 'R',
		static_cast<Byte>((width >> 24) & 0xFF), static_cast<Byte>((width >> 16) & 0xFF),
		static_cast<Byte>((width >> 8) & 0xFF),  static_cast<Byte>(width & 0xFF),
		static_cast<Byte>((height >> 24) & 0xFF), static_cast<Byte>((height >> 16) & 0xFF),
		static_cast<Byte>((height >> 8) & 0xFF),  static_cast<Byte>(height & 0xFF),
		bit_depth, color_type,
		0, 0, 0  // compression, filter, interlace - lodepng default is 0 for all.
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
}

[[nodiscard]] std::optional<std::pair<unsigned, unsigned>> findLinuxSafeResizeTarget(const PngIhdr& current) {
	const unsigned width = static_cast<unsigned>(current.width);
	const unsigned height = static_cast<unsigned>(current.height);
	const unsigned max_width_delta = std::min<unsigned>(MAX_RESIZE_DELTA, width - MIN_SAFE_DIMENSION);
	const unsigned max_height_delta = std::min<unsigned>(MAX_RESIZE_DELTA, height - MIN_SAFE_DIMENSION);

	// Search by the total number of removed rows/columns, so the first result
	// changes the dimensions as little as possible. For a given total, prefer
	// the candidate that best preserves the original aspect ratio. A zero delta
	// on either axis is intentional: an image already at the 68-pixel minimum on
	// one axis can still be made safe by reducing only the other axis.
	for (unsigned total_delta = 1;
		total_delta <= max_width_delta + max_height_delta;
		++total_delta) {

		std::optional<std::pair<unsigned, unsigned>> best;
		std::uint64_t best_aspect_distortion = std::numeric_limits<std::uint64_t>::max();
		const unsigned first_width_delta = total_delta > max_height_delta
			? total_delta - max_height_delta
			: 0;
		const unsigned last_width_delta = std::min(total_delta, max_width_delta);

		for (unsigned width_delta = first_width_delta;
			width_delta <= last_width_delta;
			++width_delta) {

			const unsigned height_delta = total_delta - width_delta;
			const unsigned candidate_width = width - width_delta;
			const unsigned candidate_height = height - height_delta;
			if (!candidateIhdrIsLinuxSafe(
				candidate_width, candidate_height, current.bit_depth, current.color_type)) {
				continue;
			}

			const std::uint64_t width_scale = static_cast<std::uint64_t>(width_delta) * height;
			const std::uint64_t height_scale = static_cast<std::uint64_t>(height_delta) * width;
			const std::uint64_t aspect_distortion = width_scale > height_scale
				? width_scale - height_scale
				: height_scale - width_scale;
			if (!best || aspect_distortion < best_aspect_distortion) {
				best = std::make_pair(candidate_width, candidate_height);
				best_aspect_distortion = aspect_distortion;
			}
		}

		if (best) {
			return best;
		}
	}

	return std::nullopt;
}

// Dimensions whose IHDR (width/height and CRC) avoids every problem byte.
[[nodiscard]] std::pair<unsigned, unsigned> linuxSafeResizeTarget(PngIhdr current, bool has_rgb_color_key) {
	if (current.color_type == TRUECOLOR_RGB && has_rgb_color_key) {
		// Resizing a color key produces RGBA. Predict its CRC using that output
		// format; the RGB CRC could select dimensions unsafe for the actual PNG.
		current.color_type = TRUECOLOR_RGBA;
	}
	const auto target = findLinuxSafeResizeTarget(current);
	if (!target) {
		throw std::runtime_error(
			"Image Error: Could not eliminate problem characters from IHDR "
			"within the resize iteration limit.");
	}
	return *target;
}

// Sanity-check that the encoder produced the IHDR we predicted.
void requireLinuxSafeIhdr(std::span<const Byte> png_data) {
	if (ihdrHasLinuxProblemCharacter(png_data)) {
		throw std::runtime_error(
			"Image Error: Post-resize IHDR still contains problem characters. "
			"Encoder produced an unexpected IHDR layout.");
	}
}

// Native indexed decoding does not check pixels against the palette, so an
// image could reference entries that do not exist. Reject it here rather than
// carrying an invalid PNG into the output.
void validatePaletteIndices(const vBytes& packed, unsigned width, unsigned height, const LodePNGColorMode& color) {
	if ((std::size_t{1} << color.bitdepth) <= color.palettesize) {
		return; // every representable index has a palette entry
	}
	static constexpr std::string_view MISSING_ENTRY =
		"Image File Error: Indexed PNG references a palette entry that does not exist.";
	if (color.bitdepth == 8) {
		// One byte per index: a max reduction vectorizes, where the generic
		// bit-unpacking visitor (and an early-exit search) does not.
		const std::size_t pixel_count = image_processing_internal::checkedPaletteIndexCount(packed, width, height, 8);
		Byte highest = 0;
		for (const Byte index : std::span(packed).first(pixel_count)) {
			highest = std::max(highest, index);
		}
		if (highest >= color.palettesize) {
			throw std::runtime_error(std::string(MISSING_ENTRY));
		}
		return;
	}
	image_processing_internal::forEachPaletteIndex(packed, width, height, color.bitdepth,
		[palette_size = color.palettesize](std::size_t, Byte index) {
			if (index >= palette_size) {
				throw std::runtime_error(std::string(MISSING_ENTRY));
			}
		});
}

// validateInputPngForDecode has already refused every color type other than
// indexed, RGB and RGBA, so only the dimensions are left to check.
void validateFinalImageCompatibility(const PngIhdr& ihdr) {
	const std::size_t max_dimension = ihdr.color_type == INDEXED_PLTE ? MAX_PLTE_DIMENSION : MAX_RGB_DIMENSION;
	const bool has_valid_dimensions =
		ihdr.width >= MIN_SAFE_DIMENSION && ihdr.width <= max_dimension
		&& ihdr.height >= MIN_SAFE_DIMENSION && ihdr.height <= max_dimension;

	if (has_valid_dimensions) {
		return;
	}

	std::print(stderr,
		"\nImage File Error: Dimensions of cover image are not within the supported range.\n\n"
		"Supported ranges:\n"
		" - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n"
		" - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]\n");

	throw std::runtime_error("Incompatible image. Aborting.");
}

} // anonymous namespace

// ============================================================================
// Public: Optimize image for polyglot embedding
// ============================================================================

void optimizeImage(vBytes& image_file_vec) {
	const PngIhdr input_ihdr = readPngIhdr(image_file_vec);
	validateInputPngForDecode(input_ihdr);

	lodepng::State state;
	image_processing_internal::useSystemZlib(state);
	// Decode every cover in its own format: indexed images keep their palette
	// and are validated as packed indices, and truecolor is counted for palette
	// conversion without first expanding RGB to RGBA.
	state.decoder.color_convert = 0;
	image_processing_internal::configurePngInflateLimit(state, image_file_vec);

	vBytes image;
	unsigned width = 0;
	unsigned height = 0;
	const unsigned error = lodepng::decode(image, width, height, state, image_file_vec);
	throwLodepngError("LodePNG decode error", error, true);

	// Each path below checks the final color class's size limit before any
	// safety resize. That resize only shrinks, never below the minimum, and
	// keeps the truecolor or indexed class, so it can neither rescue an
	// oversized cover (a 901-pixel truecolor image whose header happens to need
	// a resize would otherwise be accepted, while one that needs none is
	// refused) nor make a valid one invalid; and a truecolor cover too big to
	// accept is not resampled first.
	//
	// Problem metacharacters can appear in IHDR width/height bytes or the IHDR
	// CRC and break the Linux extraction script. Safe dimensions are computed
	// analytically over a 17-byte scratch buffer, and the pixels decoded above
	// are resized at most once, so no path decodes or encodes the PNG twice.
	// Checked ranges: width+height at 0x10..0x17 and CRC at 0x1D..0x20.
	if (input_ihdr.color_type != INDEXED_PLTE) {
		vBytes indexed_image;
		OwnedColorMode palette_mode;
		if (convertToPalette(indexed_image, palette_mode.get(), image, width, height, state.info_raw)) {
			vBytes().swap(image);

			// LodePNG writes zero compression, filter and interlace fields, so
			// the output IHDR is known before encoding.
			const PngIhdr palette_ihdr{
				.width = width, .height = height, .bit_depth = 8, .color_type = INDEXED_PLTE };
			validateFinalImageCompatibility(palette_ihdr);
			if (candidateIhdrIsLinuxSafe(width, height, palette_ihdr.bit_depth, palette_ihdr.color_type)) {
				encodePaletteImage(image_file_vec, indexed_image, width, height, palette_mode.get());
			} else {
				const auto [new_width, new_height] = linuxSafeResizeTarget(palette_ihdr, false);
				resizeDecodedImage(image_file_vec, std::move(indexed_image), width, height,
					palette_mode.get(), palette_mode.get(), new_width, new_height);
			}
			requireLinuxSafeIhdr(image_file_vec);
			return;
		}
	} else {
		validatePaletteIndices(image, width, height, state.info_png.color);
	}

	// Keep the cover's own encoding. Its IHDR may carry an interlace flag, so
	// check the actual header bytes rather than a predicted one.
	stripAndCopyChunks(image_file_vec, input_ihdr);
	validateFinalImageCompatibility(input_ihdr);
	if (!ihdrHasLinuxProblemCharacter(image_file_vec)) {
		return;
	}

	const bool has_rgb_color_key =
		input_ihdr.color_type == TRUECOLOR_RGB && state.info_png.color.key_defined != 0;
	const auto [new_width, new_height] = linuxSafeResizeTarget(input_ihdr, has_rgb_color_key);
	// The pixels are native, as info_raw (a copy of the PNG's mode, color key
	// included) describes; the resize turns an RGB color key into RGBA.
	resizeDecodedImage(image_file_vec, std::move(image), width, height,
		state.info_raw, state.info_png.color, new_width, new_height);
	requireLinuxSafeIhdr(image_file_vec);
}
