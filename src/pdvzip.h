//	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v4.9)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#pragma once

// ---------------------------------------------------------------------------
// Toolchain floor. pdvzip is distributed as source and built by each user, so
// fail early with a clear message rather than a wall of template errors when
// the compiler is too old. The floor is set by std::print / std::format
// (and other C++23 library features used throughout). std::views::zip, chunk,
// enumerate, pairwise and join_with are not in libc++ 18, so Clang builds must
// use libstdc++ 14+.
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 14
#  error "pdvzip requires GCC >= 14 (for std::print/std::format). Please upgrade your compiler."
#elif defined(__clang__) && __clang_major__ < 18
#  error "pdvzip requires Clang >= 18 with libstdc++ 14+. Please upgrade your compiler."
#endif

#include <array>
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
	JAVASCRIPT,
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

constexpr std::string_view PDVZIP_VERSION = "4.9";

constexpr std::size_t
	CHUNK_FIELDS_COMBINED_LENGTH = 12,
	MAX_SCRIPT_SIZE              = 2000;  // iCCP data; X-Twitter keeps an iCCP chunk only up to 10 KB

constexpr uint32_t
	ZIP_LOCAL_FILE_HEADER_SIGNATURE   = 0x04034B50,
	ZIP_CENTRAL_DIRECTORY_SIGNATURE   = 0x02014B50,
	ZIP_END_CENTRAL_DIRECTORY_SIGNATURE = 0x06054B50,
	ZIP_DATA_DESCRIPTOR_SIGNATURE     = 0x08074B50,
	ZIP64_END_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE = 0x07064B50;

// ZIP record layout shared by archive validation and polyglot assembly.
constexpr std::size_t
	ZIP_EOCD_MIN_SIZE                 = 22,
	ZIP_EOCD_CENTRAL_OFFSET           = 16,
	ZIP_EOCD_COMMENT_LENGTH_OFFSET    = 20,
	ZIP64_LOCATOR_SIZE                = 20,
	ZIP_CENTRAL_RECORD_MIN_SIZE       = 46,
	ZIP_CENTRAL_RECORD_NAME_INDEX     = 46,
	ZIP_CENTRAL_FLAGS_OFFSET          = 8,
	ZIP_CENTRAL_COMPRESSION_METHOD    = 10,
	ZIP_CENTRAL_CRC32                 = 16,
	ZIP_CENTRAL_COMPRESSED_SIZE       = 20,
	ZIP_CENTRAL_UNCOMPRESSED_SIZE     = 24,
	ZIP_CENTRAL_NAME_LENGTH_OFFSET    = 28,
	ZIP_CENTRAL_EXTRA_LENGTH_OFFSET   = 30,
	ZIP_CENTRAL_COMMENT_LENGTH_OFFSET = 32,
	ZIP_CENTRAL_DISK_START            = 34,
	ZIP_CENTRAL_EXTERNAL_ATTRIBUTES   = 38,
	ZIP_CENTRAL_LOCAL_OFFSET          = 42;

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
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck check_type = FileTypeCheck::archive_file);
// Writes the parts back to back into one new output file.
void writePolyglotFile(std::span<const std::span<const Byte>> parts, bool is_zip_file);
inline void writePolyglotFile(std::span<const Byte> image, bool is_zip_file) {
	const std::span<const Byte> parts[] { image };
	writePolyglotFile(parts, is_zip_file);
}

// crc32.cpp
// Continues a zlib-style CRC-32 (start from 0): PCLMULQDQ when available.
// LodePNG's chunk CRCs (lodepng_crc32) use the same code.
[[nodiscard]] std::uint32_t crc32Update(std::uint32_t crc, std::span<const Byte> data) noexcept;

// binary_utils.cpp
// Big-endian 32-bit fields (PNG chunk lengths, dimensions and CRCs). Both
// throw std::out_of_range past the end of data; writeBe32 also throws for a
// value that does not fit in 32 bits.
void writeBe32(std::span<Byte> data, std::size_t offset, std::size_t value);
[[nodiscard]] uint32_t readBe32(std::span<const Byte> data, std::size_t offset);

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

// Length of the well-formed UTF-8 sequence starting at text[i] (no overlong
// forms, surrogates or values past U+10FFFF), or 0 if there is none.
[[nodiscard]] inline std::size_t utf8SequenceLength(std::string_view text, std::size_t i) {
	const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
	const unsigned char lead = byte(i);
	std::size_t length = 0;
	unsigned char second_min = 0x80;
	unsigned char second_max = 0xBF;
	if (lead >= 0xC2 && lead <= 0xDF) {
		length = 2;
	} else if (lead >= 0xE0 && lead <= 0xEF) {
		length = 3;
		if (lead == 0xE0) second_min = 0xA0;  // overlong
		if (lead == 0xED) second_max = 0x9F;  // surrogates
	} else if (lead >= 0xF0 && lead <= 0xF4) {
		length = 4;
		if (lead == 0xF0) second_min = 0x90;  // overlong
		if (lead == 0xF4) second_max = 0x8F;  // past U+10FFFF
	} else {
		return 0;
	}
	if (text.size() - i < length || byte(i + 1) < second_min || byte(i + 1) > second_max) {
		return 0;
	}
	for (std::size_t k = 2; k < length; ++k) {
		if (byte(i + k) < 0x80 || byte(i + k) > 0xBF) return 0;
	}
	return length;
}

