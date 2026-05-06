#include "pdvzip.h"

namespace binary_utils_detail {

[[noreturn]] void throwOutOfRange(std::string_view fn_name) {
	throw std::out_of_range(std::format("{}: index out of bounds", fn_name));
}

} // namespace binary_utils_detail

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

std::optional<ZipEocdLocator> findZipEocdLocator(
	std::span<const Byte> data,
	std::size_t archive_begin,
	std::size_t archive_end) {
	constexpr std::size_t
		EOCD_MIN_SIZE = 22,
		EOCD_SIGNATURE_SIZE = 4,
		EOCD_COMMENT_LENGTH_OFFSET = 20,
		MAX_EOCD_SEARCH_DISTANCE = EOCD_MIN_SIZE + static_cast<std::size_t>(UINT16_MAX);

	if (archive_end > data.size() || archive_end < archive_begin
		|| archive_end - archive_begin < EOCD_MIN_SIZE) {
		return std::nullopt;
	}

	const std::size_t distance_floor = (archive_end > MAX_EOCD_SEARCH_DISTANCE)
		? archive_end - MAX_EOCD_SEARCH_DISTANCE
		: std::size_t{0};
	const std::size_t search_floor = std::max(archive_begin, distance_floor);
	const std::size_t search_start = archive_end - EOCD_SIGNATURE_SIZE;

	if (search_start < search_floor) {
		return std::nullopt;
	}

	for (std::size_t pos = search_start; ; --pos) {
		if (hasLe32Signature(data, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)
			&& pos + EOCD_MIN_SIZE <= archive_end) {
			const uint16_t comment_length = readLe16(data, pos + EOCD_COMMENT_LENGTH_OFFSET);
			if (comment_length == archive_end - pos - EOCD_MIN_SIZE) {
				return ZipEocdLocator{ .index = pos, .comment_length = comment_length };
			}
		}

		if (pos == search_floor) {
			break;
		}
	}

	return std::nullopt;
}
