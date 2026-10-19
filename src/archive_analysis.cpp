#include "pdvzip.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <zlib.h>

namespace {

constexpr auto EXTENSION_LIST = std::to_array<std::string_view>({
	"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
	"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
	"xvid", "pdf", "py", "js", "ps1", "sh", "exe"
});

// The extension->FileType mapping in analyzeArchive() is positional: an entry at
// index i resolves to FileType(max(i, VIDEO_AUDIO)). That only stays correct if
// the distinct (non-VIDEO_AUDIO) types sit at exactly their enum-value indices
// and nothing follows "exe". These asserts fail the build if the list is ever
// reordered, extended past WINDOWS_EXECUTABLE, or drifts from the enum.
static_assert(EXTENSION_LIST.size() == std::to_underlying(FileType::WINDOWS_EXECUTABLE) + 1,
	"EXTENSION_LIST must end at the WINDOWS_EXECUTABLE entry; trailing entries would be misclassified.");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::PDF)] == "pdf");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::PYTHON)] == "py");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::JAVASCRIPT)] == "js");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::POWERSHELL)] == "ps1");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::BASH_SHELL)] == "sh");
static_assert(EXTENSION_LIST[std::to_underlying(FileType::WINDOWS_EXECUTABLE)] == "exe");

template <typename T>
[[nodiscard]] T readZipField(std::span<const Byte> data, std::size_t offset, std::string_view context) {
	static_assert(sizeof(T) == 2 || sizeof(T) == 4);
	try {
		if constexpr (sizeof(T) == 2) {
			return static_cast<T>(readLe16(data, offset));
		} else {
			return static_cast<T>(readLe32(data, offset));
		}
	}
	catch (const std::exception&) {
		throw std::runtime_error(std::format("{}: Truncated ZIP record.", context));
	}
}

[[nodiscard]] bool containsControlCharacters(std::string_view value) {
	return std::ranges::any_of(value, [](unsigned char c) static {
		return std::iscntrl(c) != 0;
	});
}

[[nodiscard]] char toLowerAscii(char ch) {
	if (ch >= 'A' && ch <= 'Z') {
		return static_cast<char>(ch - 'A' + 'a');
	}
	return ch;
}

[[nodiscard]] bool hasNonAsciiByte(std::string_view value) {
	return std::ranges::any_of(value, [](unsigned char c) static { return c >= 0x80; });
}

// Decode one well-formed UTF-8 sequence at value[pos], advancing pos. Rejects
// overlong forms, surrogates and code points above U+10FFFF.
[[nodiscard]] std::optional<char32_t> decodeUtf8(std::string_view value, std::size_t& pos) {
	const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(value[i]); };
	const unsigned char lead = byte(pos);
	std::size_t length = 0;
	char32_t cp = 0, min_cp = 0;
	if (lead < 0x80) {
		++pos;
		return lead;
	} else if (lead >= 0xC2 && lead <= 0xDF) {
		length = 2; cp = lead & 0x1F; min_cp = 0x80;
	} else if (lead >= 0xE0 && lead <= 0xEF) {
		length = 3; cp = lead & 0x0F; min_cp = 0x800;
	} else if (lead >= 0xF0 && lead <= 0xF4) {
		length = 4; cp = lead & 0x07; min_cp = 0x10000;
	} else {
		return std::nullopt;
	}
	if (length > value.size() - pos) {
		return std::nullopt;
	}
	for (std::size_t i = 1; i < length; ++i) {
		if ((byte(pos + i) & 0xC0) != 0x80) {
			return std::nullopt;
		}
		cp = (cp << 6) | (byte(pos + i) & 0x3F);
	}
	if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		return std::nullopt;
	}
	pos += length;
	return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

// Non-ASCII names must be flagged UTF-8 and well formed. Unflagged names use a
// legacy code page that extractors decode differently (Info-ZIP converts DOS
// CP437 names; Windows uses the OEM code page), so they are not portable.
void validateEntryNameEncoding(std::string_view name, uint16_t flags, std::size_t entry_number) {
	constexpr uint16_t GENERAL_PURPOSE_UTF8 = 1u << 11;
	if (!hasNonAsciiByte(name)) {
		return;
	}
	if ((flags & GENERAL_PURPOSE_UTF8) == 0) {
		throw std::runtime_error(std::format(
			"Archive File Error: Entry {} has a non-ASCII name without the ZIP UTF-8 flag. "
			"Legacy filename encodings are not supported; re-create the archive with UTF-8 filenames.",
			entry_number));
	}
	for (std::size_t pos = 0; pos < name.size();) {
		const auto cp = decodeUtf8(name, pos);
		if (!cp) {
			throw std::runtime_error(std::format(
				"Archive File Error: Entry {} name is not valid UTF-8.", entry_number));
		}
		if (*cp >= 0x80 && *cp <= 0x9F) {
			throw std::runtime_error(std::format(
				"Archive File Error: Entry {} contains unsupported control characters.", entry_number));
		}
	}
}

// Simple uppercase mappings for the BMP (generated from Unicode 15.1 data):
// code points first..last, every stride-th one, map to cp + delta.
// Regenerate or verify with tools/generate_upcase_table.py; do not edit by hand.
struct UpcaseRange {
	uint16_t first;
	uint16_t last;
	int32_t delta;
	uint8_t stride;
};

