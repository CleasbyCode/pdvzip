#include "pdvzip.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

#include <zlib.h>

namespace {

constexpr auto EXTENSION_LIST = std::to_array<std::string_view>({
	"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
	"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
	"xvid", "pdf", "py", "ps1", "sh", "exe"
});

// The extension->FileType mapping in analyzeArchive() is positional: an entry at
// index i resolves to FileType(max(i, VIDEO_AUDIO)). That only stays correct if
// the distinct (non-VIDEO_AUDIO) types sit at exactly their enum-value indices
// and nothing follows "exe". These asserts fail the build if the list is ever
// reordered, extended past WINDOWS_EXECUTABLE, or drifts from the enum.
static_assert(EXTENSION_LIST.size() == static_cast<std::size_t>(FileType::WINDOWS_EXECUTABLE) + 1,
	"EXTENSION_LIST must end at the WINDOWS_EXECUTABLE entry; trailing entries would be misclassified.");
static_assert(EXTENSION_LIST[static_cast<std::size_t>(FileType::PDF)] == "pdf");
static_assert(EXTENSION_LIST[static_cast<std::size_t>(FileType::PYTHON)] == "py");
static_assert(EXTENSION_LIST[static_cast<std::size_t>(FileType::POWERSHELL)] == "ps1");
static_assert(EXTENSION_LIST[static_cast<std::size_t>(FileType::BASH_SHELL)] == "sh");
static_assert(EXTENSION_LIST[static_cast<std::size_t>(FileType::WINDOWS_EXECUTABLE)] == "exe");

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
	return std::ranges::any_of(value, [](unsigned char c) {
		return std::iscntrl(c) != 0;
	});
}

[[nodiscard]] char toLowerAscii(char ch) {
	if (ch >= 'A' && ch <= 'Z') {
		return static_cast<char>(ch - 'A' + 'a');
	}
	return ch;
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

	auto equalsReservedName = [](std::string_view lhs, std::string_view rhs) {
		return std::ranges::equal(lhs, rhs, [](char a, char b) {
			return toLowerAscii(a) == b;
		});
	};

	if (equalsReservedName(stem, "con"sv)
		|| equalsReservedName(stem, "prn"sv)
		|| equalsReservedName(stem, "aux"sv)
		|| equalsReservedName(stem, "nul"sv)) {
		return true;
	}
	return stem.size() == 4
		&& stem[3] >= '1' && stem[3] <= '9'
		&& ((toLowerAscii(stem[0]) == 'c' && toLowerAscii(stem[1]) == 'o' && toLowerAscii(stem[2]) == 'm')
			|| (toLowerAscii(stem[0]) == 'l' && toLowerAscii(stem[1]) == 'p' && toLowerAscii(stem[2]) == 't'));
}

[[nodiscard]] bool hasWindowsInvalidPathCharacter(std::string_view segment) {
	return std::ranges::any_of(segment, [](char ch) {
		return ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*';
	});
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path);

struct LocalEntrySpan {
	std::size_t begin;
	std::size_t end;
};

constexpr std::size_t
	ZIP_WRAP_PREFIX_SIZE          = 8,
	ZIP_WRAP_TRAILER_SIZE         = 4,
	LOCAL_RECORD_MIN_SIZE         = 30,
	LOCAL_RECORD_NAME_INDEX       = 30,
	CENTRAL_RECORD_MIN_SIZE       = 46,
	CENTRAL_RECORD_NAME_INDEX     = 46,
	CENTRAL_VERSION_MADE_BY       = 4,
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
	ZIP_EXTRA_INFOZIP_UNICODE_PATH     = 0x7075;

struct CentralDirectoryBounds {
	std::size_t start;
	std::size_t end;
	uint16_t total_records;
};

struct CentralEntryMetadata {
	std::size_t entry_number;
	uint16_t version_made_by;
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
	static constexpr std::size_t NO_NODE = std::numeric_limits<std::size_t>::max();

