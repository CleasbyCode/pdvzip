void adjustZipOffsets(std::vector<uint8_t>& vec, const uint32_t VEC_SIZE, const uint32_t LAST_IDAT_INDEX) {
	auto valueUpdater = [](std::vector<uint8_t>& vec, uint32_t value_insert_index, const uint32_t NEW_VALUE, uint8_t bits) {
		while (bits) { vec[value_insert_index--] = (NEW_VALUE >> (bits -= 8)) & 0xff; }
	};

	constexpr std::array<uint8_t, 4>
		ZIP_LOCAL_SIG		{ 0x50, 0x4B, 0x03, 0x04 },
		END_CENTRAL_DIR_SIG	{ 0x50, 0x4B, 0x05, 0x06 },
		START_CENTRAL_DIR_SIG	{ 0x50, 0x4B, 0x01, 0x02 };

	constexpr uint8_t
		SIG_LENGTH 			= 4,
		CENTRAL_LOCAL_INDEX_DIFF 	= 45,
		END_CENTRAL_START_INDEX_DIFF 	= 19,
		ZIP_COMMENT_LENGTH_INDEX_DIFF 	= 21,
		ZIP_RECORDS_INDEX_DIFF 		= 11,
		ZIP_LOCAL_INDEX_DIFF 		= 4,
		INCREMENT_SEARCH_INDEX 		= 1,
		PNG_IEND_LENGTH 		= 16,
		BYTE_LENGTH 			= 2;
						 
	const uint32_t END_CENTRAL_DIR_INDEX = VEC_SIZE - static_cast<uint32_t>(std::distance(vec.rbegin(), std::search(vec.rbegin(), vec.rend(), 
			END_CENTRAL_DIR_SIG.rbegin(), END_CENTRAL_DIR_SIG.rend()))) - SIG_LENGTH;
	uint32_t 
		total_zip_records_index = END_CENTRAL_DIR_INDEX + ZIP_RECORDS_INDEX_DIFF,
		end_central_start_index = END_CENTRAL_DIR_INDEX + END_CENTRAL_START_INDEX_DIFF,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + ZIP_COMMENT_LENGTH_INDEX_DIFF,
		zip_record_local_index = LAST_IDAT_INDEX + ZIP_LOCAL_INDEX_DIFF,
		start_central_dir_index = VEC_SIZE;
						 
	uint16_t 
		total_zip_records = getByteValue(vec, total_zip_records_index, BYTE_LENGTH, false),
		zip_comment_length = getByteValue(vec, zip_comment_length_index, BYTE_LENGTH, false) + PNG_IEND_LENGTH, 
		record_count = 0;
						 
	uint8_t value_bit_length = 16;
	valueUpdater(vec, zip_comment_length_index, zip_comment_length, value_bit_length); 

	while (record_count++ != total_zip_records) {
		start_central_dir_index = VEC_SIZE - static_cast<uint32_t>(std::distance(vec.rbegin(), std::search(vec.rbegin() + (VEC_SIZE - start_central_dir_index + SIG_LENGTH),
			vec.rend(), START_CENTRAL_DIR_SIG.rbegin(), START_CENTRAL_DIR_SIG.rend()))) - SIG_LENGTH;
	}

	uint32_t central_dir_local_index = start_central_dir_index + CENTRAL_LOCAL_INDEX_DIFF;

	value_bit_length = 32;
	valueUpdater(vec, end_central_start_index, start_central_dir_index, value_bit_length);  
	
	while (total_zip_records--) {
		valueUpdater(vec, central_dir_local_index, zip_record_local_index, value_bit_length);
		zip_record_local_index = searchFunc(vec, zip_record_local_index, INCREMENT_SEARCH_INDEX, ZIP_LOCAL_SIG);
		central_dir_local_index = searchFunc(vec, central_dir_local_index, INCREMENT_SEARCH_INDEX, START_CENTRAL_DIR_SIG) + CENTRAL_LOCAL_INDEX_DIFF;
	}
}
