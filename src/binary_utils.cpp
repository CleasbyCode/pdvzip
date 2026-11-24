#include "pdvzip.h"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>

namespace binary_utils_detail {

[[noreturn]] void throwOutOfRange(std::string_view fn_name) {
	throw std::out_of_range(std::format("{}: index out of bounds", fn_name));
}

} // namespace binary_utils_detail

// Big-endian 32-bit fields (PNG chunk lengths, dimensions and CRCs).
void writeBe32(std::span<Byte> data, std::size_t offset, std::size_t value) {
	if (offset > data.size() || data.size() - offset < 4) {
		binary_utils_detail::throwOutOfRange("writeBe32");
	}
	if (value > UINT32_MAX) {
		throw std::out_of_range(std::format("writeBe32: value {} exceeds a 4-byte field", value));
	}
	for (std::size_t i = 0; i < 4; ++i) {
		data[offset + i] = static_cast<Byte>(value >> ((3 - i) * 8));
	}
}

uint32_t readBe32(std::span<const Byte> data, std::size_t offset) {
	if (offset > data.size() || data.size() - offset < 4) {
		binary_utils_detail::throwOutOfRange("readBe32");
	}
	uint32_t value = 0;
	for (std::size_t i = 0; i < 4; ++i) {
		value = (value << 8) | data[offset + i];
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
	return checkedAdd(
		ZIP_CENTRAL_RECORD_MIN_SIZE,
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

namespace {

struct ZipEocdLocator {
	std::size_t index;
	uint16_t comment_length;
};

// Returns the EOCD record whose comment ends exactly at archive_end. Throws if
// another EOCD signature follows it or a ZIP64 locator precedes it, since
// extractors could then select a different central directory.
[[nodiscard]] std::optional<ZipEocdLocator> findZipEocdLocator(
	std::span<const Byte> data,
	std::size_t archive_begin,
	std::size_t archive_end) {
	constexpr std::size_t
		EOCD_SIGNATURE_SIZE = 4,
		MAX_EOCD_SEARCH_DISTANCE = ZIP_EOCD_MIN_SIZE + static_cast<std::size_t>(UINT16_MAX);

	if (archive_end > data.size() || archive_end < archive_begin
		|| archive_end - archive_begin < ZIP_EOCD_MIN_SIZE) {
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

	// Extractors disagree on which EOCD to use when more than one signature is
	// present: Info-ZIP takes the last one in the file even if its comment
	// length does not reach the archive end. A signature after the selected
	// record could therefore point those extractors at a different, unvalidated
	// central directory, so treat it as an error rather than skipping it.
	bool found_later_signature = false;
	for (std::size_t pos = search_start; ; --pos) {
		if (hasLe32Signature(data, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)) {
			if (pos + ZIP_EOCD_MIN_SIZE <= archive_end
				&& readLe16(data, pos + ZIP_EOCD_COMMENT_LENGTH_OFFSET) == archive_end - pos - ZIP_EOCD_MIN_SIZE) {
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
					.comment_length = static_cast<uint16_t>(archive_end - pos - ZIP_EOCD_MIN_SIZE)
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

} // anonymous namespace

ZipCentralDirectory readZipCentralDirectory(
	std::span<const Byte> data,
	std::size_t zip_begin,
	std::size_t zip_end,
	std::string_view error_prefix) {
	constexpr std::size_t
		EOCD_DISK_NUMBER_OFFSET   = 4,
		EOCD_CENTRAL_DISK_OFFSET  = 6,
		EOCD_RECORDS_ON_DISK      = 8,
		EOCD_TOTAL_RECORDS_OFFSET = 10,
		EOCD_CENTRAL_SIZE_OFFSET  = 12;
	const auto fail = [error_prefix](std::string_view message) -> std::runtime_error {
		return std::runtime_error(std::format("{}: {}", error_prefix, message));
	};

	if (zip_end > data.size() || zip_end < zip_begin || zip_end - zip_begin < ZIP_EOCD_MIN_SIZE) {
		throw fail("Archive is too small.");
	}
	const auto locator = findZipEocdLocator(data, zip_begin, zip_end);
	if (!locator) {
		throw fail("End of central directory record not found.");
	}

	const std::size_t eocd = locator->index;
	const uint16_t total_records = readLe16(data, eocd + EOCD_TOTAL_RECORDS_OFFSET);
	const uint32_t central_size = readLe32(data, eocd + EOCD_CENTRAL_SIZE_OFFSET);
	const uint32_t central_offset = readLe32(data, eocd + ZIP_EOCD_CENTRAL_OFFSET);
	if (readLe16(data, eocd + EOCD_DISK_NUMBER_OFFSET) != 0
		|| readLe16(data, eocd + EOCD_CENTRAL_DISK_OFFSET) != 0
		|| readLe16(data, eocd + EOCD_RECORDS_ON_DISK) != total_records) {
		throw fail("Multi-disk ZIP archives are not supported.");
	}
	if (total_records == 0) {
		throw fail("Archive contains no central directory entries.");
	}
	if (total_records == UINT16_MAX || central_size == UINT32_MAX || central_offset == UINT32_MAX) {
		throw fail("ZIP64 archives are not supported.");
	}

	const std::size_t central_start = checkedAdd(
		zip_begin, static_cast<std::size_t>(central_offset),
		std::format("{}: Central directory offset overflow.", error_prefix));
	const std::size_t central_end = checkedAdd(
		central_start, static_cast<std::size_t>(central_size),
		std::format("{}: Central directory size overflow.", error_prefix));
	if (central_end > eocd) {
		throw fail("Central directory bounds are invalid.");
	}
	if (central_end != eocd) {
		throw fail("Central directory does not end at the EOCD record.");
	}
	return ZipCentralDirectory{
		.eocd_index          = eocd,
		.eocd_comment_length = locator->comment_length,
		.total_records       = total_records,
		.start               = central_start,
		.end                 = central_end,
	};
}
