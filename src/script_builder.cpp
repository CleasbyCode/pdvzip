#include "pdvzip.h"

namespace {

// ============================================================================
// Internal: Script templates (Linux + Windows pairs per file type)
// ============================================================================

const std::vector<std::vector<Byte>>& getExtractionScripts() {
	static const std::vector<std::vector<Byte>> SCRIPTS = [] {
		constexpr auto CRLF = "\r\n"sv;
		constexpr std::array<std::pair<std::string_view, std::string_view>, 10> TEMPLATES = {{
			// VIDEO_AUDIO
			{
				R"(ITEM="";DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v mpv >$NUL 2>&1;then clear;mpv --quiet --geometry=50%:50% "$ITEM" &> $NUL;elif command -v vlc >$NUL 2>&1;then clear;vlc --play-and-exit --no-video-title-show "$ITEM" &> $NUL;elif command -v firefox >$NUL 2>&1;then clear;firefox "$ITEM" &> $NUL;else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&exit)"sv
			},
			// PDF
			{
				R"(ITEM="";DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v evince >$NUL 2>&1;then clear;evince "$ITEM" &> $NUL;else firefox "$ITEM" &> $NUL;clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&exit)"sv
			},
			// PYTHON
			{
				R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v python3 >/dev/null 2>&1;then clear;python3 "$ITEM" ;else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set ITEM=&set ARGS=&set APP=python3&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&where !APP! >nul 2>&1 && (!APP! "!ITEM!" !ARGS! ) || (cls&exit)&echo.&exit)"sv
			},
			// POWERSHELL
			{
				R"(DIR="pdvzip_extracted";ITEM="";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v pwsh >/dev/null 2>&1;then clear;pwsh "$ITEM" ;else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set ITEM=&set ARGS=&set DIR=pdvzip_extracted&set PDIR="%SystemDrive%\Program Files\PowerShell\"&cls&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&IF EXIST !PDIR! (pwsh -ExecutionPolicy Bypass -File "!ITEM!" !ARGS!&echo.&exit) ELSE (powershell -ExecutionPolicy Bypass -File "!ITEM!" !ARGS!&echo.&exit))"sv
			},
			// BASH_SHELL
			{
				R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" ;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&"" &cls&exit)"sv
			},
			// WINDOWS_EXECUTABLE
			{
				R"(DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";clear;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&"" &echo.&exit)"sv
			},
			// FOLDER
			{
				R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM" &> /dev/null;clear;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&powershell "II ''"&cls&exit)"sv
			},
			// LINUX_EXECUTABLE
			{
				R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" ;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&cls&exit)"sv
			},
			// JAR
			{
				R"(clear;hash -r;if command -v java >/dev/null 2>&1;then clear;java -jar "$0" ;else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set ARGS=&set APP=java&cls&where !APP! >nul 2>&1 && (!APP! -jar "%~dpnx0" !ARGS! ) || (cls)&ren "%~dpnx0" *.png&echo.&exit)"sv
			},
			// UNKNOWN_FILE_TYPE
			{
				R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM";exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&echo.&exit)"sv
			}
		}};

		std::vector<std::vector<Byte>> result;
		result.reserve(TEMPLATES.size());
		for (const auto& [linux_part, windows_part] : TEMPLATES) {
			vBytes combined;
			combined.reserve(linux_part.size() + CRLF.size() + windows_part.size());
			combined.insert(combined.end(), linux_part.begin(), linux_part.end());
			combined.insert(combined.end(), CRLF.begin(), CRLF.end());
			combined.insert(combined.end(), windows_part.begin(), windows_part.end());
			result.emplace_back(std::move(combined));
		}
		return result;
	}();
	return SCRIPTS;
}

// Script insertion offset map: { script_id, offset1, offset2, ... }
// Offsets are insertion points within the script vector for filenames, arguments, etc.
const std::unordered_map<FileType, std::vector<uint16_t>> SCRIPT_OFFSET_MAP = {
	{FileType::VIDEO_AUDIO,          { 0, 0x1E4, 0x1C }},
	{FileType::PDF,                  { 1, 0x196, 0x1C }},
	{FileType::PYTHON,               { 2, 0x10B, 0x101, 0xBC, 0x1C }},
	{FileType::POWERSHELL,           { 3, 0x105, 0xFB,  0xB6, 0x33 }},
	{FileType::BASH_SHELL,           { 4, 0x134, 0x132, 0x8E, 0x1C }},
	{FileType::WINDOWS_EXECUTABLE,   { 5, 0x116, 0x114 }},
	{FileType::FOLDER,               { 6, 0x149, 0x1C }},
	{FileType::LINUX_EXECUTABLE,     { 7, 0x8E,  0x1C }},
	{FileType::JAR,                  { 8, 0xA6,  0x61 }},
	{FileType::UNKNOWN_FILE_TYPE,    { 9, 0x127, 0x1C }},
};

} // anonymous namespace

