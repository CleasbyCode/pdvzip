// 	PNG Data Vehicle, ZIP Edition (PDVZIP v1.5). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux):
// 	$ g++ pdvzip.cpp -O2 -s -o pdvzip

// 	Run it:
// 	$ ./pdvzip

#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>

typedef unsigned char BYTE;

bool isBig = true;

void 
	// Update values, such as chunk lengths, crc, file sizes and other values. Writes them into the relevant vector index locations.
	Value_Updater(std::vector<BYTE>&, size_t, const size_t&, uint_fast8_t, bool),

	// Read-in user PNG image and ZIP file and store in vectors.
	Store_Files(std::ifstream&, std::ifstream&, const std::string&, const std::string&, bool),

	// Make sure user PNG image and ZIP file fulfil valid program requirements. Display relevant error message and exit program if checks fail.
	Check_File_Requirements(std::vector<BYTE>&, std::vector<BYTE>&, const std::string&),

	// Keep critical PNG chunks, remove the rest.
	Erase_Image_Chunks(std::vector<BYTE>&),

	// Update barebones extraction script determined by embedded zip-file content. 
	Complete_Extraction_Script(std::vector<BYTE>&, std::vector<BYTE>&, const std::string&),

	// Insert contents of vectors storing user ZIP file and the completed extraction script into the vector containing PNG image, then write vector's content out to file.
	Combine_Vectors(std::vector< BYTE>&, std::vector<BYTE>&, std::vector<BYTE>&),

	// Adjust embedded ZIP file offsets within the PNG image, so that it remains a valid, working ZIP archive.
	Fix_Zip_Offset(std::vector<BYTE>&, const size_t&),

	// Output to screen detailed program usage information.
	Display_Info();

uint_fast64_t
	// Code to compute CRC32 (for "IDAT" & "iCCP" chunks within this program) is taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
	Crc_Update(const uint_fast64_t&, BYTE*, const uint_fast64_t&),
	Crc(BYTE*, const uint_fast64_t&);

int main(int argc, char** argv) {

	bool imgur_size_hack = false;

	if (argc == 4 && std::string(argv[3]) == "--imgur") {
		imgur_size_hack = true;
		argc = 3;
	}

	if (argc == 2 && std::string(argv[1]) == "--info") {
		Display_Info();
	}
	else if (argc < 3 || argc > 3) {
		std::cout << "\nUsage: pdvzip <png_image> <zip_file>\n\t\bpdvzip <png_image> <zip_file> [--imgur]\n\t\bpdvzip --info\n\n";
	}
	else {
		const std::string
			IMAGE_NAME = argv[1],
			ZIP_NAME = argv[2];

		std::cout << "\nReading files. Please wait...\n";

		// Open user files.
		std::ifstream
			read_image_fs(IMAGE_NAME, std::ios::binary),
			read_zip_fs(ZIP_NAME, std::ios::binary);

		if (read_image_fs && read_zip_fs) {
			// OK, now read files into vectors.
			Store_Files(read_image_fs, read_zip_fs, IMAGE_NAME, ZIP_NAME, imgur_size_hack);
		}
		else { // Display relevant error message and exit program if any file fails to open.
			const std::string
				READ_ERR_MSG = "Read Error: Unable to open/read file: ",
				ERR_MSG = !read_image_fs ? "\nPNG " + READ_ERR_MSG + "'" + IMAGE_NAME + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_NAME + "'\n\n";
			std::cerr << ERR_MSG;
			std::exit(EXIT_FAILURE);
		}
	}
	return 0;
}

