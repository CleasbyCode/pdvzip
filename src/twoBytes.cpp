uint_fast16_t getTwoByteValue(const std::vector<uint_fast8_t>& VEC, uint_fast32_t index, bool isBigEndian) {
	return (static_cast<uint_fast16_t>(VEC[index]) << 8) | 
		static_cast<uint_fast16_t>(VEC[isBigEndian ? ++index : --index]);
}

