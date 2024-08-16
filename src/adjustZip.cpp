// ZIP file has been moved to another location. We need to find and adjust the ZIP file record offsets to their new location.
void adjustZipOffsets(std::vector<uint8_t>& Vec, const uint32_t LAST_IDAT_CHUNK_NAME_INDEX) {
 
	constexpr uint8_t 
		ZIP_SIG[] 		{ 0x50, 0x4B, 0x03, 0x04 },
		START_CENTRAL_DIR_SIG[] { 0x50, 0x4B, 0x01, 0x02 },
		END_CENTRAL_DIR_SIG[] 	{ 0x50, 0x4B, 0x05, 0x06 },
		PNG_IEND_LENGTH = 16,
		BYTE_LENGTH = 2;

	const uint32_t
		START_CENTRAL_DIR_INDEX = searchFunc(Vec, LAST_IDAT_CHUNK_NAME_INDEX, 0, START_CENTRAL_DIR_SIG),
		END_CENTRAL_DIR_INDEX = searchFunc(Vec, START_CENTRAL_DIR_INDEX, 0, END_CENTRAL_DIR_SIG);

	uint32_t
		total_zip_records_index = END_CENTRAL_DIR_INDEX + 0x0B,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + 0x15,
		end_central_start_index = END_CENTRAL_DIR_INDEX + 0x13,
		central_local_index = START_CENTRAL_DIR_INDEX - 1,
		new_offset = LAST_IDAT_CHUNK_NAME_INDEX;

	uint16_t 
		total_zip_records = getByteValue(Vec, total_zip_records_index, BYTE_LENGTH, false),
		zip_comment_length = getByteValue(Vec, zip_comment_length_index, BYTE_LENGTH, false) + PNG_IEND_LENGTH;

	uint8_t value_bit_length = 16;

	valueUpdater(Vec, zip_comment_length_index, zip_comment_length, value_bit_length, false);

	value_bit_length = 32;

	while (total_zip_records--) {
		new_offset = searchFunc(Vec, new_offset, 1, ZIP_SIG),
		central_local_index = 0x2D + searchFunc(Vec, central_local_index, 0, START_CENTRAL_DIR_SIG);
		valueUpdater(Vec, central_local_index, new_offset, value_bit_length, false);
	}

	valueUpdater(Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, value_bit_length, false);
}
