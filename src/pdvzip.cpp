
void startPdv(const std::string& IMAGE_NAME, const std::string& ZIP_NAME, bool isZipFile) {
	
	std::ifstream
		image_ifs(IMAGE_NAME, std::ios::binary),
		zip_ifs(ZIP_NAME, std::ios::binary);

	if (!image_ifs || !zip_ifs) {
		std::cerr << "\nRead File Error: " << (!image_ifs ? "Unable to open image file" : "Unable to open ZIP file") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	constexpr uint_fast32_t MAX_FILE_SIZE = 209715200;
	constexpr uint_fast8_t MIN_FILE_SIZE = 30;

	size_t
		tmp_image_size{},
		tmp_zip_size{},
		tmp_combined_file_size{};

	image_ifs.seekg(0, image_ifs.end);
	tmp_image_size = image_ifs.tellg();
	image_ifs.seekg(0, image_ifs.beg);

	zip_ifs.seekg(0, zip_ifs.end);
	tmp_zip_size = zip_ifs.tellg();
	zip_ifs.seekg(0, zip_ifs.beg);

	tmp_combined_file_size = tmp_image_size + tmp_zip_size;

	bool file_size_check = tmp_image_size && tmp_zip_size > MIN_FILE_SIZE && MAX_FILE_SIZE >= tmp_combined_file_size;

	if (!file_size_check) {
		std::cerr << "\nFile Size Error: " << (MIN_FILE_SIZE > tmp_image_size ? 
			"Invalid PNG image. File too small"
			: (MIN_FILE_SIZE > tmp_zip_size ? "Invalid ZIP file. File too small"
			: "The combined file size of your PNG image and ZIP file exceeds maximum limit")) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_ifs)), std::istreambuf_iterator<char>());

	uint_fast32_t image_size = static_cast<uint_fast32_t>(Image_Vec.size());

	const std::string
		PNG_SIG = "\x89\x50\x4E\x47",
		PNG_END_SIG = "\x49\x45\x4E\x44\xAE\x42\x60\x82",
		READ_PNG_SIG{ Image_Vec.begin(), Image_Vec.begin() + PNG_SIG.length() },
		READ_PNG_END_SIG{ Image_Vec.end() - PNG_END_SIG.length(), Image_Vec.end() };

	if (READ_PNG_SIG != PNG_SIG || READ_PNG_END_SIG != PNG_END_SIG) {
		std::cerr << "\nImage File Error: File does not appear to be a valid PNG image.\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	constexpr uint_fast8_t MAX_INDEX = 32;
	constexpr char LINUX_PROBLEM_CHARACTERS[7]{ 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 };

	uint_fast8_t ihdr_chunk_index = 18;

	while (ihdr_chunk_index++ != MAX_INDEX) {
		if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS),
			Image_Vec[ihdr_chunk_index]) != std::end(LINUX_PROBLEM_CHARACTERS)) {
			std::cerr << "\nImage File Error : \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
				"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue.\nRepeat if necessary, or try another image.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}
	
	const uint_fast16_t
		IMAGE_WIDTH = (static_cast<uint_fast16_t>(Image_Vec[18]) << 8) | Image_Vec[19],
		IMAGE_HEIGHT = (static_cast<uint_fast16_t>(Image_Vec[22]) << 8) | Image_Vec[23];

	const uint_fast8_t IMAGE_COLOR_TYPE = Image_Vec[25] == 6 ? 2 : Image_Vec[25];

	constexpr uint_fast16_t
		MAX_TRUECOLOR_DIMS = 899,
		MAX_INDEXED_COLOR_DIMS = 4096;

	constexpr uint_fast8_t
		MIN_DIMS = 68,
		INDEXED_COLOR_TYPE = 3,
		TRUECOLOR_TYPE = 2;

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
		std::cerr << "\nImage File Error: " << (!VALID_COLOR_TYPE ? 
			"Color type of PNG image is not supported.\n\nPNG-32/24 (Truecolor) or PNG-8 (Indexed-Color) only"
			: "Dimensions of PNG image are not within the supported range.\n\nPNG-32/24 Truecolor: [68 x 68]<->[899 x 899].\nPNG-8 Indexed-Color: [68 x 68]<->[4096 x 4096]") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	image_size = eraseChunks(Image_Vec, image_size);

	std::vector<uint_fast8_t>Idat_Zip_Vec = {	0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 };

	Idat_Zip_Vec.insert(Idat_Zip_Vec.begin() + 8, std::istreambuf_iterator<char>(zip_ifs), std::istreambuf_iterator<char>());

	uint_fast32_t zip_size = static_cast<uint_fast32_t>(Idat_Zip_Vec.size());

	uint_fast8_t 
		idat_chunk_length_index{},
		value_bit_length = 32;

	valueUpdater(Idat_Zip_Vec, idat_chunk_length_index, zip_size - 12, value_bit_length, true);

	const std::string
		ZIP_SIG = "\x50\x4B\x03\x04",
		GET_ZIP_SIG{ Idat_Zip_Vec.begin() + 8, Idat_Zip_Vec.begin() + 8 + ZIP_SIG.length() };

	constexpr uint_fast8_t MIN_INZIP_NAME_LENGTH = 4;

	const uint_fast8_t INZIP_NAME_LENGTH = Idat_Zip_Vec[34];

	if (GET_ZIP_SIG != ZIP_SIG || MIN_INZIP_NAME_LENGTH > INZIP_NAME_LENGTH) {
		std::cerr << "\nZIP File Error: " << (GET_ZIP_SIG != ZIP_SIG ? 
			"File does not appear to be a valid ZIP archive"
			: "\n\nName length of first file within ZIP archive is too short.\nIncrease its length (minimum 4 characters) and make sure it has a valid extension") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::vector<std::string> Extension_List_Vec {	"3gp", "aac", "aif", "ala", "chd", "avi", "dsd", "f4v", "lac", "flv", "m4a", "mkv", "mov", "mp3", "mp4",
							"mpg", "peg", "ogg", "pcm", "swf", "wav", "ebm", "wma", "wmv", "pdf", ".py", "ps1", ".sh", "exe" };

	constexpr uint_fast8_t
		ZIP_RECORD_FIRST_FILE_NAME_LENGTH_INDEX = 34, 
		ZIP_RECORD_FIRST_FILE_NAME_INDEX = 38,

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

	const uint_fast8_t ZIP_RECORD_FIRST_FILE_NAME_LENGTH = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILE_NAME_LENGTH_INDEX];

	const std::string
		ZIP_RECORD_FIRST_FILE_NAME{ Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILE_NAME_INDEX, Idat_Zip_Vec.begin() + ZIP_RECORD_FIRST_FILE_NAME_INDEX + ZIP_RECORD_FIRST_FILE_NAME_LENGTH },
		ZIP_RECORD_FIRST_FILE_NAME_EXTENSION = ZIP_RECORD_FIRST_FILE_NAME.substr(ZIP_RECORD_FIRST_FILE_NAME_LENGTH - 3, 3);

	uint_fast8_t extension_list_index = (isZipFile) ? 0 : JAR;

	const auto CHECK_FOR_FILE_EXTENSION = ZIP_RECORD_FIRST_FILE_NAME.find_last_of('.');

	if (CHECK_FOR_FILE_EXTENSION == 0 || CHECK_FOR_FILE_EXTENSION > ZIP_RECORD_FIRST_FILE_NAME_LENGTH) {
		extension_list_index = Idat_Zip_Vec[ZIP_RECORD_FIRST_FILE_NAME_INDEX + ZIP_RECORD_FIRST_FILE_NAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
	}

	for (; DEFAULT > extension_list_index; extension_list_index++) {
		if (Extension_List_Vec[extension_list_index] == ZIP_RECORD_FIRST_FILE_NAME_EXTENSION) {
			extension_list_index = extension_list_index <= VIDEO_AUDIO ? VIDEO_AUDIO : extension_list_index;
			break;
		}
	}

	std::string
		args_linux{},
		args_windows{},
		args{};

	if (extension_list_index > PDF && extension_list_index < DEFAULT || extension_list_index == LINUX_EXECUTABLE) {

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
		{VIDEO_AUDIO,		{	0, 484, 28}},
		{PDF,			{	1, 406, 28}},
		{PYTHON,		{	2, 267, 257, 188, 28}},
		{POWERSHELL,		{	3, 261, 251, 182, 51}},
		{BASH_SHELL,		{	4, 308, 306, 142, 28}},
		{WINDOWS_EXECUTABLE,	{	5, 278, 276}},
		{FOLDER,		{	6, 329, 28}},
		{LINUX_EXECUTABLE,	{	7, 142, 28}},
		{JAR,			{	8}},
		{-1,			{	9, 295, 28}} // Default case
	};

	std::vector<uint_fast16_t> Case_Values_Vec = case_map.count(extension_list_index) ? case_map[extension_list_index] : case_map[-1];

	const uint_fast16_t SCRIPT = Case_Values_Vec[0];

	constexpr uint_fast8_t SCRIPT_INSERT_INDEX = 22;

	Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + SCRIPT_INSERT_INDEX, Extraction_Scripts_Vec[SCRIPT].begin(), Extraction_Scripts_Vec[SCRIPT].end());

	if (isZipFile) {
		if (extension_list_index == WINDOWS_EXECUTABLE || extension_list_index == LINUX_EXECUTABLE) {
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args.begin(), args.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILE_NAME.begin(), ZIP_RECORD_FIRST_FILE_NAME.end());
			
		} else if (extension_list_index > PDF && extension_list_index < WINDOWS_EXECUTABLE) {
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], args_windows.begin(), args_windows.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILE_NAME.begin(), ZIP_RECORD_FIRST_FILE_NAME.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[3], args_linux.begin(), args_linux.end());
			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[4], ZIP_RECORD_FIRST_FILE_NAME.begin(), ZIP_RECORD_FIRST_FILE_NAME.end());
			
		} else { Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[1], ZIP_RECORD_FIRST_FILE_NAME.begin(), ZIP_RECORD_FIRST_FILE_NAME.end());
			 Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + Case_Values_Vec[2], ZIP_RECORD_FIRST_FILE_NAME.begin(), ZIP_RECORD_FIRST_FILE_NAME.end());
		}
	}

	uint_fast8_t iccp_chunk_length_index{};
	
	uint_fast16_t iccp_chunk_script_size = static_cast<uint_fast16_t>(Iccp_Script_Vec.size() - 12);

	uint_fast32_t
		buf_index{},
		initialize_crc_value = 0xffffffffL;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);

	const uint_fast8_t iccp_chunk_length_first_byte_value = Iccp_Script_Vec[3];

	if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS), iccp_chunk_length_first_byte_value) != std::end(LINUX_PROBLEM_CHARACTERS)) {

			const std::string INCREASE_CHUNK_LENGTH_STRING = "..........";

			Iccp_Script_Vec.insert(Iccp_Script_Vec.begin() + iccp_chunk_script_size + 8, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
			iccp_chunk_script_size = static_cast<uint_fast16_t>(Iccp_Script_Vec.size() - 12);

			valueUpdater(Iccp_Script_Vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, true);
	}
	
	uint_fast32_t combined_file_size = static_cast<uint_fast32_t>(Iccp_Script_Vec.size() + Image_Vec.size() + Idat_Zip_Vec.size());

	constexpr uint_fast16_t MAX_SCRIPT_SIZE = 1500;
	constexpr uint_fast8_t
		ICCP_CHUNK_NAME_INDEX = 4,
		ICCP_CHUNK_INSERT_INDEX = 33;

	if (iccp_chunk_script_size > MAX_SCRIPT_SIZE || combined_file_size > MAX_FILE_SIZE) {
		std::cerr << "\nFile Size Error: " << (iccp_chunk_script_size > MAX_SCRIPT_SIZE ? 
			"Extraction script exceeds size limit"
			: "The combined file size of your PNG image, ZIP file and Extraction Script, exceeds file size limit") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	const uint_fast16_t ICCP_CHUNK_LENGTH = iccp_chunk_script_size + 4;
	const uint_fast32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Script_Vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH, buf_index, initialize_crc_value);

	uint_fast16_t iccp_chunk_crc_index = iccp_chunk_script_size + 8;

	valueUpdater(Iccp_Script_Vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_bit_length, true);

	Image_Vec.insert((Image_Vec.begin() + ICCP_CHUNK_INSERT_INDEX), Iccp_Script_Vec.begin(), Iccp_Script_Vec.end());
	Image_Vec.insert((Image_Vec.end() - 12), Idat_Zip_Vec.begin(), Idat_Zip_Vec.end());

	const uint_fast32_t LAST_IDAT_CHUNK_NAME_INDEX = image_size + iccp_chunk_script_size + 4;

	adjustZipOffsets(Image_Vec, LAST_IDAT_CHUNK_NAME_INDEX);

	const uint_fast32_t LAST_IDAT_CHUNK_CRC = crcUpdate(&Image_Vec[LAST_IDAT_CHUNK_NAME_INDEX], zip_size - 8, buf_index, initialize_crc_value);

	image_size = static_cast<uint_fast32_t>(Image_Vec.size());

	uint_fast32_t last_idat_chunk_crc_index = image_size - 16;
	
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

	file_ofs.write((char*)&Image_Vec[0], image_size);

	std::cout << "\nCreated " << (isZipFile ? "PNG-ZIP" : "PNG-JAR") << " polyglot image file: " + POLYGLOT_FILENAME + '\x20' + std::to_string(image_size) + " Bytes.\n\nComplete!\n\nYou can now post your image on the relevant supported platforms.\n\n";
}
