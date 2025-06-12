// Writes updated values, such as chunk lengths, CRC, etc. into the relevant vector index locations.

#include "updateValue.h"

void updateValue(std::vector<uint8_t>& vec, size_t insert_index, const uint32_t NEW_VALUE, uint8_t value_bit_length) {
	while (value_bit_length) {
		vec[insert_index++] = (NEW_VALUE >> (value_bit_length -= 8)) & 0xff;
	}
}
