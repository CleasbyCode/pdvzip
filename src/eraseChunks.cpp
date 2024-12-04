// Make a copy of the cover image, but only include the critical PNG chunks/data. (HEADER/IHDR/*PLTE/IDAT/IEND).
void eraseChunks(std::vector<uint8_t>& Vec, const size_t VEC_SIZE) {
	constexpr uint8_t 
		PLTE_SIG[] { 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] { 0x49, 0x44, 0x41, 0x54 },
		PNG_FIRST_BYTES 	= 33,
		PNG_IEND_CHUNK_LENGTH 	= 12,
		COMBINED_FIELD_LENGTHS 	= 12, // Length (4), Name (4), CRC (4).
		INC_NEXT_SEARCH_INDEX 	= 5,
		BYTE_LENGTH 		= 4;
	
	std::vector<uint8_t>Temp_Vec;
	Temp_Vec.reserve(VEC_SIZE);

	Temp_Vec.insert(Temp_Vec.begin(), Vec.begin(), Vec.begin() + PNG_FIRST_BYTES); // Copy first 33 bytes of cover image into Temp_Vec. PNG header and IHDR chunk.

	auto copyChunk = [&](const uint8_t (&SIG)[4]) {
		uint32_t chunk_name_index = PNG_FIRST_BYTES;
    		while (true) {
			chunk_name_index = searchFunc(Vec, chunk_name_index, INC_NEXT_SEARCH_INDEX, SIG);
			if (chunk_name_index == VEC_SIZE) {
				break;
			}
        		uint32_t chunk_length_index = chunk_name_index - 4;
        		uint32_t chunk_length = getByteValue(Vec, chunk_length_index, BYTE_LENGTH, false) + COMBINED_FIELD_LENGTHS;
			std::copy_n(Vec.begin() + chunk_length_index, chunk_length, std::back_inserter(Temp_Vec));
    		}
	};

	copyChunk(PLTE_SIG);
	copyChunk(IDAT_SIG);

	Temp_Vec.insert(Temp_Vec.end(), Vec.end() - PNG_IEND_CHUNK_LENGTH, Vec.end());
	Temp_Vec.swap(Vec);
}
