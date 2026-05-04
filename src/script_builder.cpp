#include "script_builder_internal.h"

using script_builder_internal::buildScriptText;

namespace {

constexpr std::size_t SCRIPT_INDEX = 0x16;
constexpr std::size_t ICCP_CHUNK_NAME_INDEX = 0x04;
constexpr std::size_t ICCP_CHUNK_NAME_LENGTH = 4;
constexpr std::size_t ICCP_CRC_INDEX_DIFF = 8;
constexpr std::size_t LENGTH_FIRST_BYTE_INDEX = 3;
constexpr std::size_t LENGTH_INDEX = 0;
constexpr std::size_t VALUE_LENGTH = 4;
constexpr std::size_t PAD_OFFSET = 8;
constexpr std::size_t MAX_PAD_ATTEMPTS = 32;
constexpr std::string_view PAD = "........";

void padChunkLengthUntilLinuxSafe(vBytes& script_vec, std::size_t& chunk_data_size) {
	std::size_t pad_attempts = 0;

	while (std::ranges::contains(LINUX_PROBLEM_METACHARACTERS, script_vec[LENGTH_FIRST_BYTE_INDEX])) {
		if (++pad_attempts > MAX_PAD_ATTEMPTS) {
			throw std::runtime_error("Script Error: Could not make iCCP chunk length Linux-safe.");
		}

		script_vec.insert(
			script_vec.begin() + chunk_data_size + PAD_OFFSET,
			PAD.begin(),
			PAD.end());

		chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
		writeValueAt(script_vec, LENGTH_INDEX, chunk_data_size, VALUE_LENGTH);
	}
}

void writeChunkCrc(vBytes& script_vec, std::size_t chunk_data_size) {
	const std::size_t iccp_chunk_length = chunk_data_size + ICCP_CHUNK_NAME_LENGTH;
	const uint32_t crc = lodepng_crc32(&script_vec[ICCP_CHUNK_NAME_INDEX], iccp_chunk_length);
	const std::size_t crc_index = chunk_data_size + ICCP_CRC_INDEX_DIFF;

	writeValueAt(script_vec, crc_index, crc, VALUE_LENGTH);
}

}  // namespace

// ============================================================================
// Public: Build the extraction script chunk (iCCP)
// ============================================================================

vBytes buildExtractionScript(FileType file_type, const std::string& first_filename,
	const UserArguments& user_args) {

	// iCCP chunk header skeleton.
	vBytes script_vec {
		0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50,
		0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 0x5F, 0x00,
		0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A,
		0x00, 0x00, 0x00, 0x00
	};
	script_vec.reserve(script_vec.size() + MAX_SCRIPT_SIZE);

	const std::string script_text = buildScriptText(file_type, first_filename, user_args);
	script_vec.insert(
		script_vec.begin() + SCRIPT_INDEX,
		script_text.begin(),
		script_text.end());

	std::size_t chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
	writeValueAt(script_vec, LENGTH_INDEX, chunk_data_size, VALUE_LENGTH);

	// If the first byte of the chunk length is a problematic metacharacter for
	// the Linux extraction script, pad the chunk until it lands on a safe byte.
	padChunkLengthUntilLinuxSafe(script_vec, chunk_data_size);

	if (chunk_data_size > MAX_SCRIPT_SIZE) {
		throw std::runtime_error("Script Size Error: Extraction script exceeds size limit.");
	}

	writeChunkCrc(script_vec, chunk_data_size);
	return script_vec;
}
