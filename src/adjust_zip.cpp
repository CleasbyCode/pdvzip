
void adjustZipOffsets(std::vector<unsigned char>& Image_Vec, const size_t LAST_IDAT_CHUNK_NAME_INDEX, bool isBigEndian) {
 
	const std::string
		ZIP_SIG = "\x50\x4B\x03\x04",
		START_CENTRAL_DIR_SIG = "PK\x01\x02",
		END_CENTRAL_DIR_SIG = "PK\x05\x06";

	const size_t
		START_CENTRAL_DIR_INDEX = std::search(Image_Vec.begin() + LAST_IDAT_CHUNK_NAME_INDEX, Image_Vec.end(),
		START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - Image_Vec.begin(),
		END_CENTRAL_DIR_INDEX = std::search(Image_Vec.begin() + START_CENTRAL_DIR_INDEX, Image_Vec.end(), END_CENTRAL_DIR_SIG.begin(),	END_CENTRAL_DIR_SIG.end()) - Image_Vec.begin();

size_t
	zip_records_index = END_CENTRAL_DIR_INDEX + 11,
	zip_comment_length_index = END_CENTRAL_DIR_INDEX + 21,
	end_central_start_index = END_CENTRAL_DIR_INDEX + 19,
	central_local_index = START_CENTRAL_DIR_INDEX - 1,
	new_offset = LAST_IDAT_CHUNK_NAME_INDEX;

uint16_t zip_records = (Image_Vec[zip_records_index] << 8) | Image_Vec[zip_records_index - 1];

uint8_t value_bit_length = 32;

isBigEndian = false;

while (zip_records--) {
	new_offset = std::search(Image_Vec.begin() + new_offset + 1, Image_Vec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - Image_Vec.begin(),
	central_local_index = 45 + std::search(Image_Vec.begin() + central_local_index, Image_Vec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - Image_Vec.begin();
	valueUpdater(Image_Vec, central_local_index, new_offset, value_bit_length, isBigEndian);
}

valueUpdater(Image_Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, value_bit_length, isBigEndian);

uint16_t zip_comment_length = 16 + (static_cast<size_t>(Image_Vec[zip_comment_length_index] << 8) | (static_cast<size_t>(Image_Vec[zip_comment_length_index - 1])));

value_bit_length = 16;

valueUpdater(Image_Vec, zip_comment_length_index, zip_comment_length, value_bit_length, isBigEndian);
}