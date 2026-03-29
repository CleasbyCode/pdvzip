#pragma once

#include "pdvzip.h"

namespace image_processing_internal {

constexpr std::size_t RGBA_COMPONENTS = 4;

inline void throwLodepngError(std::string_view context, unsigned error, bool include_error_text = false) {
	if (!error) return;
	throw std::runtime_error(include_error_text
		? std::format("{} {}: {}", context, error, lodepng_error_text(error))
		: std::format("{}: {}", context, error));
}

inline void copyPalette(const Byte* palette, std::size_t count, LodePNGColorMode& target) {
	for (std::size_t i = 0; i < count; ++i) {
		const Byte* p = &palette[i * RGBA_COMPONENTS];
		throwLodepngError("LodePNG palette setup error", lodepng_palette_add(&target, p[0], p[1], p[2], p[3]));
	}
}

void resizeImage(vBytes& image_file_vec);

}  // namespace image_processing_internal
