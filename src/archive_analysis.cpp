#include "pdvzip.h"

namespace {

[[nodiscard]] uint16_t readLe16(std::span<const Byte> data, std::size_t index) {
	if (index > data.size() || 2 > (data.size() - index)) {
		throw std::runtime_error("Archive File Error: Truncated ZIP local header.");
	}

	return static_cast<uint16_t>(data[index])
	     | (static_cast<uint16_t>(data[index + 1]) << 8);
}

[[nodiscard]] uint32_t readLe32(std::span<const Byte> data, std::size_t index) {
	if (index > data.size() || 4 > (data.size() - index)) {
		throw std::runtime_error("Archive File Error: Truncated ZIP record.");
	}

	return static_cast<uint32_t>(data[index])
	     | (static_cast<uint32_t>(data[index + 1]) << 8)
	     | (static_cast<uint32_t>(data[index + 2]) << 16)
	     | (static_cast<uint32_t>(data[index + 3]) << 24);
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

	const std::size_t filename_length = readLe16(archive_data, FILENAME_LENGTH_INDEX);
	const std::size_t extra_length    = readLe16(archive_data, EXTRA_LENGTH_INDEX);

	if (filename_length < FIRST_FILENAME_MIN_LENGTH) {
		throw std::runtime_error(
			"File Error:\n\nName length of first file within archive is too short.\n"
			"Increase length (minimum 4 characters). Make sure it has a valid extension.");
	}

	const std::size_t filename_end = FILENAME_INDEX + filename_length;
	if (filename_end > archive_data.size()) {
		throw std::runtime_error("Archive File Error: First filename extends past archive bounds.");
	}

	// Extra field bounds validation helps catch malformed local headers early.
	if (filename_end + extra_length > archive_data.size()) {
		throw std::runtime_error("Archive File Error: First ZIP header extra field extends past archive bounds.");
	}

	std::string filename(
		reinterpret_cast<const char*>(archive_data.data() + FILENAME_INDEX),
		filename_length);

	if (filename.find('\0') != std::string::npos) {
		throw std::runtime_error("Archive File Error: First filename contains invalid NUL bytes.");
	}

	return filename;
}

[[nodiscard]] bool isUnsafeEntryPath(std::string_view path) {
	if (path.empty()) {
		return true;
	}

	const auto has_drive_prefix = [&](std::string_view p) {
		return p.size() >= 2
			&& std::isalpha(static_cast<unsigned char>(p[0]))
			&& p[1] == ':';
	};

	if (path[0] == '/' || path[0] == '\\') {
		return true;
	}
	if (has_drive_prefix(path)) {
		return true;
	}
	if (path.size() >= 2 && ((path[0] == '/' && path[1] == '/') || (path[0] == '\\' && path[1] == '\\'))) {
		return true;
	}

	std::size_t segment_start = 0;
	for (std::size_t i = 0; i <= path.size(); ++i) {
		const bool at_separator = (i < path.size() && (path[i] == '/' || path[i] == '\\'));
		const bool at_end = (i == path.size());
		if (!at_separator && !at_end) {
			continue;
		}

		const std::string_view segment = path.substr(segment_start, i - segment_start);
		if (segment == ".."sv) {
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
		EOCD_MIN_SIZE     = 22;

	constexpr auto EOCD_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x05, 0x06 });

	if (archive_data.size() < WRAP_PREFIX_SIZE + WRAP_TRAILER_SIZE + EOCD_MIN_SIZE) {
		throw std::runtime_error("Archive File Error: Archive is too small.");
	}

	const std::size_t search_end = archive_data.size() - WRAP_TRAILER_SIZE;
	for (std::size_t pos = search_end - EOCD_SIG.size(); pos + 1 > WRAP_PREFIX_SIZE; --pos) {
		if (!std::equal(EOCD_SIG.begin(), EOCD_SIG.end(), archive_data.begin() + pos)) {
			continue;
		}

		if (pos + EOCD_MIN_SIZE > archive_data.size()) {
			continue;
		}

		const std::size_t comment_length = readLe16(archive_data, pos + 20);
		const std::size_t eocd_end = pos + EOCD_MIN_SIZE + comment_length;
		if (eocd_end > search_end) {
			continue;
		}

		return pos;
	}

	throw std::runtime_error("Archive File Error: End of central directory record not found.");
}

} // anonymous namespace