constexpr UpcaseRange BMP_UPCASE_RANGES[] = {
	{0x00B5, 0x00B5, 743, 1},
	{0x00E0, 0x00F6, -32, 1},
	{0x00F8, 0x00FE, -32, 1},
	{0x00FF, 0x00FF, 121, 1},
	{0x0101, 0x012F, -1, 2},
	{0x0131, 0x0131, -232, 1},
	{0x0133, 0x0137, -1, 2},
	{0x013A, 0x0148, -1, 2},
	{0x014B, 0x0177, -1, 2},
	{0x017A, 0x017E, -1, 2},
	{0x017F, 0x017F, -300, 1},
	{0x0180, 0x0180, 195, 1},
	{0x0183, 0x0185, -1, 2},
	{0x0188, 0x0188, -1, 1},
	{0x018C, 0x018C, -1, 1},
	{0x0192, 0x0192, -1, 1},
	{0x0195, 0x0195, 97, 1},
	{0x0199, 0x0199, -1, 1},
	{0x019A, 0x019A, 163, 1},
	{0x019E, 0x019E, 130, 1},
	{0x01A1, 0x01A5, -1, 2},
	{0x01A8, 0x01A8, -1, 1},
	{0x01AD, 0x01AD, -1, 1},
	{0x01B0, 0x01B0, -1, 1},
	{0x01B4, 0x01B6, -1, 2},
	{0x01B9, 0x01B9, -1, 1},
	{0x01BD, 0x01BD, -1, 1},
	{0x01BF, 0x01BF, 56, 1},
	{0x01C5, 0x01C5, -1, 1},
	{0x01C6, 0x01C6, -2, 1},
	{0x01C8, 0x01C8, -1, 1},
	{0x01C9, 0x01C9, -2, 1},
	{0x01CB, 0x01CB, -1, 1},
	{0x01CC, 0x01CC, -2, 1},
	{0x01CE, 0x01DC, -1, 2},
	{0x01DD, 0x01DD, -79, 1},
	{0x01DF, 0x01EF, -1, 2},
	{0x01F2, 0x01F2, -1, 1},
	{0x01F3, 0x01F3, -2, 1},
	{0x01F5, 0x01F5, -1, 1},
	{0x01F9, 0x021F, -1, 2},
	{0x0223, 0x0233, -1, 2},
	{0x023C, 0x023C, -1, 1},
	{0x023F, 0x0240, 10815, 1},
	{0x0242, 0x0242, -1, 1},
	{0x0247, 0x024F, -1, 2},
	{0x0250, 0x0250, 10783, 1},
	{0x0251, 0x0251, 10780, 1},
	{0x0252, 0x0252, 10782, 1},
	{0x0253, 0x0253, -210, 1},
	{0x0254, 0x0254, -206, 1},
	{0x0256, 0x0257, -205, 1},
	{0x0259, 0x0259, -202, 1},
	{0x025B, 0x025B, -203, 1},
	{0x025C, 0x025C, 42319, 1},
	{0x0260, 0x0260, -205, 1},
	{0x0261, 0x0261, 42315, 1},
	{0x0263, 0x0263, -207, 1},
	{0x0265, 0x0265, 42280, 1},
	{0x0266, 0x0266, 42308, 1},
	{0x0268, 0x0268, -209, 1},
	{0x0269, 0x0269, -211, 1},
	{0x026A, 0x026A, 42308, 1},
	{0x026B, 0x026B, 10743, 1},
	{0x026C, 0x026C, 42305, 1},
	{0x026F, 0x026F, -211, 1},
	{0x0271, 0x0271, 10749, 1},
	{0x0272, 0x0272, -213, 1},
	{0x0275, 0x0275, -214, 1},
	{0x027D, 0x027D, 10727, 1},
	{0x0280, 0x0280, -218, 1},
	{0x0282, 0x0282, 42307, 1},
	{0x0283, 0x0283, -218, 1},
	{0x0287, 0x0287, 42282, 1},
	{0x0288, 0x0288, -218, 1},
	{0x0289, 0x0289, -69, 1},
	{0x028A, 0x028B, -217, 1},
	{0x028C, 0x028C, -71, 1},
	{0x0292, 0x0292, -219, 1},
	{0x029D, 0x029D, 42261, 1},
	{0x029E, 0x029E, 42258, 1},
	{0x0345, 0x0345, 84, 1},
	{0x0371, 0x0373, -1, 2},
	{0x0377, 0x0377, -1, 1},
	{0x037B, 0x037D, 130, 1},
	{0x03AC, 0x03AC, -38, 1},
	{0x03AD, 0x03AF, -37, 1},
	{0x03B1, 0x03C1, -32, 1},
	{0x03C2, 0x03C2, -31, 1},
	{0x03C3, 0x03CB, -32, 1},
	{0x03CC, 0x03CC, -64, 1},
	{0x03CD, 0x03CE, -63, 1},
	{0x03D0, 0x03D0, -62, 1},
	{0x03D1, 0x03D1, -57, 1},
	{0x03D5, 0x03D5, -47, 1},
	{0x03D6, 0x03D6, -54, 1},
	{0x03D7, 0x03D7, -8, 1},
	{0x03D9, 0x03EF, -1, 2},
	{0x03F0, 0x03F0, -86, 1},
	{0x03F1, 0x03F1, -80, 1},
	{0x03F2, 0x03F2, 7, 1},
	{0x03F3, 0x03F3, -116, 1},
	{0x03F5, 0x03F5, -96, 1},
	{0x03F8, 0x03F8, -1, 1},
	{0x03FB, 0x03FB, -1, 1},
	{0x0430, 0x044F, -32, 1},
	{0x0450, 0x045F, -80, 1},
	{0x0461, 0x0481, -1, 2},
	{0x048B, 0x04BF, -1, 2},
	{0x04C2, 0x04CE, -1, 2},
	{0x04CF, 0x04CF, -15, 1},
	{0x04D1, 0x052F, -1, 2},
	{0x0561, 0x0586, -48, 1},
	{0x10D0, 0x10FA, 3008, 1},
	{0x10FD, 0x10FF, 3008, 1},
	{0x13F8, 0x13FD, -8, 1},
	{0x1C80, 0x1C80, -6254, 1},
	{0x1C81, 0x1C81, -6253, 1},
	{0x1C82, 0x1C82, -6244, 1},
	{0x1C83, 0x1C84, -6242, 1},
	{0x1C85, 0x1C85, -6243, 1},
	{0x1C86, 0x1C86, -6236, 1},
	{0x1C87, 0x1C87, -6181, 1},
	{0x1C88, 0x1C88, 35266, 1},
	{0x1D79, 0x1D79, 35332, 1},
	{0x1D7D, 0x1D7D, 3814, 1},
	{0x1D8E, 0x1D8E, 35384, 1},
	{0x1E01, 0x1E95, -1, 2},
	{0x1E9B, 0x1E9B, -59, 1},
	{0x1EA1, 0x1EFF, -1, 2},
	{0x1F00, 0x1F07, 8, 1},
	{0x1F10, 0x1F15, 8, 1},
	{0x1F20, 0x1F27, 8, 1},
	{0x1F30, 0x1F37, 8, 1},
	{0x1F40, 0x1F45, 8, 1},
	{0x1F51, 0x1F57, 8, 2},
	{0x1F60, 0x1F67, 8, 1},
	{0x1F70, 0x1F71, 74, 1},
	{0x1F72, 0x1F75, 86, 1},
	{0x1F76, 0x1F77, 100, 1},
	{0x1F78, 0x1F79, 128, 1},
	{0x1F7A, 0x1F7B, 112, 1},
	{0x1F7C, 0x1F7D, 126, 1},
	{0x1F80, 0x1F87, 8, 1},
	{0x1F90, 0x1F97, 8, 1},
	{0x1FA0, 0x1FA7, 8, 1},
	{0x1FB0, 0x1FB1, 8, 1},
	{0x1FB3, 0x1FB3, 9, 1},
	{0x1FBE, 0x1FBE, -7205, 1},
	{0x1FC3, 0x1FC3, 9, 1},
	{0x1FD0, 0x1FD1, 8, 1},
	{0x1FE0, 0x1FE1, 8, 1},
	{0x1FE5, 0x1FE5, 7, 1},
	{0x1FF3, 0x1FF3, 9, 1},
	{0x214E, 0x214E, -28, 1},
	{0x2170, 0x217F, -16, 1},
	{0x2184, 0x2184, -1, 1},
	{0x24D0, 0x24E9, -26, 1},
	{0x2C30, 0x2C5F, -48, 1},
	{0x2C61, 0x2C61, -1, 1},
	{0x2C65, 0x2C65, -10795, 1},
	{0x2C66, 0x2C66, -10792, 1},
	{0x2C68, 0x2C6C, -1, 2},
	{0x2C73, 0x2C73, -1, 1},
	{0x2C76, 0x2C76, -1, 1},
	{0x2C81, 0x2CE3, -1, 2},
	{0x2CEC, 0x2CEE, -1, 2},
	{0x2CF3, 0x2CF3, -1, 1},
	{0x2D00, 0x2D25, -7264, 1},
	{0x2D27, 0x2D27, -7264, 1},
	{0x2D2D, 0x2D2D, -7264, 1},
	{0xA641, 0xA66D, -1, 2},
	{0xA681, 0xA69B, -1, 2},
	{0xA723, 0xA72F, -1, 2},
	{0xA733, 0xA76F, -1, 2},
	{0xA77A, 0xA77C, -1, 2},
	{0xA77F, 0xA787, -1, 2},
	{0xA78C, 0xA78C, -1, 1},
	{0xA791, 0xA793, -1, 2},
	{0xA794, 0xA794, 48, 1},
	{0xA797, 0xA7A9, -1, 2},
	{0xA7B5, 0xA7C3, -1, 2},
	{0xA7C8, 0xA7CA, -1, 2},
	{0xA7D1, 0xA7D1, -1, 1},
	{0xA7D7, 0xA7D9, -1, 2},
	{0xA7F6, 0xA7F6, -1, 1},
	{0xAB53, 0xAB53, -928, 1},
	{0xAB70, 0xABBF, -38864, 1},
	{0xFF41, 0xFF5A, -32, 1},
};

// Folds a code point for the case-insensitive collision check. Each UTF-16 code
// unit is upcased on its own, like the NTFS/exFAT comparison, so supplementary
// characters pass through unchanged. This uses Unicode simple-uppercase, which
// was verified on Windows (RtlUpcaseUnicodeChar over the whole BMP) to be a
// strict superset of the NTFS upcase table: every pair NTFS treats as equal is
// folded here too, so no colliding pair is ever accepted. It folds slightly
// more than NTFS (e.g. U+00B5, the Dz/Lj/Nj titlecase digraphs, final sigma,
// some Greek symbol variants), which can only reject a name NTFS keeps distinct,
// never admit a collision. Matching NTFS exactly is deliberately not attempted.
[[nodiscard]] char32_t upcaseForWindowsFilename(char32_t cp) {
	if (cp < 0x80) {
		return (cp >= 'a' && cp <= 'z') ? cp - 'a' + 'A' : cp;
	}
	if (cp > 0xFFFF) {
		return cp;
	}
	const auto range = std::ranges::upper_bound(BMP_UPCASE_RANGES, cp, {}, &UpcaseRange::first);
	if (range == std::ranges::begin(BMP_UPCASE_RANGES)) {
		return cp;
	}
	const UpcaseRange& r = *std::ranges::prev(range);
	if (cp > r.last || (cp - r.first) % r.stride != 0) {
		return cp;
	}
	return static_cast<char32_t>(static_cast<int32_t>(cp) + r.delta);
}

