// ZIP file has been moved to another location. We need to find and adjust the ZIP file record offsets to their new location.
void adjustZipOffsets(std::vector<uint_fast8_t>& Vec, const uint_fast32_t LAST_IDAT_INDEX) {
	constexpr uint_fast8_t 
		ZIP_SIG[] 		{ 0x50, 0x4B, 0x03, 0x04 },
		START_CENTRAL_DIR_SIG[] { 0x50, 0x4B, 0x01, 0x02 },
		END_CENTRAL_DIR_SIG[] 	{ 0x50, 0x4B, 0x05, 0x06 },
		
		INCREMENT_SEARCH_INDEX = 1,
		PNG_IEND_LENGTH = 16,
		BYTE_LENGTH = 2;

	const uint_fast32_t
		START_CENTRAL_DIR_INDEX = searchFunc(Vec, LAST_IDAT_INDEX, INCREMENT_SEARCH_INDEX, START_CENTRAL_DIR_SIG),
		END_CENTRAL_DIR_INDEX = searchFunc(Vec, START_CENTRAL_DIR_INDEX, INCREMENT_SEARCH_INDEX, END_CENTRAL_DIR_SIG);

	uint_fast32_t
		total_zip_records_index = END_CENTRAL_DIR_INDEX + 11,
		end_central_start_index = END_CENTRAL_DIR_INDEX + 19,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + 21,
		zip_record_offset_index = LAST_IDAT_INDEX + 4,
		central_dir_local_offset_index = START_CENTRAL_DIR_INDEX + 45;

	uint_fast16_t 
		total_zip_records = getByteValue(Vec, total_zip_records_index, BYTE_LENGTH, false),
		zip_comment_length = getByteValue(Vec, zip_comment_length_index, BYTE_LENGTH, false) + PNG_IEND_LENGTH;

	uint_fast8_t value_bit_length = 16;

	valueUpdater(Vec, zip_comment_length_index, zip_comment_length, value_bit_length, false);

	value_bit_length = 32;

	valueUpdater(Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, value_bit_length, false);
	
	while (total_zip_records--) {
		while (value_bit_length) {
			static_cast<uint_fast32_t>(Vec[central_dir_local_offset_index--] = (zip_record_offset_index >> (value_bit_length -= 8)) & 0xff);
		}
		value_bit_length = 32;
		zip_record_offset_index = searchFunc(Vec, zip_record_offset_index, INCREMENT_SEARCH_INDEX, ZIP_SIG);
		central_dir_local_offset_index = searchFunc(Vec, central_dir_local_offset_index, INCREMENT_SEARCH_INDEX, START_CENTRAL_DIR_SIG) + 45;
	}
}
