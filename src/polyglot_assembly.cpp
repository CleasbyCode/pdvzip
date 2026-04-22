#include "pdvzip.h"

namespace {

constexpr auto
	ZIP_LOCAL_SIG       = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 }),
	END_CENTRAL_DIR_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x05, 0x06 }),
	CENTRAL_DIR_SIG     = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });

struct ZipEocdInfo {
	std::size_t index;
	uint16_t total_records;
	uint32_t central_size;
	uint32_t central_offset;
	uint16_t comment_length;
};

[[nodiscard]] ZipEocdInfo findZipEocd(std::span<const Byte> image_vec, std::size_t zip_base_offset) {
	constexpr std::size_t
		EOCD_MIN_SIZE             = 22,
		EOCD_TOTAL_RECORDS_OFFSET = 10,
		EOCD_CENTRAL_SIZE_OFFSET  = 12,
		EOCD_CENTRAL_OFFSET       = 16,
		EOCD_COMMENT_LENGTH       = 20,
		MAX_EOCD_SEARCH_DISTANCE  = 65557; // EOCD_MIN_SIZE + max 65535-byte comment.

	if (image_vec.size() < EOCD_MIN_SIZE) {
		throw std::runtime_error("ZIP Error: Archive is too small.");
	}

	// Bound the backward search to the archive region and the maximum
	// possible EOCD distance from the end of the file.
	const std::size_t distance_floor = (image_vec.size() > MAX_EOCD_SEARCH_DISTANCE)
		? image_vec.size() - MAX_EOCD_SEARCH_DISTANCE
		: std::size_t{0};
	const std::size_t search_floor = std::max(zip_base_offset, distance_floor);
	const std::size_t search_start = image_vec.size() - END_CENTRAL_DIR_SIG.size();

	// Guard against an EOCD that could only lie outside the bounded region —
	// without this, the reverse loop would decrement past zero.
	if (search_start < search_floor) {
		throw std::runtime_error("ZIP Error: End of Central Directory signature not found.");
	}

	bool saw_empty_records = false;
	bool saw_zip64 = false;

	for (std::size_t pos = search_start; ; --pos) {
		if (std::equal(END_CENTRAL_DIR_SIG.begin(), END_CENTRAL_DIR_SIG.end(), image_vec.begin() + pos)
			&& pos + EOCD_MIN_SIZE <= image_vec.size()) {
			const auto info = ZipEocdInfo{
				.index          = pos,
				.total_records  = readLe16(image_vec, pos + EOCD_TOTAL_RECORDS_OFFSET),
				.central_size   = readLe32(image_vec, pos + EOCD_CENTRAL_SIZE_OFFSET),
				.central_offset = readLe32(image_vec, pos + EOCD_CENTRAL_OFFSET),
				.comment_length = readLe16(image_vec, pos + EOCD_COMMENT_LENGTH),
			};

			if (pos + EOCD_MIN_SIZE + info.comment_length <= image_vec.size()) {
				if (info.total_records == 0) {
					saw_empty_records = true;
				} else if (info.total_records == UINT16_MAX
					|| info.central_size == UINT32_MAX
					|| info.central_offset == UINT32_MAX) {
					saw_zip64 = true;
				} else {
					const std::size_t central_start = checkedAdd(
						zip_base_offset,
						static_cast<std::size_t>(info.central_offset),
						"ZIP Error: Central directory offset overflow.");
					const std::size_t central_end = checkedAdd(
						central_start,
						static_cast<std::size_t>(info.central_size),
						"ZIP Error: Central directory size overflow.");
					if (central_start <= image_vec.size() && central_end <= image_vec.size() && central_end == pos) {
						return info;
					}
				}
			}
		}

		if (pos == search_floor) {
			break;
		}
	}

	if (saw_zip64) {
		throw std::runtime_error("ZIP Error: ZIP64 archives are not supported.");
	}
	if (saw_empty_records) {
		throw std::runtime_error("ZIP Error: Archive contains no records.");
	}
	throw std::runtime_error("ZIP Error: End of Central Directory signature not found.");
}