// Fold a validated entry name into the key used for collision checks.
void foldNameForCollisionCheck(std::string_view name, std::string& out) {
	out.clear();
	for (std::size_t pos = 0; pos < name.size();) {
		const auto cp = decodeUtf8(name, pos);
		if (!cp) {
			throw std::runtime_error("Archive File Error: Entry name is not valid UTF-8.");
		}
		appendUtf8(out, upcaseForWindowsFilename(*cp));
	}
}

[[nodiscard]] std::string_view readZipStringView(std::span<const Byte> data, std::size_t start, std::size_t length,
                                                 std::string_view overflow_error, std::string_view bounds_error) {
	const std::size_t end = checkedAdd(start, length, overflow_error);
	if (end > data.size()) {
		throw std::runtime_error(std::string(bounds_error));
	}
	return std::string_view(reinterpret_cast<const char*>(data.data() + start), length);
}

[[nodiscard]] bool hasWindowsReservedSegmentName(std::string_view segment) {
	const std::size_t dot_pos = segment.find('.');
	const std::string_view stem = segment.substr(0, dot_pos);

	auto equalsReservedName = [](std::string_view lhs, std::string_view rhs) static {
		return std::ranges::equal(lhs, rhs, [](char a, char b) static {
			return toLowerAscii(a) == b;
		});
	};

	if (equalsReservedName(stem, "con"sv)
		|| equalsReservedName(stem, "prn"sv)
		|| equalsReservedName(stem, "aux"sv)
		|| equalsReservedName(stem, "nul"sv)
		|| equalsReservedName(stem, "conin$"sv)
		|| equalsReservedName(stem, "conout$"sv)
		|| equalsReservedName(stem, "clock$"sv)) {
		return true;
	}
	if (stem.size() < 4
		|| !(equalsReservedName(stem.substr(0, 3), "com"sv) || equalsReservedName(stem.substr(0, 3), "lpt"sv))) {
		return false;
	}
	// Windows also reserves COM/LPT followed by superscript 1-3 (UTF-8 encoded).
	const std::string_view suffix = stem.substr(3);
	return (suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9')
		|| suffix == "\xC2\xB9"sv || suffix == "\xC2\xB2"sv || suffix == "\xC2\xB3"sv;
}

[[nodiscard]] bool hasWindowsInvalidPathCharacter(std::string_view segment) {
	return std::ranges::any_of(segment, [](char ch) static {
		return ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*';
	});
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path);

struct LocalEntrySpan {
	std::size_t begin;
	std::size_t end;
};

// A local entry's compressed payload, recorded during the metadata/overlap pass
// and inflated only after every span has been checked for overlap. Decompressing
// eagerly would let nested, overlapping local headers inflate the same bytes many
// times before the archive is rejected (empty DEFLATE blocks evade the output
// cap), so verification is deferred until overlaps are ruled out.
struct PendingPayload {
	uint16_t compression_method;
	std::span<const Byte> compressed;
	uint32_t uncompressed_size;
	uint32_t crc32;
	std::size_t entry_number;
};

constexpr std::size_t
	ZIP_WRAP_PREFIX_SIZE          = 8,
	ZIP_WRAP_TRAILER_SIZE         = 4,
	LOCAL_RECORD_MIN_SIZE         = 30,
	LOCAL_RECORD_NAME_INDEX       = 30,
	CENTRAL_RECORD_MIN_SIZE       = 46,
	CENTRAL_RECORD_NAME_INDEX     = 46,
	CENTRAL_FLAGS_OFFSET          = 8,
	CENTRAL_COMPRESSION_METHOD    = 10,
	CENTRAL_CRC32                 = 16,
	CENTRAL_COMPRESSED_SIZE       = 20,
	CENTRAL_UNCOMPRESSED_SIZE     = 24,
	CENTRAL_NAME_LENGTH_OFFSET    = 28,
	CENTRAL_EXTRA_LENGTH_OFFSET   = 30,
	CENTRAL_COMMENT_LENGTH_OFFSET = 32,
	CENTRAL_DISK_START            = 34,
	CENTRAL_EXTERNAL_ATTRIBUTES   = 38,
	CENTRAL_LOCAL_OFFSET          = 42;

constexpr std::uint64_t MAX_TOTAL_UNCOMPRESSED_SIZE = 2ULL * 1024 * 1024 * 1024;

constexpr uint16_t
	ZIP_EXTRA_ZIP64                    = 0x0001,
	ZIP_EXTRA_EXTENDED_LANGUAGE        = 0x0008,
	ZIP_EXTRA_INFOZIP_UNICODE_PATH     = 0x7075,
	ZIP_EXTRA_ASI_UNIX                 = 0x756e,
	ZIP_EXTRA_LIBARCHIVE_XL            = 0x6c78;

struct CentralDirectoryBounds {
	std::size_t start;
	std::size_t end;
	uint16_t total_records;
};

struct CentralEntryMetadata {
	std::size_t entry_number;
	uint16_t flags;
	uint16_t compression_method;
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint16_t disk_start;
	uint32_t external_attributes;
	std::size_t local_header_offset;
	std::string_view name;
	std::span<const Byte> extra;
	std::size_t record_size;
};

class PortablePathTrie {
private:
	using NodeIndex = uint32_t;
	static constexpr NodeIndex NO_NODE = std::numeric_limits<NodeIndex>::max();
	// Each normalized path byte can add a node. Bound aggregate processing even
	// when paths share prefixes, and cap capacity so vector growth cannot exceed
	// that bound. With 12-byte nodes, storage is at most 12 MiB plus the root;
	// the old and new allocations can coexist temporarily during growth.
	static constexpr std::size_t MAX_TOTAL_PATH_BYTES = 1024 * 1024;
	static constexpr std::size_t MAX_NODES = MAX_TOTAL_PATH_BYTES + 1;
	static_assert(MAX_NODES < NO_NODE);

	struct Node {
		NodeIndex first_child = NO_NODE;
		NodeIndex next_sibling = NO_NODE;
		Byte label = 0;
		bool has_explicit_entry = false;
		bool is_file = false;
		bool is_directory = false;
	};

	std::vector<Node> nodes_{1}; // index zero is the root
	std::size_t total_path_bytes_ = 0;
	std::string folded_; // reused buffer for the normalized key

	[[nodiscard]] NodeIndex findOrAddChild(NodeIndex parent, Byte label) {
		for (NodeIndex child = nodes_[parent].first_child;
			 child != NO_NODE;
			 child = nodes_[child].next_sibling) {
			if (nodes_[child].label == label) {
				return child;
			}
		}

		const auto child = static_cast<NodeIndex>(nodes_.size());
		if (child == nodes_.capacity()) {
			if (child == MAX_NODES) {
				throw std::runtime_error("Archive Security Error: Archive path trie exceeds its safety limit.");
			}
			// Reserve explicitly: push_back's geometric growth could otherwise
			// allocate beyond MAX_NODES even when the new size fits the limit.
			const std::size_t growth = std::min(nodes_.capacity(), MAX_NODES - nodes_.capacity());
			nodes_.reserve(nodes_.capacity() + growth);
		}
		nodes_.push_back(Node{
			.next_sibling = nodes_[parent].first_child,
			.label = label
		});
		nodes_[parent].first_child = child;
		return child;
	}

public:
	void reserve(std::size_t entry_count) {
		// This is only a small starting reservation. Nodes are added once per
		// distinct normalized path byte, rather than once per full path prefix.
		nodes_.reserve(std::min(entry_count, MAX_TOTAL_PATH_BYTES) + 1);
	}

	// entry_name must already have passed validateEntryNameEncoding().
	void insert(std::string_view entry_name, std::size_t entry_number) {
		const bool directory_entry = entry_name.ends_with('/');
		if (entry_name.size() == static_cast<std::size_t>(directory_entry)) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Empty normalized path for archive entry {}.", entry_number));
		}
		// Compare names the way case-insensitive Windows filesystems do. Folding
		// can change the UTF-8 length, so charge the folded key. '/' is ASCII and
		// never appears inside a multi-byte sequence, so it survives unchanged.
		foldNameForCollisionCheck(entry_name.substr(0, entry_name.size() - directory_entry), folded_);
		const std::size_t key_size = folded_.size();
		// Charge before traversing or allocating any nodes for this entry.
		if (key_size > MAX_TOTAL_PATH_BYTES - total_path_bytes_) {
			throw std::runtime_error(
				"Archive Security Error: Total archive path length exceeds the 1 MiB safety limit.");
		}
		total_path_bytes_ += key_size;

		NodeIndex node = 0;
		for (std::size_t i = 0; i < key_size; ++i) {
			const char ch = folded_[i];
			if (ch == '/') {
				if (nodes_[node].is_file) {
					throw std::runtime_error(std::format(
						"Archive Security Error: Archive entry {} conflicts with an existing file path.",
						entry_number));
				}
				nodes_[node].is_directory = true;
			}

			node = findOrAddChild(node, static_cast<Byte>(ch));
		}

		Node& terminal = nodes_[node];
		if (terminal.has_explicit_entry) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Duplicate or case-conflicting archive entry path detected: \"{}\".",
				entry_name));
		}

		if (directory_entry) {
			if (terminal.is_file) {
				throw std::runtime_error(std::format(
					"Archive Security Error: Directory entry {} conflicts with an existing file path.",
					entry_number));
			}
			terminal.is_directory = true;
		} else {
			if (terminal.is_directory) {
				throw std::runtime_error(std::format(
					"Archive Security Error: File entry {} conflicts with an existing directory path.",
					entry_number));
			}
			terminal.is_file = true;
		}
		terminal.has_explicit_entry = true;
	}
};

