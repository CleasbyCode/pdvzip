void checkFileSizes(const std::string& IMAGE_FILENAME, const std::string ZIP_FILENAME) {
	const size_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		ZIP_FILE_SIZE = std::filesystem::file_size(ZIP_FILENAME),
		COMBINED_FILE_SIZE = ZIP_FILE_SIZE + IMAGE_FILE_SIZE;

	constexpr uint32_t MAX_FILE_SIZE = 1094713344;
	constexpr uint8_t MIN_FILE_SIZE = 30;

	if (COMBINED_FILE_SIZE > MAX_FILE_SIZE 
		|| MIN_FILE_SIZE > IMAGE_FILE_SIZE
		|| MIN_FILE_SIZE > ZIP_FILE_SIZE) {
		
			std::cerr << "\nFile Size Error: " 
				<< (COMBINED_FILE_SIZE > MAX_FILE_SIZE 
					? "Combined size of image and ZIP file exceeds the maximum limit of 1GB"
        				: (MIN_FILE_SIZE > IMAGE_FILE_SIZE 
	        				? "Image is too small to be a valid PNG image" 
						: "ZIP file is too small to be a valid ZIP archive")) 	
				<< ".\n\n";
    			std::exit(EXIT_FAILURE);
	}
}
