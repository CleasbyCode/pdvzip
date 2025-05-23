#include "fileChecks.h"
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <cstdint>
#include <filesystem>

bool hasValidArchiveExtension(const std::string& ext) {
    	return ext == ".zip" || ext == ".jar";
}

bool hasValidImageExtension(const std::string& ext) {
	return ext == ".png";
}

bool hasValidFilename(const std::string& filename) {
	return std::all_of(filename.begin(), filename.end(), 
        	[](char c) { return std::isalnum(c) || c == '.' || c == '/' || c == '\\' || c == '-' || c == '_' || c == '@' || c == '%'; });
}

void validateFiles(const std::string& image_file, const std::string& archive_file) {
	std::filesystem::path image_path(image_file), archive_path(archive_file);

    	std::string 
		image_ext = image_path.extension().string(),
		archive_ext = archive_path.extension().string();

    	if (!hasValidArchiveExtension(archive_ext)) {
        	throw std::runtime_error("Archive File Error: Invalid file extension. Only expecting \".zip or .jar\" archive extensions.");
    	}
	
	if (!hasValidImageExtension(image_ext)) {
        	throw std::runtime_error("Image File Error: Invalid file extension. Only expecting \".png\" image extension.");
    	}

    	if (!std::filesystem::exists(image_path)) {
        	throw std::runtime_error("Image File Error: File not found. Check the filename and try again.");
    	}

    	if (!std::filesystem::exists(archive_path)) {
        	throw std::runtime_error("Archive File Error: File not found. Check the filename and try again.");
    	}

    	constexpr uint8_t 
		MINIMUM_IMAGE_SIZE = 87,
		MINIMUM_ARCHIVE_SIZE = 30;

    	if (MINIMUM_IMAGE_SIZE > std::filesystem::file_size(image_path)) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}

	if (MINIMUM_ARCHIVE_SIZE > std::filesystem::file_size(archive_path)) {
		throw std::runtime_error("Archive File Error: Invalid file size.");
	}

    	constexpr uintmax_t MAX_SIZE_DEFAULT = 2ULL * 1024 * 1024 * 1024;   // 2GB (cover image + data file)

    	const uintmax_t COMBINED_FILE_SIZE = std::filesystem::file_size(archive_path) + std::filesystem::file_size(image_path);
	
   	if (COMBINED_FILE_SIZE > MAX_SIZE_DEFAULT) {
   		throw std::runtime_error("File Size Error: Combined size of cover image and archive file exceeds maximum size limit.");
   	}
}