struct ArchiveEntryTracking {
	std::uint64_t total_declared_uncompressed = 0;
	std::uint64_t total_verified_uncompressed = 0;
	PortablePathTrie paths;
	std::vector<LocalEntrySpan> local_spans;
	std::vector<PendingPayload> pending_payloads;

	void reserve(std::size_t total_records) {
		paths.reserve(total_records);
		local_spans.reserve(total_records);
		pending_payloads.reserve(total_records);
	}
};

void validateZipExtraFields(std::span<const Byte> extra, std::string_view location,
							std::optional<std::size_t> entry_number = std::nullopt) {
	std::size_t cursor = 0;
	const auto malformedExtraField = [&]() -> std::runtime_error {
		return std::runtime_error(entry_number
			? std::format("Archive File Error: Malformed {} extra field on entry {}.", location, *entry_number)
			: std::format("Archive File Error: Malformed {} extra field.", location));
	};
	while (cursor < extra.size()) {
		if (extra.size() - cursor < 4) {
			throw malformedExtraField();
		}

		const uint16_t field_id = readLe16(extra, cursor);
		const std::size_t field_size = readLe16(extra, cursor + 2);
		cursor += 4;
		if (field_size > extra.size() - cursor) {
			throw malformedExtraField();
		}

		if (field_id == ZIP_EXTRA_ZIP64) {
			throw std::runtime_error(entry_number
				? std::format("Archive File Error: ZIP64 extra field is not supported on entry {}.", *entry_number)
				: "Archive File Error: ZIP64 extra field is not supported.");
		}
		if (field_id == ZIP_EXTRA_INFOZIP_UNICODE_PATH
			|| field_id == ZIP_EXTRA_EXTENDED_LANGUAGE) {
			throw std::runtime_error(entry_number
				? std::format(
					"Archive Security Error: Path-override extra field 0x{:04x} is not supported on entry {}.",
					field_id, *entry_number)
				: std::format(
					"Archive Security Error: Path-override extra field 0x{:04x} is not supported.",
					field_id));
		}
		// Info-ZIP can obtain a replacement mode from ASi Unix metadata, and
		// libarchive's xl field can override both creator host and mode. Neither
		// may change an entry's validated file type during later extraction.
		if (field_id == ZIP_EXTRA_ASI_UNIX || field_id == ZIP_EXTRA_LIBARCHIVE_XL) {
			throw std::runtime_error(entry_number
				? std::format(
					"Archive Security Error: File-type override extra field 0x{:04x} is not supported in {} on entry {}.",
					field_id, location, *entry_number)
				: std::format(
					"Archive Security Error: File-type override extra field 0x{:04x} is not supported in {}.",
					field_id, location));
		}

		cursor += field_size;
	}
}

void validateEntryName(std::string_view entry_name, std::string_view control_label, std::string_view unsafe_error,
                       std::optional<std::size_t> entry_number = std::nullopt) {
	if (containsControlCharacters(entry_name)) {
		throw std::runtime_error(entry_number
			? std::format("{} {} contains unsupported control characters.", control_label, *entry_number)
			: std::string(control_label));
	}
	if (isUnsafeEntryPath(entry_name)) {
		throw std::runtime_error(std::format("{}: \"{}\".", unsafe_error, entry_name));
	}
}

void validateEntryAttributes(uint16_t flags, uint32_t external_attributes,
                             std::string_view entry_name, std::size_t entry_number) {
	constexpr uint16_t
		GENERAL_PURPOSE_ENCRYPTED = 1u << 0,
		GENERAL_PURPOSE_STRONG_ENCRYPTION = 1u << 6;
	constexpr uint32_t
		UNIX_FILE_TYPE_MASK = 0170000,
		UNIX_REGULAR_FILE   = 0100000,
		UNIX_DIRECTORY      = 0040000,
		UNIX_SYMLINK        = 0120000;

	if ((flags & (GENERAL_PURPOSE_ENCRYPTED | GENERAL_PURPOSE_STRONG_ENCRYPTION)) != 0) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Encrypted archive entry {} is not supported.", entry_number));
	}

	// libarchive (and so Windows tar) extracts an entry with the DOS directory
	// attribute as a directory, whatever its name. Require it to agree with the
	// trailing slash, so the slash alone decides an entry's type everywhere. A
	// directory without the attribute is fine: extractors honor the slash, and
	// Java's jar tool writes directories with zero attributes.
	constexpr uint32_t DOS_DIRECTORY_ATTRIBUTE = 0x10;
	if ((external_attributes & DOS_DIRECTORY_ATTRIBUTE) != 0 && !entry_name.ends_with('/')) {
		throw std::runtime_error(std::format(
			"Archive File Error: Directory metadata does not match archive entry path {}.", entry_number));
	}

	// Extractors also honor UNIX type bits for other creator hosts, including
	// DOS-compatible metadata. Validate every advertised type regardless of the
	// host ID; foreign attribute encodings that conflict are not portable here.
	const uint32_t mode_type = (external_attributes >> 16) & UNIX_FILE_TYPE_MASK;
	if (mode_type == 0) {
		// Ordinary DOS/JAR entries may omit the type or contain permissions only.
		// Extra fields capable of supplying a different type are rejected above.
		return;
	}
	if (mode_type == UNIX_SYMLINK) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Symlink archive entry {} is not supported: \"{}\".",
			entry_number, entry_name));
	}
	if (mode_type != UNIX_REGULAR_FILE && mode_type != UNIX_DIRECTORY) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Special archive entry {} is not supported: \"{}\".",
			entry_number, entry_name));
	}
	if (mode_type == UNIX_DIRECTORY && !entry_name.ends_with('/')) {
		throw std::runtime_error(std::format(
			"Archive File Error: Directory metadata does not match archive entry path {}.", entry_number));
	}
	if (mode_type == UNIX_REGULAR_FILE && entry_name.ends_with('/')) {
		throw std::runtime_error(std::format(
			"Archive File Error: File metadata does not match archive entry path {}.", entry_number));
	}
}

