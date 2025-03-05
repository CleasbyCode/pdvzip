int pdvZip(const std::string& IMAGE_FILENAME, const std::string& ARCHIVE_FILENAME, ArchiveType thisArchiveType) {
	
	std::ifstream
		image_file_ifs(IMAGE_FILENAME, std::ios::binary),
		archive_file_ifs(ARCHIVE_FILENAME, std::ios::binary);

	if (!image_file_ifs || !archive_file_ifs ) {
		std::cerr << "\nRead File Error: Unable to read " 
			<< (!image_file_ifs 
				? "image file" 
				: "archive file") 
			<< ".\n\n";
		return 1;
	}
	
	const uintmax_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		ARCHIVE_FILE_SIZE = std::filesystem::file_size(ARCHIVE_FILENAME);

	std::vector<uint8_t> image_vec;
	image_vec.resize(IMAGE_FILE_SIZE); 
	
	image_file_ifs.read(reinterpret_cast<char*>(image_vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr std::array<uint8_t, 8>
		PNG_SIG		{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(PNG_SIG.begin(), PNG_SIG.end(), image_vec.begin()) || !std::equal(PNG_IEND_SIG.begin(), PNG_IEND_SIG.end(), image_vec.end() - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. Not a valid PNG image.\n\n";
			return 1;
    	}
	
	// A selection of "problem characters" may appear within the cover image's width/height dimension fields of the IHDR chunk or within the 4-byte IHDR chunk's CRC field.
	// These characters will break the Linux extraction script. If any of these characters are detected, the program will attempt to decrease the width/height dimension size
	// of the image by 1 pixel value, repeated if necessary, until no problem characters are found within the dimension size fields or the IHDR chunk's CRC field.

	constexpr std::array<uint8_t, 7> LINUX_PROBLEM_CHARACTERS { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 }; // This list could grow...
	
	constexpr uint8_t IHDR_STOP_INDEX = 0x20;

	bool isBadImage = true;

	while (isBadImage) {
    		isBadImage = false;
    		for (uint8_t index = 0x12; index < IHDR_STOP_INDEX; ++index) { // Check IHDR chunk section where problem characters may occur.
        		if (std::find(LINUX_PROBLEM_CHARACTERS.begin(), LINUX_PROBLEM_CHARACTERS.end(), image_vec[index]) != LINUX_PROBLEM_CHARACTERS.end()) {
            			resizeImage(image_vec);
            			isBadImage = true;
            			break;
        		}
    		}
	}

	// Check cover image for valid image dimensions and color type values.
	constexpr uint8_t
		IMAGE_WIDTH_INDEX 	= 0x12,
		IMAGE_HEIGHT_INDEX 	= 0x16,
		IMAGE_COLOR_TYPE_INDEX 	= 0x19,
		MIN_DIMS 		= 68,
		INDEXED_COLOR 		= 3,
		TRUECOLOR_ALPHA		= 6,
		TRUECOLOR 		= 2,
		BYTE_LENGTH 		= 2;

	constexpr uint16_t
		MAX_TRUECOLOR_DIMS 	= 900,
		MAX_INDEXED_COLOR_DIMS 	= 4096;

	bool isBigEndian = true;

	const uint16_t
		IMAGE_WIDTH  = getByteValue(image_vec, IMAGE_WIDTH_INDEX, BYTE_LENGTH, isBigEndian),
		IMAGE_HEIGHT = getByteValue(image_vec, IMAGE_HEIGHT_INDEX, BYTE_LENGTH, isBigEndian);

	const uint8_t IMAGE_COLOR_TYPE = image_vec[IMAGE_COLOR_TYPE_INDEX] == TRUECOLOR_ALPHA ? TRUECOLOR : image_vec[IMAGE_COLOR_TYPE_INDEX];

	bool hasValidColorType = (IMAGE_COLOR_TYPE == INDEXED_COLOR || IMAGE_COLOR_TYPE == TRUECOLOR);

	auto checkDimensions = [&](uint8_t COLOR_TYPE, uint16_t MAX_DIMS) {
		return (IMAGE_COLOR_TYPE == COLOR_TYPE &&
            		IMAGE_WIDTH <= MAX_DIMS && IMAGE_HEIGHT <= MAX_DIMS &&
            		IMAGE_WIDTH >= MIN_DIMS && IMAGE_HEIGHT >= MIN_DIMS);
	};

	bool hasValidDimensions = checkDimensions(TRUECOLOR, MAX_TRUECOLOR_DIMS) || checkDimensions(INDEXED_COLOR, MAX_INDEXED_COLOR_DIMS);

	if (!hasValidColorType || !hasValidDimensions) {
    		std::cerr << "\nImage File Error: ";
    		if (!hasValidColorType) {
        		std::cerr << "Color type of cover image is not supported.\n\nSupported types: PNG-32/24 (Truecolor) or PNG-8 (Indexed-Color).";
    		} else {
        		std::cerr << "Dimensions of cover image are not within the supported range.\n\nSupported ranges:\n - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]";
    		}
    		std::cerr << "\n\n";
    		return 1;
	}

	copyEssentialPngChunks(image_vec);

	const uint32_t IMAGE_VEC_SIZE = static_cast<uint32_t>(image_vec.size()); // New size after chunks removed.

	std::vector<uint8_t>archive_vec { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };
	archive_vec.resize(archive_vec.size() + ARCHIVE_FILE_SIZE);
				    
	archive_file_ifs.read(reinterpret_cast<char*>(archive_vec.data() + 8), ARCHIVE_FILE_SIZE);
	archive_file_ifs.close();

	constexpr std::array<uint8_t, 4> ARCHIVE_SIG { 0x50, 0x4B, 0x03, 0x04 };
	
	if (!std::equal(ARCHIVE_SIG.begin(), ARCHIVE_SIG.end(), std::begin(archive_vec) + 8)) {
		std::cerr << "\nArchive File Error: Signature check failure. Not a valid archive file.\n\n";
		return 1;
	}

	const uint32_t IDAT_CHUNK_ARCHIVE_FILE_SIZE = static_cast<uint32_t>(archive_vec.size());

	uint8_t 
		idat_chunk_length_index = 0,
		value_bit_length = 32;

	valueUpdater(archive_vec, idat_chunk_length_index, IDAT_CHUNK_ARCHIVE_FILE_SIZE - 12, value_bit_length);

	// The following section (~158 lines) completes and embeds the extraction script, based on the file type within the archive.
	constexpr uint8_t 
		ARC_RECORD_FIRST_FILENAME_MIN_LENGTH 	= 4,
		ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX 	= 0x22, 
		ARC_RECORD_FIRST_FILENAME_INDEX 	= 0x26;
	
	const uint8_t ARC_RECORD_FIRST_FILENAME_LENGTH = archive_vec[ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX];

	if (ARC_RECORD_FIRST_FILENAME_MIN_LENGTH > ARC_RECORD_FIRST_FILENAME_LENGTH) {
		std::cerr << "\nArchive File Error:\n\nName length of first file within archive is too short.\nIncrease its length (minimum 4 characters) and make sure it has a valid extension.\n\n";
		return 1;
	}

	constexpr std::array<const char*, 35> EXTENSION_LIST  { "mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
								"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
								"xvid", "pdf", "py", "ps1", "sh", "exe" };
						    
	const std::string ARC_RECORD_FIRST_FILENAME{ archive_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX, archive_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH };

	enum FileType { VIDEO_AUDIO = 29, PDF, PYTHON, POWERSHELL, BASH_SHELL, WINDOWS_EXECUTABLE, UNKNOWN_FILE_TYPE, FOLDER, LINUX_EXECUTABLE, JAR};

	bool isZipFile = (thisArchiveType == ArchiveType::ZIP);

	uint8_t extension_list_index = (isZipFile) ? 0 : JAR;
						    
	if (extension_list_index == JAR && (ARC_RECORD_FIRST_FILENAME != "META-INF/MANIFEST.MF" && ARC_RECORD_FIRST_FILENAME != "META-INF/")) {
		std::cerr << "\nFile Type Error: Archive file does not appear to be a valid JAR file.\n\n";
		return 1;
	}
	
	const size_t EXTENSION_POS = ARC_RECORD_FIRST_FILENAME.rfind('.');
	const std::string ARC_RECORD_FIRST_FILENAME_EXTENSION = (EXTENSION_POS != std::string::npos) ? ARC_RECORD_FIRST_FILENAME.substr(EXTENSION_POS + 1) : "?";
	
	// Deal with filenames that don't have extensions. Folders and Linux executables.
	if (isZipFile && ARC_RECORD_FIRST_FILENAME_EXTENSION  == "?") {
		extension_list_index = archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
	}
						    
	// Even though we found a period character, indicating a file extension, it could still be a folder that just has a "." somewhere within its name, check for it here.
	// Linux allows a zipped folder to have a "." for the last character of its name (e.g. "my_folder."), but this will cause issues with Windows, so also check for it here.
	if (isZipFile && extension_list_index != FOLDER && archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
		if (archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
			extension_list_index = FOLDER; 
		} else {
			std::cerr << "\nZIP File Error: Invalid folder name within ZIP archive.\n\n"; 
			return 1;
		}
	}
	
	// Try to match the file extension of the first file of the archive with the array list of file extensions (Extension_List).
	// This will determine what extraction script to embed within the image, so that it correctly deals with the file type.
	while (UNKNOWN_FILE_TYPE > extension_list_index) {
		if (EXTENSION_LIST[extension_list_index] == ARC_RECORD_FIRST_FILENAME_EXTENSION) {
			extension_list_index = VIDEO_AUDIO >= extension_list_index ? VIDEO_AUDIO : extension_list_index;
			break;
		}
		extension_list_index++;
	}

	std::string
		args_linux,
		args_windows,
		args;

	if ((extension_list_index > PDF && UNKNOWN_FILE_TYPE > extension_list_index) || extension_list_index == LINUX_EXECUTABLE || extension_list_index == JAR) {
		std::cout << "\nFor this file type, if required, you can provide command-line arguments here.\n";
		if (extension_list_index != WINDOWS_EXECUTABLE) {
			std::cout << "\nLinux: ";
			std::getline(std::cin, args_linux);
			args = args_linux;
		}
		if (extension_list_index != LINUX_EXECUTABLE) {
			std::cout << "\nWindows: ";
			std::getline(std::cin, args_windows);
			args = args_windows;
		}
	}

	std::vector<uint8_t> script_vec { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 
					  0x5F, 0x00, 0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00 };

	constexpr uint16_t MAX_SCRIPT_SIZE = 1500;

	script_vec.reserve(script_vec.size() + MAX_SCRIPT_SIZE);
	
	std::unordered_map<uint8_t, std::vector<uint16_t>> case_map = {

	// The single digit integer is the extraction script id (see Extraction_Scripts_Vec), the hex values are insert index locations
	// within the extraction script vector. We use these index locations to insert additional items into the script in order to complete it.

		{VIDEO_AUDIO,		{ 0, 0x1E4, 0x1C }}, 
		{PDF,			{ 1, 0x196, 0x1C }}, 
		{PYTHON,		{ 2, 0x10B, 0x101, 0xBC, 0x1C}},
		{POWERSHELL,		{ 3, 0x105, 0xFB, 0xB6, 0x33 }},
		{BASH_SHELL,		{ 4, 0x134, 0x132, 0x8E, 0x1C }},
		{WINDOWS_EXECUTABLE,	{ 5, 0x116, 0x114 }},
		{FOLDER,		{ 6, 0x149, 0x1C }},
		{LINUX_EXECUTABLE,	{ 7, 0x8E,  0x1C }},
		{JAR,			{ 8, 0xA6,  0x61 }},
		{UNKNOWN_FILE_TYPE,	{ 9, 0x127, 0x1C}} // Fallback/placeholder. Unknown file type, unmatched file extension case.
	};

	auto it = case_map.find(extension_list_index);

	if (it == case_map.end()) {
    		extension_list_index = UNKNOWN_FILE_TYPE;
	}

	std::vector<uint16_t> case_values_vec = (it != case_map.end()) ? it->second : case_map[extension_list_index];

	constexpr uint8_t EXTRACTION_SCRIPT_ELEMENT_INDEX = 0;
	const uint16_t EXTRACTION_SCRIPT = case_values_vec[EXTRACTION_SCRIPT_ELEMENT_INDEX];

	constexpr uint8_t SCRIPT_INDEX = 0x16;

	script_vec.insert(script_vec.begin() + SCRIPT_INDEX, extraction_scripts_vec[EXTRACTION_SCRIPT].begin(), extraction_scripts_vec[EXTRACTION_SCRIPT].end());

	if (extension_list_index == WINDOWS_EXECUTABLE || extension_list_index == LINUX_EXECUTABLE) {
		script_vec.insert(script_vec.begin() + case_values_vec[1], args.begin(), args.end());
		script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());	
	} else if (extension_list_index == JAR) {
		script_vec.insert(script_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
		script_vec.insert(script_vec.begin() + case_values_vec[2], args_linux.begin(), args_linux.end());
	} else if (extension_list_index > PDF && WINDOWS_EXECUTABLE > extension_list_index) { 
		script_vec.insert(script_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
		script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
		script_vec.insert(script_vec.begin() + case_values_vec[3], args_linux.begin(), args_linux.end());
		script_vec.insert(script_vec.begin() + case_values_vec[4], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
	} else { 
		 script_vec.insert(script_vec.begin() + case_values_vec[1], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
		 script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
	}
	
	uint8_t 
		iccp_chunk_length_index = 0,
		iccp_chunk_length_first_byte_index = 3;
	
	uint16_t iccp_chunk_script_size = static_cast<uint16_t>(script_vec.size()) - 12;

	valueUpdater(script_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length);

	const uint8_t iccp_chunk_length_first_byte_value = script_vec[iccp_chunk_length_first_byte_index];

	// If a problem character (that breaks the Linux extraction script) is found within the first byte of the updated iccp chunk length field, 
	// insert a short string to the end of the iccp chunk to increase its length, avoiding the problem characters when chunk length is updated.

	if (std::find(LINUX_PROBLEM_CHARACTERS.begin(), LINUX_PROBLEM_CHARACTERS.end(), 
		iccp_chunk_length_first_byte_value) != LINUX_PROBLEM_CHARACTERS.end()) {
			const std::string INCREASE_CHUNK_LENGTH_STRING = "........";
			script_vec.insert(script_vec.begin() + iccp_chunk_script_size + 8, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
			iccp_chunk_script_size = static_cast<uint16_t>(script_vec.size()) - 12;
			valueUpdater(script_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length);
	}
	
	constexpr uint8_t
		ICCP_CHUNK_NAME_INDEX = 4,
		ICCP_CHUNK_INDEX = 0x21;

	if (iccp_chunk_script_size > MAX_SCRIPT_SIZE) {
		std::cerr << "\nScript Size Error: Extraction script exceeds size limit.\n\n";
		return 1;
	}

	const uint16_t ICCP_CHUNK_LENGTH = iccp_chunk_script_size + 4;

	const uint32_t ICCP_CHUNK_CRC = crcUpdate(&script_vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH);

	uint16_t iccp_chunk_crc_index = iccp_chunk_script_size + 8;

	valueUpdater(script_vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_bit_length);

	image_vec.insert((image_vec.begin() + ICCP_CHUNK_INDEX), script_vec.begin(), script_vec.end());
	image_vec.insert((image_vec.end() - 12), archive_vec.begin(), archive_vec.end());

	std::vector<uint8_t>().swap(script_vec);
	std::vector<uint8_t>().swap(archive_vec);
				 
	const uint32_t 
		LAST_IDAT_CHUNK_NAME_INDEX = IMAGE_VEC_SIZE + iccp_chunk_script_size + 4, 	// Important to use the old image size before the above inserts.
		COMPLETE_POLYGLOT_IMAGE_SIZE = static_cast<uint32_t>(image_vec.size());  	// Image size updated to include the inserts.

	adjustZipOffsets(image_vec, COMPLETE_POLYGLOT_IMAGE_SIZE, LAST_IDAT_CHUNK_NAME_INDEX);

	const uint32_t LAST_IDAT_CHUNK_CRC = crcUpdate(&image_vec[LAST_IDAT_CHUNK_NAME_INDEX], IDAT_CHUNK_ARCHIVE_FILE_SIZE - 8);
	
	uint32_t last_idat_chunk_crc_index = COMPLETE_POLYGLOT_IMAGE_SIZE - 16;
	
	value_bit_length = 32;
	valueUpdater(image_vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_bit_length);
	
	if (!writeFile(image_vec, COMPLETE_POLYGLOT_IMAGE_SIZE, isZipFile)) {
		return 1;
	}
	return 0;
}