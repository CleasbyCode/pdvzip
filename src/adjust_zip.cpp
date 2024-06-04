// ZIP file has been moved to another location. We need to find and adjust the ZIP file record offsets to their new location.

void adjustZipOffsets(std::vector<uint_fast8_t>& Image_Vec, const uint_fast32_t LAST_IDAT_CHUNK_NAME_INDEX) {
 
	constexpr uint_fast8_t 
		ZIP_SIG[4] 			{0x50, 0x4B, 0x03, 0x04},
		START_CENTRAL_DIR_SIG [4] 	{0x50, 0x4B, 0x01, 0x02},
		END_CENTRAL_DIR_SIG [4] 	{0x50, 0x4B, 0x05, 0x06},
		PNG_IEND_LENGTH = 16;

	const uint_fast32_t
		START_CENTRAL_DIR_INDEX = static_cast<uint_fast32_t>(std::search(Image_Vec.begin() + LAST_IDAT_CHUNK_NAME_INDEX, Image_Vec.end(), 
				std::begin(START_CENTRAL_DIR_SIG), std::end(START_CENTRAL_DIR_SIG)) - Image_Vec.begin()),

		END_CENTRAL_DIR_INDEX = static_cast<uint_fast32_t>(std::search(Image_Vec.begin() + START_CENTRAL_DIR_INDEX, Image_Vec.end(), 
				std::begin(END_CENTRAL_DIR_SIG), std::end(END_CENTRAL_DIR_SIG)) - Image_Vec.begin());

	uint_fast32_t
		zip_records_index = END_CENTRAL_DIR_INDEX + 0x0B,
		zip_comment_length_index = END_CENTRAL_DIR_INDEX + 0x15,
		end_central_start_index = END_CENTRAL_DIR_INDEX + 0x13,
		central_local_index = START_CENTRAL_DIR_INDEX - 1,
		new_offset = LAST_IDAT_CHUNK_NAME_INDEX;

	uint_fast16_t 
		zip_records = (static_cast<uint_fast16_t>(Image_Vec[zip_records_index]) << 8) | 
				static_cast<uint_fast16_t>(Image_Vec[zip_records_index - 1]),

		zip_comment_length = PNG_IEND_LENGTH + ((static_cast<uint_fast16_t>(Image_Vec[zip_comment_length_index]) << 8) | 
				static_cast<uint_fast16_t>(Image_Vec[zip_comment_length_index - 1]));

	uint_fast8_t value_bit_length = 16;

	valueUpdater(Image_Vec, zip_comment_length_index, zip_comment_length, value_bit_length, false);

	value_bit_length = 32;

	while (zip_records--) {
		new_offset = static_cast<uint_fast32_t>(std::search(Image_Vec.begin() + new_offset + 1, Image_Vec.end(), 
				std::begin(ZIP_SIG), std::end(ZIP_SIG)) - Image_Vec.begin()),

		central_local_index = 0x2D + static_cast<uint_fast32_t>(std::search(Image_Vec.begin() + central_local_index, Image_Vec.end(), 
				std::begin(START_CENTRAL_DIR_SIG), std::end(START_CENTRAL_DIR_SIG)) - Image_Vec.begin());

		valueUpdater(Image_Vec, central_local_index, new_offset, value_bit_length, false);
	}

	valueUpdater(Image_Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, value_bit_length, false);
}
