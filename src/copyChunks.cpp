#include "copyChunks.h"
#include "searchFunc.h"
#include "getByteValue.h"     
#include <iterator>     
#include <stdexcept>    
#include <utility>            

void copyEssentialChunks(std::vector<uint8_t>& image_vec) {
	constexpr uint8_t SIG_LENGTH = 4;

	constexpr std::array<uint8_t, SIG_LENGTH> 
		PLTE_SIG {0x50, 0x4C, 0x54, 0x45},
		TRNS_SIG {0x74, 0x52, 0x4E, 0x53},
		IDAT_SIG {0x49, 0x44, 0x41, 0x54};

    	constexpr uint8_t
		PNG_FIRST_BYTES = 33,
		PNG_IEND_BYTES = 12,
		PNG_COLOR_TYPE_INDEX = 0x19,
		PNG_TRUECOLOR_VAL = 2,
		PNG_INDEXED_COLOR_VAL = 3;
		
    	const size_t IMAGE_VEC_SIZE = image_vec.size();
    	
    	const uint32_t FIRST_IDAT_INDEX = searchFunc(image_vec, 0, 0, IDAT_SIG);
    	
    	if (FIRST_IDAT_INDEX == IMAGE_VEC_SIZE) {
    		throw std::runtime_error("Image Error: Invalid or corrupt image file. IDAT chunk not found.");
    	}
    	
    	std::vector<uint8_t> copied_image_vec;
    	copied_image_vec.reserve(IMAGE_VEC_SIZE);     
  
    	std::copy_n(image_vec.begin(), PNG_FIRST_BYTES, std::back_inserter(copied_image_vec));	

    	auto copy_chunk_type = [&](const auto& chunk_signature) {
		constexpr uint8_t 
			PNG_CHUNK_FIELDS_COMBINED_LENGTH = 12, // Size_field + Name_field + CRC_field.
			PNG_CHUNK_LENGTH_FIELD_SIZE = 4,
			SEARCH_INCREMENT = 5;

        	uint32_t 
			chunk_search_pos = 0,
			chunk_length_pos = 0,
			chunk_count = 0,
			chunk_length = 0;
			
		bool isBigEndian = true;
		
        	while (true) {
            		chunk_search_pos = searchFunc(image_vec, chunk_search_pos, SEARCH_INCREMENT, chunk_signature);
            		
			if (chunk_signature != IDAT_SIG && chunk_search_pos > FIRST_IDAT_INDEX) {
				if (chunk_signature == PLTE_SIG && !chunk_count) {
					throw std::runtime_error("Image Error: Invalid or corrupt image file. PLTE chunk not found.");
				} else {
					break;
				}
			} else if (chunk_search_pos == IMAGE_VEC_SIZE) {
				break;
			}
			
			++chunk_count;
			chunk_length_pos = chunk_search_pos - PNG_CHUNK_LENGTH_FIELD_SIZE;
            		chunk_length = getByteValue(image_vec, chunk_length_pos, PNG_CHUNK_LENGTH_FIELD_SIZE, isBigEndian) + PNG_CHUNK_FIELDS_COMBINED_LENGTH;
	    		std::copy_n(image_vec.begin() + chunk_length_pos, chunk_length, std::back_inserter(copied_image_vec));
        	}
    	};

	if (image_vec[PNG_COLOR_TYPE_INDEX] == PNG_INDEXED_COLOR_VAL) {
    		copy_chunk_type(PLTE_SIG);
    		copy_chunk_type(TRNS_SIG);
	}
	
	if (image_vec[PNG_COLOR_TYPE_INDEX] == PNG_TRUECOLOR_VAL) {
		copy_chunk_type(TRNS_SIG);
	}
	
    	copy_chunk_type(IDAT_SIG);

    	std::copy_n(image_vec.end() - PNG_IEND_BYTES, PNG_IEND_BYTES, std::back_inserter(copied_image_vec));
    	image_vec = std::move(copied_image_vec);
}
