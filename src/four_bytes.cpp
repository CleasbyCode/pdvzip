size_t getFourByteValue(const std::vector<uchar>& VEC, const size_t INDEX) {
	return	(static_cast<size_t>(VEC[INDEX]) << 24) |
		(static_cast<size_t>(VEC[INDEX + 1]) << 16) |
		(static_cast<size_t>(VEC[INDEX + 2]) << 8) |
		static_cast<size_t>(VEC[INDEX + 3]); 
}
