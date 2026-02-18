#include "pdvzip.h"

std::optional<std::size_t> searchSig(std::span<const Byte> data, std::span<const Byte> sig, std::size_t start) {
	if (start >= data.size()) {
		return std::nullopt;
	}

	const auto search_range = data.subspan(start);
	const auto result = std::ranges::search(search_range, sig);

	if (result.empty()) {
		return std::nullopt;
	}
	return start + static_cast<std::size_t>(std::ranges::distance(search_range.begin(), result.begin()));
}

void updateValue(std::span<Byte> data, std::size_t index, std::size_t value, std::size_t length, bool isBigEndian) {
	static_assert(std::endian::native == std::endian::little, "byteswap logic assumes little-endian host");

	std::size_t write_index = isBigEndian ? index : index - (length - 1);

	if (write_index + length > data.size()) {
		throw std::out_of_range("updateValue: Index out of bounds.");
	}

	auto write = [&]<typename T>(T val) {
		if (isBigEndian) {
			val = std::byteswap(val);
		}
		std::memcpy(data.data() + write_index, &val, sizeof(T));
	};

	switch (length) {
		case 2: write(static_cast<uint16_t>(value)); break;
		case 4: write(static_cast<uint32_t>(value)); break;
		default:
			throw std::invalid_argument(std::format("updateValue: unsupported length {}", length));
	}
}

std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length, bool isBigEndian) {
	static_assert(std::endian::native == std::endian::little, "byteswap logic assumes little-endian host");

	const std::size_t read_index = isBigEndian ? index : index - (length - 1);

	if (read_index + length > data.size()) {
		throw std::out_of_range("getValue: index out of bounds");
	}

	auto read = [&]<typename T>() -> T {
		T val;
		std::memcpy(&val, data.data() + read_index, sizeof(T));
		if (isBigEndian) {
			val = std::byteswap(val);
		}
		return val;
	};

	switch (length) {
		case 2: return read.operator()<uint16_t>();
		case 4: return read.operator()<uint32_t>();
		default:
			throw std::invalid_argument(
				std::format("getValue: unsupported length {}", length));
	}
}
