#include "pdvzip.h"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>

namespace binary_utils_detail {

[[noreturn]] void throwOutOfRange(std::string_view fn_name) {
	throw std::out_of_range(std::format("{}: index out of bounds", fn_name));
}

} // namespace binary_utils_detail

namespace {

void validateFieldBounds(std::size_t data_size, std::size_t offset, std::size_t length, std::string_view fn_name) {
	if (length != 2 && length != 4) {
		throw std::invalid_argument(std::format("{}: unsupported length {}", fn_name, length));
	}
	if (offset > data_size || length > (data_size - offset)) {
		binary_utils_detail::throwOutOfRange(fn_name);
	}
}

[[nodiscard]] std::size_t maxFieldValue(std::size_t length) {
	return (length == 2)
		? static_cast<std::size_t>(UINT16_MAX)
		: static_cast<std::size_t>(UINT32_MAX);
}

} // anonymous namespace

void writeValueAt(
	std::span<Byte> data,
	std::size_t offset,
	std::size_t value,
	std::size_t length) {

	validateFieldBounds(data.size(), offset, length, "writeValueAt");

	if (value > maxFieldValue(length)) {
		throw std::out_of_range(std::format("writeValueAt: value {} exceeds {}-byte field", value, length));
	}

	for (std::size_t i = 0; i < length; ++i) {
		data[offset + i] = static_cast<Byte>((value >> ((length - 1 - i) * 8)) & 0xFF);
	}
}

std::size_t readValueAt(
	std::span<const Byte> data,
	std::size_t offset,
	std::size_t length) {

	validateFieldBounds(data.size(), offset, length, "readValueAt");

	std::size_t value = 0;
	for (std::size_t i = 0; i < length; ++i) {
		value |= static_cast<std::size_t>(data[offset + i]) << ((length - 1 - i) * 8);
	}
	return value;
}

std::size_t checkedAdd(std::size_t lhs, std::size_t rhs, std::string_view context) {
	if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
		throw std::runtime_error(std::string(context));
	}
	return lhs + rhs;
}

std::size_t zipCentralDirectoryRecordSize(
	std::size_t name_length,
	std::size_t extra_length,
	std::size_t comment_length) {
	constexpr std::size_t CENTRAL_RECORD_MIN_SIZE = 46;
	return checkedAdd(
		CENTRAL_RECORD_MIN_SIZE,
		checkedAdd(
			name_length,
			checkedAdd(
				extra_length,
				comment_length,
				"Archive File Error: Central directory metadata length overflow."),
			"Archive File Error: Central directory metadata length overflow."),
		"Archive File Error: Central directory record size overflow.");
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
		ZIP64_LOCATOR_SIZE = 20,
		MAX_EOCD_SEARCH_DISTANCE = EOCD_MIN_SIZE + static_cast<std::size_t>(UINT16_MAX);

	if (archive_end > data.size() || archive_end < archive_begin
		|| archive_end - archive_begin < EOCD_MIN_SIZE) {
		return std::nullopt;
	}

	const std::size_t distance_floor = (archive_end > MAX_EOCD_SEARCH_DISTANCE)
		? archive_end - MAX_EOCD_SEARCH_DISTANCE
		: 0uz;
	const std::size_t search_floor = std::max(archive_begin, distance_floor);
	const std::size_t search_start = archive_end - EOCD_SIGNATURE_SIZE;

	if (search_start < search_floor) {
		return std::nullopt;
	}

	constexpr uint32_t ZIP64_END_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE = 0x07064B50;

	// Extractors disagree on which EOCD to use when more than one signature is
	// present: Info-ZIP takes the last one in the file even if its comment
	// length does not reach the archive end. A signature after the selected
	// record could therefore point those extractors at a different, unvalidated
	// central directory, so treat it as an error rather than skipping it.
	bool found_later_signature = false;
	for (std::size_t pos = search_start; ; --pos) {
		if (hasLe32Signature(data, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)) {
			if (pos + EOCD_MIN_SIZE <= archive_end
				&& readLe16(data, pos + EOCD_COMMENT_LENGTH_OFFSET) == archive_end - pos - EOCD_MIN_SIZE) {
				if (found_later_signature) {
					throw std::runtime_error(
						"Archive Security Error: Archive comment contains an end-of-central-directory signature.");
				}
				// Info-ZIP, 7-Zip and libarchive switch to ZIP64 when a ZIP64
				// locator immediately precedes the EOCD. Those bytes lie inside
				// the last central record, so a crafted record could redirect them.
				if (pos >= archive_begin + ZIP64_LOCATOR_SIZE
					&& hasLe32Signature(data, pos - ZIP64_LOCATOR_SIZE, ZIP64_END_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE)) {
					throw std::runtime_error("Archive Security Error: ZIP64 locator signature precedes the EOCD record.");
				}
				return ZipEocdLocator{
					.index = pos,
					.comment_length = static_cast<uint16_t>(archive_end - pos - EOCD_MIN_SIZE)
				};
			}
			found_later_signature = true;
		}

		if (pos == search_floor) {
			break;
		}
	}

	return std::nullopt;
}
