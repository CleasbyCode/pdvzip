// Using lodepng to resize the cover image within the vector, by reducing it's width & height by 1 pixel. 
// https://github.com/lvandeve/lodepng  (Copyright (c) 2005-2024 Lode Vandevenne).

void resizeImage(std::vector<uint8_t>& Image_Vec) {
    std::vector<uint8_t> Temp_Vec; 
    unsigned width, height;

    unsigned error = lodepng::decode(Temp_Vec, width, height, Image_Vec); 
    if (error) {
        throw std::runtime_error("Decoder error: " + std::string(lodepng_error_text(error)));
    }

   unsigned newWidth = width - 1;
   unsigned newHeight = height - 1;

   std::vector<uint8_t> resizedImage(newWidth * newHeight * 4); 

   for (unsigned y = 0; y < newHeight; ++y) {
        for (unsigned x = 0; x < newWidth; ++x) {

            unsigned origX = x;
            unsigned origY = y;

            for (unsigned c = 0; c < 4; ++c) { // R, G, B, A channels
                resizedImage[4 * (y * newWidth + x) + c] =
                    Temp_Vec[4 * (origY * width + origX) + c];
            }
        }
    }

    std::vector<uint8_t> outputPng;
    error = lodepng::encode(outputPng, resizedImage, newWidth, newHeight); 
    if (error) {
        throw std::runtime_error("Encoder error: " + std::string(lodepng_error_text(error)));
    }

   Image_Vec.swap(outputPng);
}
