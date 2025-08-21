#include "fileChecks.h"
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <set>
#include <filesystem>
#include <fstream> 
#include <array>
#include <iostream>

bool hasValidFilename(const std::string& filename) {
	return std::all_of(filename.begin(), filename.end(), 
        	[](char c) { return std::isalnum(c) || c == '.' || c == '/' || c == '\\' || c == '-' || c == '_' || c == '@' || c == '%'; });
}

bool hasValidArchiveExtension(const std::string& ext) {
    	return ext == ".zip" || ext == ".jar";
}

bool hasValidImageExtension(const std::string& ext) {
	return ext == ".png";
}

void validateFiles(std::string& image, std::string& archive, uintmax_t& image_size, uintmax_t& archive_size, std::vector<uint8_t>& image_vec, std::vector<uint8_t>& archive_vec) {
	std::filesystem::path image_path(image);
	std::filesystem::path archive_path(archive);

    	std::string 
    		image_ext = image_path.extension().string(),
    		archive_ext = archive_path.extension().string();

	if (!hasValidImageExtension(image_ext)) {
        	throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".png\".");
    	}
    	
    	if (!hasValidArchiveExtension(archive_ext)) {
        	throw std::runtime_error("Archive File Error: Invalid file extension. Only expecting \".zip or .jar\" archive extensions.");
    	}

	if (!std::filesystem::exists(image_path)) {
        	throw std::runtime_error("Image File Error: File not found.");
    	}
    	
    	if (!std::filesystem::exists(archive_path) || !std::filesystem::is_regular_file(archive_path)) {
        	throw std::runtime_error("Archive File Error: File not found or not a regular file.");
    	}
    	
	if (!hasValidFilename(image_path.string()) || !hasValidFilename(archive_path.string())) {
            	throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        }
	
	std::ifstream image_ifs(image_path, std::ios::binary);	
	std::ifstream archive_ifs(archive_path, std::ios::binary);	
	
        if (!image_ifs) {
        	throw std::runtime_error("Read File Error: Unable to read image file. Check the filename and try again.");
	}
	
	if(!archive_ifs) {
		throw std::runtime_error("Read File Error: Unable to read archive file. Check the filename and try again.");
	}

	image_size = std::filesystem::file_size(image_path);
	archive_size = std::filesystem::file_size(archive_path);
	
	constexpr uintmax_t MAX_SIZE = 2ULL * 1024 * 1024 * 1024;   // 2GB (cover image + data file)
    	
    	const uintmax_t COMBINED_FILE_SIZE = image_size + archive_size;
    	
    	constexpr uint8_t 
    		MINIMUM_IMAGE_SIZE = 87,
    		MINIMUM_ARCHIVE_SIZE = 30;

    	if (MINIMUM_IMAGE_SIZE > image_size) {
        	throw std::runtime_error("Image File Error: Invalid size.");
    	}
	
	if (MINIMUM_ARCHIVE_SIZE > archive_size) {
		throw std::runtime_error("Archive File Error: Invalid file size.");
	}
	
	if (COMBINED_FILE_SIZE > MAX_SIZE) {
   		throw std::runtime_error("File Size Error: Combined size of cover image and archive file exceeds maximum size limit.");
   	}
	
	std::vector<uint8_t> tmp_vec(image_size);
	
	image_ifs.read(reinterpret_cast<char*>(tmp_vec.data()), image_size);
	image_ifs.close();
	
	image_vec.swap(tmp_vec);
	std::vector<uint8_t>().swap(tmp_vec);
	
	constexpr std::array<uint8_t, 8>
		PNG_SIG 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(PNG_SIG.begin(), PNG_SIG.end(), image_vec.begin()) || !std::equal(PNG_IEND_SIG.begin(), PNG_IEND_SIG.end(), image_vec.end() - 8)) {
        	throw std::runtime_error("\nImage File Error: Signature check failure. Not a valid PNG image.\n\n");
    	}
    	
    	tmp_vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };
    	
    	tmp_vec.resize(tmp_vec.size() + archive_size);
    	
    	constexpr uint8_t 
		INSERT_INDEX = 0x08,
		INDEX_DIFF = 8;
    	
    	archive_ifs.read(reinterpret_cast<char*>(tmp_vec.data() + INSERT_INDEX), archive_size);
	archive_ifs.close();
	
	archive_vec.swap(tmp_vec);
	std::vector<uint8_t>().swap(tmp_vec);
	
	constexpr std::array<uint8_t, 4> ARCHIVE_SIG { 0x50, 0x4B, 0x03, 0x04 };
	
	if (!std::equal(ARCHIVE_SIG.begin(), ARCHIVE_SIG.end(), std::begin(archive_vec) + INDEX_DIFF)) {
		throw std::runtime_error("Archive File Error: Signature check failure. Not a valid archive file.");
	}
}
