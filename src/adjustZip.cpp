// ZIP file has been moved to another location. We need to find and adjust the ZIP file record offsets to their new location.
void adjustZipOffsets(std::vector<uint_fast8_t>& Image_Vec, const uint_fast32_t LAST_IDAT_CHUNK_NAME_INDEX) {
 
	constexpr uint_fast8_t 
		ZIP_SIG[] 		{ 0x50, 0x4B, 0x03, 0x04 },
		START_CENTRAL_DIR_SIG[] { 0x50, 0x4B, 0x01, 0x02 },
		END_CENTRAL_DIR_SIG[] 	{ 0x50, 0x4B, 0x05, 0x06 },
		PNG_IEND_LENGTH = 16;

	const uint_fast32_t
		START_CENTRAL_DIR_INDEX = searchFunc(Image_Vec, LAST_IDAT_CHUNK_NAME_INDEX, 0, START_CENTRAL_DIR_SIG),
		END_CENTRAL_DIR_INDEX = searchFunc(Image_Vec, START_CENTRAL_DIR_INDEX, 0, END_CENTRAL_DIR_SIG);

	uint_fast32_t
		zip_records_index = END_CENTRAL_DIR_INDEX + 0x0B,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + 0x15,
		end_central_start_index = END_CENTRAL_DIR_INDEX + 0x13,
		central_local_index = START_CENTRAL_DIR_INDEX - 1,
		new_offset = LAST_IDAT_CHUNK_NAME_INDEX;

	uint_fast16_t 
		zip_records = getTwoByteValue(Image_Vec, zip_records_index, false),
		zip_comment_length = getTwoByteValue(Image_Vec, zip_comment_length_index, false) + PNG_IEND_LENGTH;

	uint_fast8_t value_bit_length = 16;

	valueUpdater(Image_Vec, zip_comment_length_index, zip_comment_length, value_bit_length, false);

	value_bit_length = 32;

	while (zip_records--) {
		new_offset = searchFunc(Image_Vec, new_offset, 1, ZIP_SIG),
		central_local_index = 0x2D + searchFunc(Image_Vec, central_local_index, 0, START_CENTRAL_DIR_SIG);
		valueUpdater(Image_Vec, central_local_index, new_offset, value_bit_length, false);
	}

	valueUpdater(Image_Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, value_bit_length, false);
}
