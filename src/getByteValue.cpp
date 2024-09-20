// Return either a 4-byte or 2-byte value from a given vector index position.
uint_fast32_t getByteValue(const std::vector<uint8_t>& VEC, const uint_fast32_t INDEX, const uint_fast8_t BYTE_LENGTH, bool isBigEndian) {
	
	if (BYTE_LENGTH == 4) {
	
	return	(static_cast<uint_fast32_t>(VEC[INDEX]) << 24) |
		(static_cast<uint_fast32_t>(VEC[INDEX + 1]) << 16) |
		(static_cast<uint_fast32_t>(VEC[INDEX + 2]) << 8) |
		 static_cast<uint_fast32_t>(VEC[INDEX + 3]); 
	} else {
		if (isBigEndian) {
       		 return (static_cast<uint_fast16_t>(VEC[INDEX]) << 8) | static_cast<uint_fast16_t>(VEC[INDEX + 1]);
    		} else {
        	  return (static_cast<uint_fast16_t>(VEC[INDEX]) << 8) | static_cast<uint_fast16_t>(VEC[INDEX - 1]);
    		}
		
	}

}
