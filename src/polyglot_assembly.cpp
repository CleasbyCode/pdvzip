#include "pdvzip.h"

namespace {

// ============================================================================
// Internal: Fix all ZIP record offsets so the archive is valid within the polyglot
// ============================================================================

void fixZipOffsets(vBytes& image_vec, std::size_t original_image_size, std::size_t script_data_size) {

	constexpr std::size_t
		CENTRAL_LOCAL_INDEX_DIFF = 45,
		ZIP_COMMENT_LENGTH_DIFF  = 21,
		END_CENTRAL_START_DIFF   = 19,
		ZIP_RECORDS_DIFF         = 11,
		PNG_IEND_LENGTH          = 16,
		ZIP_LOCAL_INDEX_DIFF     = 4,
		ZIP_SIG_LENGTH           = 4,
		LAST_IDAT_INDEX_DIFF     = 4;

	constexpr auto
		ZIP_LOCAL_SIG         = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 }),
		END_CENTRAL_DIR_SIG   = std::to_array<Byte>({ 0x50, 0x4B, 0x05, 0x06 }),
		START_CENTRAL_DIR_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });

	constexpr auto LE = std::endian::little;

	constexpr std::size_t
		VALUE_BYTE_LENGTH_TWO = 2,
		VALUE_BYTE_LENGTH_FOUR = 4;

	const std::size_t
		complete_size   = image_vec.size(),
		last_idat_index = original_image_size + script_data_size + LAST_IDAT_INDEX_DIFF;

	// --- Locate the End of Central Directory record (searching backwards) ---

	auto eocd_it = std::search(image_vec.rbegin(), image_vec.rend(), END_CENTRAL_DIR_SIG.rbegin(), END_CENTRAL_DIR_SIG.rend());

	if (eocd_it == image_vec.rend()) {
		throw std::runtime_error("ZIP Error: End of Central Directory signature not found.");
	}

	const std::size_t
		eocd_distance         = static_cast<std::size_t>(std::distance(image_vec.rbegin(), eocd_it)),
		end_central_dir_index = complete_size - eocd_distance - ZIP_SIG_LENGTH;

	// Validate that the EOCD leaves room for the fields we need to read.
	constexpr std::size_t EOCD_MIN_SIZE = 22; // Minimum EOCD record size.

	if (end_central_dir_index + EOCD_MIN_SIZE > complete_size) {
		throw std::runtime_error("ZIP Error: End of Central Directory record is truncated.");
	}

	const std::size_t
		total_records_index  = end_central_dir_index + ZIP_RECORDS_DIFF,
		end_central_start    = end_central_dir_index + END_CENTRAL_START_DIFF,
		comment_length_index = end_central_dir_index + ZIP_COMMENT_LENGTH_DIFF;

	const uint16_t total_records = static_cast<uint16_t>(getValue(image_vec, total_records_index, VALUE_BYTE_LENGTH_TWO, LE));

	if (total_records == 0) {
		throw std::runtime_error("ZIP Error: Archive contains no records.");
	}

	// Extend the ZIP comment length to cover the PNG IEND chunk (required for JAR).
	const uint16_t original_comment_length = static_cast<uint16_t>(getValue(image_vec, comment_length_index, VALUE_BYTE_LENGTH_TWO, LE));

	if (original_comment_length > UINT16_MAX - PNG_IEND_LENGTH) {
		throw std::runtime_error("ZIP Error: Comment length overflow.");
	}

	const uint16_t new_comment_length = original_comment_length + PNG_IEND_LENGTH;
	updateValue(image_vec, comment_length_index, new_comment_length, VALUE_BYTE_LENGTH_TWO, LE);

	// --- Find the first Start Central Directory entry (searching backwards) ---

	std::size_t start_central_index = 0;
	auto search_it = image_vec.rbegin();

	for (uint16_t i = 0; i < total_records; ++i) {
		search_it = std::search(search_it, image_vec.rend(), START_CENTRAL_DIR_SIG.rbegin(), START_CENTRAL_DIR_SIG.rend());

		if (search_it == image_vec.rend()) {
			throw std::runtime_error(std::format("ZIP Error: Expected {} central directory records, found only {}.", total_records, i));
		}

		start_central_index = complete_size - static_cast<std::size_t>(std::distance(image_vec.rbegin(), search_it++)) - ZIP_SIG_LENGTH;
	}

	// Update the End Central Directory to point to the Start Central Directory.
	updateValue(image_vec, end_central_start, static_cast<uint32_t>(start_central_index), VALUE_BYTE_LENGTH_FOUR, LE);

	// --- Rewrite each central directory entry's local header offset ---

	std::size_t
		local_index         = last_idat_index + ZIP_LOCAL_INDEX_DIFF,
		central_local_index = start_central_index + CENTRAL_LOCAL_INDEX_DIFF;

	for (uint16_t i = 0; i < total_records; ++i) {
		updateValue(image_vec, central_local_index, static_cast<uint32_t>(local_index), VALUE_BYTE_LENGTH_FOUR, LE);

		// Only search for the next record if this isn't the last one.
		if (i + 1 < total_records) {
			auto next_local = searchSig(image_vec, ZIP_LOCAL_SIG, local_index + 1);
			if (!next_local) {
				throw std::runtime_error(std::format("ZIP Error: Local file header {} of {} not found.", i + 2, total_records));
			}
			local_index = *next_local;

			auto next_central = searchSig(image_vec, START_CENTRAL_DIR_SIG, central_local_index + 1);
			if (!next_central) {
				throw std::runtime_error(std::format("ZIP Error: Central directory entry {} of {} not found.", i + 2, total_records));
			}
			central_local_index = *next_central + CENTRAL_LOCAL_INDEX_DIFF;
		}
	}
}

} // anonymous namespace

// ============================================================================
// Public: Embed the script chunk and archive into the image
// ============================================================================

void embedChunks(vBytes& image_vec, vBytes script_vec, vBytes archive_vec, std::size_t original_image_size) {

	constexpr std::size_t
		ICCP_CHUNK_INDEX            = 0x21,
		VALUE_BYTE_LENGTH_FOUR		= 4,
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

	const uint32_t last_idat_crc = lodepng_crc32(&image_vec[last_idat_index], archive_file_size - EXCLUDE_SIZE_AND_CRC_LENGTH);

	const std::size_t crc_index = complete_size - LAST_IDAT_CRC_INDEX_DIFF;
	updateValue(image_vec, crc_index, last_idat_crc, VALUE_BYTE_LENGTH_FOUR);
}