void validateEntrySizeMetadata(uint32_t compressed_size, uint32_t uncompressed_size, std::uint64_t& total_uncompressed,
                               bool is_directory, std::size_t entry_number) {
	if (compressed_size == UINT32_MAX || uncompressed_size == UINT32_MAX) {
		throw std::runtime_error(std::format(
			"Archive File Error: ZIP64 size metadata is not supported for entry {}.", entry_number));
	}
	if (is_directory) {
		// A directory entry must have no actual content (uncompressed_size == 0).
		// compressed_size may legitimately be non-zero — Java's jar tool deflates
		// directory entries, producing a 2-byte empty-stream marker (0x03 0x00).
		if (uncompressed_size != 0) {
			throw std::runtime_error(std::format(
				"Archive File Error: Directory entry {} has non-zero uncompressed size.", entry_number));
		}
		return;
	}
	if (static_cast<std::uint64_t>(uncompressed_size) > MAX_TOTAL_UNCOMPRESSED_SIZE
		|| total_uncompressed > MAX_TOTAL_UNCOMPRESSED_SIZE - static_cast<std::uint64_t>(uncompressed_size)) {
		throw std::runtime_error("Archive Security Error: Total uncompressed archive size exceeds the safety limit.");
	}
	total_uncompressed += uncompressed_size;
}

void validateCompressionMethod(uint16_t method, std::size_t entry_number) {
	if (method != 0 && method != 8) {
		throw std::runtime_error(std::format(
			"Archive File Error: Unsupported ZIP compression method {} on entry {}.",
			method, entry_number));
	}
}

[[nodiscard]] uLong updatePayloadCrc(uLong crc, std::span<const Byte> data) {
	std::size_t cursor = 0;
	while (cursor < data.size()) {
		const std::size_t chunk_size = std::min<std::size_t>(
			data.size() - cursor,
			std::numeric_limits<uInt>::max());
		crc = ::crc32(
			crc,
			reinterpret_cast<const Bytef*>(data.data() + cursor),
			static_cast<uInt>(chunk_size));
		cursor += chunk_size;
	}
	return crc;
}

struct InflateEndGuard {
	z_stream* stream;

	~InflateEndGuard() {
		(void)::inflateEnd(stream);
	}
};

[[nodiscard]] std::uint64_t verifyDeflatedPayload(
	std::span<const Byte> compressed,
	uint32_t expected_uncompressed_size,
	uint32_t expected_crc32,
	std::uint64_t output_limit,
	std::size_t entry_number) {

	z_stream stream{};
	const int init_status = ::inflateInit2(&stream, -MAX_WBITS);
	if (init_status != Z_OK) {
		throw std::runtime_error("Archive File Error: Unable to initialize DEFLATE validation.");
	}
	const InflateEndGuard cleanup{ .stream = &stream };

	std::array<Byte, 64 * 1024> output_buffer{};
	std::size_t supplied_input = 0;
	std::uint64_t output_size = 0;
	uLong payload_crc = ::crc32(0L, Z_NULL, 0);
	int status = Z_OK;

	while (status != Z_STREAM_END) {
		if (stream.avail_in == 0 && supplied_input < compressed.size()) {
			const std::size_t input_chunk_size = std::min<std::size_t>(
				compressed.size() - supplied_input,
				std::numeric_limits<uInt>::max());
			stream.next_in = const_cast<Bytef*>(
				reinterpret_cast<const Bytef*>(compressed.data() + supplied_input));
			stream.avail_in = static_cast<uInt>(input_chunk_size);
			supplied_input += input_chunk_size;
		}

		stream.next_out = reinterpret_cast<Bytef*>(output_buffer.data());
		stream.avail_out = static_cast<uInt>(output_buffer.size());
		status = ::inflate(&stream, Z_NO_FLUSH);

		const std::size_t produced = output_buffer.size() - stream.avail_out;
		if (output_size > output_limit || produced > output_limit - output_size) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Actual output for entry {} exceeds its permitted size.",
				entry_number));
		}
		payload_crc = updatePayloadCrc(
			payload_crc,
			std::span<const Byte>(output_buffer.data(), produced));
		output_size += produced;

		if (status == Z_STREAM_END) {
			break;
		}
		if (status != Z_OK) {
			throw std::runtime_error(std::format(
				"Archive File Error: Corrupt DEFLATE stream on entry {} (zlib status {}).",
				entry_number, status));
		}
		if (produced == 0 && stream.avail_in == 0 && supplied_input == compressed.size()) {
			throw std::runtime_error(std::format(
				"Archive File Error: Truncated DEFLATE stream on entry {}.", entry_number));
		}
	}

	const std::size_t consumed_input = supplied_input - stream.avail_in;
	if (consumed_input != compressed.size()
		|| static_cast<std::uint64_t>(stream.total_in) != compressed.size()) {
		throw std::runtime_error(std::format(
			"Archive File Error: DEFLATE stream on entry {} has trailing compressed bytes.",
			entry_number));
	}
	if (output_size != expected_uncompressed_size
		|| static_cast<std::uint64_t>(stream.total_out) != output_size) {
		throw std::runtime_error(std::format(
			"Archive File Error: Actual uncompressed size differs from metadata on entry {}.",
			entry_number));
	}
	if (static_cast<uint32_t>(payload_crc) != expected_crc32) {
		throw std::runtime_error(std::format(
			"Archive File Error: CRC-32 verification failed on entry {}.", entry_number));
	}

	return output_size;
}

[[nodiscard]] std::uint64_t verifyEntryPayload(
	uint16_t compression_method,
	std::span<const Byte> compressed,
	uint32_t expected_uncompressed_size,
	uint32_t expected_crc32,
	std::uint64_t total_verified_uncompressed,
	std::size_t entry_number) {

	if (total_verified_uncompressed > MAX_TOTAL_UNCOMPRESSED_SIZE) {
		throw std::runtime_error("Archive Security Error: Actual uncompressed archive size exceeds the safety limit.");
	}
	const std::uint64_t remaining_output = MAX_TOTAL_UNCOMPRESSED_SIZE - total_verified_uncompressed;
	const std::uint64_t output_limit = std::min<std::uint64_t>(expected_uncompressed_size, remaining_output);

	if (compression_method == 8) {
		return verifyDeflatedPayload(
			compressed,
			expected_uncompressed_size,
			expected_crc32,
			output_limit,
			entry_number);
	}
	if (compressed.size() != expected_uncompressed_size
		|| compressed.size() > output_limit) {
		throw std::runtime_error(std::format(
			"Archive File Error: Stored payload size differs from metadata on entry {}.",
			entry_number));
	}
	const uLong payload_crc = updatePayloadCrc(::crc32(0L, Z_NULL, 0), compressed);
	if (static_cast<uint32_t>(payload_crc) != expected_crc32) {
		throw std::runtime_error(std::format(
			"Archive File Error: CRC-32 verification failed on entry {}.", entry_number));
	}
	return compressed.size();
}

[[nodiscard]] bool descriptor32Matches(std::span<const Byte> archive_data, std::size_t offset,
                                       uint32_t crc32, uint32_t compressed_size, uint32_t uncompressed_size) {
	return readLe32(archive_data, offset) == crc32
		&& readLe32(archive_data, offset + 4) == compressed_size
		&& readLe32(archive_data, offset + 8) == uncompressed_size;
}

[[nodiscard]] std::size_t readDataDescriptorLength(std::span<const Byte> archive_data, std::size_t descriptor_start,
                                                   std::size_t central_start, uint32_t crc32,
                                                   uint32_t compressed_size, uint32_t uncompressed_size,
                                                   std::size_t entry_number) {
	constexpr std::size_t
		DESCRIPTOR_WITHOUT_SIGNATURE_SIZE = 12,
		DESCRIPTOR_WITH_SIGNATURE_SIZE    = 16;

	if (descriptor_start > central_start) {
		throw std::runtime_error(std::format(
			"Archive File Error: Compressed data for entry {} extends past the central directory.", entry_number));
	}

	const std::size_t available = central_start - descriptor_start;
	if (available >= DESCRIPTOR_WITH_SIGNATURE_SIZE
		&& hasLe32Signature(archive_data, descriptor_start, ZIP_DATA_DESCRIPTOR_SIGNATURE)
		&& descriptor32Matches(archive_data, descriptor_start + 4, crc32, compressed_size, uncompressed_size)) {
		return DESCRIPTOR_WITH_SIGNATURE_SIZE;
	}
	if (available >= DESCRIPTOR_WITHOUT_SIGNATURE_SIZE
		&& descriptor32Matches(archive_data, descriptor_start, crc32, compressed_size, uncompressed_size)) {
		return DESCRIPTOR_WITHOUT_SIGNATURE_SIZE;
	}

	throw std::runtime_error(std::format(
		"Archive File Error: Data descriptor for entry {} is missing or inconsistent.", entry_number));
}

