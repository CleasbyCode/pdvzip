#include "pdvzip.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define PDVZIP_HAS_X86_SIMD 1
#else
#define PDVZIP_HAS_X86_SIMD 0
#endif

namespace {

constexpr std::size_t RGBA_COMPONENTS = 4;

void throwLodepngError(std::string_view context, unsigned error, bool include_error_text = false) {
	if (!error) return;
	throw std::runtime_error(include_error_text
		? std::format("{} {}: {}", context, error, lodepng_error_text(error))
		: std::format("{}: {}", context, error));
}

void copyPalette(const Byte* palette, std::size_t count, LodePNGColorMode& target) {
	for (std::size_t i = 0; i < count; ++i) {
		const Byte* p = &palette[i * RGBA_COMPONENTS];
		throwLodepngError("LodePNG palette setup error", lodepng_palette_add(&target, p[0], p[1], p[2], p[3]));
	}
}

struct ResizeAxisSample {
	unsigned lower{};
	unsigned upper{};
	unsigned nearest{};
	float lower_weight{};
	float upper_weight{};
};

[[nodiscard]] constexpr std::uint32_t packRgbaKey(Byte red, Byte green, Byte blue, Byte alpha) {
	return (static_cast<std::uint32_t>(red) << 24) | (static_cast<std::uint32_t>(green) << 16)
		| (static_cast<std::uint32_t>(blue) << 8) | static_cast<std::uint32_t>(alpha);
}

[[nodiscard]] std::vector<ResizeAxisSample> buildResizeAxisSamples(unsigned source_size, unsigned target_size) {
	constexpr double SAMPLING_OFFSET = 0.5;

	std::vector<ResizeAxisSample> samples(target_size);
	const double ratio = static_cast<double>(source_size) / static_cast<double>(target_size);

	for (unsigned i = 0; i < target_size; ++i) {
		const double source_position = std::clamp(
			(i + SAMPLING_OFFSET) * ratio - SAMPLING_OFFSET,
			0.0,
			static_cast<double>(source_size - 1)
		);
		const unsigned lower = static_cast<unsigned>(source_position);
		const float upper_weight = static_cast<float>(source_position - static_cast<double>(lower));

		samples[i] = ResizeAxisSample{
			.lower = lower,
			.upper = std::min(lower + 1, source_size - 1),
			.nearest = static_cast<unsigned>(std::round(source_position)),
			.lower_weight = 1.0f - upper_weight,
			.upper_weight = upper_weight
		};
	}

	return samples;
}

template <unsigned Channels>
inline void interpolatePixelScalar(
	Byte* destination,
	const Byte* top_left,
	const Byte* top_right,
	const Byte* bottom_left,
	const Byte* bottom_right,
	float top_left_weight,
	float top_right_weight,
	float bottom_left_weight,
	float bottom_right_weight
) {
	for (unsigned channel = 0; channel < Channels; ++channel) {
		const float value =
			top_left_weight * static_cast<float>(top_left[channel]) +
			top_right_weight * static_cast<float>(top_right[channel]) +
			bottom_left_weight * static_cast<float>(bottom_left[channel]) +
			bottom_right_weight * static_cast<float>(bottom_right[channel]);
		destination[channel] = static_cast<Byte>(std::clamp(std::round(value), 0.0f, 255.0f));
	}
}

#if PDVZIP_HAS_X86_SIMD
template <unsigned Channels>
[[nodiscard]] inline __m128 loadPixelAsFloat(const Byte* source) {
	if constexpr (Channels == 4) {
		return _mm_setr_ps(
			static_cast<float>(source[0]),
			static_cast<float>(source[1]),
			static_cast<float>(source[2]),
			static_cast<float>(source[3]));
	} else {
		return _mm_setr_ps(
			static_cast<float>(source[0]),
			static_cast<float>(source[1]),
			static_cast<float>(source[2]),
			0.0f);
	}
}

template <unsigned Channels>
inline void storeRoundedPixel(Byte* destination, __m128 value) {
	const __m128 clamped = _mm_min_ps(_mm_max_ps(value, _mm_setzero_ps()), _mm_set1_ps(255.0f));
	const __m128 biased = _mm_add_ps(clamped, _mm_set1_ps(0.5f));
	const __m128i rounded = _mm_cvttps_epi32(biased);

	alignas(16) int components[4]{};
	_mm_storeu_si128(reinterpret_cast<__m128i*>(components), rounded);

	for (unsigned channel = 0; channel < Channels; ++channel) {
		destination[channel] = static_cast<Byte>(components[channel]);
	}
}

template <unsigned Channels>
inline void interpolatePixelSimd(
	Byte* destination,
	const Byte* top_left,
	const Byte* top_right,
	const Byte* bottom_left,
	const Byte* bottom_right,
	float top_left_weight,
	float top_right_weight,
	float bottom_left_weight,
	float bottom_right_weight
) {
	const __m128 sum = _mm_add_ps(
		_mm_add_ps(
			_mm_mul_ps(loadPixelAsFloat<Channels>(top_left), _mm_set1_ps(top_left_weight)),
			_mm_mul_ps(loadPixelAsFloat<Channels>(top_right), _mm_set1_ps(top_right_weight))),
		_mm_add_ps(
			_mm_mul_ps(loadPixelAsFloat<Channels>(bottom_left), _mm_set1_ps(bottom_left_weight)),
			_mm_mul_ps(loadPixelAsFloat<Channels>(bottom_right), _mm_set1_ps(bottom_right_weight))));

	storeRoundedPixel<Channels>(destination, sum);
}
#endif

template <unsigned Channels>
inline void interpolatePixel(
	Byte* destination,
	const Byte* top_left,
	const Byte* top_right,
	const Byte* bottom_left,
	const Byte* bottom_right,
	float top_left_weight,
	float top_right_weight,
	float bottom_left_weight,
	float bottom_right_weight
) {
#if PDVZIP_HAS_X86_SIMD
	interpolatePixelSimd<Channels>(
		destination,
		top_left,
		top_right,
		bottom_left,
		bottom_right,
		top_left_weight,
		top_right_weight,
		bottom_left_weight,
		bottom_right_weight);
#else
	interpolatePixelScalar<Channels>(
		destination,
		top_left,
		top_right,
		bottom_left,
		bottom_right,
		top_left_weight,
		top_right_weight,
		bottom_left_weight,
		bottom_right_weight);
#endif
}

void resizePaletteImage(
	vBytes& resized,
	const vBytes& pixels,
	unsigned width,
	unsigned new_width,
	unsigned new_height,
	const std::vector<ResizeAxisSample>& x_samples,
	const std::vector<ResizeAxisSample>& y_samples
) {
	for (unsigned y = 0; y < new_height; ++y) {
		const std::size_t source_row = static_cast<std::size_t>(y_samples[y].nearest) * width;
		Byte* const destination_row = resized.data() + static_cast<std::size_t>(y) * new_width;

		for (unsigned x = 0; x < new_width; ++x) {
			destination_row[x] = pixels[source_row + x_samples[x].nearest];
		}
	}
}

template <unsigned Channels>
void resizeTruecolorImage(
	vBytes& resized,
	const vBytes& pixels,
	unsigned width,
	unsigned new_width,
	unsigned new_height,
	const std::vector<ResizeAxisSample>& x_samples,
	const std::vector<ResizeAxisSample>& y_samples
) {
	const std::size_t source_row_stride = static_cast<std::size_t>(width) * Channels;
	const std::size_t destination_row_stride = static_cast<std::size_t>(new_width) * Channels;

	for (unsigned y = 0; y < new_height; ++y) {
		const ResizeAxisSample& y_sample = y_samples[y];
		const Byte* const top_row = pixels.data() + static_cast<std::size_t>(y_sample.lower) * source_row_stride;
		const Byte* const bottom_row = pixels.data() + static_cast<std::size_t>(y_sample.upper) * source_row_stride;
		Byte* const destination_row = resized.data() + static_cast<std::size_t>(y) * destination_row_stride;

		for (unsigned x = 0; x < new_width; ++x) {
			const ResizeAxisSample& x_sample = x_samples[x];
			const std::size_t left_offset = static_cast<std::size_t>(x_sample.lower) * Channels;
			const std::size_t right_offset = static_cast<std::size_t>(x_sample.upper) * Channels;

			const float top_left_weight = x_sample.lower_weight * y_sample.lower_weight;
			const float top_right_weight = x_sample.upper_weight * y_sample.lower_weight;
			const float bottom_left_weight = x_sample.lower_weight * y_sample.upper_weight;
			const float bottom_right_weight = x_sample.upper_weight * y_sample.upper_weight;

			interpolatePixel<Channels>(
				destination_row + static_cast<std::size_t>(x) * Channels,
				top_row + left_offset,
				top_row + right_offset,
				bottom_row + left_offset,
				bottom_row + right_offset,
				top_left_weight,
				top_right_weight,
				bottom_left_weight,
				bottom_right_weight);
		}
	}
}

struct PngIhdr {
	std::size_t width;
	std::size_t height;
	Byte bit_depth;
	Byte color_type;
};

[[nodiscard]] PngIhdr readPngIhdr(std::span<const Byte> png_data) {
	constexpr auto PNG_SIG = std::to_array<Byte>({
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
	});
	constexpr auto IHDR_SIG = std::to_array<Byte>({ 0x49, 0x48, 0x44, 0x52 });

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

	if (!std::equal(IHDR_SIG.begin(), IHDR_SIG.end(), png_data.begin() + IHDR_NAME_INDEX)) {
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
// Internal: Resize image by 1 pixel in each dimension
// ============================================================================

void resizeImage(vBytes& image_file_vec) {

	constexpr unsigned
		MIN_DIMENSION       = 68,
		DIMENSION_REDUCTION = 1;

	// Decode preserving original color type (no conversion to RGBA).
	lodepng::State decode_state;
	decode_state.decoder.color_convert = 0;

	vBytes pixels;
	unsigned
		width  = 0,
		height = 0,
		error  = lodepng::decode(pixels, width, height, decode_state, image_file_vec);

	throwLodepngError("LodePNG decode error", error);

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
	if (!is_palette && bitdepth != 8) {
		throw std::runtime_error("Image Error: Only 8-bit truecolor/grayscale PNGs are supported.");
	}

	// Sub-byte palette images need conversion to 8-bit palette first; bilinear
	// interpolation on packed palette indices would add disproportionate complexity.
	if (is_palette && bitdepth < 8) {
		lodepng::State redecode_state;
		redecode_state.decoder.color_convert = 1;
		redecode_state.info_raw.colortype = LCT_PALETTE;
		redecode_state.info_raw.bitdepth = 8;

		// Copy the palette into info_raw so lodepng can convert.
		const auto& src_color = decode_state.info_png.color;
		copyPalette(src_color.palette, src_color.palettesize, redecode_state.info_raw);

		pixels.clear();
		error = lodepng::decode(pixels, width, height, redecode_state, image_file_vec);
		throwLodepngError("LodePNG re-decode error", error);
		// From here on, treat as 8-bit palette (1 byte per pixel index).
	}

	const unsigned new_width = width - DIMENSION_REDUCTION, new_height = height - DIMENSION_REDUCTION;

	const std::vector<ResizeAxisSample> x_samples = buildResizeAxisSamples(width, new_width);
	const std::vector<ResizeAxisSample> y_samples = buildResizeAxisSamples(height, new_height);

	// Palette images use 1 byte per pixel index; truecolor uses `channels`.
	const std::size_t bytes_per_pixel = is_palette ? 1 : channels;
	const std::size_t resized_pixel_count = checkedMultiply(
		static_cast<std::size_t>(new_width),
		static_cast<std::size_t>(new_height),
		"Image Error: Resized image dimensions overflow.");
	vBytes resized(checkedMultiply(
		resized_pixel_count,
		bytes_per_pixel,
		"Image Error: Resized image buffer size overflow."));

	if (is_palette) {
		resizePaletteImage(resized, pixels, width, new_width, new_height, x_samples, y_samples);
	} else if (channels == 4) {
		resizeTruecolorImage<4>(resized, pixels, width, new_width, new_height, x_samples, y_samples);
	} else {
		resizeTruecolorImage<3>(resized, pixels, width, new_width, new_height, x_samples, y_samples);
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
		copyPalette(png_color.palette, png_color.palettesize, encode_state.info_png.color);
		copyPalette(png_color.palette, png_color.palettesize, encode_state.info_raw);
	}

	vBytes output;
	error = lodepng::encode(output, resized, new_width, new_height, encode_state);
	throwLodepngError("LodePNG encode error", error);

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

	std::unordered_map<std::uint32_t, Byte> color_to_index;
	color_to_index.reserve(palette_size);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		color_to_index.emplace(packRgbaKey(src[0], src[1], src[2], src[3]), static_cast<Byte>(i));
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

	for (std::size_t i = 0; i < pixel_count; ++i) {
		const std::size_t offset = i * channels;
		const std::uint32_t key = packRgbaKey(
			image[offset],
			image[offset + 1],
			image[offset + 2],
			(channels == RGBA_COMPONENTS) ? image[offset + 3] : ALPHA_OPAQUE
		);

		const auto it = color_to_index.find(key);
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

	constexpr auto
		IHDR_SIG = std::to_array<Byte>({ 0x49, 0x48, 0x44, 0x52 }),
		PLTE_SIG = std::to_array<Byte>({ 0x50, 0x4C, 0x54, 0x45 }),
		TRNS_SIG = std::to_array<Byte>({ 0x74, 0x52, 0x4E, 0x53 }),
		IDAT_SIG = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 }),
		IEND_SIG = std::to_array<Byte>({ 0x49, 0x45, 0x4E, 0x44 });

	constexpr std::size_t
		PNG_SIGNATURE_SIZE       = 8,
		CHUNK_OVERHEAD           = 12,  // length(4) + name(4) + crc(4)
		LENGTH_FIELD_SIZE        = 4,
		TYPE_FIELD_SIZE          = 4;

	vBytes cleaned_png;
	cleaned_png.reserve(image_file_vec.size());
	cleaned_png.insert(
		cleaned_png.end(),
		image_file_vec.begin(),
		image_file_vec.begin() + PNG_SIGNATURE_SIZE);

	std::size_t chunk_start = PNG_SIGNATURE_SIZE;
	bool saw_idat = false;
	bool saw_iend = false;

	while (chunk_start < image_file_vec.size()) {
		if (chunk_start > image_file_vec.size() || CHUNK_OVERHEAD > image_file_vec.size() - chunk_start) {
			throw std::runtime_error("PNG Error: Truncated chunk header.");
		}

		const std::size_t data_length = readValueAt(image_file_vec, chunk_start, LENGTH_FIELD_SIZE);
		if (data_length > image_file_vec.size() - chunk_start - CHUNK_OVERHEAD) {
			throw std::runtime_error(std::format(
				"PNG Error: Chunk at offset 0x{:X} exceeds file size.",
				chunk_start));
		}

		const std::size_t name_index = chunk_start + LENGTH_FIELD_SIZE;
		const auto chunk_type = std::span<const Byte>(image_file_vec).subspan(name_index, TYPE_FIELD_SIZE);

		const bool is_ihdr = std::equal(chunk_type.begin(), chunk_type.end(), IHDR_SIG.begin());
		const bool is_plte = std::equal(chunk_type.begin(), chunk_type.end(), PLTE_SIG.begin());
		const bool is_trns = std::equal(chunk_type.begin(), chunk_type.end(), TRNS_SIG.begin());
		const bool is_idat = std::equal(chunk_type.begin(), chunk_type.end(), IDAT_SIG.begin());
		const bool is_iend = std::equal(chunk_type.begin(), chunk_type.end(), IEND_SIG.begin());

		const bool keep_chunk = is_ihdr || is_idat || is_iend || is_trns || (color_type == INDEXED_PLTE && is_plte);
		const std::size_t total_chunk_size = CHUNK_OVERHEAD + data_length;

		if (keep_chunk) {
			cleaned_png.insert(
				cleaned_png.end(),
				image_file_vec.begin() + chunk_start,
				image_file_vec.begin() + chunk_start + total_chunk_size);
		}

		saw_idat = saw_idat || is_idat;
		chunk_start += total_chunk_size;

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

	image_file_vec = std::move(cleaned_png);
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
	// and break the Linux extraction script, so resize until those fields are safe.
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

		auto check = [&](std::size_t start, std::size_t end) {
			for (std::size_t i = start; i < end; ++i) {
				if (std::ranges::contains(LINUX_PROBLEM_METACHARACTERS, image_file_vec[i])) {
					return true;
				}
			}
			return false;
		};
		return check(WIDTH_START, HEIGHT_END) || check(CRC_START, CRC_END);
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
