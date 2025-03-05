// Using lodepng to resize the cover image within the vector (if required), by reducing the width & height by 1 pixel. 
// https://github.com/lvandeve/lodepng  (Copyright (c) 2005-2025 Lode Vandevenne).
void resizeImage(std::vector<uint8_t>& image_vec) {
    std::vector<uint8_t> temp_vec; 
    unsigned width, height;
    unsigned error = lodepng::decode(temp_vec, width, height, image_vec); 
    if (error) {
        throw std::runtime_error("Decoder error: " + std::string(lodepng_error_text(error)));
    }
   unsigned new_width = width - 1;
   unsigned new_height = height - 1;

   std::vector<uint8_t> resized_image_vec(new_width * new_height * 4); 

   for (unsigned y = 0; y < new_height; ++y) {
        for (unsigned x = 0; x < new_width; ++x) {
            unsigned origX = x;
            unsigned origY = y;
            for (unsigned c = 0; c < 4; ++c) { // R, G, B, A channels
                resized_image_vec[4 * (y * new_width + x) + c] =
                    temp_vec[4 * (origY * width + origX) + c];
            }
        }
    }

    std::vector<uint8_t> output_image_vec;
    error = lodepng::encode(output_image_vec, resized_image_vec, new_width, new_height); 
    if (error) {
        throw std::runtime_error("Encoder error: " + std::string(lodepng_error_text(error)));
    }
   image_vec = std::move(output_image_vec);
}
