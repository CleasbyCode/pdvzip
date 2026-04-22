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

[[nodiscard]] std::string_view readZipStringView(std::span<const Byte> data, std::size_t start, std::size_t length,
                                                 std::string_view overflow_error, std::string_view bounds_error) {
	const std::size_t end = checkedAdd(start, length, overflow_error);
	if (end > data.size()) {
		throw std::runtime_error(std::string(bounds_error));
	}
	return std::string_view(reinterpret_cast<const char*>(data.data() + start), length);
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path);

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
		if (path.substr(segment_start, i - segment_start) == ".."sv) {
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
			if (eocd_end <= search_end) {
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
		CENTRAL_LOCAL_OFFSET      = 42;

	constexpr auto
		ZIP_LOCAL_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 }),
		CENTRAL_SIG   = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });

	const std::size_t eocd_index = findEndOfCentralDirectory(archive_data);

	const uint16_t total_records = readZipField<uint16_t>(archive_data, eocd_index + 10, "Archive File Error");
	const uint32_t central_size = readZipField<uint32_t>(archive_data, eocd_index + 12, "Archive File Error");
	const uint32_t central_offset = readZipField<uint32_t>(archive_data, eocd_index + 16, "Archive File Error");

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

	std::size_t cursor = central_start;
	// string_view keys reference bytes inside archive_data, which is stable
	// for the lifetime of this function. Skips per-entry heap allocation.
	std::unordered_set<std::string_view> seen_entries;
	seen_entries.reserve(total_records);

	for (uint16_t i = 0; i < total_records; ++i) {
		if (cursor > archive_data.size() || CENTRAL_RECORD_MIN_SIZE > archive_data.size() - cursor) {
			throw std::runtime_error("Archive File Error: Truncated central directory file header.");
		}

		if (!std::equal(CENTRAL_SIG.begin(), CENTRAL_SIG.end(), archive_data.begin() + cursor)) {
			throw std::runtime_error("Archive File Error: Invalid central directory file header signature.");
		}

		const std::size_t name_length = readZipField<uint16_t>(archive_data, cursor + 28, "Archive File Error");
		const std::size_t extra_length = readZipField<uint16_t>(archive_data, cursor + 30, "Archive File Error");
		const std::size_t comment_length = readZipField<uint16_t>(archive_data, cursor + 32, "Archive File Error");
		const std::size_t local_header_offset = readZipField<uint32_t>(archive_data, cursor + CENTRAL_LOCAL_OFFSET, "Archive File Error");

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
		if (!seen_entries.insert(entry_name).second) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Duplicate archive entry path detected: \"{}\".", entry_name));
		}

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
}