void validateLocalEntryPayload(std::span<const Byte> archive_data, std::size_t local_header_start,
                               std::size_t local_record_end, std::size_t central_start,
                               uint16_t central_flags, uint16_t central_compression_method, uint32_t crc32,
                               uint32_t compressed_size, uint32_t uncompressed_size,
                               std::vector<LocalEntrySpan>& local_spans,
                               std::vector<PendingPayload>& pending_payloads, std::size_t entry_number) {
	constexpr uint16_t GENERAL_PURPOSE_DATA_DESCRIPTOR = 1u << 3;

	const uint16_t local_flags = readZipField<uint16_t>(archive_data, local_header_start + 6, "Archive File Error");
	if (local_flags != central_flags) {
		throw std::runtime_error(std::format(
			"Archive File Error: Local and central ZIP flags differ for entry {}.", entry_number));
	}
	const uint16_t local_compression_method = readZipField<uint16_t>(
		archive_data, local_header_start + 8, "Archive File Error");
	if (local_compression_method != central_compression_method) {
		throw std::runtime_error(std::format(
			"Archive File Error: Local and central compression methods differ for entry {}.",
			entry_number));
	}

	const bool has_data_descriptor = (central_flags & GENERAL_PURPOSE_DATA_DESCRIPTOR) != 0;
	const uint32_t local_crc32 = readZipField<uint32_t>(archive_data, local_header_start + 14, "Archive File Error");
	const uint32_t local_compressed_size = readZipField<uint32_t>(archive_data, local_header_start + 18, "Archive File Error");
	const uint32_t local_uncompressed_size = readZipField<uint32_t>(archive_data, local_header_start + 22, "Archive File Error");
	const bool local_metadata_matches = local_crc32 == crc32
		&& local_compressed_size == compressed_size
		&& local_uncompressed_size == uncompressed_size;
	const bool local_descriptor_metadata_is_compatible = (local_crc32 == 0 || local_crc32 == crc32)
		&& (local_compressed_size == 0 || local_compressed_size == compressed_size)
		&& (local_uncompressed_size == 0 || local_uncompressed_size == uncompressed_size);
	if ((!has_data_descriptor && !local_metadata_matches)
		|| (has_data_descriptor && !local_descriptor_metadata_is_compatible)) {
		throw std::runtime_error(std::format(
			"Archive File Error: Local and central CRC/size metadata differ for entry {}.", entry_number));
	}

	const std::size_t compressed_end = checkedAdd(
		local_record_end,
		static_cast<std::size_t>(compressed_size),
		"Archive File Error: Local compressed data size overflow.");
	if (compressed_end > central_start) {
		throw std::runtime_error(std::format(
			"Archive File Error: Compressed data for entry {} extends into the central directory.", entry_number));
	}
	const std::span<const Byte> compressed_payload = archive_data.subspan(local_record_end, compressed_size);

	std::size_t local_payload_end = compressed_end;
	if (has_data_descriptor) {
		local_payload_end = checkedAdd(
			compressed_end,
			readDataDescriptorLength(
				archive_data,
				compressed_end,
				central_start,
				crc32,
				compressed_size,
				uncompressed_size,
				entry_number),
			"Archive File Error: Local data descriptor size overflow.");
		if (local_payload_end > central_start) {
			throw std::runtime_error(std::format(
				"Archive File Error: Data descriptor for entry {} extends into the central directory.", entry_number));
		}
	}

	local_spans.push_back(LocalEntrySpan{
		.begin = local_header_start,
		.end   = local_payload_end
	});
	pending_payloads.push_back(PendingPayload{
		.compression_method = central_compression_method,
		.compressed         = compressed_payload,
		.uncompressed_size  = uncompressed_size,
		.crc32              = crc32,
		.entry_number       = entry_number
	});
}

void validateLocalEntrySpans(std::vector<LocalEntrySpan>& local_spans) {
	std::ranges::sort(local_spans, [](const LocalEntrySpan& lhs, const LocalEntrySpan& rhs) static {
		return lhs.begin < rhs.begin;
	});

	for (const auto& [previous, current] : local_spans | std::views::pairwise) {
		if (current.begin < previous.end) {
			throw std::runtime_error("Archive File Error: Local ZIP entry payloads overlap.");
		}
	}
}

// Inflate and verify every recorded payload. Called only after overlaps have
// been ruled out, so each compressed span is processed at most once.
void verifyPendingPayloads(const std::vector<PendingPayload>& pending_payloads,
                           std::uint64_t& total_verified_uncompressed) {
	for (const PendingPayload& payload : pending_payloads) {
		const std::uint64_t verified_size = verifyEntryPayload(
			payload.compression_method,
			payload.compressed,
			payload.uncompressed_size,
			payload.crc32,
			total_verified_uncompressed,
			payload.entry_number);
		if (verified_size > MAX_TOTAL_UNCOMPRESSED_SIZE - total_verified_uncompressed) {
			throw std::runtime_error("Archive Security Error: Actual uncompressed archive size exceeds the safety limit.");
		}
		total_verified_uncompressed += verified_size;
	}
}

[[nodiscard]] CentralDirectoryBounds readCentralDirectoryBounds(std::span<const Byte> archive_data,
                                                                std::size_t eocd_index) {
	const uint16_t disk_number = readZipField<uint16_t>(archive_data, eocd_index + 4, "Archive File Error");
	const uint16_t central_dir_disk = readZipField<uint16_t>(archive_data, eocd_index + 6, "Archive File Error");
	const uint16_t records_on_disk = readZipField<uint16_t>(archive_data, eocd_index + 8, "Archive File Error");
	const uint16_t total_records = readZipField<uint16_t>(archive_data, eocd_index + 10, "Archive File Error");
	const uint32_t central_size = readZipField<uint32_t>(archive_data, eocd_index + 12, "Archive File Error");
	const uint32_t central_offset = readZipField<uint32_t>(archive_data, eocd_index + 16, "Archive File Error");

	if (disk_number != 0 || central_dir_disk != 0 || records_on_disk != total_records) {
		throw std::runtime_error("Archive File Error: Multi-disk ZIP archives are not supported.");
	}
	if (total_records == 0) {
		throw std::runtime_error("Archive File Error: Archive contains no central directory entries.");
	}
	if (total_records == UINT16_MAX || central_size == UINT32_MAX || central_offset == UINT32_MAX) {
		throw std::runtime_error("Archive File Error: ZIP64 archives are not supported.");
	}

	const std::size_t central_start = checkedAdd(
		ZIP_WRAP_PREFIX_SIZE,
		static_cast<std::size_t>(central_offset),
		"Archive File Error: Central directory offset overflow.");
	const std::size_t central_end = checkedAdd(
		central_start,
		static_cast<std::size_t>(central_size),
		"Archive File Error: Central directory size overflow.");

	if (central_start > archive_data.size() || central_end > archive_data.size() || central_end > eocd_index) {
		throw std::runtime_error("Archive File Error: Central directory bounds are invalid.");
	}
	if (central_end != eocd_index) {
		throw std::runtime_error("Archive File Error: Central directory does not end at the EOCD record.");
	}

	return CentralDirectoryBounds{
		.start = central_start,
		.end = central_end,
		.total_records = total_records
	};
}

