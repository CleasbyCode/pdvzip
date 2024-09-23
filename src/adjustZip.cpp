// ZIP file has been moved to another location. We need to find and adjust the ZIP file record offsets to their new location.
void adjustZipOffsets(std::vector<uint8_t>& Vec, const uint_fast32_t VEC_SIZE, const uint_fast32_t LAST_IDAT_INDEX) {

	auto valueUpdater = [](std::vector<uint8_t>& Vec, uint_fast32_t value_insert_index, const uint_fast32_t NEW_VALUE, uint_fast8_t bits) {
		while (bits) { Vec[value_insert_index--] = (NEW_VALUE >> (bits -= 8)) & 0xff; }
	};

	constexpr uint_fast8_t 
		ZIP_SIG[] 			{ 0x50, 0x4B, 0x03, 0x04 },
		END_CENTRAL_DIR_SIG[] 		{ 0x50, 0x4B, 0x05, 0x06 },
		START_CENTRAL_DIR_SIG[] 	{ 0x50, 0x4B, 0x01, 0x02 },
		SIG_LENGTH 			= 4,
		CENTRAL_LOCAL_INDEX_DIFF 	= 45,
		END_CENTRAL_START_INDEX_DIFF 	= 19,
		ZIP_COMMENT_LENGTH_INDEX_DIFF 	= 21,
		ZIP_RECORDS_INDEX_DIFF 		= 11,
		ZIP_OFFSET_INDEX_DIFF 		= 4,
		INCREMENT_SEARCH_INDEX 		= 1,
		PNG_IEND_LENGTH 		= 16,
		BYTE_LENGTH 			= 2;
	
	uint_fast32_t end_central_dir_index = 0;

	// Find the end_central_directory index location by reverse iterating over the vector, starting from the end of the IMAGE/ZIP contents.
	for (uint_fast32_t index = VEC_SIZE - SIG_LENGTH;; --index) {
		if (std::equal(std::begin(END_CENTRAL_DIR_SIG), std::end(END_CENTRAL_DIR_SIG), Vec.begin() + index)) {
			end_central_dir_index = index;
			break;
		}
	}

	uint_fast32_t 
		total_zip_records_index = end_central_dir_index + ZIP_RECORDS_INDEX_DIFF,
		end_central_start_index = end_central_dir_index + END_CENTRAL_START_INDEX_DIFF,
		zip_comment_length_index = end_central_dir_index + ZIP_COMMENT_LENGTH_INDEX_DIFF,
		zip_record_offset_index = LAST_IDAT_INDEX + ZIP_OFFSET_INDEX_DIFF,
		start_central_dir_index = 0;
	
	uint_fast16_t 
		total_zip_records = getByteValue(Vec, total_zip_records_index, BYTE_LENGTH, false),
		zip_comment_length = getByteValue(Vec, zip_comment_length_index, BYTE_LENGTH, false) + PNG_IEND_LENGTH, // Get and extend comment length to include end bytes of PNG. Important for JAR files.
		record_count = 0;
	
	uint_fast8_t value_bit_length = 16;

	valueUpdater(Vec, zip_comment_length_index, zip_comment_length, value_bit_length); // Update extended comment length value. 
		
	// Find the first start_central_directory index location by reverse iterating over the vector, starting from the end of the IMAGE/ZIP contents.
	for (uint_fast32_t index = VEC_SIZE - SIG_LENGTH; record_count != total_zip_records; --index) {
		if (std::equal(std::begin(START_CENTRAL_DIR_SIG), std::end(START_CENTRAL_DIR_SIG), Vec.begin() + index)) {
			start_central_dir_index = index;
			record_count++;
		}
	}	
	
	uint_fast32_t central_dir_local_offset_index = start_central_dir_index + CENTRAL_LOCAL_INDEX_DIFF;

	value_bit_length = 32;

	valueUpdater(Vec, end_central_start_index, start_central_dir_index, value_bit_length);
	
	while (total_zip_records--) {
		valueUpdater(Vec, central_dir_local_offset_index, zip_record_offset_index, value_bit_length);
		zip_record_offset_index = searchFunc(Vec, zip_record_offset_index, INCREMENT_SEARCH_INDEX, ZIP_SIG);
		central_dir_local_offset_index = searchFunc(Vec, central_dir_local_offset_index, INCREMENT_SEARCH_INDEX, START_CENTRAL_DIR_SIG) + CENTRAL_LOCAL_INDEX_DIFF;
	}
}
