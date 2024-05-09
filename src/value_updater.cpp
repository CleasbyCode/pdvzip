// Update values, such as chunk lengths, CRC, etc. Writes them into the relevant vector index locations.

void valueUpdater(std::vector<uchar>& vec, size_t value_insert_index, const size_t NEW_VALUE, uint8_t value_bit_length, bool isBigEndian) {
	while (value_bit_length) {
		static_cast<size_t>(vec[isBigEndian ? value_insert_index++ : value_insert_index--] = (NEW_VALUE >> (value_bit_length -= 8)) & 0xff);
	}
}
