#include "image_processing_internal.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define PDVZIP_HAS_X86_SIMD 1
#else
#define PDVZIP_HAS_X86_SIMD 0
#endif

namespace {

struct ResizeAxisSample {
	unsigned lower{};
	unsigned upper{};
	unsigned nearest{};
	float lower_weight{};
	float upper_weight{};
};

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

}  // namespace

namespace image_processing_internal {

void resizeImage(vBytes& image_file_vec, unsigned new_width, unsigned new_height) {
	constexpr unsigned MIN_DIMENSION = 68;

	// Decode preserving original color type (no conversion to RGBA).
	lodepng::State decode_state;
	decode_state.decoder.color_convert = 0;

	vBytes pixels;
	unsigned
		width = 0,
		height = 0,
		error = lodepng::decode(pixels, width, height, decode_state, image_file_vec);

	throwLodepngError("LodePNG decode error", error);

	if (new_width < MIN_DIMENSION || new_height < MIN_DIMENSION) {
		throw std::runtime_error("Image Error: Resize target dimensions are below the minimum.");
	}
	if (new_width > width || new_height > height) {
		throw std::runtime_error("Image Error: Resize target must not exceed source dimensions.");
	}
	if (new_width == width && new_height == height) {
		// Nothing to resample — keep the current encoding untouched.
		return;
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

	encode_state.info_raw.colortype = png_color.colortype;
	encode_state.info_raw.bitdepth = encode_bitdepth;
	encode_state.info_png.color.colortype = png_color.colortype;
	encode_state.info_png.color.bitdepth = encode_bitdepth;
	encode_state.encoder.auto_convert = 0;

	if (is_palette) {
		copyPalette(png_color.palette, png_color.palettesize, encode_state.info_png.color);
		copyPalette(png_color.palette, png_color.palettesize, encode_state.info_raw);
	}

	vBytes output;
	error = lodepng::encode(output, resized, new_width, new_height, encode_state);
	throwLodepngError("LodePNG encode error", error);

	image_file_vec = std::move(output);
}

}  // namespace image_processing_internal
