uint_fast8_t pdvZip(const std::string& IMAGE_FILENAME, const std::string& ZIP_FILENAME, bool isZipFile) {

	constexpr uint_fast32_t MAX_FILE_SIZE = 1094713344;
	constexpr uint_fast8_t MIN_FILE_SIZE = 30;
	
	// Check cover image and ZIP file for valid file sizes.
	const size_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		ZIP_FILE_SIZE = std::filesystem::file_size(ZIP_FILENAME),
		COMBINED_FILE_SIZE = ZIP_FILE_SIZE + IMAGE_FILE_SIZE;

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
    		return 1;
	}

	// Attempt to open both cover image and ZIP file.
	std::ifstream
		image_file_ifs(IMAGE_FILENAME, std::ios::binary),
		zip_file_ifs(ZIP_FILENAME, std::ios::binary);

	if (!image_file_ifs || !zip_file_ifs ) {
		std::cerr << "\nRead File Error: Unable to read " 
			<< (!image_file_ifs 
				? "image file" 
				: "ZIP file") 
			<< ".\n\n";
		return 1;
	}
	
	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_file_ifs)), std::istreambuf_iterator<char>());

	// Check for valid PNG cover image. 
	constexpr uint_fast8_t
			PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
			PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
			return 1;
    	}
	
	// A range of characters that may appear within the image width/height dimension fields of the IHDR chunk or within the 4-byte IHDR chunk's CRC field,
	// which will break the Linux extraction script. If any of these characters are detected, the user will have to modify the image manually or try another image.
	constexpr uint_fast8_t
			LINUX_PROBLEM_CHARACTERS[] { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 },
			IHDR_STOP_INDEX = 0x20;
	
	uint_fast8_t ihdr_check_index = 0x12;

	while (ihdr_check_index++ < IHDR_STOP_INDEX) {
		if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS),
			Image_Vec[ihdr_check_index]) != std::end(LINUX_PROBLEM_CHARACTERS)) {
				std::cerr << "\nImage File Error:\n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
				"\nTry modifying image width & height dimensions (1% increase or decrease) to resolve the issue.\nRepeat if necessary or try another image.\n\n";
			return 1;
		}
	}

	// Check cover image for valid image dimensions and color type values.
	constexpr uint_fast8_t
		IMAGE_WIDTH_INDEX = 0x12,
		IMAGE_HEIGHT_INDEX = 0x16,
		IMAGE_COLOR_TYPE_INDEX = 0x19,
		MIN_DIMS = 68,
		INDEXED_COLOR = 3,
		TRUECOLOR = 2,
		BYTE_LENGTH = 2;

	constexpr uint_fast16_t
		MAX_TRUECOLOR_DIMS = 899,
		MAX_INDEXED_COLOR_DIMS = 4096;

	const uint_fast16_t
		IMAGE_WIDTH = getByteValue(Image_Vec, IMAGE_WIDTH_INDEX, BYTE_LENGTH, true),
		IMAGE_HEIGHT = getByteValue(Image_Vec, IMAGE_HEIGHT_INDEX, BYTE_LENGTH, true);

	const uint_fast8_t IMAGE_COLOR_TYPE = Image_Vec[IMAGE_COLOR_TYPE_INDEX] == 6 ? 2 : Image_Vec[IMAGE_COLOR_TYPE_INDEX];

	const bool VALID_COLOR_TYPE = (IMAGE_COLOR_TYPE == INDEXED_COLOR || IMAGE_COLOR_TYPE == TRUECOLOR);

	auto checkDimensions = [&](uint_fast8_t COLOR_TYPE, uint_fast16_t MAX_DIMS) {
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
        		std::cerr << "Dimensions of cover image are not within the supported range.\n\nSupported ranges:\n - PNG-32/24 Truecolor: [68 x 68] to [899 x 899]\n - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]";
    		}
    		std::cerr << "\n\n";
    		return 1;
	}

	// Strip superfluous PNG chunks from the cover image.
	if(!eraseChunks(Image_Vec)) {
		return 1;
	}
	
	const uint_fast32_t IMAGE_VEC_SIZE = static_cast<uint_fast32_t>(Image_Vec.size());

	std::vector<uint_fast8_t>Idat_Zip_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };

	Idat_Zip_Vec.insert(Idat_Zip_Vec.begin() + 8, std::istreambuf_iterator<char>(zip_file_ifs), std::istreambuf_iterator<char>());

	// Check for valid ZIP file.
	constexpr uint_fast8_t ZIP_SIG[] { 0x50, 0x4B, 0x03, 0x04 };

	if (!std::equal(std::begin(ZIP_SIG), std::end(ZIP_SIG), std::begin(Idat_Zip_Vec) + 8)) {
		std::cerr << "\nZIP File Error: File does not appear to be a valid ZIP archive.\n\n";
		return 1;
	}

	const uint_fast32_t IDAT_CHUNK_ZIP_FILE_SIZE = static_cast<uint_fast32_t>(Idat_Zip_Vec.size());

	uint_fast8_t 
		idat_chunk_length_index{},
		value_bit_length = 32;

	valueUpdater(Idat_Zip_Vec, idat_chunk_length_index, IDAT_CHUNK_ZIP_FILE_SIZE - 12, value_bit_length, true);

	// The following section (~158 lines) completes and embeds the extraction script, based on the file type within the ZIP archive.
	constexpr uint_fast8_t 
		ZIP_RECORD_FIRST_FILENAME_MIN_LENGTH = 4,
		ZIP_RECORD_FIRST_FILENAME_LENGTH_INDEX = 0x22, 
		ZIP_RECORD_FIRST_FILENAME_INDEX = 0x26;
	
	const uint_fast8_t ZIP_RECORD_FIRST_FILENAME_LENGTH = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_LENGTH_INDEX];

	if (ZIP_RECORD_FIRST_FILENAME_MIN_LENGTH > ZIP_RECORD_FIRST_FILENAME_LENGTH) {
		std::cerr << "\nZIP File Error:\n\nName length of first file within ZIP archive is too short.\nIncrease its length (minimum 4 characters) and make sure it has a valid extension.\n\n";
		return 1;
	}

	constexpr const char* Extension_List_Vec[] = {	"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi", "dsd", "divx",
    							"f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv", "xvid", "pdf", "py", "ps1", "sh", "exe"	};
						    
	const std::string ZIP_RECORD_FIRST_FILENAME{ Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILENAME_INDEX, Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH };

	constexpr uint_fast8_t 
		VIDEO_AUDIO = 29,
		PDF = 30, 
		PYTHON = 31, 
		POWERSHELL = 32, 
		BASH_SHELL = 33,
		WINDOWS_EXECUTABLE = 34, 
		UNKNOWN_FILE_TYPE = 35, // Default case, unmatched file extension.
		FOLDER = 36, 
		LINUX_EXECUTABLE = 37, 
		JAR = 38;

	uint_fast8_t extension_list_index = (isZipFile) ? 0 : JAR;

	const uint_fast16_t EXTENSION_POS = static_cast<uint_fast16_t>(ZIP_RECORD_FIRST_FILENAME.rfind('.'));
	
	const std::string ZIP_RECORD_FIRST_FILENAME_EXTENSION = ZIP_RECORD_FIRST_FILENAME_LENGTH > EXTENSION_POS ? ZIP_RECORD_FIRST_FILENAME.substr(EXTENSION_POS + 1) : "?";

	// Deal with names that don't have extensions. Folders and Linux executables.
	if (ZIP_RECORD_FIRST_FILENAME_EXTENSION  == "?") {
		extension_list_index = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
	}
						    
	// Even though we found a period character, indicating a file extension, it could still be a folder that just has a "." somewhere within its name, check for it here.
	// Linux allows a zipped folder to have a "." for the last character of its name (e.g. "my_folder."), but this will cause issues with Windows, so also check for it here.
	if (extension_list_index != FOLDER && Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
		if (Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
			extension_list_index = FOLDER; 
		} else {
			std::cerr << "\nZIP File Error: Invalid folder name within ZIP archive.\n\n"; 
			return 1;
		}
	}
	
	// Try to match the file extension of the first file of the ZIP archive with the vector list of file extensions (Extension_List_Vec).
	// This will determine what extraction script to embed within the image, so that it correctly deals with the file type.
	while (UNKNOWN_FILE_TYPE > extension_list_index) {
		if (Extension_List_Vec[extension_list_index] == ZIP_RECORD_FIRST_FILENAME_EXTENSION) {
			extension_list_index = VIDEO_AUDIO >= extension_list_index ? VIDEO_AUDIO : extension_list_index;
			break;
		}
		extension_list_index++;
	}

	std::string
		args_linux{},
		args_windows{},
		args{};

	if ((extension_list_index > PDF && UNKNOWN_FILE_TYPE > extension_list_index) || extension_list_index == LINUX_EXECUTABLE) {
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

	std::vector<uint_fast8_t> Iccp_Script_Vec = {	0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 
							0x5F, 0x00, 0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00 };

	std::unordered_map<int_fast8_t, std::vector<uint_fast16_t>> case_map = {
		{VIDEO_AUDIO,		{	0, 0x1E4, 0x1C }}, // The single digit integer is the extraction script id (see Extraction_Scripts_Vec), the hex values are insert index locations
		{PDF,			{	1, 0x196, 0x1C }}, // within the extraction script vector. We use these index locations to insert additional items into the script in order to complete it.
		{PYTHON,		{	2, 0x10B, 0x101, 0xBC, 0x1C}},
		{POWERSHELL,		{	3, 0x105, 0xFB, 0xB6, 0x33 }},
		{BASH_SHELL,		{	4, 0x134, 0x132, 0x8E, 0x1C }},
		{WINDOWS_EXECUTABLE,	{	5, 0x116, 0x114 }},
		{FOLDER,		{	6, 0x149, 0x1C }},
		{LINUX_EXECUTABLE,	{	7, 0x8E,  0x1C }},
		{JAR,			{	8 }},
		{-1,			{	9, 0x127, 0x1C}} // Unknown file type, unmatched file extension. Default case.
	};

	std::vector<uint_fast16_t> Case_Values_Vec = case_map.count(extension_list_index) ? case_map[extension_list_index] : case_map[-1];

	const uint_fast16_t SCRIPT_SELECTION = Case_Values_Vec[0];

	constexpr uint_fast8_t SCRIPT_INSERT_INDEX = 0x16;

	Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + SCRIPT_INSERT_INDEX, Extraction_Scripts_Vec[SCRIPT_SELECTION].begin(), Extraction_Scripts_Vec[SCRIPT_SELECTION].end());

	if (isZipFile) {
		if (extension_list_index == WINDOWS_EXECUTABLE || extension_list_index == LINUX_EXECUTABLE) {
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args.begin(), args.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILENAME.begin(), ZIP_RECORD_FIRST_FILENAME.end());
			
		} else if (extension_list_index > PDF && extension_list_index < WINDOWS_EXECUTABLE) {
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args_windows.begin(), args_windows.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILENAME.begin(), ZIP_RECORD_FIRST_FILENAME.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[3], args_linux.begin(), args_linux.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[4], ZIP_RECORD_FIRST_FILENAME.begin(), ZIP_RECORD_FIRST_FILENAME.end());
			
		} else { Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], ZIP_RECORD_FIRST_FILENAME.begin(), ZIP_RECORD_FIRST_FILENAME.end());
			 Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILENAME.begin(), ZIP_RECORD_FIRST_FILENAME.end());
		}
	}
	
	uint_fast8_t 
		iccp_chunk_length_index{},
		iccp_chunk_length_first_byte_index = 3;
	
	uint_fast16_t iccp_chunk_script_size = static_cast<uint_fast16_t>(Iccp_Script_Vec.size()) - 12;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);

	const uint_fast8_t iccp_chunk_length_first_byte_value = Iccp_Script_Vec[iccp_chunk_length_first_byte_index];

	// If a problem character (that breaks the Linux extraction script) is found within the first byte of the updated iccp chunk length field, 
	// insert a short string to the end of the iccp chunk to increase its length, avoiding the problem characters when chunk length is updated.
	if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS), 
		iccp_chunk_length_first_byte_value) != std::end(LINUX_PROBLEM_CHARACTERS)) {

			const std::string INCREASE_CHUNK_LENGTH_STRING = "........";
	
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + iccp_chunk_script_size + 8, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
			iccp_chunk_script_size = static_cast<uint_fast16_t>(Iccp_Script_Vec.size()) - 12;

			valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);
	}
	
	constexpr uint_fast16_t MAX_SCRIPT_SIZE = 1500;

	constexpr uint_fast8_t
		ICCP_CHUNK_NAME_INDEX = 4,
		ICCP_CHUNK_INSERT_INDEX = 0x21;

	if (iccp_chunk_script_size > MAX_SCRIPT_SIZE) {
		std::cerr << "\nScript Size Error: Extraction script exceeds size limit.\n\n";
		return 1;
	}

	const uint_fast16_t ICCP_CHUNK_LENGTH = iccp_chunk_script_size + 4;

	const uint_fast32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Script_Vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH);

	uint_fast16_t iccp_chunk_crc_index = iccp_chunk_script_size + 8;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_bit_length, true);

	Image_Vec.insert((Image_Vec.begin() + ICCP_CHUNK_INSERT_INDEX), Iccp_Script_Vec.begin(), Iccp_Script_Vec.end());
	Image_Vec.insert((Image_Vec.end() - 12), Idat_Zip_Vec.begin(), Idat_Zip_Vec.end());

	const uint_fast32_t LAST_IDAT_CHUNK_NAME_INDEX = IMAGE_VEC_SIZE + iccp_chunk_script_size + 4;

	adjustZipOffsets(Image_Vec, LAST_IDAT_CHUNK_NAME_INDEX);

	const uint_fast32_t 
		LAST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[LAST_IDAT_CHUNK_NAME_INDEX], IDAT_CHUNK_ZIP_FILE_SIZE - 8),
		COMPLETE_POLYGLOT_IMAGE_SIZE = static_cast<uint_fast32_t>(Image_Vec.size());
	
	uint_fast32_t last_idat_chunk_crc_index = COMPLETE_POLYGLOT_IMAGE_SIZE - 16;
	
	value_bit_length = 32;

	valueUpdater(Image_Vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_bit_length, true);
	
	// Attempt to write out completed polyglot image file.		
	if (!writeFile(Image_Vec, COMPLETE_POLYGLOT_IMAGE_SIZE, isZipFile)) {
		return 1;
	}

	return 0;
}