	struct Node {
		std::size_t first_child = NO_NODE;
		std::size_t next_sibling = NO_NODE;
		Byte label = 0;
		bool has_explicit_entry = false;
		bool is_file = false;
		bool is_directory = false;
	};

	std::vector<Node> nodes_{1}; // index zero is the root

	[[nodiscard]] std::size_t findOrAddChild(std::size_t parent, Byte label) {
		for (std::size_t child = nodes_[parent].first_child;
			 child != NO_NODE;
			 child = nodes_[child].next_sibling) {
			if (nodes_[child].label == label) {
				return child;
			}
		}

		const std::size_t child = nodes_.size();
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
		nodes_.reserve(entry_count + 1);
	}

	void insert(std::string_view entry_name, std::size_t entry_number) {
		const bool directory_entry = entry_name.ends_with('/');
		const std::size_t key_size = entry_name.size() - static_cast<std::size_t>(directory_entry);
		if (key_size == 0) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Empty normalized path for archive entry {}.", entry_number));
		}

		std::size_t node = 0;
		for (std::size_t i = 0; i < key_size; ++i) {
			const char ch = entry_name[i];
			if (ch == '/') {
				if (nodes_[node].is_file) {
					throw std::runtime_error(std::format(
						"Archive Security Error: Archive entry {} conflicts with an existing file path.",
						entry_number));
				}
				nodes_[node].is_directory = true;
			}

			const char normalized = ch == '\\' ? '/' : toLowerAscii(ch);
			node = findOrAddChild(node, static_cast<Byte>(normalized));
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

	void reserve(std::size_t total_records) {
		paths.reserve(total_records);
		local_spans.reserve(total_records);
	}
};

void validateZipExtraFields(std::span<const Byte> extra, std::string_view location,
							std::optional<std::size_t> entry_number = std::nullopt) {
	std::size_t cursor = 0;
	while (cursor < extra.size()) {
		if (extra.size() - cursor < 4) {
			throw std::runtime_error(entry_number
				? std::format("Archive File Error: Malformed {} extra field on entry {}.", location, *entry_number)
				: std::format("Archive File Error: Malformed {} extra field.", location));
		}

		const uint16_t field_id = readLe16(extra, cursor);
		const std::size_t field_size = readLe16(extra, cursor + 2);
		cursor += 4;
		if (field_size > extra.size() - cursor) {
			throw std::runtime_error(entry_number
				? std::format("Archive File Error: Malformed {} extra field on entry {}.", location, *entry_number)
				: std::format("Archive File Error: Malformed {} extra field.", location));
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

[[nodiscard]] bool isUnixLikeZipHost(uint16_t version_made_by) {
	const Byte host = static_cast<Byte>(version_made_by >> 8);
	return host == 3   // UNIX
		|| host == 19; // macOS/OS X
}

void validateEntryAttributes(uint16_t version_made_by, uint16_t flags, uint32_t external_attributes,
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

	if (!isUnixLikeZipHost(version_made_by)) {
		return;
	}

	const uint32_t mode_type = (external_attributes >> 16) & UNIX_FILE_TYPE_MASK;
	if (mode_type == 0) {
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
	if (compression_method != 0) {
		validateCompressionMethod(compression_method, entry_number);
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
                               std::uint64_t& total_verified_uncompressed,
                               std::vector<LocalEntrySpan>& local_spans, std::size_t entry_number) {
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
	const std::uint64_t verified_size = verifyEntryPayload(
		central_compression_method,
		compressed_payload,
		uncompressed_size,
		crc32,
		total_verified_uncompressed,
		entry_number);
	if (verified_size > MAX_TOTAL_UNCOMPRESSED_SIZE - total_verified_uncompressed) {
		throw std::runtime_error("Archive Security Error: Actual uncompressed archive size exceeds the safety limit.");
	}
	total_verified_uncompressed += verified_size;

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
}

void validateLocalEntrySpans(std::vector<LocalEntrySpan>& local_spans) {
	std::ranges::sort(local_spans, [](const LocalEntrySpan& lhs, const LocalEntrySpan& rhs) {
		return lhs.begin < rhs.begin;
	});

	for (std::size_t i = 1; i < local_spans.size(); ++i) {
		if (local_spans[i].begin < local_spans[i - 1].end) {
			throw std::runtime_error("Archive File Error: Local ZIP entry payloads overlap.");
		}
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

	const uint16_t version_made_by = readZipField<uint16_t>(archive_data, cursor + CENTRAL_VERSION_MADE_BY, "Archive File Error");
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
		.version_made_by = version_made_by,
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
	validateZipExtraFields(entry.extra, "central-directory", entry.entry_number);
	validateCompressionMethod(entry.compression_method, entry.entry_number);
	validateEntryAttributes(
		entry.version_made_by,
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
                                       std::uint64_t& total_verified_uncompressed,
                                       std::vector<LocalEntrySpan>& local_spans) {
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
		total_verified_uncompressed,
		local_spans,
		entry.entry_number);
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path) {
	if (path.empty()) {
		return true;
	}
	if (path.find('\\') != std::string::npos) {
		return true;
	}
	if (path[0] == '/') {
		return true;
	}
	if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
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

[[nodiscard]] bool isRegularFileEntry(const CentralEntryMetadata& entry) {
	if (entry.name.ends_with('/')) {
		return false;
	}

	if (isUnixLikeZipHost(entry.version_made_by)) {
		constexpr uint32_t
			UNIX_FILE_TYPE_MASK = 0170000,
			UNIX_REGULAR_FILE = 0100000;
		const uint32_t mode_type = (entry.external_attributes >> 16) & UNIX_FILE_TYPE_MASK;
		return mode_type == 0 || mode_type == UNIX_REGULAR_FILE;
	}

	constexpr uint32_t DOS_DIRECTORY_ATTRIBUTE = 0x10;
	return (entry.external_attributes & DOS_DIRECTORY_ATTRIBUTE) == 0;
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
			tracking.total_verified_uncompressed,
			tracking.local_spans);

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
	if (tracking.total_verified_uncompressed != tracking.total_declared_uncompressed) {
		throw std::runtime_error("Archive File Error: Verified archive size differs from declared metadata.");
	}
	validateLocalEntrySpans(tracking.local_spans);
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
	if (filename.size() < FIRST_FILENAME_MIN_LENGTH) {
		throw std::runtime_error(
			"File Error:\n\nName length of first file within archive is too short.\n"
			"Increase length (minimum 4 characters). Make sure it has a valid extension.");
	}

	// --- ZIP path: inspect the earliest referenced local entry's filename/extension ---

	const bool is_folder = filename.back() == '/';
	const std::size_t dot_pos = filename.rfind('.');

	// Check for folders (entries ending with '/').
	if (dot_pos == std::string_view::npos) {
		metadata.file_type = is_folder ? FileType::FOLDER : FileType::LINUX_EXECUTABLE;
		return metadata;
	}

	// A name with a dot could still be a folder if it ends with '/'.
	if (is_folder) {
		if (filename[filename.size() - 2] == '.') {
			throw std::runtime_error("ZIP File Error: Invalid folder name within ZIP archive.");
		}
		metadata.file_type = FileType::FOLDER;
		return metadata;
	}

	// Match extension against the known list.
	const std::string_view extension = filename.substr(dot_pos + 1);
	for (std::size_t i = 0; i < EXTENSION_LIST.size(); ++i) {
		if (std::ranges::equal(EXTENSION_LIST[i], extension, [](char lhs, char rhs) {
				return lhs == toLowerAscii(rhs);
			})) {
			// Indices 0..28 all map to VIDEO_AUDIO; 29+ map 1:1 with the enum.
			metadata.file_type = static_cast<FileType>(std::max(i, static_cast<std::size_t>(FileType::VIDEO_AUDIO)));
			return metadata;
		}
	}

	return metadata;
}

void validateArchiveEntryPaths(std::span<const Byte> archive_data) {
	(void)validateAndSummarizeArchive(archive_data);
}
