#include "pdvzip.h"

namespace {

template <typename T>
[[nodiscard]] T readZipField(std::span<const Byte> data, std::size_t offset, std::string_view context) {
	try {
		return static_cast<T>(readValueAt(data, offset, sizeof(T), std::endian::little));
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

	std::string upper_stem;
	upper_stem.reserve(stem.size());
	for (char ch : stem) {
		upper_stem.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
	}

	if (upper_stem == "CON" || upper_stem == "PRN" || upper_stem == "AUX" || upper_stem == "NUL") {
		return true;
	}
	if (upper_stem.size() == 4
		&& (upper_stem.starts_with("COM") || upper_stem.starts_with("LPT"))
		&& upper_stem[3] >= '1' && upper_stem[3] <= '9') {
		return true;
	}
	return false;
}

[[nodiscard]] bool hasWindowsInvalidPathCharacter(std::string_view segment) {
	return std::ranges::any_of(segment, [](char ch) {
		return ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*';
	});
}

[[nodiscard]] std::string makePortableEntryKey(std::string_view path) {
	std::string key;
	key.reserve(path.size());
	for (char ch : path) {
		key.push_back(ch == '\\' ? '/' : toLowerAscii(ch));
	}
	while (!key.empty() && key.back() == '/') {
		key.pop_back();
	}
	return key;
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path);

struct LocalEntrySpan {
	std::size_t begin;
	std::size_t end;
};

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
	constexpr std::uint64_t MAX_TOTAL_UNCOMPRESSED_SIZE = 2ULL * 1024 * 1024 * 1024;

	if (compressed_size == UINT32_MAX || uncompressed_size == UINT32_MAX) {
		throw std::runtime_error(std::format(
			"Archive File Error: ZIP64 size metadata is not supported for entry {}.", entry_number));
	}
	if (is_directory) {
		if (compressed_size != 0 || uncompressed_size != 0) {
			throw std::runtime_error(std::format(
				"Archive File Error: Directory entry {} has non-zero size metadata.", entry_number));
		}
		return;
	}
	if (static_cast<std::uint64_t>(uncompressed_size) > MAX_TOTAL_UNCOMPRESSED_SIZE
		|| total_uncompressed > MAX_TOTAL_UNCOMPRESSED_SIZE - static_cast<std::uint64_t>(uncompressed_size)) {
		throw std::runtime_error("Archive Security Error: Total uncompressed archive size exceeds the safety limit.");
	}
	total_uncompressed += uncompressed_size;
}

void validateEntryPathCollision(std::string_view entry_name, std::unordered_set<std::string>& seen_entries,
                                std::unordered_set<std::string>& file_entries,
                                std::unordered_set<std::string>& directory_entries,
                                std::size_t entry_number) {
	const bool is_directory = entry_name.ends_with('/');
	const std::string key = makePortableEntryKey(entry_name);
	if (key.empty()) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Empty normalized path for archive entry {}.", entry_number));
	}
	if (!seen_entries.insert(key).second) {
		throw std::runtime_error(std::format(
			"Archive Security Error: Duplicate or case-conflicting archive entry path detected: \"{}\".",
			entry_name));
	}

