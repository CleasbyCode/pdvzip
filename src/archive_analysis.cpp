#include "pdvzip.h"

std::string toLowerCase(std::string str) {
	std::ranges::transform(str, str.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return str;
}

FileType determineFileType(std::span<const Byte> archive_data, bool isZipFile) {
	constexpr std::size_t
		FIRST_FILENAME_LENGTH_INDEX = 0x22,
		FIRST_FILENAME_INDEX        = 0x26,
		FIRST_FILENAME_MIN_LENGTH   = 4;

	const std::size_t filename_length = archive_data[FIRST_FILENAME_LENGTH_INDEX];

	if (filename_length < FIRST_FILENAME_MIN_LENGTH) {
		throw std::runtime_error(
			"File Error:\n\nName length of first file within archive is too short.\n"
			"Increase length (minimum 4 characters). Make sure it has a valid extension.");
	}

	const std::string filename =
		archive_data
		| std::views::drop(FIRST_FILENAME_INDEX)
		| std::views::take(filename_length)
		| std::ranges::to<std::string>();

	if (!isZipFile) {
		if (filename != "META-INF/MANIFEST.MF" && filename != "META-INF/") {
			throw std::runtime_error("File Type Error: Archive does not appear to be a valid JAR file.");
		}
		return FileType::JAR;
	}

	// --- ZIP path: inspect the first record's filename/extension ---

	const Byte last_char = archive_data[FIRST_FILENAME_INDEX + filename_length - 1];
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
		const Byte second_last = archive_data[FIRST_FILENAME_INDEX + filename_length - 2];
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
	constexpr std::size_t
		LENGTH_INDEX = 0x22,
		NAME_INDEX   = 0x26;

	const std::size_t length = archive_data[LENGTH_INDEX];

	return archive_data
		| std::views::drop(NAME_INDEX)
		| std::views::take(length)
		| std::ranges::to<std::string>();
}
