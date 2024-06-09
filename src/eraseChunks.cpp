// Erase superfluous PNG chunks from the cover image. Just keep the critical PNG chunks/data. (HEADER/IHDR/*PLTE/IDAT/IEND).

uint_fast32_t eraseChunks(std::vector<uint_fast8_t>& Image_Vec) {
	
	std::vector<uint_fast8_t>Temp_Vec;
	Temp_Vec.reserve(Image_Vec.size());

	constexpr uint_fast8_t 
		PNG_HEADER_IHDR_CHUNK_LENGTH = 33,
		PNG_IEND_CHUNK_LENGTH = 12,
		INDEXED_COLOR_TYPE = 3,

		PLTE_SIG[] { 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] { 0x49, 0x44, 0x41, 0x54 };

	Temp_Vec.insert(Temp_Vec.begin(), Image_Vec.begin(), Image_Vec.begin() + PNG_HEADER_IHDR_CHUNK_LENGTH);

	uint_fast32_t
		idat_chunk_index = searchFunc(Image_Vec, 0, 0, IDAT_SIG) - 4,
		idat_chunk_length{};

	const uint_fast32_t
		IMAGE_FILE_SIZE = getVecSize(Image_Vec),
		FIRST_IDAT_CHUNK_LENGTH = getFourByteValue(Image_Vec, idat_chunk_index),
		FIRST_IDAT_CHUNK_CRC_INDEX = idat_chunk_index + FIRST_IDAT_CHUNK_LENGTH + 8,
		FIRST_IDAT_CHUNK_CRC = getFourByteValue(Image_Vec, FIRST_IDAT_CHUNK_CRC_INDEX),
		CALC_FIRST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[idat_chunk_index + 4], FIRST_IDAT_CHUNK_LENGTH + 4);

	if (FIRST_IDAT_CHUNK_CRC != CALC_FIRST_IDAT_CHUNK_CRC) {
		std::cerr << "\nImage File Error: CRC value for first IDAT chunk is invalid.\n\n";
		std::exit(EXIT_FAILURE);
	}

	const uint_fast8_t IMAGE_COLOR_TYPE = Image_Vec[25];

	if (IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE) {
		const uint_fast32_t PLTE_CHUNK_INDEX = searchFunc(Image_Vec, 0, 0, PLTE_SIG) - 4;
		if (idat_chunk_index > PLTE_CHUNK_INDEX) {
			const uint_fast32_t PLTE_CHUNK_LENGTH = getFourByteValue(Image_Vec, PLTE_CHUNK_INDEX);
			Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + PLTE_CHUNK_INDEX, Image_Vec.begin() + PLTE_CHUNK_INDEX + (PLTE_CHUNK_LENGTH + 12));
		} else {
		    std::cerr << "\nImage File Error: Required PLTE chunk not found for PNG-8 Indexed-color image.\n\n";
		    std::exit(EXIT_FAILURE);
		}
	}

	while (IMAGE_FILE_SIZE != idat_chunk_index + 4) {
		uint_fast32_t idat_chunk_length = getFourByteValue(Image_Vec, idat_chunk_index);
		Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + idat_chunk_index, Image_Vec.begin() + idat_chunk_index + (idat_chunk_length + 12));
		idat_chunk_index = searchFunc(Image_Vec, idat_chunk_index, 6, IDAT_SIG) - 4;
	}

	Temp_Vec.insert(Temp_Vec.end(), Image_Vec.end() - PNG_IEND_CHUNK_LENGTH, Image_Vec.end());
	Temp_Vec.swap(Image_Vec);
	
	return(getVecSize(Image_Vec));
}
