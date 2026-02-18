//	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v4.3)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#pragma once

#include "lodepng/lodepng.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <random>
#include <unordered_map>

#ifdef __unix__
	#include <sys/stat.h>
#endif

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

constexpr auto EXTENSION_LIST = std::to_array<std::string_view>({
	"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
	"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
	"xvid", "pdf", "py", "ps1", "sh", "exe"
});

constexpr Byte
	INDEXED_PLTE    = 3,
	TRUECOLOR_RGB   = 2,
	TRUECOLOR_RGBA  = 6;

constexpr std::size_t
	CHUNK_FIELDS_COMBINED_LENGTH = 12,
	MAX_SCRIPT_SIZE              = 1500;

// Shell metacharacters that corrupt bash's parser state when encountered
// in the PNG binary preamble before the extraction script. These either:
//   - Open unterminated quoting contexts (" ' `) causing bash to hang
//   - Cause fatal syntax errors that terminate parsing ( ) )
//   - Create destructive side effects (> redirects to a garbage filename)
//   - Split garbage into unpredictable discrete commands ( ; )
// Other metacharacters (| & $ \ # etc.) merely produce "command not found"
// errors that bash recovers from, allowing execution to reach the script.
constexpr auto LINUX_PROBLEM_METACHARACTERS = std::to_array<Byte>({
	0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60
});

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
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck FileType = FileTypeCheck::archive_file);
void writePolyglotFile(const vBytes& image_vec, bool isZipFile);

// binary_utils.cpp
[[nodiscard]] std::optional<std::size_t> searchSig(std::span<const Byte> data, std::span<const Byte> sig, std::size_t start = 0);
void updateValue(std::span<Byte> data, std::size_t index, std::size_t value, std::size_t length, bool isBigEndian = true);
[[nodiscard]] std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length, bool isBigEndian = true);

// image_processing.cpp
void optimizeImage(vBytes& image_file_vec);

// archive_analysis.cpp
std::string toLowerCase(std::string str);
FileType determineFileType(std::span<const Byte> archive_data, bool isZipFile);
std::string getArchiveFirstFilename(std::span<const Byte> archive_data);

// user_input.cpp
bool hasBalancedQuotes(std::string_view str);
UserArguments promptForArguments(FileType file_type);

// script_builder.cpp
vBytes buildExtractionScript(FileType file_type, const std::string& first_filename, const UserArguments& user_args);

// polyglot_assembly.cpp
void embedChunks(vBytes& image_vec, vBytes script_vec, vBytes archive_vec, std::size_t original_image_size);
