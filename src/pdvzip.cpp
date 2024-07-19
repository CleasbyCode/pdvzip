void startPdv(const std::string& IMAGE_FILENAME, const std::string& ZIP_FILENAME, bool isZipFile) {
	
	const size_t 
		TMP_IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		TMP_ZIP_FILE_SIZE = std::filesystem::file_size(ZIP_FILENAME),
		COMBINED_FILE_SIZE = TMP_ZIP_FILE_SIZE + TMP_IMAGE_FILE_SIZE;

	constexpr uint_fast32_t MAX_FILE_SIZE = 1094713344;
	constexpr uint_fast8_t MIN_FILE_SIZE = 30;

	if (COMBINED_FILE_SIZE > MAX_FILE_SIZE 
		|| MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE
		|| MIN_FILE_SIZE > TMP_ZIP_FILE_SIZE) {

	   std::cerr << "\nFile Size Error: " 
		<< (COMBINED_FILE_SIZE > MAX_FILE_SIZE 
			? "Combined size of image and ZIP file exceeds the maximum limit of 1GB"
        		: (MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE 
	        		? "Image is too small to be a valid PNG image" 
				: "ZIP file is too small to be a valid ZIP archive")) 	
		<< ".\n\n";

    		std::exit(EXIT_FAILURE);
	}

	std::ifstream
		image_file_ifs(IMAGE_FILENAME, std::ios::binary),
		zip_file_ifs(ZIP_FILENAME, std::ios::binary);

	if (!image_file_ifs || !zip_file_ifs ) {
		std::cerr << "\nRead File Error: Unable to read " 
			<< (!image_file_ifs 
				? "image file" 
				: "ZIP file") 
			<< ".\n\n";

		std::exit(EXIT_FAILURE);
	}

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_file_ifs)), std::istreambuf_iterator<char>());

	constexpr uint_fast8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		LINUX_PROBLEM_CHARACTERS[] { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 },
		MAX_INDEX = 0x20;

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
			std::exit(EXIT_FAILURE);
    	}
	
	uint_fast8_t ihdr_chunk_index = 0x12;

	while (ihdr_chunk_index++ != MAX_INDEX) {
		if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS),
			Image_Vec[ihdr_chunk_index]) != std::end(LINUX_PROBLEM_CHARACTERS)) {
				std::cerr << "\nImage File Error : \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
				"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue.\nRepeat if necessary, or use another image.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}
	
	constexpr uint_fast8_t
		IMAGE_WIDTH_INDEX = 0x12,
		IMAGE_HEIGHT_INDEX = 0x16,
		IMAGE_COLOR_TYPE_INDEX = 0x19,
		MIN_DIMS = 68,
		INDEXED_COLOR_TYPE = 3,
		TRUECOLOR_TYPE = 2;

	constexpr uint_fast16_t
		MAX_TRUECOLOR_DIMS = 899,
		MAX_INDEXED_COLOR_DIMS = 4096;

	const uint_fast16_t
		IMAGE_WIDTH = getTwoByteValue(Image_Vec, IMAGE_WIDTH_INDEX, true),
		IMAGE_HEIGHT = getTwoByteValue(Image_Vec, IMAGE_HEIGHT_INDEX, true);

	const uint_fast8_t IMAGE_COLOR_TYPE = Image_Vec[IMAGE_COLOR_TYPE_INDEX] == 6 ? 2 : Image_Vec[IMAGE_COLOR_TYPE_INDEX];

	const bool
		VALID_COLOR_TYPE = (IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE) ? true
		: ((IMAGE_COLOR_TYPE == TRUECOLOR_TYPE) ? true : false),

		VALID_IMAGE_DIMS = (IMAGE_COLOR_TYPE == TRUECOLOR_TYPE
			&& MAX_TRUECOLOR_DIMS >= IMAGE_WIDTH
			&& MAX_TRUECOLOR_DIMS >= IMAGE_HEIGHT
			&& IMAGE_WIDTH >= MIN_DIMS
			&& IMAGE_HEIGHT >= MIN_DIMS) ? true
		: ((IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE
			&& MAX_INDEXED_COLOR_DIMS >= IMAGE_WIDTH
			&& MAX_INDEXED_COLOR_DIMS >= IMAGE_HEIGHT
			&& IMAGE_WIDTH >= MIN_DIMS
			&& IMAGE_HEIGHT >= MIN_DIMS) ? true : false);

	if (!VALID_COLOR_TYPE || !VALID_IMAGE_DIMS) {
		std::cerr << "\nImage File Error: " 
			<< (!VALID_COLOR_TYPE 
				? "Color type of PNG image is not supported.\n\nPNG-32/24 (Truecolor) or PNG-8 (Indexed-Color) only"
				: "Dimensions of PNG image are not within the supported range.\n\nPNG-32/24 Truecolor: [68 x 68]<->[899 x 899].\nPNG-8 Indexed-Color: [68 x 68]<->[4096 x 4096]") 
			<< ".\n\n";

		std::exit(EXIT_FAILURE);
	}

	const uint_fast32_t IMAGE_VEC_SIZE = eraseChunks(Image_Vec);

	std::vector<uint_fast8_t>Idat_Zip_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };

	Idat_Zip_Vec.insert(Idat_Zip_Vec.begin() + 8, std::istreambuf_iterator<char>(zip_file_ifs), std::istreambuf_iterator<char>());

	const uint_fast32_t IDAT_CHUNK_ZIP_FILE_SIZE = getVecSize(Idat_Zip_Vec);

	Image_Vec.reserve(IMAGE_VEC_SIZE + IDAT_CHUNK_ZIP_FILE_SIZE);

	uint_fast8_t 
		idat_chunk_length_index{},
		value_bit_length = 32;

	valueUpdater(Idat_Zip_Vec, idat_chunk_length_index, IDAT_CHUNK_ZIP_FILE_SIZE - 12, value_bit_length, true);

	constexpr uint_fast8_t 
		ZIP_SIG[] { 0x50, 0x4B, 0x03, 0x04 },
		ZIP_RECORD_FIRST_FILENAME_MIN_LENGTH = 4,
		ZIP_RECORD_FIRST_FILENAME_LENGTH_INDEX = 0x22, 
		ZIP_RECORD_FIRST_FILENAME_INDEX = 0x26,
		VIDEO_AUDIO = 23,
		PDF = 24, 
		PYTHON = 25, 
		POWERSHELL = 26, 
		BASH_SHELL = 27,
		WINDOWS_EXECUTABLE = 28, 
		DEFAULT = 29, 
		FOLDER = 30, 
		LINUX_EXECUTABLE = 31, 
		JAR = 32;

	const uint_fast8_t ZIP_RECORD_FIRST_FILENAME_LENGTH = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_LENGTH_INDEX];

	if (!std::equal(std::begin(ZIP_SIG), std::end(ZIP_SIG), std::begin(Idat_Zip_Vec) + 8) || ZIP_RECORD_FIRST_FILENAME_MIN_LENGTH > ZIP_RECORD_FIRST_FILENAME_LENGTH) {
		std::cerr << "\nZIP File Error: " 
			<< (ZIP_RECORD_FIRST_FILENAME_MIN_LENGTH > ZIP_RECORD_FIRST_FILENAME_LENGTH 
				? "\n\nName length of first file within ZIP archive is too short.\nIncrease its length (minimum 4 characters) and make sure it has a valid extension"
				: "File does not appear to be a valid ZIP archive") 
			<< ".\n\n";

		std::exit(EXIT_FAILURE);
	}

	std::vector<std::string> Extension_List_Vec {	"3gp", "aac", "aif", "ala", "chd", "avi", "dsd", "f4v", "lac", "flv", "m4a", "mkv", "mov", "mp3", "mp4",
							"mpg", "peg", "ogg", "pcm", "swf", "wav", "ebm", "wma", "wmv", "pdf", ".py", "ps1", ".sh", "exe" };
		
	const std::string
		ZIP_RECORD_FIRST_FILENAME{ Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILENAME_INDEX, Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH },
		ZIP_RECORD_FIRST_FILENAME_EXTENSION = ZIP_RECORD_FIRST_FILENAME.substr(ZIP_RECORD_FIRST_FILENAME_LENGTH - 3, 3);

	uint_fast8_t extension_list_index = (isZipFile) ? 0 : JAR;

	const uint_fast16_t CHECK_FOR_FILE_EXTENSION = static_cast<uint_fast16_t>(ZIP_RECORD_FIRST_FILENAME.find_last_of('.'));

	if (CHECK_FOR_FILE_EXTENSION == 0 || CHECK_FOR_FILE_EXTENSION > ZIP_RECORD_FIRST_FILENAME_LENGTH ) {
			extension_list_index = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
	}

	if (extension_list_index != FOLDER && Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
		if (Idat_Zip_Vec[ZIP_RECORD_FIRST_FILENAME_INDEX + ZIP_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
			extension_list_index = FOLDER;
		} else {
			std::cerr << 
				"\nZIP File Error: Invalid folder name within ZIP archive.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}

	for (; DEFAULT > extension_list_index; extension_list_index++) {
		if (Extension_List_Vec[extension_list_index] == ZIP_RECORD_FIRST_FILENAME_EXTENSION) {
			extension_list_index = extension_list_index <= VIDEO_AUDIO ? VIDEO_AUDIO : extension_list_index;
			break;
		}
	}

	std::string
		args_linux{},
		args_windows{},
		args{};

	if ((extension_list_index > PDF && extension_list_index < DEFAULT) || extension_list_index == LINUX_EXECUTABLE) {
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
		{VIDEO_AUDIO,		{	0, 0x1E4, 0x1C }},
		{PDF,			{	1, 0x196, 0x1C }},
		{PYTHON,		{	2, 0x10B, 0x101, 0xBC, 0x1C}},
		{POWERSHELL,		{	3, 0x105, 0xFB, 0xB6, 0x33 }},
		{BASH_SHELL,		{	4, 0x134, 0x132, 0x8E, 0x1C }},
		{WINDOWS_EXECUTABLE,	{	5, 0x116, 0x114 }},
		{FOLDER,		{	6, 0x149, 0x1C }},
		{LINUX_EXECUTABLE,	{	7, 0x8E,  0x1C }},
		{JAR,			{	8 }},
		{-1,			{	9, 0x127, 0x1C}} // Default case
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
	
	Extraction_Scripts_Vec.clear();
	Extraction_Scripts_Vec.shrink_to_fit();

	uint_fast8_t iccp_chunk_length_index{};
	
	uint_fast32_t iccp_chunk_script_size = getVecSize(Iccp_Script_Vec) - 12;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);

	const uint_fast8_t iccp_chunk_length_first_byte_value = Iccp_Script_Vec[3];

	if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS), iccp_chunk_length_first_byte_value) != std::end(LINUX_PROBLEM_CHARACTERS)) {

			const std::string INCREASE_CHUNK_LENGTH_STRING = "..........";

			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + iccp_chunk_script_size + 8, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
			iccp_chunk_script_size = getVecSize(Iccp_Script_Vec) - 12;

			valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);
	}
	
	uint_fast32_t combined_file_size = iccp_chunk_script_size + IMAGE_VEC_SIZE + IDAT_CHUNK_ZIP_FILE_SIZE;

	constexpr uint_fast16_t MAX_SCRIPT_SIZE = 1500;

	constexpr uint_fast8_t
		ICCP_CHUNK_NAME_INDEX = 4,
		ICCP_CHUNK_INSERT_INDEX = 0x21;

	if (iccp_chunk_script_size > MAX_SCRIPT_SIZE || combined_file_size > MAX_FILE_SIZE) {
		std::cerr << "\nFile Size Error: " 
			<< (iccp_chunk_script_size > MAX_SCRIPT_SIZE 
				? "Extraction script exceeds size limit"
				: "The combined file size of your PNG image, ZIP file and extraction script, exceeds file size limit") 
			<< ".\n\n";

		std::exit(EXIT_FAILURE);
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
		COMPLETE_POLYGLOT_IMAGE_SIZE = getVecSize(Image_Vec);
	
	uint_fast32_t last_idat_chunk_crc_index = COMPLETE_POLYGLOT_IMAGE_SIZE - 16;
	
	value_bit_length = 32;

	valueUpdater(Image_Vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_bit_length, true);
		
	srand((unsigned)time(NULL));

	const std::string
		RANDOM_NAME_VALUE = std::to_string(rand()),
		POLYGLOT_FILENAME = "pzip_" + RANDOM_NAME_VALUE.substr(0, 5) + ".png";

	std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	file_ofs.write((char*)&Image_Vec[0], COMPLETE_POLYGLOT_IMAGE_SIZE);

	std::cout << "\nCreated " 
		<< (isZipFile 
			? "PNG-ZIP" 
			: "PNG-JAR") 
		<< " polyglot image file: " + POLYGLOT_FILENAME + '\x20' + std::to_string(COMPLETE_POLYGLOT_IMAGE_SIZE) + " Bytes.\n\nComplete!\n\n";
}
