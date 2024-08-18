uint32_t getByteValue(const std::vector<uint8_t>& VEC, const uint32_t INDEX, const uint8_t BYTE_LENGTH, bool isBigEndian) {
	
	if (BYTE_LENGTH == 4) {
	
	return	(static_cast<uint32_t>(VEC[INDEX]) << 24) |
		(static_cast<uint32_t>(VEC[INDEX + 1]) << 16) |
		(static_cast<uint32_t>(VEC[INDEX + 2]) << 8) |
		 static_cast<uint32_t>(VEC[INDEX + 3]); 
	} else {
		if (isBigEndian) {
       		 return (static_cast<uint16_t>(VEC[INDEX]) << 8) | static_cast<uint16_t>(VEC[INDEX + 1]);
    		} else {
        	  return (static_cast<uint16_t>(VEC[INDEX]) << 8) | static_cast<uint16_t>(VEC[INDEX - 1]);
    		}
		
	}

}
