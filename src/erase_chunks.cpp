
size_t eraseChunks(std::vector<unsigned char>& Image_Vec, size_t image_size) {
	
	std::vector<unsigned char>Temp_Vec;

	constexpr uint8_t PNG_HEADER_IHDR_CHUNK = 33;

	Temp_Vec.insert(Temp_Vec.begin(), Image_Vec.begin(), Image_Vec.begin() + PNG_HEADER_IHDR_CHUNK);

	const std::string IDAT_SIG = "IDAT";

	int n = -1;
	size_t 
		idat_chunk_index = std::search(Image_Vec.begin(), Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - Image_Vec.begin() - 4,
		initialize_crc_value = 0xffffffffL;

	const size_t
		FIRST_IDAT_CHUNK_LENGTH = getFourByteValue(Image_Vec, idat_chunk_index),
		FIRST_IDAT_CHUNK_CRC_INDEX = idat_chunk_index + FIRST_IDAT_CHUNK_LENGTH + 8,
		FIRST_IDAT_CHUNK_CRC = getFourByteValue(Image_Vec, FIRST_IDAT_CHUNK_CRC_INDEX),
		CALC_FIRST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[idat_chunk_index + 4], FIRST_IDAT_CHUNK_LENGTH + 4, n, initialize_crc_value);

	const uint8_t IMAGE_COLOR_TYPE = Image_Vec[25];
	constexpr uint8_t INDEXED_COLOR_TYPE = 3;

	if (FIRST_IDAT_CHUNK_CRC != CALC_FIRST_IDAT_CHUNK_CRC) {
		std::cerr << "\nImage File Error: CRC value for first IDAT chunk is invalid.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE) {

		const std::string PLTE_SIG = "PLTE";

		const size_t PLTE_CHUNK_INDEX = std::search(Image_Vec.begin(), Image_Vec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - Image_Vec.begin() - 4;

		if (idat_chunk_index > PLTE_CHUNK_INDEX) {
			const size_t PLTE_CHUNK_LENGTH = ((static_cast<size_t>(Image_Vec[PLTE_CHUNK_INDEX + 1]) << 16)
				| (static_cast<size_t>(Image_Vec[PLTE_CHUNK_INDEX + 2]) << 8)
				| (static_cast<size_t>(Image_Vec[PLTE_CHUNK_INDEX + 3])));

			Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + PLTE_CHUNK_INDEX, Image_Vec.begin() + PLTE_CHUNK_INDEX + (PLTE_CHUNK_LENGTH + 12));
		}
		else {
			std::cerr << "\nImage File Error: Required PLTE chunk not found for PNG-8 Indexed-color image.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}

	while (image_size != idat_chunk_index + 4) {
		const size_t IDAT_CHUNK_LENGTH = getFourByteValue(Image_Vec, idat_chunk_index);
		Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + idat_chunk_index, Image_Vec.begin() + idat_chunk_index + (IDAT_CHUNK_LENGTH + 12));
		idat_chunk_index = std::search(Image_Vec.begin() + idat_chunk_index + 6, Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - Image_Vec.begin() - 4;
	}

	constexpr uint8_t PNG_IEND_LENGTH = 12;

	Temp_Vec.insert(Temp_Vec.end(), Image_Vec.end() - PNG_IEND_LENGTH, Image_Vec.end());

	Temp_Vec.swap(Image_Vec);

	return Image_Vec.size();
}
