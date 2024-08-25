// Writes updated values, such as chunk lengths, CRC, etc. into the relevant vector index locations.
void valueUpdater(std::vector<uint_fast8_t>& vec, uint_fast32_t value_insert_index, const uint_fast32_t NEW_VALUE, uint_fast8_t value_bit_length, bool isBigEndian) {
	while (value_bit_length) {
		static_cast<uint_fast32_t>(vec[isBigEndian ? value_insert_index++ : value_insert_index--] = (NEW_VALUE >> (value_bit_length -= 8)) & 0xff);
	}
}
