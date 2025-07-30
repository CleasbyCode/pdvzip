#include "checkTruecolor.h"
#include "lodepng/lodepng.h"
#include <vector>
#include <iostream>

// Function to analyze and convert PNG to PNG-8 if applicable
bool checkTruecolor(std::vector<uint8_t>& image_vec, unsigned& width, unsigned& height) {
    // Decode the PNG from the input vector
    std::vector<unsigned char> image; // Raw pixel data
    lodepng::State state;
    unsigned error = lodepng::decode(image, width, height, state, image_vec);
    if (error) {
        std::cout << "Decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
        return false;
    }
   
   // Initialize color stats
    LodePNGColorStats stats;
    lodepng_color_stats_init(&stats);
    stats.allow_palette = 1; // Enable palette computation
    stats.allow_greyscale = 0; // Disable grayscale optimization

    // Compute color statistics
    error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
    if (error) {
        std::cout << "Stats error " << error << std::endl;
        return false;
    }

    // Check if the image has 256 or fewer unique colors
    if (stats.numcolors <= 256) {
    
   	std::cout << "\nWarning: Your cover image (PNG-24/32 Truecolor) has fewer than 257 unique colors.\nFor compatibility reasons, the cover image will be converted to PNG-8 Indexed-Color." << std::endl;
        // Convert raw pixel data to indexed format (palette indices)
        std::vector<unsigned char> indexed_image(width * height);
        std::vector<unsigned char> palette;
        size_t palette_size = stats.numcolors > 0 ? stats.numcolors : 1;

        // Populate palette from stats or use a default color
        if (stats.numcolors > 0) {
            for (size_t i = 0; i < palette_size; ++i) {
                palette.push_back(stats.palette[i * 4]);     // R
                palette.push_back(stats.palette[i * 4 + 1]); // G
                palette.push_back(stats.palette[i * 4 + 2]); // B
                palette.push_back(stats.palette[i * 4 + 3]); // A
            }
        } else {
            // Handle edge case: single color or empty palette
            unsigned char r = 0, g = 0, b = 0, a = 255; // Default: black, opaque
            if (!image.empty()) {
                r = image[0];
                g = image[1];
                b = image[2];
                if (state.info_raw.colortype == LCT_RGBA) a = image[3];
            }
            palette.push_back(r);
            palette.push_back(g);
            palette.push_back(b);
            palette.push_back(a);
        }

        // Map pixels to palette indices
        size_t channels = (state.info_raw.colortype == LCT_RGBA) ? 4 : 3;
        for (size_t i = 0; i < width * height; ++i) {
            unsigned char r = image[i * channels];
            unsigned char g = image[i * channels + 1];
            unsigned char b = image[i * channels + 2];
            unsigned char a = (channels == 4) ? image[i * channels + 3] : 255;

            // Find the palette index for this pixel
            size_t index = 0;
            for (size_t j = 0; j < palette_size; ++j) {
                if (palette[j * 4] == r && palette[j * 4 + 1] == g &&
                    palette[j * 4 + 2] == b && palette[j * 4 + 3] == a) {
                    index = j;
                    break;
                }
            }
            indexed_image[i] = static_cast<unsigned char>(index);
        }

        // Set up state for PNG-8 encoding (Indexed color, color type 3)
        lodepng::State encodeState;
        encodeState.info_raw.colortype = LCT_PALETTE; // Force Indexed color
        encodeState.info_raw.bitdepth = 8; // Force 8-bit depth
        encodeState.info_png.color.colortype = LCT_PALETTE;
        encodeState.info_png.color.bitdepth = 8;
        encodeState.encoder.auto_convert = 0; // Disable auto-conversion to grayscale

        // Add palette to encodeState
        for (size_t i = 0; i < palette_size; ++i) {
            lodepng_palette_add(&encodeState.info_png.color, palette[i * 4], 
                                palette[i * 4 + 1], palette[i * 4 + 2], 
                                palette[i * 4 + 3]);
            lodepng_palette_add(&encodeState.info_raw, palette[i * 4], 
                                palette[i * 4 + 1], palette[i * 4 + 2], 
                                palette[i * 4 + 3]);
        }

        // Encode the indexed image as PNG-8
        std::vector<unsigned char> output;
        error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
        if (error) {
            std::cout << "Encode error " << error << std::endl;
            return false;
        }

        // Swap the input vector with the new PNG-8 data
        image_vec.swap(output);
        std::cout << "\nCover image successfully converted to PNG-8 Indexed-Color (color type 3, 8-bit depth)." << std::endl;
    } 
    return true;
}
