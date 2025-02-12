void eraseChunks(std::vector<uint8_t>& Vec) {
	constexpr uint8_t 
		PLTE_SIG[] 	{ 0x50, 0x4C, 0x54, 0x45 },
		IDAT_SIG[] 	{ 0x49, 0x44, 0x41, 0x54 },
		PNG_FIRST_BYTES 	= 33,
		PNG_IEND_CHUNK_LENGTH 	= 12,
		COMBINED_FIELD_LENGTHS 	= 12, 
		INC_NEXT_SEARCH_INDEX 	= 5,
		BYTE_LENGTH 		= 4;
	
	std::vector<uint8_t>Tmp_Vec;
	Tmp_Vec.insert(Tmp_Vec.begin(), Vec.begin(), Vec.begin() + PNG_FIRST_BYTES); 

	const size_t VEC_SIZE = Vec.size();
	auto copyChunk = [&](const uint8_t (&SIG)[4]) {
		uint32_t chunk_name_index = 0;
    		while (true) {
			chunk_name_index = searchFunc(Vec, chunk_name_index, INC_NEXT_SEARCH_INDEX, SIG);
			if (chunk_name_index == VEC_SIZE) {
				break;
			}
        		uint32_t chunk_length_index = chunk_name_index - 4;
        		uint32_t chunk_length = getByteValue(Vec, chunk_length_index, BYTE_LENGTH, false) + COMBINED_FIELD_LENGTHS;
			std::copy_n(Vec.begin() + chunk_length_index, chunk_length, std::back_inserter(Tmp_Vec));
    		}
	};

	copyChunk(PLTE_SIG);
	copyChunk(IDAT_SIG);

	Tmp_Vec.insert(Tmp_Vec.end(), Vec.end() - PNG_IEND_CHUNK_LENGTH, Vec.end());
	Tmp_Vec.swap(Vec);
}
