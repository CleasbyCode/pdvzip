#include "pdvzip.h"

namespace {

[[nodiscard]] bool isSupportedFieldLength(std::size_t length) {
	return length == 2 || length == 4;
}

void validateFieldBounds(std::size_t data_size, std::size_t offset, std::size_t length, std::string_view fn_name) {
	if (!isSupportedFieldLength(length)) {
		throw std::invalid_argument(std::format("{}: unsupported length {}", fn_name, length));
	}
	if (offset > data_size || length > (data_size - offset)) {
		throw std::out_of_range(std::format("{}: index out of bounds", fn_name));
	}
}

[[nodiscard]] std::size_t maxFieldValue(std::size_t length) {
	return (length == 2)
		? static_cast<std::size_t>(UINT16_MAX)
		: static_cast<std::size_t>(UINT32_MAX);
}

} // anonymous namespace

void writeValueAt(std::span<Byte> data, std::size_t offset, std::size_t value, std::size_t length, std::endian byte_order) {
	validateFieldBounds(data.size(), offset, length, "writeValueAt");

	if (value > maxFieldValue(length)) {
		throw std::out_of_range(std::format("writeValueAt: value {} exceeds {}-byte field", value, length));
	}

	for (std::size_t i = 0; i < length; ++i) {
		const std::size_t shift = (byte_order == std::endian::big)
			? ((length - 1 - i) * 8)
			: (i * 8);
		data[offset + i] = static_cast<Byte>((value >> shift) & 0xFF);
	}
}

std::size_t readValueAt(std::span<const Byte> data, std::size_t offset, std::size_t length, std::endian byte_order) {
	validateFieldBounds(data.size(), offset, length, "readValueAt");

	std::size_t value = 0;
	for (std::size_t i = 0; i < length; ++i) {
		const std::size_t shift = (byte_order == std::endian::big)
			? ((length - 1 - i) * 8)
			: (i * 8);
		value |= static_cast<std::size_t>(data[offset + i]) << shift;
	}
	return value;
}

std::size_t checkedAdd(std::size_t lhs, std::size_t rhs, std::string_view context) {
	if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
		throw std::runtime_error(std::string(context));
	}
	return lhs + rhs;
}

std::size_t checkedMultiply(std::size_t lhs, std::size_t rhs, std::string_view context) {
	if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
		throw std::runtime_error(std::string(context));
	}
	return lhs * rhs;
}
