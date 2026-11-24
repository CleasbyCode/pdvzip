#include "pdvzip.h"

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

// Rebase all ZIP record offsets onto zip_base_offset, the absolute file offset
// of the ZIP data once the archive chunk is placed in the output image.
// Returns the EOCD record's position in the archive chunk.
[[nodiscard]] std::size_t fixZipOffsets(vBytes& archive, std::size_t zip_base_offset) {
	// Re-check the EOCD at the mutation boundary.
	const ZipCentralDirectory directory = readZipCentralDirectory(
		archive, ARCHIVE_PREFIX_SIZE, archive.size() - ARCHIVE_CRC_SIZE, "ZIP Error");

	const std::size_t central_offset = checkedAdd(
		zip_base_offset,
		directory.start - ARCHIVE_PREFIX_SIZE,
		"ZIP Error: Central directory offset overflow.");
	if (central_offset > UINT32_MAX) {
		throw std::runtime_error("ZIP Error: Central directory offset exceeds ZIP32 limits.");
	}
	if (directory.eocd_comment_length > UINT16_MAX - PNG_TRAILING_BYTES) {
		throw std::runtime_error("ZIP Error: Comment length overflow.");
	}

	writeLe16(archive, directory.eocd_index + ZIP_EOCD_COMMENT_LENGTH_OFFSET, static_cast<uint16_t>(directory.eocd_comment_length + PNG_TRAILING_BYTES));
	writeLe32(archive, directory.eocd_index + ZIP_EOCD_CENTRAL_OFFSET, static_cast<uint32_t>(central_offset));

	std::size_t cursor = directory.start;
	for (uint16_t i = 0; i < directory.total_records; ++i) {
		if (ZIP_CENTRAL_RECORD_MIN_SIZE > directory.end - cursor) {
			throw std::runtime_error("ZIP Error: Truncated central directory file header.");
		}

		if (!hasLe32Signature(archive, cursor, ZIP_CENTRAL_DIRECTORY_SIGNATURE)) {
			throw std::runtime_error(std::format(
				"ZIP Error: Invalid central directory file header signature at record {}.", i + 1));
		}

		const std::size_t name_length = readLe16(archive, cursor + ZIP_CENTRAL_NAME_LENGTH_OFFSET);
		const std::size_t extra_length = readLe16(archive, cursor + ZIP_CENTRAL_EXTRA_LENGTH_OFFSET);
		const std::size_t comment_length = readLe16(archive, cursor + ZIP_CENTRAL_COMMENT_LENGTH_OFFSET);
		const std::size_t record_size = zipCentralDirectoryRecordSize(name_length, extra_length, comment_length);

		if (readLe16(archive, cursor + ZIP_CENTRAL_DISK_START) != 0) {
			throw std::runtime_error(std::format(
				"ZIP Error: Multi-disk local header reference on record {} is not supported.", i + 1));
		}
		if (record_size > directory.end - cursor) {
			throw std::runtime_error("ZIP Error: Central directory entry exceeds archive bounds.");
		}

		const std::size_t relative_local_offset = readLe32(archive, cursor + ZIP_CENTRAL_LOCAL_OFFSET);
		const std::size_t local_header = checkedAdd(
			ARCHIVE_PREFIX_SIZE,
			relative_local_offset,
			"ZIP Error: Local file header offset overflow.");
		if (local_header >= directory.start
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

		writeLe32(archive, cursor + ZIP_CENTRAL_LOCAL_OFFSET, static_cast<uint32_t>(local_offset));
		cursor += record_size;
	}

	if (cursor != directory.end) {
		throw std::runtime_error("ZIP Error: Central directory size does not match parsed records.");
	}
	return directory.eocd_index;
}

void writeArchiveChunkCrc(vBytes& archive) {
	// The CRC covers the chunk type and data, excluding the length and CRC fields.
	const std::size_t crc_start = ARCHIVE_PREFIX_SIZE - 4;
	const std::size_t crc_index = archive.size() - ARCHIVE_CRC_SIZE;
	const uint32_t crc = crc32Update(0, std::span<const Byte>(archive).subspan(crc_start, crc_index - crc_start));
	writeBe32(archive, crc_index, crc);
}

// findZipEocdLocator checked the archive as the user supplied it, but the
// bytes written since then can spell a signature that extractors would act on:
// rebasing rewrote the EOCD's comment length and central directory offset and
// every record's local header offset (the last record's sits 20 bytes before
// the EOCD, where a ZIP64 locator would be read), and the final CRC and IEND
// chunk extend the comment to the end of the file. Re-check the final bytes:
// no EOCD signature may follow the real record, since Info-ZIP and others take
// the last one, and no ZIP64 locator may precede it.
void ensureEocdIsStillSelected(std::span<const Byte> archive, std::size_t eocd_index, std::span<const Byte> iend) {
	constexpr std::size_t SIGNATURE_OVERLAP = 3;

	const std::size_t crc_index = archive.size() - ARCHIVE_CRC_SIZE;
	for (std::size_t pos = eocd_index + 1; pos + 4 <= crc_index; ++pos) {
		if (hasLe32Signature(archive, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)) {
			throw std::runtime_error(
				"Embed Error: Rebasing the archive's offsets for this cover image forms a second ZIP "
				"end-of-central-directory signature. Change the archive (or its comment) or the cover "
				"image and try again.");
		}
	}
	vBytes tail(archive.begin() + static_cast<std::ptrdiff_t>(crc_index - SIGNATURE_OVERLAP), archive.end());
	tail.insert(tail.end(), iend.begin(), iend.end());
	for (std::size_t pos = 0; pos + 4 <= tail.size(); ++pos) {
		if (hasLe32Signature(tail, pos, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE)) {
			throw std::runtime_error(
				"Embed Error: The final IDAT CRC forms a ZIP end-of-central-directory signature. "
				"Change the archive or cover image slightly and try again.");
		}
	}
	if (eocd_index >= ARCHIVE_PREFIX_SIZE + ZIP64_LOCATOR_SIZE
		&& hasLe32Signature(archive, eocd_index - ZIP64_LOCATOR_SIZE, ZIP64_END_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE)) {
		throw std::runtime_error(
			"Embed Error: Rebasing the archive's offsets for this cover image forms a ZIP64 locator "
			"signature before the end-of-central-directory record. Change the archive or the cover "
			"image and try again.");
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
	const std::size_t eocd_index = fixZipOffsets(archive_vec, zip_base_offset);
	writeArchiveChunkCrc(archive_vec);
	ensureEocdIsStillSelected(archive_vec, eocd_index, std::span<const Byte>(image_vec).last(IEND_CHUNK_SIZE));

	return PolyglotImage{ .image = std::move(image_vec), .archive_chunk = std::move(archive_vec) };
}