	std::size_t slash_pos = 0;
	while ((slash_pos = key.find('/', slash_pos)) != std::string::npos) {
		const std::string parent = key.substr(0, slash_pos);
		if (file_entries.contains(parent)) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Archive entry {} conflicts with file path \"{}\".",
				entry_number, parent));
		}
		directory_entries.insert(parent);
		++slash_pos;
	}

	if (is_directory) {
		if (file_entries.contains(key)) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Directory entry {} conflicts with an existing file path.", entry_number));
		}
		directory_entries.insert(key);
		return;
	}

	if (directory_entries.contains(key)) {
		throw std::runtime_error(std::format(
			"Archive Security Error: File entry {} conflicts with an existing directory path.", entry_number));
	}
	file_entries.insert(key);
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
	constexpr uint32_t DATA_DESCRIPTOR_SIGNATURE = 0x08074B50;

	if (descriptor_start > central_start) {
		throw std::runtime_error(std::format(
			"Archive File Error: Compressed data for entry {} extends past the central directory.", entry_number));
	}

	const std::size_t available = central_start - descriptor_start;
	if (available >= DESCRIPTOR_WITH_SIGNATURE_SIZE
		&& readLe32(archive_data, descriptor_start) == DATA_DESCRIPTOR_SIGNATURE
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
                               uint16_t central_flags, uint32_t crc32,
                               uint32_t compressed_size, uint32_t uncompressed_size,
                               std::vector<LocalEntrySpan>& local_spans, std::size_t entry_number) {
	constexpr uint16_t
		GENERAL_PURPOSE_ENCRYPTED         = 1u << 0,
		GENERAL_PURPOSE_DATA_DESCRIPTOR   = 1u << 3,
		GENERAL_PURPOSE_STRONG_ENCRYPTION = 1u << 6;
	constexpr uint16_t FLAGS_THAT_AFFECT_LAYOUT =
		GENERAL_PURPOSE_ENCRYPTED | GENERAL_PURPOSE_DATA_DESCRIPTOR | GENERAL_PURPOSE_STRONG_ENCRYPTION;

	const uint16_t local_flags = readZipField<uint16_t>(archive_data, local_header_start + 6, "Archive File Error");
	if ((local_flags & FLAGS_THAT_AFFECT_LAYOUT) != (central_flags & FLAGS_THAT_AFFECT_LAYOUT)) {
		throw std::runtime_error(std::format(
			"Archive File Error: Local and central ZIP flags differ for entry {}.", entry_number));
	}

	const bool has_data_descriptor = (central_flags & GENERAL_PURPOSE_DATA_DESCRIPTOR) != 0;
	if (!has_data_descriptor) {
		const uint32_t local_crc32 = readZipField<uint32_t>(archive_data, local_header_start + 14, "Archive File Error");
		const uint32_t local_compressed_size = readZipField<uint32_t>(archive_data, local_header_start + 18, "Archive File Error");
		const uint32_t local_uncompressed_size = readZipField<uint32_t>(archive_data, local_header_start + 22, "Archive File Error");
		if (local_crc32 != crc32
			|| local_compressed_size != compressed_size
			|| local_uncompressed_size != uncompressed_size) {
			throw std::runtime_error(std::format(
				"Archive File Error: Local and central size metadata differ for entry {}.", entry_number));
		}
	}

	const std::size_t compressed_end = checkedAdd(
		local_record_end,
		static_cast<std::size_t>(compressed_size),
		"Archive File Error: Local compressed data size overflow.");
	if (compressed_end > central_start) {
		throw std::runtime_error(std::format(
			"Archive File Error: Compressed data for entry {} extends into the central directory.", entry_number));
	}

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

[[nodiscard]] std::string parseFirstZipFilename(std::span<const Byte> archive_data) {
	constexpr std::size_t
		ZIP_LOCAL_HEADER_INDEX    = 8,
		ZIP_LOCAL_HEADER_MIN_SIZE = 30,
		FILENAME_LENGTH_INDEX     = ZIP_LOCAL_HEADER_INDEX + 26,
		EXTRA_LENGTH_INDEX        = ZIP_LOCAL_HEADER_INDEX + 28,
		FILENAME_INDEX            = ZIP_LOCAL_HEADER_INDEX + 30,
		FIRST_FILENAME_MIN_LENGTH = 4;

	constexpr auto ZIP_LOCAL_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 });

	if (archive_data.size() < ZIP_LOCAL_HEADER_INDEX + ZIP_LOCAL_HEADER_MIN_SIZE) {
		throw std::runtime_error("Archive File Error: ZIP header is truncated.");
	}

	if (!std::equal(
			ZIP_LOCAL_SIG.begin(),
			ZIP_LOCAL_SIG.end(),
			archive_data.begin() + ZIP_LOCAL_HEADER_INDEX)) {
		throw std::runtime_error("Archive File Error: Missing ZIP local file header signature.");
	}

	const std::size_t filename_length = readZipField<uint16_t>(archive_data, FILENAME_LENGTH_INDEX, "Archive File Error");
	const std::size_t extra_length    = readZipField<uint16_t>(archive_data, EXTRA_LENGTH_INDEX, "Archive File Error");

	if (filename_length < FIRST_FILENAME_MIN_LENGTH) {
		throw std::runtime_error(
			"File Error:\n\nName length of first file within archive is too short.\n"
			"Increase length (minimum 4 characters). Make sure it has a valid extension.");
	}

	const std::string filename(readZipStringView(
		archive_data,
		FILENAME_INDEX,
		filename_length,
		"Archive File Error: First filename length overflow.",
		"Archive File Error: First filename extends past archive bounds."));

	// Extra field bounds validation helps catch malformed local headers early.
	if (checkedAdd(
			FILENAME_INDEX + filename_length,
			extra_length,
			"Archive File Error: First ZIP header extra field length overflow.") > archive_data.size()) {
		throw std::runtime_error("Archive File Error: First ZIP header extra field extends past archive bounds.");
	}

	validateEntryName(
		filename,
		"Archive File Error: First filename contains unsupported control characters.",
		"Archive Security Error: Unsafe first archive entry path detected");

	return filename;
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
	constexpr std::size_t
		WRAP_PREFIX_SIZE  = 8,
		WRAP_TRAILER_SIZE = 4,
		EOCD_MIN_SIZE     = 22,
		// PKZIP spec: ZIP comment is capped at 65535 bytes, so the EOCD sits
		// no further than EOCD_MIN_SIZE + 65535 bytes from the archive trailer.
		EOCD_MAX_SEARCH   = EOCD_MIN_SIZE + static_cast<std::size_t>(UINT16_MAX);

	constexpr auto EOCD_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x05, 0x06 });

	if (archive_data.size() < WRAP_PREFIX_SIZE + WRAP_TRAILER_SIZE + EOCD_MIN_SIZE) {
		throw std::runtime_error("Archive File Error: Archive is too small.");
	}

	const std::size_t search_end    = archive_data.size() - WRAP_TRAILER_SIZE;
	const std::size_t distance_floor = (search_end > EOCD_MAX_SEARCH) ? search_end - EOCD_MAX_SEARCH : std::size_t{0};
	const std::size_t search_floor  = std::max(WRAP_PREFIX_SIZE, distance_floor);
	const std::size_t search_start  = search_end - EOCD_SIG.size();

	if (search_start < search_floor) {
		throw std::runtime_error("Archive File Error: End of central directory record not found.");
	}

	for (std::size_t pos = search_start; ; --pos) {
		if (std::equal(EOCD_SIG.begin(), EOCD_SIG.end(), archive_data.begin() + pos)
			&& pos + EOCD_MIN_SIZE <= archive_data.size()) {
			const std::size_t comment_length = readZipField<uint16_t>(archive_data, pos + 20, "Archive File Error");
			const std::size_t eocd_end = pos + EOCD_MIN_SIZE + comment_length;
			if (eocd_end == search_end) {
				return pos;
			}
		}
		if (pos == search_floor) {
			break;
		}
	}

	throw std::runtime_error("Archive File Error: End of central directory record not found.");
}

} // anonymous namespace