// Neutralize terminal controls in untrusted text before it is embedded in a
// diagnostic, so a crafted filename, archive entry name or argv[0] cannot
// inject escape sequences into the user's terminal via stderr: C0 controls
// (ESC 0x1B among them) and DEL; C1 controls U+0080..U+009F written as UTF-8
// (U+009B is CSI and U+009D is OSC, the same as ESC [ and ESC ]); and bytes
// 0x80..0x9F outside a valid UTF-8 sequence, which 8-bit terminals read as C1.
// Other valid UTF-8, and other bytes, are kept so non-ASCII names still display.
[[nodiscard]] inline std::string sanitizeForDisplay(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size();) {
		const auto c = static_cast<unsigned char>(text[i]);
		if (c < 0x80) {
			out.push_back((c < 0x20 || c == 0x7F) ? '?' : text[i]);
			++i;
			continue;
		}
		const std::size_t length = utf8SequenceLength(text, i);
		if (length == 0) {
			out.push_back(c <= 0x9F ? '?' : text[i]);
			++i;
			continue;
		}
		const bool is_c1_control = c == 0xC2 && static_cast<unsigned char>(text[i + 1]) <= 0x9F;
		if (is_c1_control) {
			out.push_back('?');
		} else {
			out.append(text.substr(i, length));
		}
		i += length;
	}
	return out;
}

// sanitizeForDisplay for a whole diagnostic, keeping the line breaks that
// multi-line messages use. Applied where errors are printed, so a message
// that embeds untrusted text without sanitizing it is still safe.
[[nodiscard]] inline std::string sanitizeMessageForDisplay(std::string_view message) {
	std::string out;
	out.reserve(message.size());
	for (std::size_t start = 0;;) {
		const std::size_t end = message.find('\n', start);
		out += sanitizeForDisplay(message.substr(start, end == std::string_view::npos ? end : end - start));
		if (end == std::string_view::npos) break;
		out.push_back('\n');
		start = end + 1;
	}
	return out;
}

[[nodiscard]] inline bool isLinuxProblemMetacharacter(Byte value) {
	// Bytes that can start comments or break bash or CMD parsing, expansion,
	// pipelines, or redirection before execution reaches the embedded extraction
	// script. These bytes must not appear in IHDR width/height/CRC or the iCCP
	// length LSB that bash (Linux) or CMD (Windows) may skim while treating the
	// PNG as a script. (The name predates the CMD cases.)
	switch (value) {
		case 0x0A: // line feed: bash strips NUL then splits the line here, so
		           // an IHDR field or iCCP length byte of 0x0A would move the
		           // bytes after it into command position before the script runs
		case 0x0D: // carriage return: the Windows stub puts its REM after a bare
		           // CR; refused in case CMD breaks the header line at one, which
		           // would move the bytes after it into command position
		case 0x1A: // Ctrl-Z: CMD reads it as a line feed in a batch file
		case 0x22: // "
		case 0x23: // #
		case 0x24: // $
		case 0x26: // &
		case 0x27: // '
		case 0x28: // (
		case 0x29: // )
		case 0x3B: // ;
		case 0x3C: // <
		case 0x3E: // >
		case 0x60: // `
		case 0x7C: // |
			return true;
		default:
			return false;
	}
}

// Shared ZIP central-directory record size (fixed header + variable fields).
[[nodiscard]] std::size_t zipCentralDirectoryRecordSize(
	std::size_t name_length,
	std::size_t extra_length,
	std::size_t comment_length);

struct ZipCentralDirectory {
	std::size_t eocd_index;
	uint16_t eocd_comment_length;
	uint16_t total_records;
	std::size_t start;  // first central record; offsets are positions in data
	std::size_t end;    // == eocd_index
};

// Finds the EOCD of the ZIP data in data[zip_begin, zip_end) and validates it:
// one disk, no ZIP64 markers, at least one record, and a central directory
// that ends exactly at the EOCD. Error messages start with error_prefix. A
// zip_end past the data (as an unsigned size - trailer underflow gives) is
// reported as too small.
[[nodiscard]] ZipCentralDirectory readZipCentralDirectory(
	std::span<const Byte> data,
	std::size_t zip_begin,
	std::size_t zip_end,
	std::string_view error_prefix);

// image_processing.cpp
void optimizeImage(vBytes& image_file_vec);

// archive_analysis.cpp
struct ArchiveMetadata {
	FileType file_type;
	std::string first_filename;
};

// Fully validates referenced ZIP entries and returns classification metadata.
// The sum of entry-name lengths, excluding trailing directory slashes, is
// limited to 1 MiB to bound path-validation memory and processing work.
ArchiveMetadata analyzeArchive(std::span<const Byte> archive_data, bool is_zip_file);
// Validation-only compatibility wrapper for focused callers/tests.
void validateArchiveEntryPaths(std::span<const Byte> archive_data);

// user_input.cpp
UserArguments promptForArguments(FileType file_type);

// script_builder.cpp
vBytes buildExtractionScript(
	FileType file_type,
	const std::string& first_filename,
	const UserArguments& user_args);

// polyglot_assembly.cpp
// The archive stays in its own buffer so the output never needs a second
// archive-sized copy; parts() lists the file contents in write order.
struct PolyglotImage {
	vBytes image;          // optimized PNG plus the iCCP script chunk, ending in IEND
	vBytes archive_chunk;  // final IDAT chunk, placed just before IEND

	[[nodiscard]] std::array<std::span<const Byte>, 3> parts() const;
};

[[nodiscard]] PolyglotImage embedChunks(vBytes image_vec, vBytes script_vec, vBytes archive_vec);
