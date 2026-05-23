//	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v4.5)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

using Byte   = std::uint8_t;
using vBytes = std::vector<Byte>;

using std::operator""sv;

enum class FileTypeCheck : Byte {
	cover_image  = 1,
	archive_file = 2
};

enum class FileType : std::size_t {
	VIDEO_AUDIO = 29,
	PDF,
	PYTHON,
	POWERSHELL,
	BASH_SHELL,
	WINDOWS_EXECUTABLE,
	UNKNOWN_FILE_TYPE,
	FOLDER,
	LINUX_EXECUTABLE,
	JAR
};

constexpr Byte
	INDEXED_PLTE    = 3,
	TRUECOLOR_RGB   = 2,
	TRUECOLOR_RGBA  = 6;

constexpr std::size_t
	CHUNK_FIELDS_COMBINED_LENGTH = 12,
	MAX_SCRIPT_SIZE              = 1500;

constexpr uint32_t
	ZIP_LOCAL_FILE_HEADER_SIGNATURE   = 0x04034B50,
	ZIP_CENTRAL_DIRECTORY_SIGNATURE   = 0x02014B50,
	ZIP_END_CENTRAL_DIRECTORY_SIGNATURE = 0x06054B50,
	ZIP_DATA_DESCRIPTOR_SIGNATURE     = 0x08074B50;

struct UserArguments {
	std::string linux_args;
	std::string windows_args;
};

struct ProgramArgs {
	std::optional<std::string> image_file_path;
	std::optional<std::string> archive_file_path;
	bool info_mode = false;

	static ProgramArgs parse(int argc, char** argv);
};

// display_info.cpp
void displayInfo();

// file_io.cpp
[[nodiscard]] bool hasValidFilename(const fs::path& p);
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck check_type = FileTypeCheck::archive_file);
void writePolyglotFile(const vBytes& image_vec, bool is_zip_file);

// binary_utils.cpp
void writeValueAt(
	std::span<Byte> data,
	std::size_t offset,
	std::size_t value,
	std::size_t length);

[[nodiscard]] std::size_t readValueAt(
	std::span<const Byte> data,
	std::size_t offset,
	std::size_t length);

[[nodiscard]] std::size_t checkedAdd(std::size_t lhs, std::size_t rhs, std::string_view context);
[[nodiscard]] std::size_t checkedMultiply(std::size_t lhs, std::size_t rhs, std::string_view context);

namespace binary_utils_detail {
	[[noreturn]] void throwOutOfRange(std::string_view fn_name);
}

[[nodiscard]] inline uint16_t readLe16(std::span<const Byte> data, std::size_t offset) {
	if (offset > data.size() || data.size() - offset < 2) {
		binary_utils_detail::throwOutOfRange("readLe16");
	}
	return static_cast<uint16_t>(
		  static_cast<uint16_t>(data[offset])
		| (static_cast<uint16_t>(data[offset + 1]) << 8));
}

[[nodiscard]] inline uint32_t readLe32(std::span<const Byte> data, std::size_t offset) {
	if (offset > data.size() || data.size() - offset < 4) {
		binary_utils_detail::throwOutOfRange("readLe32");
	}
	return  static_cast<uint32_t>(data[offset])
	     | (static_cast<uint32_t>(data[offset + 1]) << 8)
	     | (static_cast<uint32_t>(data[offset + 2]) << 16)
	     | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

inline void writeLe16(std::span<Byte> data, std::size_t offset, uint16_t value) {
	if (offset > data.size() || data.size() - offset < 2) {
		binary_utils_detail::throwOutOfRange("writeLe16");
	}
	data[offset]     = static_cast<Byte>(value & 0xFF);
	data[offset + 1] = static_cast<Byte>((value >> 8) & 0xFF);
}

inline void writeLe32(std::span<Byte> data, std::size_t offset, uint32_t value) {
	if (offset > data.size() || data.size() - offset < 4) {
		binary_utils_detail::throwOutOfRange("writeLe32");
	}
	data[offset]     = static_cast<Byte>(value & 0xFF);
	data[offset + 1] = static_cast<Byte>((value >> 8) & 0xFF);
	data[offset + 2] = static_cast<Byte>((value >> 16) & 0xFF);
	data[offset + 3] = static_cast<Byte>((value >> 24) & 0xFF);
}

[[nodiscard]] inline bool hasLe32Signature(std::span<const Byte> data, std::size_t offset, uint32_t signature) {
	return offset <= data.size() && data.size() - offset >= 4 && readLe32(data, offset) == signature;
}

[[nodiscard]] inline bool isLinuxProblemMetacharacter(Byte value) {
	// Metacharacters that can break bash parsing or redirect before execution
	// reaches the embedded extraction script.
	switch (value) {
		case 0x22: // "
		case 0x27: // '
		case 0x28: // (
		case 0x29: // )
		case 0x3B: // ;
		case 0x3E: // >
		case 0x60: // `
			return true;
		default:
			return false;
	}
}

struct ZipEocdLocator {
	std::size_t index;
	uint16_t comment_length;
};

[[nodiscard]] std::optional<ZipEocdLocator> findZipEocdLocator(
	std::span<const Byte> data,
	std::size_t archive_begin,
	std::size_t archive_end);

// image_processing.cpp
void optimizeImage(vBytes& image_file_vec);

// archive_analysis.cpp
struct ArchiveMetadata {
	FileType file_type;
	std::string first_filename;
};

ArchiveMetadata analyzeArchive(std::span<const Byte> archive_data, bool is_zip_file);
void validateArchiveEntryPaths(std::span<const Byte> archive_data);

// user_input.cpp
UserArguments promptForArguments(FileType file_type);

// script_builder.cpp
vBytes buildExtractionScript(
	FileType file_type,
	const std::string& first_filename,
	const UserArguments& user_args);

// polyglot_assembly.cpp
void embedChunks(vBytes& image_vec, vBytes script_vec, vBytes archive_vec, std::size_t original_image_size);
