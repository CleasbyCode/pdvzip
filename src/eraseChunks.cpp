// Erase superfluous PNG chunks from the cover image. Just keep the critical PNG chunks/data. (HEADER/IHDR/*PLTE/IDAT/IEND).
bool eraseChunks(std::vector<uint_fast8_t>& Vec) {

	constexpr uint_fast8_t 
		PNG_HEADER_WITH_IHDR_CHUNK_BYTES = 33,
		PNG_IEND_CHUNK_LENGTH = 12,
		PNG_COLOR_TYPE_INDEX = 0x19,
		LENGTH_NAME_CRC_FIELD_BYTES = 12,
		INDEXED_COLOR = 3,
		INCREMENT_SEARCH_INDEX = 5,
		BYTE_LENGTH = 4,

		PLTE_SIG[] { 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] { 0x49, 0x44, 0x41, 0x54 };

	std::vector<uint_fast8_t>Temp_Vec;

	// Copy the first 33 bytes of the cover image into Temp_Vec. This contains the PNG header along with the IHDR chunk.
	Temp_Vec.insert(Temp_Vec.begin(), Vec.begin(), Vec.begin() + PNG_HEADER_WITH_IHDR_CHUNK_BYTES);

	uint_fast32_t idat_chunk_name_index{};

	idat_chunk_name_index = searchFunc(Vec, idat_chunk_name_index, INCREMENT_SEARCH_INDEX, IDAT_SIG); // Search for first IDAT chunk.

	const uint_fast8_t PNG_COLOR_TYPE = Vec[PNG_COLOR_TYPE_INDEX];

	// If required, search for and copy the PLTE chunk from cover image into Temp_Vec.
	if (PNG_COLOR_TYPE == INDEXED_COLOR) {
		uint_fast32_t plte_chunk_name_index{};
		plte_chunk_name_index = searchFunc(Vec, plte_chunk_name_index, INCREMENT_SEARCH_INDEX, PLTE_SIG);
		if (idat_chunk_name_index > plte_chunk_name_index) {
			const uint_fast32_t 
				PLTE_CHUNK_LENGTH_INDEX = plte_chunk_name_index - 4,
				PLTE_CHUNK_LENGTH = getByteValue(Vec, PLTE_CHUNK_LENGTH_INDEX, BYTE_LENGTH, false) + LENGTH_NAME_CRC_FIELD_BYTES;
			Temp_Vec.insert(Temp_Vec.end(), Vec.begin() + PLTE_CHUNK_LENGTH_INDEX, Vec.begin() + PLTE_CHUNK_LENGTH_INDEX + PLTE_CHUNK_LENGTH);
		} else {
		    std::cerr << "\nImage File Error: Required PLTE chunk not found for this PNG-8 Indexed-Color image.\n\n";
		    return false;
		}
	}

	const uint_fast32_t IMAGE_FILE_SIZE = static_cast<uint_fast32_t>(Vec.size());

	// Search for and copy all IDAT chunks from cover image into Temp_Vec.
	while (IMAGE_FILE_SIZE != idat_chunk_name_index) {
		uint_fast32_t
			idat_chunk_length_index = idat_chunk_name_index - 4,
			idat_chunk_length = getByteValue(Vec, idat_chunk_length_index, BYTE_LENGTH, false) + LENGTH_NAME_CRC_FIELD_BYTES;
		Temp_Vec.insert(Temp_Vec.end(), Vec.begin() + idat_chunk_length_index, Vec.begin() + idat_chunk_length_index + idat_chunk_length);
		idat_chunk_name_index = searchFunc(Vec, idat_chunk_name_index, INCREMENT_SEARCH_INDEX, IDAT_SIG);
	}
	
	// Copy last 12 bytes of cover image into Temp_Vec.
	Temp_Vec.insert(Temp_Vec.end(), Vec.end() - PNG_IEND_CHUNK_LENGTH, Vec.end());
	// Swap the contents of Temp_Vec with Image_Vec (Vec). Cover image now only contains the critical PNG chunks as no other chunk types were copied. 
	Temp_Vec.swap(Vec);
	
	return true;
}
