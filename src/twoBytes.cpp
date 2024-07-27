uint16_t getTwoByteValue(const std::vector<uint8_t>& VEC, uint32_t index, bool isBigEndian) {
	return (static_cast<uint16_t>(VEC[index]) << 8) | 
		static_cast<uint16_t>(VEC[isBigEndian ? ++index : --index]);
}