std::string toLowerCase(std::string str) {
	std::ranges::transform(str, str.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return str;
}

FileType determineFileType(std::span<const Byte> archive_data, bool isZipFile) {
	const std::string filename = parseFirstZipFilename(archive_data);

	if (!isZipFile) {
		if (filename != "META-INF/MANIFEST.MF" && filename != "META-INF/") {
			throw std::runtime_error("File Type Error: Archive does not appear to be a valid JAR file.");
		}
		return FileType::JAR;
	}

	// --- ZIP path: inspect the first record's filename/extension ---

	const Byte last_char = static_cast<Byte>(filename.back());
	const std::size_t dot_pos = filename.rfind('.');
	const std::string extension = (dot_pos != std::string::npos)
		? filename.substr(dot_pos + 1)
		: "?";

	// Check for folders (entries ending with '/').
	if (extension == "?") {
		return (last_char == '/') ? FileType::FOLDER : FileType::LINUX_EXECUTABLE;
	}

	// A name with a dot could still be a folder if it ends with '/'.
	if (last_char == '/') {
		const Byte second_last = static_cast<Byte>(filename[filename.size() - 2]);
		if (second_last == '.') {
			throw std::runtime_error("ZIP File Error: Invalid folder name within ZIP archive.");
		}
		return FileType::FOLDER;
	}

	// Match extension against the known list.
	const std::string lower_ext = toLowerCase(extension);
	for (std::size_t i = 0; i < EXTENSION_LIST.size(); ++i) {
		if (EXTENSION_LIST[i] == lower_ext) {
			// Indices 0..28 all map to VIDEO_AUDIO; 29+ map 1:1 with the enum.
			return static_cast<FileType>(std::max(i, static_cast<std::size_t>(FileType::VIDEO_AUDIO)));
		}
	}

	return FileType::UNKNOWN_FILE_TYPE;
}

std::string getArchiveFirstFilename(std::span<const Byte> archive_data) {
	return parseFirstZipFilename(archive_data);
}

void validateArchiveEntryPaths(std::span<const Byte> archive_data) {
	constexpr std::size_t
		WRAP_PREFIX_SIZE          = 8,
		CENTRAL_RECORD_MIN_SIZE   = 46,
		CENTRAL_RECORD_NAME_INDEX = 46;

	constexpr auto CENTRAL_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });

	const std::size_t eocd_index = findEndOfCentralDirectory(archive_data);

	const uint16_t total_records = readLe16(archive_data, eocd_index + 10);
	const uint32_t central_size = readLe32(archive_data, eocd_index + 12);
	const uint32_t central_offset = readLe32(archive_data, eocd_index + 16);

	if (total_records == 0) {
		throw std::runtime_error("Archive File Error: Archive contains no central directory entries.");
	}
	if (total_records == UINT16_MAX || central_size == UINT32_MAX || central_offset == UINT32_MAX) {
		throw std::runtime_error("Archive File Error: ZIP64 archives are not supported.");
	}

	const std::size_t central_start = WRAP_PREFIX_SIZE + static_cast<std::size_t>(central_offset);
	const std::size_t central_end = central_start + static_cast<std::size_t>(central_size);

	if (central_start > archive_data.size() || central_end > archive_data.size() || central_end > eocd_index) {
		throw std::runtime_error("Archive File Error: Central directory bounds are invalid.");
	}

	std::size_t cursor = central_start;

	for (uint16_t i = 0; i < total_records; ++i) {
		if (cursor > archive_data.size() || CENTRAL_RECORD_MIN_SIZE > archive_data.size() - cursor) {
			throw std::runtime_error("Archive File Error: Truncated central directory file header.");
		}

		if (!std::equal(CENTRAL_SIG.begin(), CENTRAL_SIG.end(), archive_data.begin() + cursor)) {
			throw std::runtime_error("Archive File Error: Invalid central directory file header signature.");
		}

		const std::size_t name_length = readLe16(archive_data, cursor + 28);
		const std::size_t extra_length = readLe16(archive_data, cursor + 30);
		const std::size_t comment_length = readLe16(archive_data, cursor + 32);

		const std::size_t name_start = cursor + CENTRAL_RECORD_NAME_INDEX;
		const std::size_t name_end = name_start + name_length;
		if (name_end > archive_data.size()) {
			throw std::runtime_error("Archive File Error: Central directory filename exceeds archive bounds.");
		}

		const std::string entry_name(
			reinterpret_cast<const char*>(archive_data.data() + name_start),
			name_length);

		if (entry_name.find('\0') != std::string::npos) {
			throw std::runtime_error(std::format(
				"Archive File Error: Entry {} contains invalid NUL bytes.", i + 1));
		}
		if (isUnsafeEntryPath(entry_name)) {
			throw std::runtime_error(std::format(
				"Archive Security Error: Unsafe archive entry path detected: \"{}\".", entry_name));
		}

		const std::size_t record_size = CENTRAL_RECORD_MIN_SIZE + name_length + extra_length + comment_length;
		if (record_size > archive_data.size() - cursor) {
			throw std::runtime_error("Archive File Error: Central directory entry exceeds archive bounds.");
		}

		cursor += record_size;
	}
}
