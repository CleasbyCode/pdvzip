// ZIP file has moved to another location. We need to find and adjust the ZIP file record offsets to their new location.
#include "adjustZip.h"
#include "searchFunc.h"
#include <iostream>
#include <iterator>         

void adjustZipOffsets(std::vector<uint8_t>& vec, const size_t LAST_IDAT_INDEX) {
	auto updateValue = [](std::vector<uint8_t>& vec, size_t insert_index, const uint32_t NEW_VALUE, uint8_t bits) {
		while (bits) { vec[insert_index--] = (NEW_VALUE >> (bits -= 8)) & 0xff; }
	};

	constexpr uint8_t
		CENTRAL_LOCAL_INDEX_DIFF 	= 45,
		ZIP_COMMENT_LENGTH_INDEX_DIFF 	= 21,
		END_CENTRAL_START_INDEX_DIFF 	= 19,
		ZIP_RECORDS_INDEX_DIFF 		= 11,
		PNG_IEND_LENGTH 		= 16,
		ZIP_LOCAL_INDEX_DIFF 		= 4,
		INCREMENT_NEXT_SEARCH_POS	= 1;
	
	constexpr size_t SIG_LENGTH = 4;
		
	constexpr std::array<uint8_t, SIG_LENGTH>
		ZIP_LOCAL_SIG		{ 0x50, 0x4B, 0x03, 0x04 },
		END_CENTRAL_DIR_SIG	{ 0x50, 0x4B, 0x05, 0x06 },
		START_CENTRAL_DIR_SIG	{ 0x50, 0x4B, 0x01, 0x02 };
		
	const size_t VEC_SIZE = vec.size();
						 
	// Starting from the end of the vector storing the archive, a single reverse search of the contents finds the "end_central directory" index. 	
	const size_t END_CENTRAL_DIR_INDEX = VEC_SIZE - static_cast<size_t>(std::distance(vec.rbegin(), std::search(vec.rbegin(), vec.rend(), 
			END_CENTRAL_DIR_SIG.rbegin(), END_CENTRAL_DIR_SIG.rend()))) - SIG_LENGTH;
	size_t
		total_zip_records_index  = END_CENTRAL_DIR_INDEX + ZIP_RECORDS_INDEX_DIFF,
		end_central_start_index  = END_CENTRAL_DIR_INDEX + END_CENTRAL_START_INDEX_DIFF,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + ZIP_COMMENT_LENGTH_INDEX_DIFF,
		zip_record_local_index 	 = LAST_IDAT_INDEX + ZIP_LOCAL_INDEX_DIFF, // First ZIP record index/offset.
		start_central_dir_index  = 0; 

	uint16_t 
		total_zip_records = (vec[total_zip_records_index] << 8) | vec[total_zip_records_index - 1],
		zip_comment_length = ((vec[zip_comment_length_index] << 8) | vec[zip_comment_length_index - 1]) + PNG_IEND_LENGTH, // Extend comment length. Includes end bytes of PNG. Required for JAR.
		record_count = 0;
						 
	uint8_t value_bit_length = 16;
	
	updateValue(vec, zip_comment_length_index, zip_comment_length, value_bit_length); 

	// Find the first, top "start_central directory" index location by searching the vector, working backwards from the vector's content.
	// By starting the search from the end of the vector, we know we are already within the record section data of the archive and this
	// helps in avoiding the increased probability of a "false-positive" signature match if we were to search the vector from the beginning and read through image & file data first.
	auto search_it = vec.rbegin();
	while (total_zip_records > record_count++) {
		search_it = std::search(search_it, vec.rend(), START_CENTRAL_DIR_SIG.rbegin(), START_CENTRAL_DIR_SIG.rend());
		start_central_dir_index = VEC_SIZE - static_cast<size_t>(std::distance(vec.rbegin(), search_it++)) - SIG_LENGTH;	
	}
	
	size_t central_dir_local_index = start_central_dir_index + CENTRAL_LOCAL_INDEX_DIFF; // First central dir index location.

	value_bit_length = 32;
	updateValue(vec, end_central_start_index, static_cast<uint32_t>(start_central_dir_index), value_bit_length);  // Update end_central index location with start_central directory offset.
	
	while (total_zip_records--) {
		updateValue(vec, central_dir_local_index, static_cast<uint32_t>(zip_record_local_index), value_bit_length); // Write the new zip_record index/offset to the current central_dir_local index location.
		zip_record_local_index  = searchFunc(vec, zip_record_local_index, INCREMENT_NEXT_SEARCH_POS, ZIP_LOCAL_SIG); // Get the next zip_record index/offset.
		central_dir_local_index = searchFunc(vec, central_dir_local_index, INCREMENT_NEXT_SEARCH_POS, START_CENTRAL_DIR_SIG) + CENTRAL_LOCAL_INDEX_DIFF; // Get the next central_dir index location.
	}
}
