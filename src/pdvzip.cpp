int pdvZip(const std::string& IMAGE_FILENAME, const std::string& ARCHIVE_FILENAME, ArchiveType thisArchiveType) {

	constexpr uint32_t COMBINED_MAX_FILE_SIZE = 2U * 1024U * 1024U * 1024U;	
	constexpr uint8_t MIN_FILE_SIZE = 30;
	
	const size_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		ARCHIVE_FILE_SIZE = std::filesystem::file_size(ARCHIVE_FILENAME),
		COMBINED_FILE_SIZE = ARCHIVE_FILE_SIZE + IMAGE_FILE_SIZE;

	if (COMBINED_FILE_SIZE > COMBINED_MAX_FILE_SIZE 
		|| MIN_FILE_SIZE > IMAGE_FILE_SIZE
		|| MIN_FILE_SIZE > ARCHIVE_FILE_SIZE) {
		std::cerr << "\nFile Size Error: " 
			<< (COMBINED_FILE_SIZE > COMBINED_MAX_FILE_SIZE 
				? "Combined size of image and archive file exceeds the maximum limit of 2GB"
        			: (MIN_FILE_SIZE > IMAGE_FILE_SIZE 
	        			? "Image is too small to be a valid PNG image" 
					: "Archive file is too small to be a valid archive")) 	
			<< ".\n\n";
    		return 1;
	}

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
	
	std::vector<uint8_t> Image_Vec;
	Image_Vec.resize(IMAGE_FILE_SIZE); 
	
	image_file_ifs.read(reinterpret_cast<char*>(Image_Vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr uint8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. Not a valid PNG image.\n\n";
			return 1;
    	}

	constexpr uint8_t
		LINUX_PROBLEM_CHARACTERS[] { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 }, 
		IHDR_STOP_INDEX = 0x20;

	bool isBadImage = true;

	while (isBadImage) {
    		isBadImage = false;
    		for (uint8_t index = 0x12; index < IHDR_STOP_INDEX; ++index) { 
        		if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS), Image_Vec[index]) != std::end(LINUX_PROBLEM_CHARACTERS)) {
            			resizeImage(Image_Vec);
            			isBadImage = true;
            			break;
        		}
    		}
	}

	constexpr uint8_t
		IMAGE_WIDTH_INDEX 	= 0x12,
		IMAGE_HEIGHT_INDEX 	= 0x16,
		IMAGE_COLOR_TYPE_INDEX 	= 0x19,
		MIN_DIMS 		= 68,
		INDEXED_COLOR 		= 3,
		TRUECOLOR 		= 2,
		BYTE_LENGTH 		= 2;

	constexpr uint16_t
		MAX_TRUECOLOR_DIMS 	= 900,
		MAX_INDEXED_COLOR_DIMS 	= 4096;

	const uint16_t
		IMAGE_WIDTH  = getByteValue(Image_Vec, IMAGE_WIDTH_INDEX, BYTE_LENGTH, true),
		IMAGE_HEIGHT = getByteValue(Image_Vec, IMAGE_HEIGHT_INDEX, BYTE_LENGTH, true);

	const uint8_t IMAGE_COLOR_TYPE = Image_Vec[IMAGE_COLOR_TYPE_INDEX] == 6 ? 2 : Image_Vec[IMAGE_COLOR_TYPE_INDEX];

	const bool VALID_COLOR_TYPE = (IMAGE_COLOR_TYPE == INDEXED_COLOR || IMAGE_COLOR_TYPE == TRUECOLOR);

	auto checkDimensions = [&](uint8_t COLOR_TYPE, uint16_t MAX_DIMS) {
		return (IMAGE_COLOR_TYPE == COLOR_TYPE &&
            		IMAGE_WIDTH <= MAX_DIMS && IMAGE_HEIGHT <= MAX_DIMS &&
            		IMAGE_WIDTH >= MIN_DIMS && IMAGE_HEIGHT >= MIN_DIMS);
	};

	const bool VALID_IMAGE_DIMS = checkDimensions(TRUECOLOR, MAX_TRUECOLOR_DIMS) || checkDimensions(INDEXED_COLOR, MAX_INDEXED_COLOR_DIMS);

	if (!VALID_COLOR_TYPE || !VALID_IMAGE_DIMS) {
    		std::cerr << "\nImage File Error: ";
    		if (!VALID_COLOR_TYPE) {
        		std::cerr << "Color type of cover image is not supported.\n\nSupported formats: PNG-32/24 (Truecolor) or PNG-8 (Indexed-Color).";
    		} else {
        		std::cerr << "Dimensions of cover image are not within the supported range.\n\nSupported ranges:\n - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]";
    		}
    		std::cerr << "\n\n";
    		return 1;
	}

	eraseChunks(Image_Vec);

	const uint32_t IMAGE_VEC_SIZE = static_cast<uint32_t>(Image_Vec.size()); 
				    
	std::vector<uint8_t>Idat_Archive_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };
	Idat_Archive_Vec.resize(Idat_Archive_Vec.size() + ARCHIVE_FILE_SIZE);
				    
	archive_file_ifs.read(reinterpret_cast<char*>(Idat_Archive_Vec.data() + 8), ARCHIVE_FILE_SIZE);
	archive_file_ifs.close();

	constexpr uint8_t ARCHIVE_SIG[] { 0x50, 0x4B, 0x03, 0x04 };
	
	if (!std::equal(std::begin(ARCHIVE_SIG), std::end(ARCHIVE_SIG), std::begin(Idat_Archive_Vec) + 8)) {
		std::cerr << "\nArchive File Error: Signature check failure. Not a valid archive file.\n\n";
		return 1;
	}

	const uint32_t IDAT_CHUNK_ARCHIVE_FILE_SIZE = static_cast<uint32_t>(Idat_Archive_Vec.size());

	uint8_t 
		idat_chunk_length_index{},
		value_bit_length = 32;

	valueUpdater(Idat_Archive_Vec, idat_chunk_length_index, IDAT_CHUNK_ARCHIVE_FILE_SIZE - 12, value_bit_length);

	constexpr uint8_t 
		ARC_RECORD_FIRST_FILENAME_MIN_LENGTH 	= 4,
		ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX 	= 0x22, 
		ARC_RECORD_FIRST_FILENAME_INDEX 	= 0x26;
	
	const uint8_t ARC_RECORD_FIRST_FILENAME_LENGTH = Idat_Archive_Vec[ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX];

	if (ARC_RECORD_FIRST_FILENAME_MIN_LENGTH > ARC_RECORD_FIRST_FILENAME_LENGTH) {
		std::cerr << "\nArchive File Error:\n\nName length of first file within archive is too short.\nIncrease its length (minimum 4 characters) and make sure it has a valid extension.\n\n";
		return 1;
	}

	constexpr const char* Extension_List[] { "mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi", "dsd", "divx", "f4v",
						 "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv", "xvid", "pdf", "py", "ps1", "sh", "exe"	};
						    
	const std::string ARC_RECORD_FIRST_FILENAME{ Idat_Archive_Vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX, Idat_Archive_Vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH };

	enum FileType { VIDEO_AUDIO = 29, PDF, PYTHON, POWERSHELL, BASH_SHELL, WINDOWS_EXECUTABLE, UNKNOWN_FILE_TYPE, FOLDER, LINUX_EXECUTABLE, JAR};

	bool isZipFile = (thisArchiveType == ArchiveType::ZIP);

	uint8_t extension_list_index = (isZipFile) ? 0 : JAR;
						    
	if (extension_list_index == JAR && (ARC_RECORD_FIRST_FILENAME != "META-INF/MANIFEST.MF" && ARC_RECORD_FIRST_FILENAME != "META-INF/")) {
		std::cerr << "\nFile Type Error: Archive file does not appear to be a valid JAR file.\n\n";
		return 1;
	}
	
	const size_t EXTENSION_POS = ARC_RECORD_FIRST_FILENAME.rfind('.');
	const std::string ARC_RECORD_FIRST_FILENAME_EXTENSION = (EXTENSION_POS != std::string::npos) ? ARC_RECORD_FIRST_FILENAME.substr(EXTENSION_POS + 1) : "?";
	
	if (isZipFile && ARC_RECORD_FIRST_FILENAME_EXTENSION  == "?") {
		extension_list_index = Idat_Archive_Vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
	}
						    
	if (isZipFile && extension_list_index != FOLDER && Idat_Archive_Vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
		if (Idat_Archive_Vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
			extension_list_index = FOLDER; 
		} else {
			std::cerr << "\nZIP File Error: Invalid folder name within ZIP archive.\n\n"; 
			return 1;
		}
	}
	
	while (UNKNOWN_FILE_TYPE > extension_list_index) {
		if (Extension_List[extension_list_index] == ARC_RECORD_FIRST_FILENAME_EXTENSION) {
			extension_list_index = VIDEO_AUDIO >= extension_list_index ? VIDEO_AUDIO : extension_list_index;
			break;
		}
		extension_list_index++;
	}

	std::string
		args_linux{},
		args_windows{},
		args{};

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

	std::vector<uint8_t> Iccp_Script_Vec { 	0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 
						0x5F, 0x00, 0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00 };

	constexpr uint16_t MAX_SCRIPT_SIZE = 1500;

	Iccp_Script_Vec.reserve(Iccp_Script_Vec.size() + MAX_SCRIPT_SIZE);
	
	std::unordered_map<uint8_t, std::vector<uint16_t>> case_map = {
		{VIDEO_AUDIO,		{ 0, 0x1E4, 0x1C }}, 
		{PDF,			{ 1, 0x196, 0x1C }}, 
		{PYTHON,		{ 2, 0x10B, 0x101, 0xBC, 0x1C}},
		{POWERSHELL,		{ 3, 0x105, 0xFB, 0xB6, 0x33 }},
		{BASH_SHELL,		{ 4, 0x134, 0x132, 0x8E, 0x1C }},
		{WINDOWS_EXECUTABLE,	{ 5, 0x116, 0x114 }},
		{FOLDER,		{ 6, 0x149, 0x1C }},
		{LINUX_EXECUTABLE,	{ 7, 0x8E,  0x1C }},
		{JAR,			{ 8, 0xA6,  0x61 }},
		{UNKNOWN_FILE_TYPE,	{ 9, 0x127, 0x1C}} 
	};

	auto it = case_map.find(extension_list_index);

	if (it == case_map.end()) {
    		extension_list_index = UNKNOWN_FILE_TYPE;
	}

	std::vector<uint16_t> Case_Values_Vec = (it != case_map.end()) ? it->second : case_map[extension_list_index];

	constexpr uint8_t EXTRACTION_SCRIPT_ELEMENT_INDEX = 0;
	const uint16_t EXTRACTION_SCRIPT = Case_Values_Vec[EXTRACTION_SCRIPT_ELEMENT_INDEX];

	constexpr uint8_t SCRIPT_INDEX = 0x16;

	Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + SCRIPT_INDEX, Extraction_Scripts_Vec[EXTRACTION_SCRIPT].begin(), Extraction_Scripts_Vec[EXTRACTION_SCRIPT].end());

	if (extension_list_index == WINDOWS_EXECUTABLE || extension_list_index == LINUX_EXECUTABLE) {
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args.begin(), args.end());
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());	
	} else if (extension_list_index == JAR) {
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args_windows.begin(), args_windows.end());
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], args_linux.begin(), args_linux.end());
	} else if (extension_list_index > PDF && WINDOWS_EXECUTABLE > extension_list_index) { 
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args_windows.begin(), args_windows.end());
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[3], args_linux.begin(), args_linux.end());
		Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[4], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
	} else { 
		 Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
		 Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
	}
	
	uint8_t 
		iccp_chunk_length_index = 0,
		iccp_chunk_length_first_byte_index = 3;
	
	uint16_t iccp_chunk_script_size = static_cast<uint16_t>(Iccp_Script_Vec.size()) - 12;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length);

	const uint8_t iccp_chunk_length_first_byte_value = Iccp_Script_Vec[iccp_chunk_length_first_byte_index];
					    
	if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS), 
		iccp_chunk_length_first_byte_value) != std::end(LINUX_PROBLEM_CHARACTERS)) {
			const std::string INCREASE_CHUNK_LENGTH_STRING = "........";
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + iccp_chunk_script_size + 8, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
			iccp_chunk_script_size = static_cast<uint16_t>(Iccp_Script_Vec.size()) - 12;
			valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length);
	}
	
	constexpr uint8_t
		ICCP_CHUNK_NAME_INDEX = 4,
		ICCP_CHUNK_INDEX = 0x21;

	if (iccp_chunk_script_size > MAX_SCRIPT_SIZE) {
		std::cerr << "\nScript Size Error: Extraction script exceeds size limit.\n\n";
		return 1;
	}

	const uint16_t ICCP_CHUNK_LENGTH = iccp_chunk_script_size + 4;

	const uint32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Script_Vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH);

	uint16_t iccp_chunk_crc_index = iccp_chunk_script_size + 8;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_bit_length);

	Image_Vec.insert((Image_Vec.begin() + ICCP_CHUNK_INDEX), Iccp_Script_Vec.begin(), Iccp_Script_Vec.end());
	Image_Vec.insert((Image_Vec.end() - 12), Idat_Archive_Vec.begin(), Idat_Archive_Vec.end());

	std::vector<uint8_t>().swap(Iccp_Script_Vec);
	std::vector<uint8_t>().swap(Idat_Archive_Vec);
				 
	const uint32_t 
		LAST_IDAT_CHUNK_NAME_INDEX = IMAGE_VEC_SIZE + iccp_chunk_script_size + 4, 	
		COMPLETE_POLYGLOT_IMAGE_SIZE = static_cast<uint32_t>(Image_Vec.size());  	

	adjustZipOffsets(Image_Vec, COMPLETE_POLYGLOT_IMAGE_SIZE, LAST_IDAT_CHUNK_NAME_INDEX);

	const uint32_t LAST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[LAST_IDAT_CHUNK_NAME_INDEX], IDAT_CHUNK_ARCHIVE_FILE_SIZE - 8);
	
	uint32_t last_idat_chunk_crc_index = COMPLETE_POLYGLOT_IMAGE_SIZE - 16;
	
	value_bit_length = 32;
	valueUpdater(Image_Vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_bit_length);
	
	if (!writeFile(Image_Vec, COMPLETE_POLYGLOT_IMAGE_SIZE, isZipFile)) {
		return 1;
	}
	return 0;
}
