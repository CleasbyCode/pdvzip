// Using lodepng to resize the cover image within the vector, by reducing it's width & height by 1 pixel. 
// https://github.com/lvandeve/lodepng  (Copyright (c) 2005-2024 Lode Vandevenne).

#include "resizeImage.h"
#include "lodepng/lodepng.h"
#include <stdexcept>
#include <vector>
#include <cmath>
#include <iostream>

void resizeImage(std::vector<uint8_t>& image_vec) {
    std::vector<unsigned char> temp_vec;
    unsigned width, height;
    lodepng::State decodeState;

    // For palette images, disable color conversion to get indexed data
    decodeState.decoder.color_convert = 0; // Always disable to handle palette correctly

    // Decode the input PNG
    unsigned error = lodepng::decode(temp_vec, width, height, decodeState, image_vec);
    if (error) {
        throw std::runtime_error("Decoder error: " + std::string(lodepng_error_text(error)));
    }

    // Check if dimensions can be reduced
    if (width <= 1 || height <= 1) {
        throw std::runtime_error("Image dimensions too small to reduce");
    }

    unsigned new_width = width - 1;
    unsigned new_height = height - 1;

    // Determine channels based on raw color mode (after disabling conversion)
    unsigned channels = lodepng_get_channels(&decodeState.info_raw);

    // Resize based on type
    std::vector<unsigned char> resized_vec(new_width * new_height * channels);
    float x_ratio = static_cast<float>(width) / new_width;
    float y_ratio = static_cast<float>(height) / new_height;

    if (decodeState.info_raw.colortype == LCT_PALETTE) {
        // For palette, use nearest-neighbor on indices to preserve exact palette colors
        for (unsigned y = 0; y < new_height; ++y) {
            for (unsigned x = 0; x < new_width; ++x) {
                float orig_x = (x + 0.5f) * x_ratio - 0.5f;
                float orig_y = (y + 0.5f) * y_ratio - 0.5f;
                orig_x = std::max(0.0f, std::min(orig_x, static_cast<float>(width - 1)));
                orig_y = std::max(0.0f, std::min(orig_y, static_cast<float>(height - 1)));

                unsigned ix = static_cast<unsigned>(std::round(orig_x));
                unsigned iy = static_cast<unsigned>(std::round(orig_y));

                resized_vec[y * new_width + x] = temp_vec[iy * width + ix];
            }
        }
    } else {
        // For other types (e.g., RGB, RGBA), use bilinear interpolation
        for (unsigned y = 0; y < new_height; ++y) {
            for (unsigned x = 0; x < new_width; ++x) {
                float orig_x = (x + 0.5f) * x_ratio - 0.5f;
                float orig_y = (y + 0.5f) * y_ratio - 0.5f;
                orig_x = std::max(0.0f, std::min(orig_x, static_cast<float>(width - 1)));
                orig_y = std::max(0.0f, std::min(orig_y, static_cast<float>(height - 1)));

                unsigned x0 = static_cast<unsigned>(std::floor(orig_x));
                unsigned y0 = static_cast<unsigned>(std::floor(orig_y));
                unsigned x1 = std::min(x0 + 1, width - 1);
                unsigned y1 = std::min(y0 + 1, height - 1);

                float dx = orig_x - x0;
                float dy = orig_y - y0;
                float wx = 1.0f - dx;
                float wy = 1.0f - dy;

                for (unsigned c = 0; c < channels; ++c) {
                    float value = wx * wy * temp_vec[channels * (y0 * width + x0) + c] +
                                  wx * dy * temp_vec[channels * (y1 * width + x0) + c] +
                                  dx * wy * temp_vec[channels * (y0 * width + x1) + c] +
                                  dx * dy * temp_vec[channels * (y1 * width + x1) + c];

                    resized_vec[channels * (y * new_width + x) + c] = static_cast<unsigned char>(std::round(value));
                }
            }
        }
    }

    // Set up encoding state to match original color type and bit depth
    lodepng::State encodeState;
    encodeState.info_raw.colortype = decodeState.info_png.color.colortype;
    encodeState.info_raw.bitdepth = decodeState.info_png.color.bitdepth;
    encodeState.info_png.color.colortype = decodeState.info_png.color.colortype;
    encodeState.info_png.color.bitdepth = decodeState.info_png.color.bitdepth;
    encodeState.encoder.auto_convert = 0; // Disable auto-conversion

    // Copy palette if original is indexed (LCT_PALETTE)
    if (decodeState.info_png.color.colortype == LCT_PALETTE) {
        for (size_t i = 0; i < decodeState.info_png.color.palettesize; ++i) {
            lodepng_palette_add(&encodeState.info_png.color,
                                decodeState.info_png.color.palette[i * 4],
                                decodeState.info_png.color.palette[i * 4 + 1],
                                decodeState.info_png.color.palette[i * 4 + 2],
                                decodeState.info_png.color.palette[i * 4 + 3]);
            lodepng_palette_add(&encodeState.info_raw,
                                decodeState.info_png.color.palette[i * 4],
                                decodeState.info_png.color.palette[i * 4 + 1],
                                decodeState.info_png.color.palette[i * 4 + 2],
                                decodeState.info_png.color.palette[i * 4 + 3]);
        }
    }

    // Encode with resized raw data
    std::vector<unsigned char> output_image_vec;
    error = lodepng::encode(output_image_vec, resized_vec, new_width, new_height, encodeState);
    if (error) {
        throw std::runtime_error("Encoder error: " + std::string(lodepng_error_text(error)));
    }

    // Move the output to the input vector
    image_vec = std::move(output_image_vec);
}