ArchiveMetadata analyzeArchive(std::span<const Byte> archive_data, bool is_zip_file) {
	ArchiveMetadata metadata{ .file_type = FileType::UNKNOWN_FILE_TYPE, .first_filename = parseFirstZipFilename(archive_data) };
	const std::string_view filename = metadata.first_filename;

	if (!is_zip_file) {
		if (filename != "META-INF/MANIFEST.MF" && filename != "META-INF/") {
			throw std::runtime_error("File Type Error: Archive does not appear to be a valid JAR file.");
		}
		metadata.file_type = FileType::JAR;
		return metadata;
	}

	// --- ZIP path: inspect the first record's filename/extension ---

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
		if (std::ranges::equal(EXTENSION_LIST[i], extension, [](unsigned char lhs, unsigned char rhs) {
				return std::tolower(lhs) == std::tolower(rhs);
			})) {
			// Indices 0..28 all map to VIDEO_AUDIO; 29+ map 1:1 with the enum.
			metadata.file_type = static_cast<FileType>(std::max(i, static_cast<std::size_t>(FileType::VIDEO_AUDIO)));
			return metadata;
		}
	}

	return metadata;
}

void validateArchiveEntryPaths(std::span<const Byte> archive_data) {
	constexpr std::size_t
		WRAP_PREFIX_SIZE          = 8,
		LOCAL_RECORD_MIN_SIZE     = 30,
		LOCAL_RECORD_NAME_INDEX   = 30,
		CENTRAL_RECORD_MIN_SIZE   = 46,
		CENTRAL_RECORD_NAME_INDEX = 46,
		CENTRAL_VERSION_MADE_BY   = 4,
		CENTRAL_FLAGS_OFFSET      = 8,
		CENTRAL_CRC32            = 16,
		CENTRAL_COMPRESSED_SIZE   = 20,
		CENTRAL_UNCOMPRESSED_SIZE = 24,
		CENTRAL_DISK_START        = 34,
		CENTRAL_LOCAL_OFFSET      = 42;

	constexpr auto
		ZIP_LOCAL_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 }),
		CENTRAL_SIG   = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });

	const std::size_t eocd_index = findEndOfCentralDirectory(archive_data);

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
		WRAP_PREFIX_SIZE,
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

	std::size_t cursor = central_start;
	std::uint64_t total_uncompressed = 0;
	std::unordered_set<std::string> seen_entries;
	std::unordered_set<std::string> file_entries;
	std::unordered_set<std::string> directory_entries;
	std::vector<LocalEntrySpan> local_spans;
	seen_entries.reserve(total_records);
	file_entries.reserve(total_records);
	directory_entries.reserve(total_records);
	local_spans.reserve(total_records);

	for (uint16_t i = 0; i < total_records; ++i) {
		if (cursor > archive_data.size() || CENTRAL_RECORD_MIN_SIZE > archive_data.size() - cursor) {
			throw std::runtime_error("Archive File Error: Truncated central directory file header.");
		}

		if (!std::equal(CENTRAL_SIG.begin(), CENTRAL_SIG.end(), archive_data.begin() + cursor)) {
			throw std::runtime_error("Archive File Error: Invalid central directory file header signature.");
		}

		const uint16_t version_made_by = readZipField<uint16_t>(archive_data, cursor + CENTRAL_VERSION_MADE_BY, "Archive File Error");
		const uint16_t flags = readZipField<uint16_t>(archive_data, cursor + CENTRAL_FLAGS_OFFSET, "Archive File Error");
		const uint32_t crc32 = readZipField<uint32_t>(archive_data, cursor + CENTRAL_CRC32, "Archive File Error");
		const uint32_t compressed_size = readZipField<uint32_t>(archive_data, cursor + CENTRAL_COMPRESSED_SIZE, "Archive File Error");
		const uint32_t uncompressed_size = readZipField<uint32_t>(archive_data, cursor + CENTRAL_UNCOMPRESSED_SIZE, "Archive File Error");
		const std::size_t name_length = readZipField<uint16_t>(archive_data, cursor + 28, "Archive File Error");
		const std::size_t extra_length = readZipField<uint16_t>(archive_data, cursor + 30, "Archive File Error");
		const std::size_t comment_length = readZipField<uint16_t>(archive_data, cursor + 32, "Archive File Error");
		const uint16_t entry_disk_start = readZipField<uint16_t>(archive_data, cursor + CENTRAL_DISK_START, "Archive File Error");
		const uint32_t external_attributes = readZipField<uint32_t>(archive_data, cursor + 38, "Archive File Error");
		const std::size_t local_header_offset = readZipField<uint32_t>(archive_data, cursor + CENTRAL_LOCAL_OFFSET, "Archive File Error");
		if (entry_disk_start != 0) {
			throw std::runtime_error(std::format(
				"Archive File Error: Multi-disk local header reference on entry {} is not supported.", i + 1));
		}

		const std::size_t name_start = checkedAdd(
			cursor,
			CENTRAL_RECORD_NAME_INDEX,
			"Archive File Error: Central directory filename offset overflow.");
		const std::string_view entry_name = readZipStringView(
			archive_data,
			name_start,
			name_length,
			"Archive File Error: Central directory filename length overflow.",
			"Archive File Error: Central directory filename exceeds archive bounds.");
		validateEntryName(
			entry_name,
			"Archive File Error: Entry",
			"Archive Security Error: Unsafe archive entry path detected",
			i + 1);
		validateEntryAttributes(version_made_by, flags, external_attributes, entry_name, i + 1);
		validateEntrySizeMetadata(compressed_size, uncompressed_size, total_uncompressed, entry_name.ends_with('/'), i + 1);
		validateEntryPathCollision(entry_name, seen_entries, file_entries, directory_entries, i + 1);

		const std::size_t local_header_start = checkedAdd(
			WRAP_PREFIX_SIZE,
			local_header_offset,
			"Archive File Error: Local file header offset overflow.");
		if (local_header_start >= central_start) {
			throw std::runtime_error("Archive File Error: Local file header points inside the central directory.");
		}
		if (local_header_start > archive_data.size()
			|| LOCAL_RECORD_MIN_SIZE > archive_data.size() - local_header_start) {
			throw std::runtime_error("Archive File Error: Truncated local file header.");
		}
		if (!std::equal(ZIP_LOCAL_SIG.begin(), ZIP_LOCAL_SIG.end(), archive_data.begin() + local_header_start)) {
			throw std::runtime_error(std::format(
				"Archive File Error: Invalid local file header signature for entry {}.", i + 1));
		}

		const std::size_t local_name_length = readZipField<uint16_t>(archive_data, local_header_start + 26, "Archive File Error");
		const std::size_t local_extra_length = readZipField<uint16_t>(archive_data, local_header_start + 28, "Archive File Error");
		const std::size_t local_name_start = checkedAdd(
			local_header_start,
			LOCAL_RECORD_NAME_INDEX,
			"Archive File Error: Local filename offset overflow.");
		const std::size_t local_record_end = checkedAdd(
			checkedAdd(
				local_name_start,
				local_name_length,
				"Archive File Error: Local filename length overflow."),
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
		validateEntryName(
			local_entry_name,
			"Archive File Error: Local entry",
			"Archive Security Error: Unsafe local archive entry path detected",
			i + 1);
		if (local_entry_name != entry_name) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Local and central directory names differ for entry {}.", i + 1));
		}
		validateLocalEntryPayload(
			archive_data,
			local_header_start,
			local_record_end,
			central_start,
			flags,
			crc32,
			compressed_size,
			uncompressed_size,
			local_spans,
			i + 1);

		const std::size_t record_size = checkedAdd(
			CENTRAL_RECORD_MIN_SIZE,
			checkedAdd(
				name_length,
				checkedAdd(
					extra_length,
					comment_length,
					"Archive File Error: Central directory metadata length overflow."),
				"Archive File Error: Central directory metadata length overflow."),
			"Archive File Error: Central directory record size overflow.");
		if (record_size > archive_data.size() - cursor) {
			throw std::runtime_error("Archive File Error: Central directory entry exceeds archive bounds.");
		}
		if (record_size > central_end - cursor) {
			throw std::runtime_error("Archive File Error: Central directory entry exceeds declared directory size.");
		}

		cursor = checkedAdd(
			cursor,
			record_size,
			"Archive File Error: Central directory cursor overflow.");
	}

	if (cursor != central_end) {
		throw std::runtime_error("Archive File Error: Central directory size does not match parsed records.");
	}
	validateLocalEntrySpans(local_spans);
}