// Fix all ZIP record offsets so the archive remains valid after being embedded
// behind the PNG wrapper bytes.
void fixZipOffsets(vBytes& image_vec, std::size_t original_image_size, std::size_t script_data_size) {
	constexpr std::size_t
		ZIP_BASE_SHIFT                 = 8,
		PNG_TRAILING_BYTES             = 16,
		CENTRAL_RECORD_MIN_SIZE        = 46,
		CENTRAL_NAME_LENGTH_OFFSET     = 28,
		CENTRAL_EXTRA_LENGTH_OFFSET    = 30,
		CENTRAL_COMMENT_LENGTH_OFFSET  = 32,
		CENTRAL_LOCAL_OFFSET_OFFSET    = 42;

	const std::size_t zip_base_offset = checkedAdd(
		checkedAdd(original_image_size, script_data_size, "ZIP Error: Base offset overflow."),
		ZIP_BASE_SHIFT,
		"ZIP Error: Base offset overflow.");
	const ZipEocdInfo eocd = findZipEocd(image_vec, zip_base_offset);
	const std::size_t central_start = checkedAdd(
		zip_base_offset,
		static_cast<std::size_t>(eocd.central_offset),
		"ZIP Error: Central directory offset overflow.");
	const std::size_t central_end = checkedAdd(
		central_start,
		static_cast<std::size_t>(eocd.central_size),
		"ZIP Error: Central directory size overflow.");

	if (central_start > image_vec.size() || central_end > image_vec.size() || central_end != eocd.index) {
		throw std::runtime_error("ZIP Error: Central directory bounds are invalid.");
	}
	if (central_start > UINT32_MAX) {
		throw std::runtime_error("ZIP Error: Central directory offset exceeds ZIP32 limits.");
	}
	if (eocd.comment_length > UINT16_MAX - PNG_TRAILING_BYTES) {
		throw std::runtime_error("ZIP Error: Comment length overflow.");
	}

	writeLe16(image_vec, eocd.index + 20, static_cast<uint16_t>(eocd.comment_length + PNG_TRAILING_BYTES));
	writeLe32(image_vec, eocd.index + 16, static_cast<uint32_t>(central_start));

	std::size_t cursor = central_start;
	for (uint16_t i = 0; i < eocd.total_records; ++i) {
		if (cursor > image_vec.size() || CENTRAL_RECORD_MIN_SIZE > image_vec.size() - cursor) {
			throw std::runtime_error("ZIP Error: Truncated central directory file header.");
		}

		if (!std::equal(CENTRAL_DIR_SIG.begin(), CENTRAL_DIR_SIG.end(), image_vec.begin() + cursor)) {
			throw std::runtime_error(std::format(
				"ZIP Error: Invalid central directory file header signature at record {}.", i + 1));
		}

		const std::size_t name_length = readLe16(image_vec, cursor + CENTRAL_NAME_LENGTH_OFFSET);
		const std::size_t extra_length = readLe16(image_vec, cursor + CENTRAL_EXTRA_LENGTH_OFFSET);
		const std::size_t comment_length = readLe16(image_vec, cursor + CENTRAL_COMMENT_LENGTH_OFFSET);
		const std::size_t record_size = CENTRAL_RECORD_MIN_SIZE + name_length + extra_length + comment_length;

		if (record_size > image_vec.size() - cursor || cursor + record_size > central_end) {
			throw std::runtime_error("ZIP Error: Central directory entry exceeds archive bounds.");
		}

		const std::size_t local_offset = checkedAdd(
			zip_base_offset,
			static_cast<std::size_t>(readLe32(image_vec, cursor + CENTRAL_LOCAL_OFFSET_OFFSET)),
			"ZIP Error: Local file header offset overflow.");
		if (local_offset > UINT32_MAX) {
			throw std::runtime_error("ZIP Error: Local file header offset exceeds ZIP32 limits.");
		}
		if (local_offset >= central_start || 4 > image_vec.size() - local_offset) {
			throw std::runtime_error("ZIP Error: Local file header offset is out of bounds.");
		}
		if (!std::equal(ZIP_LOCAL_SIG.begin(), ZIP_LOCAL_SIG.end(), image_vec.begin() + local_offset)) {
			throw std::runtime_error(std::format(
				"ZIP Error: Local file header signature mismatch for record {}.", i + 1));
		}

		writeLe32(image_vec, cursor + CENTRAL_LOCAL_OFFSET_OFFSET, static_cast<uint32_t>(local_offset));
		cursor += record_size;
	}

	if (cursor != central_end) {
		throw std::runtime_error("ZIP Error: Central directory size does not match parsed records.");
	}
}

} // anonymous namespace

// ============================================================================
// Public: Embed the script chunk and archive into the image
// ============================================================================

void embedChunks(vBytes& image_vec, vBytes script_vec, vBytes archive_vec, std::size_t original_image_size) {

	constexpr std::size_t
		ICCP_CHUNK_INDEX            = 0x21,
		VALUE_BYTE_LENGTH_FOUR      = 4,
		ARCHIVE_INSERT_INDEX_DIFF   = 12,
		EXCLUDE_SIZE_AND_CRC_LENGTH = 8,
		LAST_IDAT_INDEX_DIFF        = 4,
		LAST_IDAT_CRC_INDEX_DIFF    = 16;

	const std::size_t
		script_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH,
		archive_file_size = archive_vec.size();

	image_vec.reserve(image_vec.size() + script_vec.size() + archive_vec.size());

	// Insert iCCP script chunk after the PNG header.
	image_vec.insert(image_vec.begin() + ICCP_CHUNK_INDEX, script_vec.begin(), script_vec.end());
	script_vec = vBytes{}; // Release memory.

	// Insert archive data before the IEND chunk.
	image_vec.insert(image_vec.end() - ARCHIVE_INSERT_INDEX_DIFF, archive_vec.begin(), archive_vec.end());
	archive_vec = vBytes{}; // Release memory.

	// Fix ZIP internal offsets (must happen before CRC computation,
	// since the offsets are within the CRC-covered region).
	fixZipOffsets(image_vec, original_image_size, script_data_size);

	// Recompute the last IDAT chunk CRC.
	const std::size_t
		last_idat_index = original_image_size + script_data_size + LAST_IDAT_INDEX_DIFF,
		complete_size   = image_vec.size();

	if (archive_file_size < EXCLUDE_SIZE_AND_CRC_LENGTH) {
		throw std::runtime_error("Embed Error: Archive too small for CRC computation.");
	}
	const std::size_t crc_length = archive_file_size - EXCLUDE_SIZE_AND_CRC_LENGTH;
	if (last_idat_index > complete_size || crc_length > complete_size - last_idat_index) {
		throw std::runtime_error("Embed Error: IDAT CRC region exceeds image bounds.");
	}

	const uint32_t last_idat_crc = lodepng_crc32(&image_vec[last_idat_index], crc_length);

	const std::size_t crc_index = complete_size - LAST_IDAT_CRC_INDEX_DIFF;
	writeValueAt(image_vec, crc_index, last_idat_crc, VALUE_BYTE_LENGTH_FOUR);
}
