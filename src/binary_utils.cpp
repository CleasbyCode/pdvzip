#include "pdvzip.h"

std::optional<std::size_t> searchSig(std::span<const Byte> data, std::span<const Byte> sig, std::size_t start) {
	if (sig.empty() || start >= data.size()) {
		return std::nullopt;
	}

	const auto search_range = data.subspan(start);
	const auto result = std::ranges::search(search_range, sig);

	if (result.empty()) {
		return std::nullopt;
	}
	return start + static_cast<std::size_t>(std::ranges::distance(search_range.begin(), result.begin()));
}

void updateValue(std::span<Byte> data, std::size_t index, std::size_t value, std::size_t length, std::endian byte_order) {
	if (length != 2 && length != 4) {
		throw std::invalid_argument(std::format("updateValue: unsupported length {}", length));
	}

	const std::size_t write_index = [&] {
		if (byte_order == std::endian::big) {
			return index;
		}
		if (index + 1 < length) {
			throw std::out_of_range("updateValue: little-endian index underflow.");
		}
		return index - (length - 1);
	}();

	if (write_index > data.size() || length > (data.size() - write_index)) {
		throw std::out_of_range("updateValue: Index out of bounds.");
	}

	const std::size_t max_value = (length == 2) ? static_cast<std::size_t>(UINT16_MAX) : static_cast<std::size_t>(UINT32_MAX);
	if (value > max_value) {
		throw std::out_of_range(std::format("updateValue: value {} exceeds {}-byte field", value, length));
	}

	for (std::size_t i = 0; i < length; ++i) {
		const std::size_t shift = (byte_order == std::endian::big)
			? ((length - 1 - i) * 8)
			: (i * 8);
		data[write_index + i] = static_cast<Byte>((value >> shift) & 0xFF);
	}
}

std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length, std::endian byte_order) {
	if (length != 2 && length != 4) {
		throw std::invalid_argument(std::format("getValue: unsupported length {}", length));
	}

	const std::size_t read_index = [&] {
		if (byte_order == std::endian::big) {
			return index;
		}
		if (index + 1 < length) {
			throw std::out_of_range("getValue: little-endian index underflow.");
		}
		return index - (length - 1);
	}();

	if (read_index > data.size() || length > (data.size() - read_index)) {
		throw std::out_of_range("getValue: index out of bounds");
	}

	std::size_t value = 0;
	for (std::size_t i = 0; i < length; ++i) {
		const std::size_t shift = (byte_order == std::endian::big)
			? ((length - 1 - i) * 8)
			: (i * 8);
		value |= static_cast<std::size_t>(data[read_index + i]) << shift;
	}
	return value;
}
