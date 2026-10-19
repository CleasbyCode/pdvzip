#include "pdvzip.h"
#include "lodepng/lodepng.h"

#include <format>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::size_t
	ICCP_CHUNK_INDEX     = 0x21,
	ARCHIVE_PREFIX_SIZE  = 8,   // IDAT length + type
	ARCHIVE_CRC_SIZE     = 4,
	IEND_CHUNK_SIZE      = 12,
	// Bytes after the ZIP data that become part of the EOCD comment.
	PNG_TRAILING_BYTES   = ARCHIVE_CRC_SIZE + IEND_CHUNK_SIZE;

struct ZipEocdInfo {
	std::size_t index;
	uint16_t total_records;
	uint16_t comment_length;
	std::size_t central_start;  // archive-chunk offset of the central directory
	std::size_t central_end;    // == index
};

// Re-check the EOCD at the mutation boundary. Offsets in the returned record
// are positions within the archive chunk (ZIP data starts at ARCHIVE_PREFIX_SIZE).
[[nodiscard]] ZipEocdInfo findZipEocd(std::span<const Byte> archive) {
	constexpr std::size_t
		EOCD_MIN_SIZE             = 22,
		EOCD_DISK_NUMBER_OFFSET   = 4,
		EOCD_CENTRAL_DISK_OFFSET  = 6,
		EOCD_RECORDS_ON_DISK      = 8,
		EOCD_TOTAL_RECORDS_OFFSET = 10,
		EOCD_CENTRAL_SIZE_OFFSET  = 12,
		EOCD_CENTRAL_OFFSET       = 16;

	if (archive.size() < ARCHIVE_PREFIX_SIZE + EOCD_MIN_SIZE + ARCHIVE_CRC_SIZE) {
		throw std::runtime_error("ZIP Error: Archive is too small.");
	}
	const std::size_t zip_end = archive.size() - ARCHIVE_CRC_SIZE;

	const auto locator = findZipEocdLocator(archive, ARCHIVE_PREFIX_SIZE, zip_end);
	if (!locator) {
		throw std::runtime_error("ZIP Error: End of Central Directory signature not found.");
	}

	const std::size_t pos = locator->index;
	const uint16_t total_records = readLe16(archive, pos + EOCD_TOTAL_RECORDS_OFFSET);
	const uint32_t central_size = readLe32(archive, pos + EOCD_CENTRAL_SIZE_OFFSET);
	const uint32_t central_offset = readLe32(archive, pos + EOCD_CENTRAL_OFFSET);

	if (readLe16(archive, pos + EOCD_DISK_NUMBER_OFFSET) != 0
		|| readLe16(archive, pos + EOCD_CENTRAL_DISK_OFFSET) != 0
		|| readLe16(archive, pos + EOCD_RECORDS_ON_DISK) != total_records
		|| total_records == UINT16_MAX || central_size == UINT32_MAX
		|| central_offset == UINT32_MAX) {
		throw std::runtime_error("ZIP Error: ZIP64 or multi-disk archives are not supported.");
	}
	if (total_records == 0) {
		throw std::runtime_error("ZIP Error: Archive contains no records.");
	}

	const std::size_t central_start = checkedAdd(
		ARCHIVE_PREFIX_SIZE,
		static_cast<std::size_t>(central_offset),
		"ZIP Error: Central directory offset overflow.");
	const std::size_t central_end = checkedAdd(
		central_start,
		static_cast<std::size_t>(central_size),
		"ZIP Error: Central directory size overflow.");
	if (central_end != pos) {
		throw std::runtime_error("ZIP Error: Central directory bounds are invalid.");
	}
	return ZipEocdInfo{
		.index          = pos,
		.total_records  = total_records,
		.comment_length = locator->comment_length,
		.central_start  = central_start,
		.central_end    = central_end,
	};
}