[[nodiscard]] CentralEntryMetadata readCentralEntryMetadata(std::span<const Byte> archive_data, std::size_t cursor,
                                                            std::size_t entry_number) {
	if (cursor > archive_data.size() || CENTRAL_RECORD_MIN_SIZE > archive_data.size() - cursor) {
		throw std::runtime_error("Archive File Error: Truncated central directory file header.");
	}

	if (!hasLe32Signature(archive_data, cursor, ZIP_CENTRAL_DIRECTORY_SIGNATURE)) {
		throw std::runtime_error("Archive File Error: Invalid central directory file header signature.");
	}

	const uint16_t flags = readZipField<uint16_t>(archive_data, cursor + CENTRAL_FLAGS_OFFSET, "Archive File Error");
	const uint16_t compression_method = readZipField<uint16_t>(archive_data, cursor + CENTRAL_COMPRESSION_METHOD, "Archive File Error");
	const uint32_t crc32 = readZipField<uint32_t>(archive_data, cursor + CENTRAL_CRC32, "Archive File Error");
	const uint32_t compressed_size = readZipField<uint32_t>(archive_data, cursor + CENTRAL_COMPRESSED_SIZE, "Archive File Error");
	const uint32_t uncompressed_size = readZipField<uint32_t>(archive_data, cursor + CENTRAL_UNCOMPRESSED_SIZE, "Archive File Error");
	const std::size_t name_length = readZipField<uint16_t>(archive_data, cursor + CENTRAL_NAME_LENGTH_OFFSET, "Archive File Error");
	const std::size_t extra_length = readZipField<uint16_t>(archive_data, cursor + CENTRAL_EXTRA_LENGTH_OFFSET, "Archive File Error");
	const std::size_t comment_length = readZipField<uint16_t>(archive_data, cursor + CENTRAL_COMMENT_LENGTH_OFFSET, "Archive File Error");
	const uint16_t disk_start = readZipField<uint16_t>(archive_data, cursor + CENTRAL_DISK_START, "Archive File Error");
	const uint32_t external_attributes = readZipField<uint32_t>(archive_data, cursor + CENTRAL_EXTERNAL_ATTRIBUTES, "Archive File Error");
	const std::size_t local_header_offset = readZipField<uint32_t>(archive_data, cursor + CENTRAL_LOCAL_OFFSET, "Archive File Error");

	const std::size_t name_start = checkedAdd(
		cursor,
		CENTRAL_RECORD_NAME_INDEX,
		"Archive File Error: Central directory filename offset overflow.");
	const std::string_view name = readZipStringView(
		archive_data,
		name_start,
		name_length,
		"Archive File Error: Central directory filename length overflow.",
		"Archive File Error: Central directory filename exceeds archive bounds.");
	const std::size_t extra_start = checkedAdd(
		name_start,
		name_length,
		"Archive File Error: Central directory extra-field offset overflow.");
	const std::size_t extra_end = checkedAdd(
		extra_start,
		extra_length,
		"Archive File Error: Central directory extra-field length overflow.");
	if (extra_end > archive_data.size()) {
		throw std::runtime_error("Archive File Error: Central directory extra field exceeds archive bounds.");
	}
	const std::span<const Byte> extra = archive_data.subspan(extra_start, extra_length);

	const std::size_t record_size = zipCentralDirectoryRecordSize(name_length, extra_length, comment_length);

	return CentralEntryMetadata{
		.entry_number = entry_number,
		.flags = flags,
		.compression_method = compression_method,
		.crc32 = crc32,
		.compressed_size = compressed_size,
		.uncompressed_size = uncompressed_size,
		.disk_start = disk_start,
		.external_attributes = external_attributes,
		.local_header_offset = local_header_offset,
		.name = name,
		.extra = extra,
		.record_size = record_size
	};
}

void validateCentralRecordSpan(std::size_t cursor, std::size_t record_size, std::size_t central_end,
                               std::size_t archive_size) {
	if (record_size > archive_size - cursor) {
		throw std::runtime_error("Archive File Error: Central directory entry exceeds archive bounds.");
	}
	if (cursor > central_end || record_size > central_end - cursor) {
		throw std::runtime_error("Archive File Error: Central directory entry exceeds declared directory size.");
	}
}

// Info-ZIP's unzip strips a trailing ";<digits>" version suffix from the final
// path component unless -V is given, so "main.py;1" lands on disk as "main.py".
// That rewrite makes the extracted name differ from the validated central name:
// a sibling "main.py" is silently overwritten, and the launcher's target no
// longer matches what was extracted. We pass -V in the extraction script, but
// also reject such names here so the archive is well defined under any unzip.
[[nodiscard]] bool endsWithInfoZipVersionSuffix(std::string_view name) {
	const std::size_t separator = name.rfind('/');
	const std::string_view basename = separator == std::string_view::npos
		? name : name.substr(separator + 1);
	const std::size_t last_non_digit = basename.find_last_not_of("0123456789");
	// A suffix exists only when the basename ends with ';' followed by at least
	// one digit (the ';' must immediately precede the trailing digit run).
	return last_non_digit != std::string_view::npos
		&& last_non_digit + 1 < basename.size()
		&& basename[last_non_digit] == ';';
}

void validateCentralEntryMetadata(const CentralEntryMetadata& entry, ArchiveEntryTracking& tracking) {
	if (entry.disk_start != 0) {
		throw std::runtime_error(std::format(
			"Archive File Error: Multi-disk local header reference on entry {} is not supported.",
			entry.entry_number));
	}
	if (entry.local_header_offset == UINT32_MAX) {
		throw std::runtime_error(std::format(
			"Archive File Error: ZIP64 local-header offset is not supported on entry {}.",
			entry.entry_number));
	}

	validateEntryName(
		entry.name,
		"Archive File Error: Entry",
		"Archive Security Error: Unsafe archive entry path detected",
		entry.entry_number);
	validateEntryNameEncoding(entry.name, entry.flags, entry.entry_number);
	if (endsWithInfoZipVersionSuffix(entry.name)) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Archive entry {} name carries an Info-ZIP version suffix "
			"that would be stripped on extraction: \"{}\".",
			entry.entry_number, entry.name));
	}
	validateZipExtraFields(entry.extra, "central-directory", entry.entry_number);
	validateCompressionMethod(entry.compression_method, entry.entry_number);
	validateEntryAttributes(
		entry.flags,
		entry.external_attributes,
		entry.name,
		entry.entry_number);
	validateEntrySizeMetadata(
		entry.compressed_size,
		entry.uncompressed_size,
		tracking.total_declared_uncompressed,
		entry.name.ends_with('/'),
		entry.entry_number);
	tracking.paths.insert(entry.name, entry.entry_number);
}

void validateLocalEntryForCentralEntry(std::span<const Byte> archive_data, std::size_t central_start,
                                       const CentralEntryMetadata& entry,
                                       std::vector<LocalEntrySpan>& local_spans,
                                       std::vector<PendingPayload>& pending_payloads) {
	const std::size_t local_header_start = checkedAdd(
		ZIP_WRAP_PREFIX_SIZE,
		entry.local_header_offset,
		"Archive File Error: Local file header offset overflow.");
	if (local_header_start >= central_start) {
		throw std::runtime_error("Archive File Error: Local file header points inside the central directory.");
	}
	if (local_header_start > archive_data.size()
		|| LOCAL_RECORD_MIN_SIZE > archive_data.size() - local_header_start) {
		throw std::runtime_error("Archive File Error: Truncated local file header.");
	}
	if (!hasLe32Signature(archive_data, local_header_start, ZIP_LOCAL_FILE_HEADER_SIGNATURE)) {
		throw std::runtime_error(std::format(
			"Archive File Error: Invalid local file header signature for entry {}.", entry.entry_number));
	}

	const std::size_t local_name_length = readZipField<uint16_t>(archive_data, local_header_start + 26, "Archive File Error");
	const std::size_t local_extra_length = readZipField<uint16_t>(archive_data, local_header_start + 28, "Archive File Error");
	const std::size_t local_name_start = checkedAdd(
		local_header_start,
		LOCAL_RECORD_NAME_INDEX,
		"Archive File Error: Local filename offset overflow.");
	const std::size_t local_extra_start = checkedAdd(
		local_name_start,
		local_name_length,
		"Archive File Error: Local filename length overflow.");
	const std::size_t local_record_end = checkedAdd(
		local_extra_start,
		local_extra_length,
		"Archive File Error: Local header extra-field length overflow.");
	if (local_record_end > archive_data.size()) {
		throw std::runtime_error("Archive File Error: Local file header exceeds archive bounds.");
	}

	const std::string_view local_entry_name = readZipStringView(
		archive_data,
		local_name_start,
		local_name_length,
		"Archive File Error: Local filename length overflow.",
		"Archive File Error: Local file header exceeds archive bounds.");
	const std::span<const Byte> local_extra = archive_data.subspan(local_extra_start, local_extra_length);
	validateEntryName(
		local_entry_name,
		"Archive File Error: Local entry",
		"Archive Security Error: Unsafe local archive entry path detected",
		entry.entry_number);
	validateZipExtraFields(local_extra, "local-header", entry.entry_number);
	if (local_entry_name != entry.name) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Local and central directory names differ for entry {}.", entry.entry_number));
	}

	validateLocalEntryPayload(
		archive_data,
		local_header_start,
		local_record_end,
		central_start,
		entry.flags,
		entry.compression_method,
		entry.crc32,
		entry.compressed_size,
		entry.uncompressed_size,
		local_spans,
		pending_payloads,
		entry.entry_number);
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path) {
	if (path.empty()) {
		return true;
	}
	if (path.contains('\\')) {
		return true;
	}
	if (path[0] == '/') {
		return true;
	}

	std::size_t segment_start = 0;
	for (std::size_t i = 0; i <= path.size(); ++i) {
		if (i < path.size() && path[i] != '/') {
			continue;
		}
		const std::string_view segment = path.substr(segment_start, i - segment_start);
		const bool trailing_directory_separator = (i == path.size() && segment.empty());
		if (!trailing_directory_separator
			&& (segment.empty()
				|| segment == "."sv
				|| segment == ".."sv
				|| segment.back() == '.'
				|| segment.back() == ' '
				|| hasWindowsInvalidPathCharacter(segment)
				|| hasWindowsReservedSegmentName(segment))) {
			return true;
		}
		segment_start = i + 1;
	}

	return false;
}

