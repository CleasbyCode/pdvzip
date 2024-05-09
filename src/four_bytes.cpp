size_t getFourByteValue(const std::vector<uchar>& vec, size_t index) {
	return	(static_cast<size_t>(vec[index]) << 24) |
		(static_cast<size_t>(vec[index + 1]) << 16) |
		(static_cast<size_t>(vec[index + 2]) << 8) |
		static_cast<size_t>(vec[index + 3]); 
}