// Rebase all ZIP record offsets onto zip_base_offset, the absolute file offset
// of the ZIP data once the archive chunk is placed in the output image.
void fixZipOffsets(vBytes& archive, std::size_t zip_base_offset) {
	constexpr std::size_t
		CENTRAL_RECORD_MIN_SIZE        = 46,
		CENTRAL_NAME_LENGTH_OFFSET     = 28,
		CENTRAL_EXTRA_LENGTH_OFFSET    = 30,
		CENTRAL_COMMENT_LENGTH_OFFSET  = 32,
		CENTRAL_DISK_START_OFFSET      = 34,
		CENTRAL_LOCAL_OFFSET_OFFSET    = 42;

	const ZipEocdInfo eocd = findZipEocd(archive);

	const std::size_t central_offset = checkedAdd(
		zip_base_offset,
		eocd.central_start - ARCHIVE_PREFIX_SIZE,
		"ZIP Error: Central directory offset overflow.");
	if (central_offset > UINT32_MAX) {
		throw std::runtime_error("ZIP Error: Central directory offset exceeds ZIP32 limits.");
	}
	if (eocd.comment_length > UINT16_MAX - PNG_TRAILING_BYTES) {
		throw std::runtime_error("ZIP Error: Comment length overflow.");
	}

	writeLe16(archive, eocd.index + 20, static_cast<uint16_t>(eocd.comment_length + PNG_TRAILING_BYTES));
	writeLe32(archive, eocd.index + 16, static_cast<uint32_t>(central_offset));

	std::size_t cursor = eocd.central_start;
	for (uint16_t i = 0; i < eocd.total_records; ++i) {
		if (CENTRAL_RECORD_MIN_SIZE > eocd.central_end - cursor) {
			throw std::runtime_error("ZIP Error: Truncated central directory file header.");
		}

		if (!hasLe32Signature(archive, cursor, ZIP_CENTRAL_DIRECTORY_SIGNATURE)) {
			throw std::runtime_error(std::format(
				"ZIP Error: Invalid central directory file header signature at record {}.", i + 1));
		}

		const std::size_t name_length = readLe16(archive, cursor + CENTRAL_NAME_LENGTH_OFFSET);
		const std::size_t extra_length = readLe16(archive, cursor + CENTRAL_EXTRA_LENGTH_OFFSET);
		const std::size_t comment_length = readLe16(archive, cursor + CENTRAL_COMMENT_LENGTH_OFFSET);
		const std::size_t record_size = zipCentralDirectoryRecordSize(name_length, extra_length, comment_length);

		if (readLe16(archive, cursor + CENTRAL_DISK_START_OFFSET) != 0) {
			throw std::runtime_error(std::format(
				"ZIP Error: Multi-disk local header reference on record {} is not supported.", i + 1));
		}
		if (record_size > eocd.central_end - cursor) {
			throw std::runtime_error("ZIP Error: Central directory entry exceeds archive bounds.");
		}

		const std::size_t relative_local_offset = readLe32(archive, cursor + CENTRAL_LOCAL_OFFSET_OFFSET);
		const std::size_t local_header = checkedAdd(
			ARCHIVE_PREFIX_SIZE,
			relative_local_offset,
			"ZIP Error: Local file header offset overflow.");
		if (local_header >= eocd.central_start
			|| !hasLe32Signature(archive, local_header, ZIP_LOCAL_FILE_HEADER_SIGNATURE)) {
			throw std::runtime_error(std::format(
				"ZIP Error: Local file header for record {} is missing or out of bounds.", i + 1));
		}
		const std::size_t local_offset = checkedAdd(
			zip_base_offset,
			relative_local_offset,
			"ZIP Error: Local file header offset overflow.");
		if (local_offset > UINT32_MAX) {
			throw std::runtime_error("ZIP Error: Local file header offset exceeds ZIP32 limits.");
		}

		writeLe32(archive, cursor + CENTRAL_LOCAL_OFFSET_OFFSET, static_cast<uint32_t>(local_offset));
		cursor += record_size;
	}

	if (cursor != eocd.central_end) {
		throw std::runtime_error("ZIP Error: Central directory size does not match parsed records.");
	}
}

void writeArchiveChunkCrc(vBytes& archive) {
	// The CRC covers the chunk type and data, excluding the length and CRC fields.
	const std::size_t crc_start = ARCHIVE_PREFIX_SIZE - 4;
	const std::size_t crc_index = archive.size() - ARCHIVE_CRC_SIZE;
	const uint32_t crc = lodepng_crc32(&archive[crc_start], crc_index - crc_start);
	writeValueAt(archive, crc_index, crc, 4);
}

// The trailing PNG bytes extend the EOCD comment to the end of the file. Make
// sure the final CRC and IEND chunk did not create a later EOCD signature that
// an extractor could pick over the real record. The comment itself was checked
// by findZipEocdLocator; only windows touching the CRC or IEND remain.
void ensureNoTrailingEocdSignature(std::span<const Byte> archive, std::span<const Byte> iend) {
	constexpr std::size_t SIGNATURE_OVERLAP = 3;
	vBytes tail(archive.end() - (ARCHIVE_CRC_SIZE + SIGNATURE_OVERLAP), archive.end());
	tail.insert(tail.end(), iend.begin(), iend.end());
	for (std::size_t pos = 0; pos + 4 <= tail.size(); ++pos) {
		if (hasLe32Signature(tail, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)) {
			throw std::runtime_error(
				"Embed Error: The final IDAT CRC forms a ZIP end-of-central-directory signature. "
				"Change the archive or cover image slightly and try again.");
		}
	}
}

} // anonymous namespace

std::array<std::span<const Byte>, 3> PolyglotImage::parts() const {
	const std::span<const Byte> png(image);
	return {
		png.first(png.size() - IEND_CHUNK_SIZE),
		std::span<const Byte>(archive_chunk),
		png.last(IEND_CHUNK_SIZE),
	};
}

// ============================================================================
// Public: Embed the script chunk and archive into the image
// ============================================================================

PolyglotImage embedChunks(vBytes image_vec, vBytes script_vec, vBytes archive_vec) {
	if (image_vec.size() < ICCP_CHUNK_INDEX + IEND_CHUNK_SIZE) {
		throw std::runtime_error("Embed Error: Optimized PNG is too small for chunk insertion.");
	}
	if (script_vec.size() < CHUNK_FIELDS_COMBINED_LENGTH) {
		throw std::runtime_error("Embed Error: Script chunk is truncated.");
	}
	if (archive_vec.size() < CHUNK_FIELDS_COMBINED_LENGTH) {
		throw std::runtime_error("Embed Error: Archive chunk is truncated.");
	}

	// Insert the iCCP script chunk after the PNG header.
	image_vec.insert(image_vec.begin() + ICCP_CHUNK_INDEX, script_vec.begin(), script_vec.end());
	script_vec = vBytes{}; // Release memory.

	// The archive chunk is written just before IEND, so its ZIP data starts
	// ARCHIVE_PREFIX_SIZE bytes after everything that precedes IEND.
	const std::size_t zip_base_offset = checkedAdd(
		image_vec.size() - IEND_CHUNK_SIZE,
		ARCHIVE_PREFIX_SIZE,
		"ZIP Error: Base offset overflow.");

	// Offsets are inside the CRC-covered region, so fix them first.
	fixZipOffsets(archive_vec, zip_base_offset);
	writeArchiveChunkCrc(archive_vec);
	ensureNoTrailingEocdSignature(archive_vec, std::span<const Byte>(image_vec).last(IEND_CHUNK_SIZE));

	return PolyglotImage{ .image = std::move(image_vec), .archive_chunk = std::move(archive_vec) };
}