[[nodiscard]] std::size_t findEndOfCentralDirectory(std::span<const Byte> archive_data) {
	constexpr std::size_t EOCD_MIN_SIZE = 22;

	if (archive_data.size() < ZIP_WRAP_PREFIX_SIZE + ZIP_WRAP_TRAILER_SIZE + EOCD_MIN_SIZE) {
		throw std::runtime_error("Archive File Error: Archive is too small.");
	}

	if (const auto eocd = findZipEocdLocator(archive_data, ZIP_WRAP_PREFIX_SIZE, archive_data.size() - ZIP_WRAP_TRAILER_SIZE)) {
		return eocd->index;
	}

	throw std::runtime_error("Archive File Error: End of central directory record not found.");
}

struct ValidatedArchiveSummary {
	std::string first_referenced_filename;
	std::size_t first_referenced_local_offset = std::numeric_limits<std::size_t>::max();
	bool has_jar_manifest_file = false;
};

// Only valid after validateEntryAttributes(): UNIX type bits and the DOS
// directory attribute have been checked against the trailing slash, and
// symlinks/special files rejected, so the slash alone determines the type.
[[nodiscard]] bool isRegularFileEntry(const CentralEntryMetadata& entry) {
	return !entry.name.ends_with('/');
}

[[nodiscard]] ValidatedArchiveSummary validateAndSummarizeArchive(std::span<const Byte> archive_data) {
	const std::size_t eocd_index = findEndOfCentralDirectory(archive_data);
	const CentralDirectoryBounds central_directory = readCentralDirectoryBounds(archive_data, eocd_index);

	std::size_t cursor = central_directory.start;
	ArchiveEntryTracking tracking;
	tracking.reserve(central_directory.total_records);
	ValidatedArchiveSummary summary;

	for (uint16_t i = 0; i < central_directory.total_records; ++i) {
		const CentralEntryMetadata entry = readCentralEntryMetadata(
			archive_data,
			cursor,
			i + 1);

		validateCentralRecordSpan(cursor, entry.record_size, central_directory.end, archive_data.size());
		validateCentralEntryMetadata(entry, tracking);
		validateLocalEntryForCentralEntry(
			archive_data,
			central_directory.start,
			entry,
			tracking.local_spans,
			tracking.pending_payloads);

		if (entry.local_header_offset < summary.first_referenced_local_offset) {
			summary.first_referenced_local_offset = entry.local_header_offset;
			summary.first_referenced_filename = entry.name;
		}
		if (entry.name == "META-INF/MANIFEST.MF"sv && isRegularFileEntry(entry)) {
			summary.has_jar_manifest_file = true;
		}

		cursor = checkedAdd(
			cursor,
			entry.record_size,
			"Archive File Error: Central directory cursor overflow.");
	}

	if (cursor != central_directory.end) {
		throw std::runtime_error("Archive File Error: Central directory size does not match parsed records.");
	}
	// Reject overlapping local spans before inflating anything, so nested
	// overlapping entries cannot force repeated decompression of shared bytes.
	validateLocalEntrySpans(tracking.local_spans);
	verifyPendingPayloads(tracking.pending_payloads, tracking.total_verified_uncompressed);
	if (tracking.total_verified_uncompressed != tracking.total_declared_uncompressed) {
		throw std::runtime_error("Archive File Error: Verified archive size differs from declared metadata.");
	}
	if (summary.first_referenced_filename.empty()) {
		throw std::runtime_error("Archive File Error: No referenced local ZIP entry was found.");
	}
	return summary;
}

} // anonymous namespace

ArchiveMetadata analyzeArchive(std::span<const Byte> archive_data, bool is_zip_file) {
	constexpr std::size_t FIRST_FILENAME_MIN_LENGTH = 4;
	ValidatedArchiveSummary summary = validateAndSummarizeArchive(archive_data);
	ArchiveMetadata metadata{
		.file_type = FileType::UNKNOWN_FILE_TYPE,
		.first_filename = std::move(summary.first_referenced_filename)
	};
	const std::string_view filename = metadata.first_filename;

	if (!is_zip_file) {
		if (!summary.has_jar_manifest_file) {
			throw std::runtime_error("File Type Error: Archive does not appear to be a valid JAR file.");
		}
		metadata.file_type = FileType::JAR;
		return metadata;
	}
	// The extraction script names this entry directly. Windows CMD decodes the
	// script in the console's OEM code page, and Info-ZIP may re-encode UTF-8
	// names outside a UTF-8 locale, so only ASCII is reproduced reliably.
	if (hasNonAsciiByte(filename)) {
		throw std::runtime_error(
			"File Error:\n\nThe first file within the archive is launched by the extraction script,\n"
			"so its path must contain ASCII characters only. Rename it and try again.");
	}
	if (filename.size() < FIRST_FILENAME_MIN_LENGTH) {
		throw std::runtime_error(
			"File Error:\n\nName length of first file within archive is too short.\n"
			"Increase length (minimum 4 characters). Make sure it has a valid extension.");
	}

	// --- ZIP path: inspect the earliest referenced local entry's filename/extension ---

	const bool is_folder = filename.back() == '/';
	if (is_folder) {
		if (filename[filename.size() - 2] == '.') {
			throw std::runtime_error("ZIP File Error: Invalid folder name within ZIP archive.");
		}
		metadata.file_type = FileType::FOLDER;
		return metadata;
	}

	// Only the final component supplies an extension. A dot in a parent
	// directory (for example pkg.v1/runner) does not change the file type.
	const std::size_t separator_pos = filename.rfind('/');
	const std::string_view basename = separator_pos == std::string_view::npos
		? filename : filename.substr(separator_pos + 1);
	const std::size_t dot_pos = basename.rfind('.');
	if (dot_pos == std::string_view::npos) {
		metadata.file_type = FileType::LINUX_EXECUTABLE;
		return metadata;
	}

	// Match extension against the known list.
	const std::string_view extension = basename.substr(dot_pos + 1);
	for (std::size_t i = 0; i < EXTENSION_LIST.size(); ++i) {
		if (std::ranges::equal(EXTENSION_LIST[i], extension, [](char lhs, char rhs) static {
				return lhs == toLowerAscii(rhs);
			})) {
			// Indices 0..28 all map to VIDEO_AUDIO; 29+ map 1:1 with the enum.
			metadata.file_type = static_cast<FileType>(std::max(i, std::to_underlying(FileType::VIDEO_AUDIO)));
			return metadata;
		}
	}

	return metadata;
}

void validateArchiveEntryPaths(std::span<const Byte> archive_data) {
	(void)validateAndSummarizeArchive(archive_data);
}
