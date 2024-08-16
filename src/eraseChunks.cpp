// Erase superfluous PNG chunks from the cover image. Just keep the critical PNG chunks/data. (HEADER/IHDR/*PLTE/IDAT/IEND).
void eraseChunks(std::vector<uint8_t>& Image_Vec) {
	
	std::vector<uint8_t>Temp_Vec;

	constexpr uint8_t 
		PNG_HEADER_IHDR_CHUNK_LENGTH = 33,
		PNG_IEND_CHUNK_LENGTH = 12,
		INDEXED_COLOR_TYPE = 3,
		BYTE_LENGTH = 4,

		PLTE_SIG[] { 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] { 0x49, 0x44, 0x41, 0x54 };

	Temp_Vec.insert(Temp_Vec.begin(), Image_Vec.begin(), Image_Vec.begin() + PNG_HEADER_IHDR_CHUNK_LENGTH);

	uint32_t idat_chunk_index = searchFunc(Image_Vec, 0, 0, IDAT_SIG) - 4;

	const uint32_t
		IMAGE_FILE_SIZE = getVecSize(Image_Vec),
		FIRST_IDAT_CHUNK_LENGTH = getByteValue(Image_Vec, idat_chunk_index, BYTE_LENGTH, false),
		FIRST_IDAT_CHUNK_CRC_INDEX = idat_chunk_index + FIRST_IDAT_CHUNK_LENGTH + 8,
		FIRST_IDAT_CHUNK_CRC = getByteValue(Image_Vec, FIRST_IDAT_CHUNK_CRC_INDEX, BYTE_LENGTH, false),
		CALC_FIRST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[idat_chunk_index + 4], FIRST_IDAT_CHUNK_LENGTH + 4);

	if (FIRST_IDAT_CHUNK_CRC != CALC_FIRST_IDAT_CHUNK_CRC) {
		std::cerr << "\nImage File Error: CRC value for first IDAT chunk is invalid.\n\n";
		std::exit(EXIT_FAILURE);
	}

	const uint8_t IMAGE_COLOR_TYPE = Image_Vec[25];

	if (IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE) {
		const uint32_t PLTE_CHUNK_INDEX = searchFunc(Image_Vec, 0, 0, PLTE_SIG) - 4;
		if (idat_chunk_index > PLTE_CHUNK_INDEX) {
			const uint32_t PLTE_CHUNK_LENGTH = getByteValue(Image_Vec, PLTE_CHUNK_INDEX, BYTE_LENGTH, false);
			Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + PLTE_CHUNK_INDEX, Image_Vec.begin() + PLTE_CHUNK_INDEX + (PLTE_CHUNK_LENGTH + 12));
		} else {
		    std::cerr << "\nImage File Error: Required PLTE chunk not found for PNG-8 Indexed-color image.\n\n";
		    std::exit(EXIT_FAILURE);
		}
	}

	while (IMAGE_FILE_SIZE != idat_chunk_index + 4) {
		uint32_t idat_chunk_length = getByteValue(Image_Vec, idat_chunk_index, BYTE_LENGTH, false);
		Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + idat_chunk_index, Image_Vec.begin() + idat_chunk_index + (idat_chunk_length + 12));
		idat_chunk_index = searchFunc(Image_Vec, idat_chunk_index, 6, IDAT_SIG) - 4;
	}

	Temp_Vec.insert(Temp_Vec.end(), Image_Vec.end() - PNG_IEND_CHUNK_LENGTH, Image_Vec.end());
	Temp_Vec.swap(Image_Vec);
}