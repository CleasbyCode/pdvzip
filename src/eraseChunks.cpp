// Erase superfluous PNG chunks from the cover image. Just keep the critical PNG chunks/data. (HEADER/IHDR/*PLTE/IDAT/IEND).
bool eraseChunks(std::vector<uint8_t>& Vec) {
	std::vector<uint8_t>Temp_Vec;

	constexpr uint8_t 
		PNG_HEADER_IHDR_CHUNK_LENGTH = 33,
		PNG_IEND_CHUNK_LENGTH = 12,
		PNG_COLOR_TYPE_INDEX = 0x19,
		INDEXED_COLOR = 3,
		BYTE_LENGTH = 4,

		PLTE_SIG[] { 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] { 0x49, 0x44, 0x41, 0x54 };

	Temp_Vec.insert(Temp_Vec.begin(), Vec.begin(), Vec.begin() + PNG_HEADER_IHDR_CHUNK_LENGTH);

	uint32_t idat_chunk_index = searchFunc(Vec, 0, 0, IDAT_SIG) - 4;

	const uint32_t
		IMAGE_FILE_SIZE = getVecSize(Vec),
		FIRST_IDAT_CHUNK_LENGTH = getByteValue(Vec, idat_chunk_index, BYTE_LENGTH, false),
		FIRST_IDAT_CHUNK_CRC_INDEX = idat_chunk_index + FIRST_IDAT_CHUNK_LENGTH + 8,
		FIRST_IDAT_CHUNK_CRC = getByteValue(Vec, FIRST_IDAT_CHUNK_CRC_INDEX, BYTE_LENGTH, false),
		CALC_FIRST_IDAT_CHUNK_CRC = crcUpdate(&Vec[idat_chunk_index + 4], FIRST_IDAT_CHUNK_LENGTH + 4);

	if (FIRST_IDAT_CHUNK_CRC != CALC_FIRST_IDAT_CHUNK_CRC) {
		std::cerr << "\nImage File Error: CRC value for first IDAT chunk is invalid.\n\n";
		return false;
	}

	const uint8_t PNG_COLOR_TYPE = Vec[PNG_COLOR_TYPE_INDEX];

	if (PNG_COLOR_TYPE == INDEXED_COLOR) {
		const uint32_t PLTE_CHUNK_INDEX = searchFunc(Vec, 0, 0, PLTE_SIG) - 4;
		if (idat_chunk_index > PLTE_CHUNK_INDEX) {
			const uint32_t PLTE_CHUNK_LENGTH = getByteValue(Vec, PLTE_CHUNK_INDEX, BYTE_LENGTH, false);
			Temp_Vec.insert(Temp_Vec.end(), Vec.begin() + PLTE_CHUNK_INDEX, Vec.begin() + PLTE_CHUNK_INDEX + (PLTE_CHUNK_LENGTH + 12));
		} else {
		    std::cerr << "\nImage File Error: Required PLTE chunk not found for PNG-8 Indexed-color image.\n\n";
		    return false;
		}
	}

	while (IMAGE_FILE_SIZE != idat_chunk_index + 4) {
		uint32_t idat_chunk_length = getByteValue(Vec, idat_chunk_index, BYTE_LENGTH, false);
		Temp_Vec.insert(Temp_Vec.end(), Vec.begin() + idat_chunk_index, Vec.begin() + idat_chunk_index + (idat_chunk_length + 12));
		idat_chunk_index = searchFunc(Vec, idat_chunk_index, 6, IDAT_SIG) - 4;
	}

	Temp_Vec.insert(Temp_Vec.end(), Vec.end() - PNG_IEND_CHUNK_LENGTH, Vec.end());
	Temp_Vec.swap(Vec);
	
	return true;
}