// ============================================================================
// Public: Build the extraction script chunk (iCCP)
// ============================================================================

vBytes buildExtractionScript(FileType file_type, const std::string& first_filename,
                             const UserArguments& user_args) {

	constexpr std::size_t
		SCRIPT_INDEX            = 0x16,
		SCRIPT_ELEMENT_INDEX    = 0,
		ICCP_CHUNK_NAME_INDEX   = 0x04,
		ICCP_CHUNK_NAME_LENGTH  = 4,
		ICCP_CRC_INDEX_DIFF     = 8,
		LENGTH_FIRST_BYTE_INDEX = 3;

	// iCCP chunk header skeleton.
	vBytes script_vec {
		0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50,
		0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 0x5F, 0x00,
		0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A,
		0x00, 0x00, 0x00, 0x00
	};
	script_vec.reserve(script_vec.size() + MAX_SCRIPT_SIZE);

	// Look up the offsets for this file type.
	FileType lookup_type = SCRIPT_OFFSET_MAP.contains(file_type) ? file_type : FileType::UNKNOWN_FILE_TYPE;
	const auto& offsets = SCRIPT_OFFSET_MAP.at(lookup_type);

	const uint16_t script_id = offsets[SCRIPT_ELEMENT_INDEX];
	const auto& extraction_scripts = getExtractionScripts();

	// Insert the base script template.
	script_vec.insert(
		script_vec.begin() + SCRIPT_INDEX,
		extraction_scripts[script_id].begin(),
		extraction_scripts[script_id].end());

	// Helper to insert a string at a given offset within the script vector.
	auto insertAt = [&script_vec](uint16_t offset, const std::string& str) {
		script_vec.insert(script_vec.begin() + offset, str.begin(), str.end());
	};

	// Patch the script with filenames and user arguments.
	// The offsets in the map are ordered largest-first so that earlier
	// insertions don't invalidate later offsets.
	const std::string& args_combined = user_args.linux_args.empty()
		? user_args.windows_args : user_args.linux_args;

	switch (file_type) {
		case FileType::WINDOWS_EXECUTABLE:
		case FileType::LINUX_EXECUTABLE:
			insertAt(offsets[1], args_combined);
			insertAt(offsets[2], first_filename);
			break;

		case FileType::JAR:
			insertAt(offsets[1], user_args.windows_args);
			insertAt(offsets[2], user_args.linux_args);
			break;

		case FileType::PYTHON:
		case FileType::POWERSHELL:
		case FileType::BASH_SHELL:
			insertAt(offsets[1], user_args.windows_args);
			insertAt(offsets[2], first_filename);
			insertAt(offsets[3], user_args.linux_args);
			insertAt(offsets[4], first_filename);
			break;

		default:
			// VIDEO_AUDIO, PDF, FOLDER, UNKNOWN — just patch the filename.
			insertAt(offsets[1], first_filename);
			insertAt(offsets[2], first_filename);
			break;
	}

	// Update the iCCP chunk length field.
	constexpr std::size_t
		LENGTH_INDEX = 0,
		VALUE_LENGTH = 4;

	std::size_t chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;

	updateValue(script_vec, LENGTH_INDEX, static_cast<uint32_t>(chunk_data_size), VALUE_LENGTH);

	// If the first byte of the chunk length is a problematic metacharacter for
	// the Linux extraction script, pad the chunk to shift past it.
	if (std::ranges::contains(LINUX_PROBLEM_METACHARACTERS, script_vec[LENGTH_FIRST_BYTE_INDEX])) {
		constexpr std::string_view PAD = "........";
		constexpr std::size_t PAD_OFFSET = 8;
		script_vec.insert(
			script_vec.begin() + chunk_data_size + PAD_OFFSET,
			PAD.begin(), PAD.end());
		chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
		updateValue(script_vec, LENGTH_INDEX, static_cast<uint32_t>(chunk_data_size), VALUE_LENGTH);
	}

	if (chunk_data_size > MAX_SCRIPT_SIZE) {
		throw std::runtime_error("Script Size Error: Extraction script exceeds size limit.");
	}

	// Compute and write the CRC for the iCCP chunk.
	const std::size_t iccp_chunk_length = chunk_data_size + ICCP_CHUNK_NAME_LENGTH;
	const uint32_t crc = lodepng_crc32(&script_vec[ICCP_CHUNK_NAME_INDEX], iccp_chunk_length);
	const std::size_t crc_index = chunk_data_size + ICCP_CRC_INDEX_DIFF;

	updateValue(script_vec, crc_index, crc, VALUE_LENGTH);

	return script_vec;
}