void Store_Files(std::ifstream& readimage, std::ifstream& readzip, const std::string& IMG_FILE, const std::string& ZIP_FILE, bool imgur_size_hack) {

	// Vector "Zip_Vec" will store the user's ZIP file. The contents of "Zip_Vec" will later be inserted into the vector "Image_Vec" as the last "IDAT" chunk. 
	// We will need to update the crc value (last 4-bytes) and the chunk length field (first 4-bytes), within this vector. Both fields currently set to zero. 
	// Vector "Image_Vec" stores the user's PNG image. Combined later with the vectors "Script_Vec" and "Zip_Vec", before writing complete vector out to file.
	std::vector<BYTE>
		Zip_Vec{ 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 },	// "IDAT" chunk name with 4-byte chunk length and crc fields.

		Image_Vec((std::istreambuf_iterator<char>(readimage)), std::istreambuf_iterator<char>());  // Insert user image file into vector "Image_Vec".

	// Insert user's ZIP file into vector "Zip_Vec" from index 8, just after "IDAT" chunk name.
	Zip_Vec.insert(Zip_Vec.begin() + 8, std::istreambuf_iterator<char>(readzip), std::istreambuf_iterator<char>());
	
	// Occurrence of these characters in the "IHDR" chunk (data & crc field), or the "iCCP" chunk (length field), breaks the Linux extraction script.
	const std::string BAD_CHAR = "\x22\x27\x28\x29\x3B\x3E\x60";
	
	// Make sure PNG and ZIP file specs meet program requirements. 
	Check_File_Requirements(Image_Vec, Zip_Vec, BAD_CHAR);

	Erase_Image_Chunks(Image_Vec);
	
	const uint_fast16_t MAX_SCRIPT_SIZE_BYTES = 750;	// Extraction script size limit, 750 bytes.

	const size_t
		IMAGE_SIZE = Image_Vec.size(),					// Get new image size after removing chunks.
		ZIP_SIZE = Zip_Vec.size() - 12, 				// Get size of user's ZIP file from vector "Zip_Vec", minux 12 bytes.  
										// We don't count chunk name "IDAT"(4), chunk length field(4), chunk crc field(4).
		MAX_PNG_SIZE_BYTES = imgur_size_hack ? 20971520 : 209715200,	// 200MB PNG "zip-embedded" size limit. (Flickr, the largest of the supported platforms.)
										// or 20MB PNG "zip-embedded" size limit if the "--imgur" option used.
		COMBINED_SIZE = IMAGE_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES;

	if (imgur_size_hack && COMBINED_SIZE > 5242880) { // If user selected the --imgur option, only enable it if the zip-embedded PNG image is over 5MB. 
		Image_Vec[Image_Vec.size() - 9] = '\x58';
	}

	// Check size of files. Display relevant error message and exit program if files exceed size limits.
	if ((IMAGE_SIZE + MAX_SCRIPT_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| ZIP_SIZE > MAX_PNG_SIZE_BYTES
		|| COMBINED_SIZE > MAX_PNG_SIZE_BYTES) {

		const size_t
			EXCEED_SIZE_LIMIT = (IMAGE_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES) - MAX_PNG_SIZE_BYTES,
			AVAILABLE_BYTES = MAX_PNG_SIZE_BYTES - (IMAGE_SIZE + MAX_SCRIPT_SIZE_BYTES);
		
		const std::string
			SIZE_ERR = "Size Error: File must not exceed pdvzip's file size limit of " + std::to_string(MAX_PNG_SIZE_BYTES) + " bytes.\n\n",
			COMBINED_SIZE_ERR = "\nSize Error: " + std::to_string(COMBINED_SIZE) +
			" bytes is the combined size of your PNG image + ZIP file + Script (750 bytes), \nwhich exceeds pdvzip's size limit by "
			+ std::to_string(EXCEED_SIZE_LIMIT) + " bytes. Available ZIP file size is " + std::to_string(AVAILABLE_BYTES) + " bytes.\n\n",

			ERR_MSG = (IMAGE_SIZE + MAX_SCRIPT_SIZE_BYTES > MAX_PNG_SIZE_BYTES) ? "\nPNG " + SIZE_ERR
			: (ZIP_SIZE > MAX_PNG_SIZE_BYTES ? "\nZIP " + SIZE_ERR : COMBINED_SIZE_ERR);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	uint_fast8_t
		chunk_length_index = 0, // Location of "IDAT" chunk length field for vector "Zip_Vec".
		bits = 32;

	// Write the updated "IDAT" chunk length of vector "Zip_Vec" within its length field. 
	Value_Updater(Zip_Vec, chunk_length_index, ZIP_SIZE, bits, isBig);

	// Finish building extraction script.
	Complete_Extraction_Script(Zip_Vec, Image_Vec, BAD_CHAR);
}

void Check_File_Requirements(std::vector<BYTE>& Image_Vec, std::vector<BYTE>& Zip_Vec, const std::string& BAD_CHAR) {

	const std::string
		PNG_SIG = "\x89PNG",	// Valid file header signature of PNG image.
		ZIP_SIG = "PK\x03\x04",	// Valid file header signature of ZIP file.
		IMAGE_VEC_SIG{ Image_Vec.begin(), Image_Vec.begin() + PNG_SIG.length() },	// Get file header from vector "Image_Vec". 
		ZIP_VEC_SIG{ Zip_Vec.begin() + 8, Zip_Vec.begin() + 8 + ZIP_SIG.length() };	// Get file header from vector "Zip_Vec".

	const uint_fast16_t
		IMAGE_WIDTH_DIMS = Image_Vec[18] << 8 | Image_Vec[19],	// Get image width dimensions from vector "Image_Vec".
		IMAGE_HEIGHT_DIMS = Image_Vec[22] << 8 | Image_Vec[23],	// Get image height dimensions from vector "Image_Vec".
		MAX_PNG_TRUECOLOR_DIMS = 899,		// 899 x 899 maximum supported dimensions for PNG truecolor (PNG-32/PNG-24, Color types 2 & 6).
		MAX_PNG_INDEXED_COLOR_DIMS = 4096;	// 4096 x 4096 maximum supported dimensions for PNG indexed-color (PNG-8, Color type 3).

	const uint_fast8_t
		MIN_PNG_DIMS = 68,			// 68 x 68 minimum supported dimensions for PNG indexed-color and truecolor.
		PNG_COLOR_TYPE = Image_Vec[25] == 6 ? 2 : Image_Vec[25],	// Get image color type value from vector "Image_Vec". If value is 6 (Truecolor with alpha), set the value to 2 (Truecolor).
		PNG_INDEXED_COLOR = 3,			// PNG-8, indexed color.
		PNG_TRUECOLOR = 2,			// PNG-24, Truecolor. (We also use this for PNG-32 / truecolor with alpha, 6) as we deal with them the same way.
		INZIP_NAME_LENGTH = Zip_Vec[34],	// Get length of zipped media filename from vector "Zip_Vec".
		MIN_INZIP_NAME_LENGTH = 4;		// Minimum filename length of zipped file. (First filename record within zip archive).

	const bool
		VALID_COLOR_TYPE = (PNG_COLOR_TYPE == PNG_INDEXED_COLOR) ? true		// Checking for valid color type of PNG image (PNG-32/24 Truecolor or PNG-8 Indexed color only).
		: ((PNG_COLOR_TYPE == PNG_TRUECOLOR) ? true : false),

		VALID_IMAGE_DIMS = (PNG_COLOR_TYPE == PNG_TRUECOLOR			// Checking for valid dimension size for PNG Truecolor (PNG-32/24) images.
			&& MAX_PNG_TRUECOLOR_DIMS >= IMAGE_WIDTH_DIMS
			&& MAX_PNG_TRUECOLOR_DIMS >= IMAGE_HEIGHT_DIMS
			&& IMAGE_WIDTH_DIMS >= MIN_PNG_DIMS
			&& IMAGE_HEIGHT_DIMS >= MIN_PNG_DIMS) ? true
		: ((PNG_COLOR_TYPE == PNG_INDEXED_COLOR					// Checking for valid dimension size for PNG Indexed color (PNG-8) images.
			&& MAX_PNG_INDEXED_COLOR_DIMS >= IMAGE_WIDTH_DIMS 
			&& MAX_PNG_INDEXED_COLOR_DIMS >= IMAGE_HEIGHT_DIMS
			&& IMAGE_WIDTH_DIMS >= MIN_PNG_DIMS
			&& IMAGE_HEIGHT_DIMS >= MIN_PNG_DIMS) ? true : false);

	// Check file requirements.
	if (IMAGE_VEC_SIG != PNG_SIG
		|| ZIP_VEC_SIG != ZIP_SIG
		|| !VALID_COLOR_TYPE
		|| !VALID_IMAGE_DIMS
		|| MIN_INZIP_NAME_LENGTH > INZIP_NAME_LENGTH) {

		// Requirements check failure, display relevant error message and exit program.
		const std::string
			FORMAT_ERR_MSG = "Format Error: File does not appear to be a valid",
			IMAGE_ERR_MSG1 = "\nImage Error: Dimensions of PNG image are not within the supported range.\n\nPNG-32/24 Truecolor: [68 x 68] -- [899 x 899]\nPNG-8 Indexed color: [68 x 68] -- [4096 x 4096]\n\n",
			IMAGE_ERR_MSG2 = "\nImage Error: Color type of PNG image is not supported.\n\nPNG-32/24 (Truecolor) or PNG-8 (Indexed color) only.\n\n",
			ZIP_ERR_MSG = "\nZIP Error: Media filename length within ZIP archive is too short. Four characters, minimum."
			"\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n",

			ERR_MSG = (IMAGE_VEC_SIG != PNG_SIG) ? "\nPNG " + FORMAT_ERR_MSG + " PNG image.\n\n"
			: (ZIP_VEC_SIG != ZIP_SIG) ? "\nZIP " + FORMAT_ERR_MSG + " ZIP archive.\n\n"
			: (!VALID_COLOR_TYPE) ? IMAGE_ERR_MSG2
			: ((!VALID_IMAGE_DIMS) ? IMAGE_ERR_MSG1 : ZIP_ERR_MSG);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	// Check a range of bytes within the "IHDR" chunk to make sure we have no "BAD_CHAR" characters that will break the Linux extraction script.
	// A script breaking character can appear within the width and height fields or the crc field of the "IHDR" chunk.
	// Manually modifying the dimensions (1% increase or decrease) of the image will usually resolve the issue. Repeat if necessary.

	uint_fast8_t chunk_index = 18;

	// From index location, increment through 14 bytes of the IHDR chunk within vector "Image_Vec" and compare each byte to the 7 characters within "BAD_CHAR" array.
	while (chunk_index++ != 32) { // We start checking from the 19th character position of the IHDR chunk within vector "Image_Vec".
		for (int i = 0; i < 7; i++) {
			if (Image_Vec[chunk_index] == BAD_CHAR[i]) { // "BAD_CHAR" character found, display error message and exit program.
				std::cerr <<
					"\nExtraction Script Error:  \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
					"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue. Repeat if necessary.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}
	}
}

void Erase_Image_Chunks(std::vector<BYTE>& Image_Vec) {

	// Just keep the critical PNG chunks: IHDR, PLTE, IDAT & IEND.

	std::vector<BYTE>Temp_Vec;
	
	// Copy the first 33 bytes of Image_Vec into Temp_Vec (PNG header + IHDR).
	Temp_Vec.insert(Temp_Vec.begin(), Image_Vec.begin(), Image_Vec.begin() + 33);
	
	const std::string IDAT_SIG = "IDAT";

	// Get first IDAT chunk index.
	size_t idat_index = std::search(Image_Vec.begin(), Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - Image_Vec.begin() - 4;  // -4 is to position the index at the start of the IDAT chunk's length field.

	// For PNG-8 indexed color we need to keep the PLTE chunk.
	if (Temp_Vec[25] == 3) {
		
		const std::string PLTE_SIG = "PLTE";

		// Find PLTE chunk index and copy its contents into Temp_Vec.
		const size_t PLTE_CHUNK_INDEX = std::search(Image_Vec.begin(), Image_Vec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - Image_Vec.begin() - 4;

		if (idat_index > PLTE_CHUNK_INDEX) {
			const size_t CHUNK_SIZE = (Image_Vec[PLTE_CHUNK_INDEX + 1] << 16) | Image_Vec[PLTE_CHUNK_INDEX + 2] << 8 | Image_Vec[PLTE_CHUNK_INDEX + 3];
			Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + PLTE_CHUNK_INDEX, Image_Vec.begin() + PLTE_CHUNK_INDEX + (CHUNK_SIZE + 12));
		}
	}

	// Find all the IDAT chunks and copy them into Temp_Vec.
	while (Image_Vec.size() != idat_index + 4) {
		const size_t CHUNK_SIZE = (Image_Vec[idat_index] << 24) | Image_Vec[idat_index + 1] << 16 | Image_Vec[idat_index + 2] << 8 | Image_Vec[idat_index + 3];
		Temp_Vec.insert(Temp_Vec.end(), Image_Vec.begin() + idat_index, Image_Vec.begin() + idat_index + (CHUNK_SIZE + 12));
		idat_index = std::search(Image_Vec.begin() + idat_index + 6, Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - Image_Vec.begin() - 4;
	}

	// Copy the last 12 bytes of Image_Vec into Temp_Vec (Includes IEND).
	Temp_Vec.insert(Temp_Vec.end(), Image_Vec.end() - 12, Image_Vec.end());

	Image_Vec.clear();
	Temp_Vec.swap(Image_Vec);
}

void Complete_Extraction_Script(std::vector<BYTE>& Zip_Vec, std::vector<BYTE>& Image_Vec, const std::string& BAD_CHAR) {

	/* Vector "Script_Vec" (See "script_info.txt" in this repo).

	First 4 bytes of the vector is the chunk length field, followed by chunk name "iCCP" then our barebones extraction script.

	This vector stores the shell/batch script used for extracting and opening the embedded zipped file (First filename within the zip file record).
	The barebones script is about 300 bytes. The script size limit is 750 bytes, which should be more than enough to account
	for the later addition of filenames, application & argument strings, plus other required script commands.

	Script supports both Linux & Windows. The completed script, when executed, will unzip the archive within
	the PNG image & attempt to open/play/run (depending on file type) the first filename within the zip file record, using an application
	command based on a matched file extension, or if no match found, defaulting to the operating system making the choice.

	The zipped file needs to be compatible with the operating system you are running it on.
	The completed script within the "iCCP" chunk will later be inserted into the vector "Image_Vec" which contains the user's PNG image file */

	std::vector<BYTE>Script_Vec{
		0x00, 0x00, 0x00, 0xFD, 0x69, 0x43, 0x43, 0x50, 0x73, 0x63, 0x72, 0x00, 0x00, 0x0D, 0x52,
		0x45, 0x4D, 0x3B, 0x63, 0x6C, 0x65, 0x61, 0x72, 0x3B, 0x6D, 0x6B, 0x64, 0x69, 0x72, 0x20,
		0x2E, 0x2F, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63,
		0x74, 0x65, 0x64, 0x3B, 0x6D, 0x76, 0x20, 0x22, 0x24, 0x30, 0x22, 0x20, 0x2E, 0x2F, 0x70,
		0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64,
		0x3B, 0x63, 0x64, 0x20, 0x2E, 0x2F, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78,
		0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x3B, 0x75, 0x6E, 0x7A, 0x69, 0x70, 0x20, 0x2D,
		0x71, 0x6F, 0x20, 0x22, 0x24, 0x30, 0x22, 0x3B, 0x63, 0x6C, 0x65, 0x61, 0x72, 0x3B, 0x22,
		0x22, 0x3B, 0x65, 0x78, 0x69, 0x74, 0x3B, 0x0D, 0x0A, 0x23, 0x26, 0x63, 0x6C, 0x73, 0x26,
		0x6D, 0x6B, 0x64, 0x69, 0x72, 0x20, 0x2E, 0x5C, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F,
		0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x26, 0x6D, 0x6F, 0x76, 0x65, 0x20,
		0x22, 0x25, 0x7E, 0x64, 0x70, 0x6E, 0x78, 0x30, 0x22, 0x20, 0x2E, 0x5C, 0x70, 0x64, 0x76,
		0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x26, 0x63,
		0x64, 0x20, 0x2E, 0x5C, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72,
		0x61, 0x63, 0x74, 0x65, 0x64, 0x26, 0x63, 0x6C, 0x73, 0x26, 0x74, 0x61, 0x72, 0x20, 0x2D,
		0x78, 0x66, 0x20, 0x22, 0x25, 0x7E, 0x6E, 0x30, 0x25, 0x7E, 0x78, 0x30, 0x22, 0x26, 0x20,
		0x22, 0x22, 0x26, 0x72, 0x65, 0x6E, 0x20, 0x22, 0x25, 0x7E, 0x6E, 0x30, 0x25, 0x7E, 0x78,
		0x30, 0x22, 0x20, 0x2A, 0x2E, 0x70, 0x6E, 0x67, 0x26, 0x65, 0x78, 0x69, 0x74, 0x0D, 0x0A,
		0x00, 0x00, 0x00, 0x00};

	// "App_Vec" string vector. 
	// Stores file extensions for some popular media types, along with several default application commands (+ args) that support those extensions.
	// These vector string elements will be used in the completion of our extraction script.
	std::vector<std::string> App_Vec{ "aac", "mp3", "mp4", "avi", "asf", "flv", "ebm", "mkv", "peg", "wav", "wmv", "wma", "mov", "3gp", "ogg", "pdf", ".py", "ps1", "exe",
		".sh", "vlc --play-and-exit --no-video-title-show ", "evince ", "python3 ", "pwsh ", "./", "xdg-open ", "powershell;Invoke-Item ",
		" &> /dev/null", "start /b \"\"", "pause&", "powershell", "chmod +x ", ";" };

	const uint_fast16_t MAX_SCRIPT_SIZE_BYTES = 750;	// 750 bytes extraction script size limit.

	const uint_fast8_t
		FIRST_ZIP_NAME_REC_LENGTH_INDEX = 34,	// "Zip_Vec" vector's index location for the length value of the zipped filename (First filename within the zip file record).
		FIRST_ZIP_NAME_REC_INDEX = 38,		// "Zip_Vec" start index location for the zipped filename.
		FIRST_ZIP_NAME_REC_LENGTH = Zip_Vec[FIRST_ZIP_NAME_REC_LENGTH_INDEX],	// Get character length of the zipped media filename from vector "Zip_Vec".

		// "App_Vec" vector element index values. 
		// Some "App_Vec" vector string elements are added later (via emplace_back), so don't currently appear in the above string vector.
		VIDEO_AUDIO = 20,			// "vlc" app command for Linux. 
		PDF = 21,				// "evince" app command for Linux. 
		PYTHON = 22,				// "python3" app command for Linux & Windows.
		POWERSHELL = 23,			// "pwsh" app command for Linux, for starting PowerShell scripts.
		EXECUTABLE = 24,			// "./" prepended to filename. Required when running Linux executables.
		BASH_XDG_OPEN = 25,			// "xdg-open" Linux command, runs shell scripts (.sh), opens folders & unmatched file extensions.
		FOLDER_INVOKE_ITEM = 26,		// "powershell;Invoke-Item" command used in Windows for opening zipped folders, instead of files.
		START_B = 28,				// "start /b" Windows command used to open most file types. Windows uses set default app for file type.
		WIN_POWERSHELL = 30,			// "powershell" commmand used by Windows for running PowerShell scripts.
		PREPEND_FIRST_ZIP_NAME_REC = 36;	// firstzipname with ".\" prepend characters. Required for Windows PowerShell, e.g. powershell ".\my_ps_script.ps1".

	std::string
		// Get the zipped filename string from vector "Zip_Vec". (First filename within the zip record).
		first_zip_name{ Zip_Vec.begin() + FIRST_ZIP_NAME_REC_INDEX, Zip_Vec.begin() + FIRST_ZIP_NAME_REC_INDEX + FIRST_ZIP_NAME_REC_LENGTH },

		// Get the file extension (last three bytes) from zipped filename.
		first_zip_name_ext = first_zip_name.substr(first_zip_name.length() - 3, 3),

		// Variables for optional command-line arguments.
		args_linux{},
		args_windows{};

	// Check for "." character to see if the "first_zip_name" has a file extension.
	const auto CHECK_FILE_EXT = first_zip_name.find_last_of('.');

	// Store this filename (first filename within the zip record) in "App_Vec" vector (33).
	App_Vec.emplace_back(first_zip_name);

	// When inserting string elements from vector "App_Vec" into the script (within vector "Script_Vec"), we are adding items in 
	// the order of last to first. The Windows script is completed first, followed by Linux. 
	// This order prevents the vector insert locations from changing every time we add a new string element into the vector.

	// The vector "Sequence_Vec" can be split into four sequences containing "Script_Vec" index values (high numbers) used by the 
	// "insert_index" variable and the corresponding "App_Vec" index values (low numbers) used by the "app_index" variable.

	// For example, in the 1st sequence, Sequence_Vec[0] = index 236 of "Script_Vec" ("insert_index") corresponds with
	// Sequence_Vec[5] = "App_Vec" index 33 ("app_index"), which is the "first_zip_name" string element (first filename within the zip record). 
	// "App_Vec" string element 33 (first_zip_name) will be inserted into the script (vector "Script_Vec") at index 236.

	uint_fast8_t
		app_index = 0,		// Uses the "App_Vec" index values from the vector Sequence_Vec.
		insert_index = 0,	// Uses the "Script_Vec" index values from vector Sequence_Vec.
		sequence_limit = 0;	// Stores the length limit of each of the four sequences. 

	std::vector<uint_fast16_t>Sequence_Vec{
		241, 239, 121, 120, 119, 33, 28, 27, 33, 20,	// 1st sequence for case "VIDEO_AUDIO".
		241, 239, 120, 119, 33, 28, 33, 21,		// 2nd sequence for cases "PDF, FOLDER_INVOKE_ITEM, DEFAULT".
		264, 242, 241, 239, 121, 120, 119, 29, 35, 33, 22, 34, 33, 22,					// 3rd sequence for cases "PYTHON, POWERSHELL".
		264, 242, 241, 239, 121, 120, 119, 119, 119, 119, 29, 35, 33, 28, 34, 33, 24, 32, 33, 31 }; 	// 4th sequence for cases "EXECUTABLE & BASH_XDG_OPEN".

	/*  [Sequence_Vec](insert_index)[Sequence_Vec](app_index)
		Build script example below is using the first sequence (see vector "Sequence_Vec" above).

		VIDEO_AUDIO:
		[0]>(241)[5]>(33) Windows: "Image_Vec" 241 insert index for the "first_zip_name", "App_Vec" 33.
		[1]>(239)[6]>(28) Windows: "Image_Vec" 239 insert index for "start /b", "App_Vec" 28.
		[2]>(121)[7]>(27) Linux: "Image_Vec" 121 insert index for "Dev Null", "App_Vec" 27.
		[3]>(120)[8]>(33) Linux: "Image_Vec" 120 insert index for the "first_zip_name", "App_Vec" 33.
		[4]>(119)[9]>(20) Linux: "Image_Vec" 119 insert index for "vlc", "App_Vec" 20.
		Sequence limit is 5 (value taken from first app_index value for each sequence).

		Matching a file extension from "first_zip_name_ext" within "App_Vec", we can select which application string and commands to use
		in our extraction script, which, when executed, will open/play/run (depending on file type) the extracted zipped file ("first_zip_name").

		Once the correct app extension has been matched by the "for loop" below, it passes the app_index result to the switch statement.
		The relevant Case sequence is then used in completing the extraction script within vector "Script_Vec".	*/

	for (; app_index != 26; app_index++) {
		if (App_Vec[app_index] == first_zip_name_ext) {
			// After a file extension match, any app_index value between 0 and 14 defaults to "App_Vec" 20 (vlc / VIDEO_AUDIO).
			// If over 14, we add 6 to the value. 15 + 6 = "App_Vec" 21 (evince for PDF (Linux) ), 16 + 6 = "App_Vec" 22 (python3/.py), etc.
			app_index = app_index <= 14 ? 20 : app_index += 6;
			break;
		}
	}

	// If no file extension detected, check if "first_zip_name" points to a folder (/), else assume file is a Linux executable.
	if (CHECK_FILE_EXT == 0 || CHECK_FILE_EXT > first_zip_name.length()) {
		app_index = Zip_Vec[FIRST_ZIP_NAME_REC_INDEX + FIRST_ZIP_NAME_REC_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	// Provide the user with the option to add command-line arguments for file types: .py, .ps1, .sh and .exe (no extension, defaults to .exe, if not a folder).
	// The provided arguments for your file type, will be stored within the PNG image, along with the extraction script.
	if (app_index > 21 && app_index < 26) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, args_linux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, args_windows);

		args_linux.insert(0, "\x20"),
		args_windows.insert(0, "\x20");

		App_Vec.emplace_back(args_linux),	// "App_Vec" (34).
		App_Vec.emplace_back(args_windows);	// "App_Vec" (35).
	}

	std::cout << "\nUpdating extraction script.\n";

	switch (app_index) {
	case VIDEO_AUDIO:	// Case uses 1st sequence: 241, 239, 121, 120, 119, 33,28,27,33,20.
		app_index = 5;
		break;
	case PDF:		// These two cases (with minor changes) use the 2nd sequence: 241, 239, 120, 119, 33,28,33,21.
	case FOLDER_INVOKE_ITEM:
		Sequence_Vec[15] = app_index == FOLDER_INVOKE_ITEM ? FOLDER_INVOKE_ITEM : START_B;
		Sequence_Vec[17] = app_index == FOLDER_INVOKE_ITEM ? BASH_XDG_OPEN : PDF;
		insert_index = 10;
		app_index = 14;
		break;
	case PYTHON:		// These two cases (with some changes) use the 3rd sequence: 264, 242, 241, 239, 121, 120, 119, 29,35,33,22,34,33,22.
	case POWERSHELL:
		if (app_index == POWERSHELL) {
			first_zip_name.insert(0, ".\\");		//  ".\" prepend to "firstzipname". Required for Windows PowerShell, e.g. powershell ".\my_ps_script.ps1".
			App_Vec.emplace_back(first_zip_name);		// Add the filename with the prepended ".\" to the "AppVec" vector (36).
			Sequence_Vec[31] = POWERSHELL;			// Swap index number to Linux PowerShell (pwsh 23)
			Sequence_Vec[28] = WIN_POWERSHELL;		// Swap index number to Windows PowerShell (powershell 30)
			Sequence_Vec[27] = PREPEND_FIRST_ZIP_NAME_REC;	// Swap index number to PREPEND_FIRST_ZIP_NAME_REC (36), used with the Windows powershell command.
		}
		insert_index = 18;
		app_index = 25;
		break;
	case EXECUTABLE:	// These two cases (with minor changes) use the 4th sequence: 264, 242, 241, 239, 121, 120, 119, 119, 119, 119, 29,35,33,28,34,33,24,32,33,31
	case BASH_XDG_OPEN:
		insert_index = app_index == EXECUTABLE ? 32 : 33;
		app_index = insert_index == 32 ? 42 : 43;
		break;
	default:			// Unmatched file extensions. Rely on operating system to use the set default program for dealing with unknown file type.
		insert_index = 10;	// Case uses 2nd sequence, we just need to alter one index number.
		app_index = 14;
		Sequence_Vec[17] = BASH_XDG_OPEN;	// Swap index number to BASH_XDG_OPEN (25)
	}

	// Set the sequence_limit variable using the first app_index value from each switch case sequence.
	// Reduce sequence_limit variable value by 1 if insert_index is 33 (case BASH_XDG_OPEN).
	sequence_limit = insert_index == 33 ? app_index - 1 : app_index;

	// With just a single vector insert command within the "while loop", we can insert all the required strings into the extraction script (vector "Script_Vec"), 
	// based on the sequence, which is selected by the relevant case from the above switch statement after the extension match. 
	while (sequence_limit > insert_index) {
		Script_Vec.insert(Script_Vec.begin() + Sequence_Vec[insert_index], App_Vec[Sequence_Vec[app_index]].begin(), App_Vec[Sequence_Vec[app_index]].end());
		insert_index++;
		app_index++;
	}

	// Display relevant error message and exit program if extraction script exceeds size limit.
	if (Script_Vec.size() > MAX_SCRIPT_SIZE_BYTES) {
		std::cerr << "\nScript Size Error: Extraction script exceeds maximum size of 750 bytes.\n\n";
		std::exit(EXIT_FAILURE);
	}

	uint_fast8_t
		chunk_length_index = 2,	// "Script_Vec" vector's index location for the "iCCP" chunk length field.
		bits = 16;

	// Call function to write updated chunk length value for the "iCCP" chunk into its length field. 
	// Due to its small size, the "iCCP" chunk will only use 2 bytes maximum (bits=16) of the 4 byte length field.
	Value_Updater(Script_Vec, chunk_length_index, Script_Vec.size() - 12, bits, isBig);

	// Check the first byte of the "iCCP" chunk length field to make sure the updated chunk length does not match 
	// any of the "BAD_CHAR" characters that will break the Linux extraction script.
	for (int i = 0; i < 7; i++) {
		if (Script_Vec[3] == BAD_CHAR[i]) {
			// "BAD_CHAR" found. Insert 10 bytes "." at the end of "Script_Vec" to increase chunk length. Update chunk length field. 
			// This should now skip over any BAD_CHAR characters, regardless of the chunk size (within its size limit).

			const std::string INCREASE_LENGTH_STRING = "..........";

			Script_Vec.insert(Script_Vec.begin() + Script_Vec.size() - 4, INCREASE_LENGTH_STRING.begin(), INCREASE_LENGTH_STRING.end());

			Value_Updater(Script_Vec, chunk_length_index, Script_Vec.size() - 12, bits, isBig); // Update size again.

			break;
		}
	}

	const uint_fast8_t ICCP_CHUNK_INDEX = 4;

	// Now the "iCCP" chunk is complete with the extraction script, we need to update the chunk's crc value.
	// Pass these two values (ICCP_CHUNK_INDEX & iCCP chunk size - 8) to the crc function to get correct "iCCP" chunk crc value.
	const size_t ICCP_CHUNK_CRC = Crc(&Script_Vec[ICCP_CHUNK_INDEX], Script_Vec.size() - 8);

	// Get vector index location for the "iCCP" chunk's crc field.
	size_t iccp_crc_index = Script_Vec.size() - 4;

	bits = 32;

	// Write the updated crc value into the "iCCP" chunk's crc field (bits=32) within vector "Script_Vec".
	Value_Updater(Script_Vec, iccp_crc_index, ICCP_CHUNK_CRC, bits, isBig);

	// Insert vectors "Scrip_Vec" ("iCCP" chunk with completed extraction script) & "Zip_Vec" ("IDAT" chunk with ZIP file) into vector "Image_Vec" (PNG image).
	Combine_Vectors(Image_Vec, Zip_Vec, Script_Vec);
}

void Combine_Vectors(std::vector<BYTE>& Image_Vec, std::vector<BYTE>& Zip_Vec, std::vector<BYTE>& Script_Vec) {

	srand((unsigned)time(NULL));  // For output filename.

	const std::string
		IDAT_SIG = "IDAT",
		NAME_VALUE = std::to_string(rand()),
		PDV_FILENAME = "pdvimg_" + NAME_VALUE.substr(0, 5) + ".png"; // Unique filename for the complete polyglot image.

	// This value will be used as the insert location within vector "Image_Vec" for contents of vector "Script_Vec". 
	// Script_Vec's inserted contents will appear within the "iCCP" chunk, just after the "IHDR" chunk of "Image_Vec".
	const uint_fast8_t FIRST_IDAT_INDEX = 33;

	// Set the index insert location within vector "Image_Vec" for contents of vector "Zip_Vec".
	// The insert location is just after the current last "IDAT" chuck within the vector "Image_Vec".
	const size_t LAST_IDAT_END_INDEX = Image_Vec.size() + Script_Vec.size() - 12;

	std::cout << "\nEmbedding extraction script within the PNG image.\n";

	// Insert contents of vector "Script_Vec" ("iCCP" chunk containing the extraction script) into vector "Image_Vec".	
	Image_Vec.insert((Image_Vec.begin() + FIRST_IDAT_INDEX), Script_Vec.begin(), Script_Vec.end());

	std::cout << "\nEmbedding zip file within the PNG image.\n";

	// Insert contents of vector "Zip_Vec" ("IDAT" chunk with ZIP file) into vector "Image_Vec".
	// This now becomes the new last "IDAT" chunk of the PNG image within vector "Image_Vec".
	Image_Vec.insert((Image_Vec.begin() + LAST_IDAT_END_INDEX), Zip_Vec.begin(), Zip_Vec.end());

	// Before updating the last "IDAT" chunk's crc value, adjust ZIP file offsets, within this chunk, to their new locations, so that the ZIP file continues to be extractable.
	Fix_Zip_Offset(Image_Vec, LAST_IDAT_END_INDEX);

	// Generate crc value for our (new) last "IDAT" chunk.
	// The +4 value points LAST_IDAT_INDEX variable value to the chunk name field, which is where the crc calculation needs to begin/include.
	const size_t LAST_IDAT_CRC = Crc(&Image_Vec[LAST_IDAT_END_INDEX + 4], Zip_Vec.size() - 8); // We don't include the length or crc fields (8 bytes).

	size_t crc_insert_index = Image_Vec.size() - 16;	// Get index location for last "IDAT" chunk's 4-byte crc field, from vector "Image_Vec".

	uint_fast8_t bits = 32;

	// Switch back to big-endian format.
	isBig = true;

	// Write new crc value into the last "IDAT" chunk's crc index field, within the vector "Image_Vec".
	Value_Updater(Image_Vec, crc_insert_index, LAST_IDAT_CRC, bits, isBig);

	std::ofstream write_file_fs(PDV_FILENAME, std::ios::binary);

	if (!write_file_fs) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::cout << "\nWriting zip-embedded PNG image out to disk.\n";

	// Write out to file vector "Image_Vec" now containing the completed polyglot image (Image + Script + ZIP).
	write_file_fs.write((char*)&Image_Vec[0], Image_Vec.size());

	std::string size_warning =
		"\n**Warning**\n\nDue to the file size of your zip-embedded PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
		"Flickr, ImgBB, PostImage, Reddit & ImgPile";
	
	const size_t 
		IMG_SIZE = Image_Vec.size(),
		MSG_LEN = size_warning.length();
	
	const uint_fast32_t 
		IMGUR_TWITTER_SIZE = 5242880,	// 5MB
		IMG_PILE_SIZE = 8388608,	// 8MB
		REDDIT_SIZE = 20971520, 	// 20MB
		POST_IMAGE_SIZE = 25165824,	// 24MB
		IMGBB_SIZE = 33554432;		// 32MB
		// Flickr is 200MB, this programs max size, no need to to make a variable for it.

	size_warning = (IMG_SIZE > IMG_PILE_SIZE && IMG_SIZE <= REDDIT_SIZE ? size_warning.substr(0, MSG_LEN - 10)
		: (IMG_SIZE > REDDIT_SIZE && IMG_SIZE <= POST_IMAGE_SIZE ? size_warning.substr(0, MSG_LEN - 18)
		: (IMG_SIZE > POST_IMAGE_SIZE && IMG_SIZE <= IMGBB_SIZE ? size_warning.substr(0, MSG_LEN - 29)
		: (IMG_SIZE > IMGBB_SIZE ? size_warning.substr(0, MSG_LEN - 36) : size_warning))));

	if (IMG_SIZE > IMGUR_TWITTER_SIZE && Image_Vec[Image_Vec.size() - 9] != 'X') {
		std::cerr << size_warning << ".\n";
	}

	// Test to see if --imgur option has been used.
	if (Image_Vec[Image_Vec.size() - 9] == 'X') {
		std::cout << "\n**Warning**\n\nDue to the selection of the --imgur option, you should only post this zip-embedded PNG image on Imgur.\n";
	}

	std::cout << "\nCreated zip-embedded PNG image: \"" + PDV_FILENAME + "\" Size: \"" << Image_Vec.size() << " Bytes\"";
	std::cout << "\n\nComplete!\n\nYou can now share your zip-embedded PNG image on the relevant supported platforms.\n\n";
}

void Fix_Zip_Offset(std::vector<BYTE>& Image_Vec, const size_t& LAST_IDAT_END_INDEX) {

	const std::string
		START_CENTRAL_DIR_SIG = "PK\x01\x02",
		END_CENTRAL_DIR_SIG = "PK\x05\x06",
		ZIP_SIG = "PK\x03\x04";

	// Search vector "Image_Vec" (start from last "IDAT" chunk index) to get locations for "Start Central Directory" & "End Central Directory".
	const size_t
		START_CENTRAL_DIR_INDEX = std::search(Image_Vec.begin() + LAST_IDAT_END_INDEX, Image_Vec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - Image_Vec.begin(),
		END_CENTRAL_DIR_INDEX = std::search(Image_Vec.begin() + START_CENTRAL_DIR_INDEX, Image_Vec.end(), END_CENTRAL_DIR_SIG.begin(), END_CENTRAL_DIR_SIG.end()) - Image_Vec.begin();

	size_t
		zip_records_index = END_CENTRAL_DIR_INDEX + 11,		// Index location for ZIP file records value.
		comment_length_index = END_CENTRAL_DIR_INDEX + 21,	// Index location for ZIP comment length.
		end_central_start_index = END_CENTRAL_DIR_INDEX + 19,	// Index location for End Central Start offset.
		central_local_index = START_CENTRAL_DIR_INDEX - 1,	// Initialise variable to just before Start Central index location.
		new_offset = LAST_IDAT_END_INDEX;			// Initialise variable to last "IDAT" chunk's end index location.

	uint_fast16_t zip_records = (Image_Vec[zip_records_index] << 8) | Image_Vec[zip_records_index - 1];	// Get ZIP file records value from index location of vector "Image_Vec".

	uint_fast8_t bits = 32;

	// ZIP uses little-endian format.
	isBig = false; 

	// Starting from the last "IDAT" chunk, search for ZIP file record offsets and update them to their new offset location.
	while (zip_records--) {
		new_offset = std::search(Image_Vec.begin() + new_offset + 1, Image_Vec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - Image_Vec.begin(),
		central_local_index = 45 + std::search(Image_Vec.begin() + central_local_index, Image_Vec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - Image_Vec.begin();
		Value_Updater(Image_Vec, central_local_index, new_offset, bits, isBig);
	}

	// Write updated "Start Central Directory" offset into End Central Directory's "Start Central Directory" index location within vector "Image_Vec".
	Value_Updater(Image_Vec, end_central_start_index, START_CENTRAL_DIR_INDEX, bits, isBig);

	// JAR file support. Get global comment length value from ZIP file within vector "Image_Vec" and increase it by 16 bytes to cover end of PNG file.
	// To run a JAR file, you will need to rename the '.png' extension to '.jar'.  
	// or run the command: java -jar your_image_file_name.png
	uint_fast16_t comment_length = 16 + (Image_Vec[comment_length_index] << 8) | Image_Vec[comment_length_index - 1];

	// Test to see if --imgur option has been used.
	if (Image_Vec[Image_Vec.size() - 9] == 'X') {
		comment_length++;
	}

	bits = 16;

	// Write the ZIP comment length value into the comment length index location within vector "Image_Vec".
	Value_Updater(Image_Vec, comment_length_index, comment_length, bits, isBig);
}

// The following code (slightly modified) to compute CRC32 (for "IDAT" & "iCCP" chunks) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint_fast64_t Crc_Update(const uint_fast64_t& Crc, BYTE* buf, const uint_fast64_t& len)
{
	// Table of CRCs of all 8-bit messages.
	const size_t Crc_Table[256]{
		0x00, 	    0x77073096, 0xEE0E612C, 0x990951BA, 0x76DC419,  0x706AF48F, 0xE963A535, 0x9E6495A3, 0xEDB8832,  0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x9B64C2B,  0x7EB17CBD,
		0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
		0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
		0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x1DB7106,  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x6B6B51F,
		0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0xF00F934,  0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x86D3D2D,  0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
		0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE,
		0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
		0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x3B6E20C,  0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x4DB2615,  0x73DC1683, 0xE3630B12, 0x94643B84, 0xD6D6A3E,  0x7A6A5AA8,
		0xE40ECF0B, 0x9309FF9D, 0xA00AE27,  0x7D079EB1, 0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0,
		0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703,
		0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x26D930A,
		0x9C0906A9, 0xEB0E363F, 0x72076785, 0x5005713,  0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0xCB61B38,  0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0xBDBDF21,  0x86D3D2D4, 0xF1D4E242,
		0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5,
		0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
		0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
	uint_fast64_t c = Crc;
	uint_fast32_t n;

	for (n = 0; n < len; n++) {
		c = Crc_Table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

// Return the CRC of the bytes buf[0..len-1].
uint_fast64_t Crc(BYTE* buf, const uint_fast64_t& len)
{
	return Crc_Update(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void Value_Updater(std::vector<BYTE>& vec, size_t value_insert_index, const size_t& VALUE, uint_fast8_t bits, bool isBig) {

	if (isBig) {
		while (bits) vec[value_insert_index++] = (VALUE >> (bits -= 8)) & 0xff;
	}
	else {
		while (bits) vec[value_insert_index--] = (VALUE >> (bits -= 8)) & 0xff;
	}
}

void Display_Info() {

	std::cout << R"(
PNG Data Vehicle ZIP Edition (PDVZIP v1.5). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.
		
PDVZIP enables you to embed a ZIP file within a *tweetable and "executable" PNG image.  		
		
The hosting sites will retain the embedded arbitrary data within the PNG image.  
		
PNG image size limits are platform dependant:  
Flickr (200MB), Imgbb (32MB), PostImage (24MB), Imgur (20MB / with --imgur option),
Reddit (20MB), ImgPile (8MB), Twitter & Imgur (5MB).

Using the --imgur option with pdvzip, increases the Imgur PNG upload size limit from 5MB to 20MB.

Once the PNG image has been uploaded to your Imgur page, you can grab links of the image for sharing.

*If the embedded image is over 5MB I would not recommend posting the image to the Imgur Community Page,
as the thumbnail preview fails and shows as a broken icon image.
(Clicking the "broken" preview image will still take you to the correctly displayed full image).

Once the ZIP file has been embedded within a PNG image, it can be shared on your chosen
hosting site or 'executed' whenever you want to access the embedded file(s).

From a Linux terminal: ./pdvzip_your_image.png (Image file requires executable permissions).
From a Windows terminal: First, rename the '.png' file extension to '.cmd', then .\pdvzip_your_image.cmd 

For common video/audio files, Linux requires 'vlc (VideoLAN)', Windows uses the set default media player.
PDF '.pdf', Linux requires the 'evince' program, Windows uses the set default PDF viewer.
Python '.py', Linux & Windows use the 'python3' command to run these programs.
PowerShell '.ps1', Linux uses the 'pwsh' command (if PowerShell installed), Windows uses 'powershell' to run these scripts.

For any other media type/file extension, Linux & Windows will rely on the operating system's set default application.

PNG Image Requirements for Arbitrary Data Preservation

PNG file size (image + embedded content) must not exceed the hosting site's size limits.
The site will either refuse to upload your image or it will convert your image to jpg, such as Twitter & Imgur.

Dimensions:

The following dimension size limits are specific to pdvzip and not necessarily the extact hosting site's size limits.

PNG-32 (Truecolor with alpha [6])
PNG-24 (Truecolor [2])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 899 x 899.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

Note: Images that are created & saved within your image editor as PNG-32/24 that are either 
black & white/grayscale, images with 256 colors or less, will be converted by Twitter to 
PNG-8 and you will lose the embedded content. If you want to use a simple "single" color PNG-32/24 image,
then fill an area with a gradient color instead of a single solid color.
Twitter should then keep the image as PNG-32/24.

PNG-8 (Indexed-color [3])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 4096 x 4096.

Chunks:

For example, with Twitter, you can overfill the following PNG chunks with arbitrary data, 
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

Other platforms may differ in what chunks they preserve and which you can overfill.

bKGD, cHRM, gAMA, hIST,
iCCP, (Only 10KB max. with Twitter).
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
PLTE, (Use only with PNG-32 & PNG-24 for arbitrary data).
pHYs, sBIT, sPLT, sRGB, (Imgur does not keep the pHYs chunk).
tRNS. (Not recommended, may distort image).

This program uses the iCCP (extraction script) and IDAT (zip file) chunk names for storing arbitrary data.

ZIP File Size & Other Information

To work out the maximum ZIP file size, start with the hosting site's size limit,  
minus your PNG image size, minus 750 bytes (internal shell extraction script size).   

Twitter example: (5MB) 5,242,880 - (307,200 + 750) = 4,934,930 bytes available for your ZIP file.

The less detailed the image, the more space available for the ZIP.

Make sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
Do not include other .zip files within the main ZIP archive. (.rar files are ok).
Do not include other pdvzip created PNG image files within the main ZIP archive, as they are essentially .zip files.
Use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.
A file without an extension will be treated as a Linux executable.
Paint.net application is recommended for easily creating compatible PNG image files.
 
)";
}
