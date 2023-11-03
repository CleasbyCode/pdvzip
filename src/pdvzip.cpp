// 	PNG Data Vehicle, ZIP Edition (PDVZIP v1.4). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux):
// 	$ g++ pdvzip.cpp -O2 -s -o pdvzip

// 	Run it:
// 	$ ./pdvzip

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

typedef unsigned char BYTE;

// Update values, such as chunk lengths, crc, file sizes and other values.
// Writes them into the relevant vector index locations. Overwrites previous values.
class ValueUpdater {
public:
	void Value(std::vector<BYTE>& vec, uint32_t valueinsertindex, const uint32_t VALUE, uint8_t bits, bool islittle) {
		if (islittle) {
			// Write value in little-endian format. (For zip file).
			while (bits) vec[valueinsertindex--] = (VALUE >> (bits -= 8)) & 0xff;
		}
		else {
			// Write value in big-endian format. 
			while (bits) vec[valueinsertindex++] = (VALUE >> (bits -= 8)) & 0xff;
		}
	}
} *update;

void 
	// Read-in user PNG image and ZIP file. Store the data in vectors.
	storeFiles(std::ifstream&, std::ifstream&, const std::string&, const std::string&),

	// Make sure user PNG image and ZIP file fulfil valid program requirements. Display relevant error message and exit program if checks fail.
	checkFileRequirements(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]),

	// Search and remove all unnecessary PNG chunks found before the first "IDAT" chunk.
	eraseChunks(std::vector<BYTE>&),

	// Imgur fix. Search for and change characters within the PNG "PLTE" chunk to prevent the Linux extraction script breaking.
	modifyPaletteChunk(std::vector<BYTE>&, const char(&)[]),

	// Update barebones extraction script determined by embedded file content. 
	completeScript(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]),

	// Insert contents of vectors storing user ZIP file and the completed extraction script into the vector containing PNG image, then write vector's content out to file.
	combineVectors(std::vector< BYTE>&, std::vector<BYTE>&, std::vector<BYTE>&),

	// Adjust embedded ZIP file offsets within the PNG image, so that it remains a valid, working ZIP archive.
	fixZipOffset(std::vector<BYTE>&, const uint32_t),

	// Output to screen detailed program usage information.
	displayInfo();

uint32_t 
	// Code to compute CRC32 (for "IDAT" & "PLTE" chunks within this program) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
	crcUpdate(const uint32_t, BYTE*, const uint32_t),
	crc(BYTE*, const uint32_t);

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	else if (argc < 3 || argc > 3) {
		std::cout << "\nUsage: pdvzip <png_image> <zip_file>\n\t\bpdvzip --info\n\n";
	}
	else {
		const std::string
			IMG_NAME = argv[1],
			ZIP_NAME = argv[2];

		std::cout << "\nReading files. Please wait...\n";

		// Open user files.
		std::ifstream
			readimage(IMG_NAME, std::ios::binary),
			readzip(ZIP_NAME, std::ios::binary);

		if (readimage && readzip) {
			// OK, now read files into vectors.
			storeFiles(readimage, readzip, IMG_NAME, ZIP_NAME);
		}
		else { // Display relevant error message and exit program if any file fails to open.
			const std::string
				READ_ERR_MSG = "Read Error: Unable to open/read file: ",
				ERR_MSG = !readimage ? "\nPNG " + READ_ERR_MSG + "'" + IMG_NAME + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_NAME + "'\n\n";
			std::cerr << ERR_MSG;
			std::exit(EXIT_FAILURE);
		}
	}
	return 0;
}

void storeFiles(std::ifstream& readimage, std::ifstream& readzip, const std::string& IMG_FILE, const std::string& ZIP_FILE) {

	// Vector "ZipVec" will store the user's ZIP file. The contents of "ZipVec" will later be inserted into the vector "ImageVec" as the last "IDAT" chunk. 
	// We will need to update the crc value (last 4-bytes) and the chunk length field (first 4-bytes), within this vector. Both fields currently set to zero. 
	// Vector "ImageVec" stores the user's PNG image. Combined later with the vectors "ScriptVec" and "ZipVec", before writing complete vector out to file.
	std::vector<BYTE>
		ZipVec{ 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x00, 0x00, 0x00, 0x00 },	// "IDAT" chunk name with 4-byte chunk length and crc fields.

		ImageVec((std::istreambuf_iterator<char>(readimage)), std::istreambuf_iterator<char>());  // Insert user image file into vector "ImageVec".

	// Insert user's ZIP file into vector "ZipVec" from index 8, just after "IDAT" chunk name.
	ZipVec.insert(ZipVec.begin() + 8, std::istreambuf_iterator<char>(readzip), std::istreambuf_iterator<char>());

	// Get size of user's ZIP file from vector "ZipVec", minux 12 bytes.  We don't count chunk name "IDAT"(4), chunk length field(4), chunk crc field(4).
	const uint64_t ZIP_SIZE = ZipVec.size() - 12;

	// Array used by different functions within this program.
	// Occurrence of these characters in the "IHDR" chunk (data/crc field), "PLTE" chunk (data/crc field) or "hIST" chunk (length field), breaks the Linux extraction script.
	const char BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' };

	// Make sure PNG and ZIP file specs meet program requirements. 
	checkFileRequirements(ImageVec, ZipVec, BAD_CHAR);

	// Find and remove unnecessary PNG chunks.
	eraseChunks(ImageVec);

	const uint16_t MAX_SCRIPT_SIZE_BYTES = 750;	// Extraction script size limit, 750 bytes.

	const uint32_t
		IMG_SIZE = static_cast<uint32_t>(ImageVec.size()),
		MAX_PNG_SIZE_BYTES = 209715200,	// 200MB, PNG "file-embedded" size limit.
		COMBINED_SIZE = IMG_SIZE + static_cast<uint32_t>(ZIP_SIZE) + MAX_SCRIPT_SIZE_BYTES;

	// Check size of files. Display relevant error message and exit program if files exceed size limits.
	if ((IMG_SIZE + MAX_SCRIPT_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| ZIP_SIZE > MAX_PNG_SIZE_BYTES
		|| COMBINED_SIZE > MAX_PNG_SIZE_BYTES) {

		const uint32_t
			EXCEED_SIZE_LIMIT = (IMG_SIZE + static_cast<uint32_t>(ZIP_SIZE) + MAX_SCRIPT_SIZE_BYTES) - MAX_PNG_SIZE_BYTES,
			AVAILABLE_BYTES = MAX_PNG_SIZE_BYTES - (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES);

		const std::string
			SIZE_ERR = "Size Error: File must not exceed pdvzip's file size limit of 200MB (209,715,200 bytes).\n\n",
			COMBINED_SIZE_ERR = "\nSize Error: " + std::to_string(COMBINED_SIZE) +
			" bytes is the combined size of your PNG image + ZIP file + Script (750 bytes), \nwhich exceeds pdvzip's 200MB size limit by "
			+ std::to_string(EXCEED_SIZE_LIMIT) + " bytes. Available ZIP file size is " + std::to_string(AVAILABLE_BYTES) + " bytes.\n\n",

			ERR_MSG = (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES > MAX_PNG_SIZE_BYTES) ? "\nPNG " + SIZE_ERR
			: (ZIP_SIZE > MAX_PNG_SIZE_BYTES ? "\nZIP " + SIZE_ERR : COMBINED_SIZE_ERR);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	// Check if image is PNG-8 format (Indexed-colour / Colour type: 3).
	if (ImageVec[25] == 3) {
		// Alter characters within the "PLTE" chunk to prevent the extraction script from breaking.
		modifyPaletteChunk(ImageVec, BAD_CHAR);
	}

	uint8_t
		chunklengthindex = 0, // Location of "IDAT" chunk length field for vector "ZipVec".
		bits = 32;

	// Write the updated "IDAT" chunk length of vector "ZipVec" within its length field.
	update->Value(ZipVec, chunklengthindex, static_cast<uint32_t>(ZIP_SIZE), bits, false);

	// Finish building extraction script.
	completeScript(ZipVec, ImageVec, BAD_CHAR);
}

void checkFileRequirements(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, const char(&BAD_CHAR)[]) {

	const std::string
		PNG_SIG = "\x89PNG",	// Valid file header signature of PNG image.
		ZIP_SIG = "PK\x03\x04",	// Valid file header signature of ZIP file.
		IMG_VEC_HDR{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() },		// Get file header from vector "ImageVec". 
		ZIP_VEC_HDR{ ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_SIG.length() };	// Get file header from vector "ZipVec".

	const uint16_t
		IMAGE_DIMS_WIDTH = ImageVec[18] << 8 | ImageVec[19],	// Get image width dimensions from vector "ImageVec".
		IMAGE_DIMS_HEIGHT = ImageVec[22] << 8 | ImageVec[23],	// Get image height dimensions from vector "ImageVec".
		PNG_TRUECOLOUR_MAX_DIMS = 899,		// 899 x 899 maximum supported dimensions for PNG truecolour (PNG-32/PNG-24, Colour types 2 & 6).
		PNG_INDEXCOLOUR_MAX_DIMS = 4096;	// 4096 x 4096 maximum supported dimensions for PNG indexed-colour (PNG-8, Colour type 3).
	
	const uint8_t
		COLOUR_TYPE = ImageVec[25] == 6 ? 2 : ImageVec[25],	// Get image colour type value from vector "ImageVec". If value is 6 (Truecolour with alpha), set the value to 2 (Truecolour).
		PNG_MIN_DIMS = 68,		// 68 x 68 minimum supported dimensions for PNG indexed-colour and truecolour.
		INZIP_NAME_LENGTH = ZipVec[34],	// Get length of zipped media filename from vector "ZipVec".
		PNG_INDEXCOLOUR = 3,		// PNG-8, indexed colour.
		PNG_TRUECOLOUR = 2,		// PNG-24, truecolour. (We also use this for PNG-32 / truecolour with alpha, 6) as we deal with them the same way.
		INZIP_NAME_MIN_LENGTH = 4;	// Minimum filename length of zipped file. (First filename record within zip archive).

	const bool
		VALID_COLOUR_TYPE = (COLOUR_TYPE == PNG_INDEXCOLOUR) ? true
		: ((COLOUR_TYPE == PNG_TRUECOLOUR) ? true : false),

		VALID_IMAGE_DIMS = (COLOUR_TYPE == PNG_TRUECOLOUR
			&& IMAGE_DIMS_WIDTH <= PNG_TRUECOLOUR_MAX_DIMS
			&& IMAGE_DIMS_HEIGHT <= PNG_TRUECOLOUR_MAX_DIMS
			&& IMAGE_DIMS_WIDTH >= PNG_MIN_DIMS
			&& IMAGE_DIMS_HEIGHT >= PNG_MIN_DIMS) ? true
		: ((COLOUR_TYPE == PNG_INDEXCOLOUR
			&& IMAGE_DIMS_WIDTH <= PNG_INDEXCOLOUR_MAX_DIMS
			&& IMAGE_DIMS_HEIGHT <= PNG_INDEXCOLOUR_MAX_DIMS
			&& IMAGE_DIMS_WIDTH >= PNG_MIN_DIMS
			&& IMAGE_DIMS_HEIGHT >= PNG_MIN_DIMS) ? true : false);

	// Check file requirements.
	if (IMG_VEC_HDR != PNG_SIG
		|| ZIP_VEC_HDR != ZIP_SIG
		|| !VALID_COLOUR_TYPE
		|| !VALID_IMAGE_DIMS
		|| INZIP_NAME_MIN_LENGTH > INZIP_NAME_LENGTH) { 
		
		// Requirements check failure, display relevant error message and exit program.
		const std::string
			HEADER_ERR_MSG = "Format Error: File does not appear to be a valid",
			IMAGE_ERR_MSG1 = "\nPNG Image Error: Dimensions of PNG image do not meet program requirements.\nSee 'pdvzip --info' for more details.\n\n",
			IMAGE_ERR_MSG2 = "\nPNG Image Error: Colour type of PNG image does not meet program requirements.\nSee 'pdvzip --info' for more details.\n\n",
			ZIP_ERR_MSG = "\nZIP Error: Media filename length within ZIP archive is too short. Four characters, minimum."
			"\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n",

			ERR_MSG = (IMG_VEC_HDR != PNG_SIG) ? "\nPNG " + HEADER_ERR_MSG + " PNG image.\n\n"
			: (ZIP_VEC_HDR != ZIP_SIG) ? "\nZIP " + HEADER_ERR_MSG + " ZIP archive.\n\n"
			: (!VALID_COLOUR_TYPE) ? IMAGE_ERR_MSG2
			: ((!VALID_IMAGE_DIMS) ? IMAGE_ERR_MSG1 : ZIP_ERR_MSG);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	// Check a range of bytes within the "IHDR" chunk to make sure we have no "BAD_CHAR" characters that will break the Linux extraction script.
	// A script breaking character can appear within the width and height fields or the crc field of the "IHDR" chunk.
	// Manually modifying the dimensions (1% increase or decrease) of the image will usually resolve the issue. Repeat if necessary.
	
	uint8_t chunkindex = 18;

	// From index location, increment through 14 bytes of the IHDR chunk within vector "ImageVec" and compare each byte to the 7 characters within "BAD_CHAR" array.
	while (chunkindex++ != 32) { // We start checking from the 19th character position of the IHDR chunk within vector "ImageVec".
		for (uint8_t i = 0; i < 7; i++) {
			if (ImageVec[chunkindex] == BAD_CHAR[i]) { // "BAD_CHAR" character found, display error message and exit program.
				std::cerr <<
					"\nExtraction Script Error:  \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
					"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue. Repeat if necessary.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}
	}
}

void eraseChunks(std::vector<BYTE>& ImageVec) {

	const std::string CHUNKS[15]{ "IDAT", "bKGD", "cHRM", "iCCP", "sRGB", "hIST", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	for (uint8_t chunkindex = 14; chunkindex > 0; chunkindex--) {
		const uint32_t
			// Get the first "IDAT" chunk index location. Don't remove any chunks after this point.
			FIRST_IDAT_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), CHUNKS[0].begin(), CHUNKS[0].end()) - ImageVec.begin()),
			// From last to first, search and get the index location of each chunk to remove.
			CHUNK_FOUND_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), CHUNKS[chunkindex].begin(), CHUNKS[chunkindex].end()) - ImageVec.begin()) - 4;
		// If found chunk is located before the first "IDAT" chunk, erase it.
		if (FIRST_IDAT_INDEX > CHUNK_FOUND_INDEX) {
			const uint32_t CHUNK_SIZE = (ImageVec[CHUNK_FOUND_INDEX + 1] << 16) | ImageVec[CHUNK_FOUND_INDEX + 2] << 8 | ImageVec[CHUNK_FOUND_INDEX + 3];
			ImageVec.erase(ImageVec.begin() + CHUNK_FOUND_INDEX, ImageVec.begin() + CHUNK_FOUND_INDEX + (CHUNK_SIZE + 12));
			chunkindex++; // Increment chunkIndex so that we search again for the same chunk name, in case of multiple occurrences.
		}
	}
}

void modifyPaletteChunk(std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	// PNG-8, Imgur (image hosting site), Linux issue. 
	// The "PLTE" chunk must be located before the "hIST" chunk (hIST stores extraction script) for pdvzip images to display correctly on Imgur.
	// This means we have to read through the PLTE chunk before reading/executing the Linux extraction script. This can be a problem because some individual characters, sequence or combination of certain characters 
	// that may appear within the PLTE chunk, will break the extraction script. This section contains a number of fixes to prevent the extraction script from breaking.
	
	const std::string PLTE_SIG = "PLTE";

	const uint32_t PLTE_CHUNK_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - ImageVec.begin()); // Search for index location of "PLTE" chunk name within vector "ImageVec".
		
	const uint16_t PLTE_CHUNK_SIZE = (ImageVec[PLTE_CHUNK_INDEX - 2] << 8) | ImageVec[PLTE_CHUNK_INDEX - 1]; // Get "PLTE" chunk size from vector "ImageVec".

	// Replace "BAD_CHAR" characters with "GOOD_CHAR" characters.
	// While image corruption is possible when altering the "PLTE" chunk, the majority of images should be fine with these replacement characters.
	const char GOOD_CHAR[7]{ '*', '&', '=', '}', 'a', '?', ':' };

	uint8_t charcount = 0;

	for (uint32_t i = PLTE_CHUNK_INDEX; i < (PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4)); i++) {

		// Individual "BAD_CHAR" characters within "PLTE" chunk that will break the Linux extraction script.
		// Replace matched "BAD_CHAR" with the relevant "GOOD_CHAR".
		ImageVec[i] = (ImageVec[i] == BAD_CHAR[0]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[1]) ? GOOD_CHAR[1] : (ImageVec[i] == BAD_CHAR[2]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[3]) ? GOOD_CHAR[4] : (ImageVec[i] == BAD_CHAR[4]) ? GOOD_CHAR[0]
			: (ImageVec[i] == BAD_CHAR[5]) ? GOOD_CHAR[5] : ((ImageVec[i] == BAD_CHAR[6]) ? GOOD_CHAR[6] : ImageVec[i]);

		// Character combinations that will break the Linux extraction script. 
		if ((ImageVec[i] == '&' && ImageVec[i + 1] == '!')	// e.g. "&!"
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '}')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '{')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == ')')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == '(')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == '&')
			|| (ImageVec[i] == '|' && ImageVec[i + 1] == '}' && ImageVec[i + 2] == '|')	// e.g. "|}|"
			|| (ImageVec[i] == '|' && ImageVec[i + 1] == '{' && ImageVec[i + 2] == '|')
			|| (ImageVec[i] == '|' && ImageVec[i + 1] == '!' && ImageVec[i + 2] == '|') 	// e.g. "|!|"
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '\x0')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '#')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '|')
			|| (ImageVec[i] == '<' && ImageVec[i + 1] == '|')
			|| (ImageVec[i] == '<' && ImageVec[i + 1] == '&')
			|| (ImageVec[i] == '<' && ImageVec[i + 1] == ')'))
		{
			// Replace character from the above matched problem combinations with the relevant "GOOD_CHAR" to prevent script failure.
			if (ImageVec[i] == '\x0a') {
				ImageVec[i + 1] = GOOD_CHAR[0];
			}
			else {
				ImageVec[i] = ImageVec[i] == '<' ? GOOD_CHAR[2] : GOOD_CHAR[0];
			}
		}

		// Two or more characters of "&" or "|" in a row will break the script. // &&, ||, &&&, |||, etc.
		if (ImageVec[i] == '&' || ImageVec[i] == '|') {
			charcount++;
			if (charcount > 1) { // Once we find two of those characters in a row, replace one of them with the relevant "GOOD_CHAR".
				ImageVec[i] = (ImageVec[i] == '&' ? GOOD_CHAR[0] : GOOD_CHAR[2]);
				charcount = 0;
			}
		}
		else {
			charcount = 0;
		}

		uint8_t
			digitpos = 0,
			lessthancharpos = 2;

		const uint8_t
			MAX_DIGIT_POS = 12,
			DIGIT_RANGE_START = 48,
			DIGIT_RANGE_END = 57;

		// The "Less-than" character "<" followed by a number, or a sequence of numbers (up to 11 digits), and ending with the same "less-than" character "<", breaks the Linux extraction script. 
		// e.g. "<1<, <4356<, <293459< <35242369849<". 
		if (ImageVec[i] == '<') { // Found this "<" less_than character, now check if it is followed by a number character (0 - 9) and ends with another "<" less_than character.
			while (MAX_DIGIT_POS != digitpos++) {
				// Digit range characters 0 to 9 (ASCII values 48 to 57)
				if (ImageVec[i + digitpos] >= DIGIT_RANGE_START && ImageVec[i + digitpos] <= DIGIT_RANGE_END && ImageVec[i + lessthancharpos++] == '<') { // Found pattern sequence 
					ImageVec[i] = GOOD_CHAR[2];  	// Replace character "<" at the beginning of the string with the relevant "GOOD_CHAR" character to break the pattern and fix this issue.
					digitpos = MAX_DIGIT_POS;	// Issue fixed, quit the loop.  
				}
			}
		}
	}

	uint8_t
		modvalue = 255,
		bits = 32;

	bool redocrc{};

	do {
		redocrc = false;

		// After modifying the "PLTE" chunk, we need to update the chunk's crc value.
		// Pass these two values (PLTE_CHUNK_INDEX & PLTE_CHUNK_SIZE + 4) to the crc function to get correct "PLTE" chunk crc value.
		const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_CHUNK_INDEX], PLTE_CHUNK_SIZE + 4);

		// Get vector index locations for "PLTE" crc chunk field and "PLTE" modvalue.
		uint32_t
			crcindex = PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4);

		const uint32_t
			CRC_END_INDEX = crcindex + 3,
			MOD_VALUE_INSERT_INDEX = crcindex - 1;

		// Write the updated crc value into the "PLTE" chunk's crc field (bits=32) within vector "ImageVec".
		update->Value(ImageVec, crcindex, PLTE_CHUNK_CRC, bits, false);

		// Make sure the new crc value does not contain any of the "BAD_CHAR" characters, which will break the Linux extraction script.
		// If we find a "BAD_CHAR" in the new crc value, modify one byte in the "PLTE" chunk (modvalue location), then recalculate crc value. 
		// Repeat process if required until no BAD_CHAR found.
		for (uint8_t i = 0; i < 7; i++) {
			if (ImageVec[crcindex++] == BAD_CHAR[i]) {
				ImageVec[MOD_VALUE_INSERT_INDEX] = modvalue;
				modvalue--;
				redocrc = true;
				break;
			} 
			else if (crcindex > CRC_END_INDEX){
				break;
			}
		}
	} while (redocrc);
}

void completeScript(std::vector<BYTE>& ZipVec, std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	/* Vector "ScriptVec" (See "script_info.txt" in this repo).

	First 4 bytes of the vector is the chunk length field, followed by chunk name "hIST" then our barebones extraction script.
	"hIST" is a valid ancillary PNG chunk type/name. It does not require a correct crc value, so no update needed.

	This vector stores the shell/batch script used for extracting and opening the embedded zipped file (First filename within the zip file record).
	The barebones script is about 300 bytes. The script size limit is 750 bytes, which should be more than enough to account
	for the later addition of filenames, application & argument strings, plus other required script commands.

	Script supports both Linux & Windows. The completed script, when executed, will unzip the archive within
	the PNG image & attempt to open/play/run (depending on file type) the first filename within the zip file record, using an application
	command based on a matched file extension, or if no match found, defaulting to the operating system making the choice.

	The zipped file needs to be compatible with the operating system you are running it on.
	The completed script within the "hIST" chunk will later be inserted into the vector "ImageVec" which contains the user's PNG image file */

	std::vector<BYTE>ScriptVec{
		0x00, 0x00, 0x00, 0xFD, 0x68, 0x49, 0x53, 0x54, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x63, 0x6C,
		0x65, 0x61, 0x72, 0x3B, 0x6D, 0x6B, 0x64, 0x69, 0x72, 0x20, 0x2E, 0x2F, 0x70, 0x64, 0x76,
		0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x3B, 0x6D,
		0x76, 0x20, 0x22, 0x24, 0x30, 0x22, 0x20, 0x2E, 0x2F, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70,
		0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x3B, 0x63, 0x64, 0x20, 0x2E,
		0x2F, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74,
		0x65, 0x64, 0x3B, 0x75, 0x6E, 0x7A, 0x69, 0x70, 0x20, 0x2D, 0x71, 0x6F, 0x20, 0x22, 0x24,
		0x30, 0x22, 0x3B, 0x63, 0x6C, 0x65, 0x61, 0x72, 0x3B, 0x22, 0x22, 0x3B, 0x65, 0x78, 0x69,
		0x74, 0x3B, 0x0D, 0x0A, 0x23, 0x26, 0x63, 0x6C, 0x73, 0x26, 0x6D, 0x6B, 0x64, 0x69, 0x72,
		0x20, 0x2E, 0x5C, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61,
		0x63, 0x74, 0x65, 0x64, 0x26, 0x6D, 0x6F, 0x76, 0x65, 0x20, 0x22, 0x25, 0x7E, 0x64, 0x70,
		0x6E, 0x78, 0x30, 0x22, 0x20, 0x2E, 0x5C, 0x70, 0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65,
		0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64, 0x26, 0x63, 0x64, 0x20, 0x2E, 0x5C, 0x70,
		0x64, 0x76, 0x7A, 0x69, 0x70, 0x5F, 0x65, 0x78, 0x74, 0x72, 0x61, 0x63, 0x74, 0x65, 0x64,
		0x26, 0x63, 0x6C, 0x73, 0x26, 0x74, 0x61, 0x72, 0x20, 0x2D, 0x78, 0x66, 0x20, 0x22, 0x25,
		0x7E, 0x6E, 0x30, 0x25, 0x7E, 0x78, 0x30, 0x22, 0x26, 0x20, 0x22, 0x22, 0x26, 0x72, 0x65,
		0x6E, 0x20, 0x22, 0x25, 0x7E, 0x6E, 0x30, 0x25, 0x7E, 0x78, 0x30, 0x22, 0x20, 0x2A, 0x2E,
		0x70, 0x6E, 0x67, 0x26, 0x65, 0x78, 0x69, 0x74, 0x0D, 0x0A };

	// "AppVec" string vector. 
	// Stores file extensions for some popular media types, along with several default application commands (+ args) that support those extensions.
	// These vector string elements will be used in the completion of our extraction script.
	std::vector<std::string> AppVec{ "aac", "mp3", "mp4", "avi", "asf", "flv", "ebm", "mkv", "peg", "wav", "wmv", "wma", "mov", "3gp", "ogg", "pdf", ".py", "ps1", "exe",
		".sh", "vlc --play-and-exit --no-video-title-show ", "evince ", "python3 ", "pwsh ", "./", "xdg-open ", "powershell;Invoke-Item ",
		" &> /dev/null", "start /b \"\"", "pause&", "powershell", "chmod +x ", ";" };

	const uint16_t MAX_SCRIPT_SIZE_BYTES = 750;	// 750 bytes extraction script size limit.

	const uint8_t
		FIRST_ZIP_NAME_REC_LENGTH_INDEX = 34,	// "ZipVec" vector's index location for the length value of the zipped filename (First filename within the zip file record).
		FIRST_ZIP_NAME_REC_INDEX = 38,		// "ZipVec" start index location for the zipped filename.
		FIRST_ZIP_NAME_REC_LENGTH = ZipVec[FIRST_ZIP_NAME_REC_LENGTH_INDEX],	// Get character length of the zipped media filename from vector "ZipVec".
		
		// "AppVec" vector element index values. 
		// Some "AppVec" vector string elements are added later (via push_back), so don't currently appear in the above string vector.
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
		// Get the zipped filename string from vector "ZipVec". (First filename within the zip record).
		firstzipname{ ZipVec.begin() + FIRST_ZIP_NAME_REC_INDEX, ZipVec.begin() + FIRST_ZIP_NAME_REC_INDEX + FIRST_ZIP_NAME_REC_LENGTH },

		// Get the file extension (last three bytes) from zipped filename.
		firstzipnameext = firstzipname.substr(firstzipname.length() - 3, 3),

		// Variables for optional command-line arguments.
		argslinux {},
		argswindows {};

	// Check for "." character to see if the "firstzipname" has a file extension.
	const uint32_t CHECK_FILE_EXT = static_cast<uint32_t>(firstzipname.find_last_of('.'));

	// Store this filename (first filename within the zip record) in "AppVec" vector (33).
	AppVec.push_back(firstzipname);

	// When inserting string elements from vector "AppVec" into the script (within vector "ScriptVec"), we are adding items in 
	// the order of last to first. The Windows script is completed first, followed by Linux. 
	// This order prevents the vector insert locations from changing every time we add a new string element into the vector.

	// The vector "SequenceVec" can be split into four sequences containing "ScriptVec" index values (high numbers) used by the 
	// "insertindex" variable and the corresponding "AppVec" index values (low numbers) used by the "appindex" variable.

	// For example, in the 1st sequence, SequenceVec[0] = index 236 of "ScriptVec" ("insertindex") corresponds with
	// SequenceVec[5] = "AppVec" index 33 ("appindex"), which is the "firstzipname" string element (first filename within the zip record). 
	// "AppVec" string element 33 (firstzipname) will be inserted into the script (vector "ScriptVec") at index 236.

	uint8_t
		appindex = 0,		// Uses the "AppVec" index values from the vector SequenceVec.
		insertindex = 0,	// Uses the "ScriptVec" index values from vector SequenceVec.
		sequencelimit = 0;	// Stores the length limit of each of the four sequences. 

	std::vector<uint16_t>SequenceVec{
				236,234,116,115,114, 33,28,27,33,20,	// 1st sequence for case "VIDEO_AUDIO".
				236,234,115,114, 33,28,33,21,		// 2nd sequence for cases "PDF, FOLDER_INVOKE_ITEM, DEFAULT".
				259,237,236,234,116,115,114, 29,35,33,22,34,33,22,	// 3rd sequence for cases "PYTHON, POWERSHELL".
				259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31 }; // 4th sequence for cases "EXECUTABLE & BASH_XDG_OPEN".
		
	/*  [SequenceVec](insertIndex)[SequenceVec](appIndex)
		Build script example below is using the first sequence (see vector "SequenceVec" above).

		VIDEO_AUDIO:
		[0]>(236)[5]>(33) Windows: "ImageVec" 236 insert index for the "firstzipname", "AppVec" 33.
		[1]>(234)[6]>(28) Windows: "ImageVec" 234 insert index for "start /b", "AppVec" 28.
		[2]>(116)[7]>(27) Linux: "ImageVec" 116 insert index for "Dev Null", "AppVec" 27.
		[3]>(115)[8]>(33) Linux: "ImageVec" 115 insert index for the "firstzipname", "AppVec" 33.
		[4]>(114)[9]>(20) Linux: "ImageVec" 114 insert index for "vlc", "AppVec" 20.
		Sequence limit is 5 (value taken from first appindex value for each sequence).

		Matching a file extension from "firstzipnamext" within "AppVec", we can select which application string and commands to use
		in our extraction script, which, when executed, will open/play/run (depending on file type) the extracted zipped file ("firstzipname").

		Once the correct app extension has been matched by the "for loop" below, it passes the appindex result to the switch statement.
		The relevant Case sequence is then used in completing the extraction script within vector "ScriptVec".	*/

	for (; appindex != 26; appindex++) {
		if (AppVec[appindex] == firstzipnameext) {
			// After a file extension match, any appindex value between 0 and 14 defaults to "AppVec" 20 (vlc / VIDEO_AUDIO).
			// If over 14, we add 6 to the value. 15 + 6 = "AppVec" 21 (evince for PDF (Linux) ), 16 + 6 = "AppVec" 22 (python3/.py), etc.
			appindex = appindex <= 14 ? 20 : appindex += 6;
			break;
		}
	}

	// If no file extension detected, check if "firstzipname" points to a folder (/), else assume file is a Linux executable.
	if (CHECK_FILE_EXT == 0 || CHECK_FILE_EXT > firstzipname.length()) {
		appindex = ZipVec[FIRST_ZIP_NAME_REC_INDEX + FIRST_ZIP_NAME_REC_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	// Provide the user with the option to add command-line arguments for file types: .py, .ps1, .sh and .exe (no extension, defaults to .exe, if not a folder).
	// The provided arguments for your file type, will be stored within the PNG image, along with the extraction script.
	if (appindex > 21 && appindex < 26) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, argslinux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, argswindows);

		argslinux.insert(0, "\x20"), 
		argswindows.insert(0, "\x20");

		AppVec.push_back(argslinux),	// "AppVec" (0x22).
		AppVec.push_back(argswindows);	// "AppVec" (0x23).
	}

	std::cout << "\nUpdating extraction script.\n";

	switch (appindex) {
	case VIDEO_AUDIO:	// Case uses 1st sequence: 236,234,116,115,114, 33,28,27,33,20.
		appindex = 5;
		break;
	case PDF:		// These two cases (with minor changes) use the 2nd sequence: 236,234,115,114, 33,28,33,21.
	case FOLDER_INVOKE_ITEM:
		SequenceVec[15] = appindex == FOLDER_INVOKE_ITEM ? FOLDER_INVOKE_ITEM : START_B;
		SequenceVec[17] = appindex == FOLDER_INVOKE_ITEM ? BASH_XDG_OPEN : PDF;
		insertindex = 10,
		appindex = 14;
		break;
	case PYTHON:		// These two cases (with some changes) use the 3rd sequence: 259,237,236,234,116,115,114, 29,35,33,22,34,33,22.
	case POWERSHELL:
		if (appindex == POWERSHELL) {
			firstzipname.insert(0, ".\\");			//  ".\" prepend to "firstzipname". Required for Windows PowerShell, e.g. powershell ".\my_ps_script.ps1".
			AppVec.push_back(firstzipname);			// Add the filename with the prepended ".\" to the "AppVec" vector (36).
			SequenceVec[31] = POWERSHELL,			// Swap index number to Linux PowerShell (pwsh 23)
			SequenceVec[28] = WIN_POWERSHELL;		// Swap index number to Windows PowerShell (powershell 30)
			SequenceVec[27] = PREPEND_FIRST_ZIP_NAME_REC;	// Swap index number to PREPEND_FIRST_ZIP_NAME_REC (36), used with the Windows powershell command.
		}
		insertindex = 18,
		appindex = 25;
		break;
	case EXECUTABLE:	// These two cases (with minor changes) use the 4th sequence: 259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31
	case BASH_XDG_OPEN:
		insertindex = appindex == EXECUTABLE ? 32 : 33;
		appindex = insertindex == 32 ? 42 : 43;
		break;
	default:			// Unmatched file extensions. Rely on operating system to use the set default program for dealing with unknown file type.
		insertindex = 10,	// Case uses 2nd sequence, we just need to alter one index number.
		appindex = 14;
		SequenceVec[17] = BASH_XDG_OPEN;	// Swap index number to BASH_XDG_OPEN (25)
	}

	// Set the sequencelimit variable using the first appindex value from each switch case sequence.
	// Reduce sequencelimit variable value by 1 if insertindex is 33 (case BASH_XDG_OPEN).
	sequencelimit = insertindex == 33 ? appindex - 1 : appindex;

	// With just a single vector insert command within the "while loop", we can insert all the required strings into the extraction script (vector "ScriptVec"), 
	// based on the sequence, which is selected by the relevant case from the above switch statement after the extension match. 
	while (sequencelimit > insertindex) {
		ScriptVec.insert(ScriptVec.begin() + SequenceVec[insertindex], AppVec[SequenceVec[appindex]].begin(), AppVec[SequenceVec[appindex]].end());
		insertindex++, 
		appindex++;
	}

	bool redochunklength{};

	do {
		redochunklength = false;

		// Extraction script within the "hIST" chunk of vector "ScriptVec", is now complete. 
		// Update "hIST" chunk length value.
		const uint16_t HIST_LENGTH = static_cast<uint16_t>(ScriptVec.size()) - 12; // Length does not include chunk length, chunk name or chunk crc fields (-12 bytes).

		// Display relevant error message and exit program if extraction script exceeds size limit.
		if (HIST_LENGTH > MAX_SCRIPT_SIZE_BYTES) {
			std::cerr << "\nScript Size Error: Extraction script exceeds maximum size of 750 bytes.\n\n";
			std::exit(EXIT_FAILURE);
		}

		uint8_t
			chunklengthindex = 2,	// "ScriptVec" vector's index location for the "hIST" chunk length field.
			bits = 16;

		// Call function to write updated chunk length value for "hIST" chunk into its length field. 
		// Due to its small size, the "hIST" chunk will only use 2 bytes maximum (bits=16) of the 4 byte length field.
		update->Value(ScriptVec, chunklengthindex, HIST_LENGTH, bits, false);

		// Check the first byte of the "hIST" chunk length field to make sure the updated chunk length does not match 
		// any of the "BAD_CHAR" characters that will break the Linux extraction script.
		for (uint8_t i = 0; i < 7; i++) {
			if (ScriptVec[3] == BAD_CHAR[i]) {
				// "BAD_CHAR" found. Insert a byte at the end of "ScriptVec" to increase chunk length. Update chunk length field. 
				// Check again for a "BAD_CHAR" match. Repeat the byte insertion and chunk length update, until no "BAD_CHAR" match.
				ScriptVec.insert(ScriptVec.begin() + (HIST_LENGTH + 10), '.');
				redochunklength = true;
				break;
			}
		}
	} while (redochunklength);

	// Insert vectors "ScripVec" ("hIST" chunk with completed extraction script) & "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec" (PNG image).
	combineVectors(ImageVec, ZipVec, ScriptVec);
}

void combineVectors(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, std::vector<BYTE>& ScriptVec) {

	srand((unsigned)time(NULL));  // For output filename.

	const std::string
		IDAT_SIG = "IDAT",
		NAME_VALUE = std::to_string(rand()),
		PDV_FILENAME = "pdvimg_" + NAME_VALUE.substr(0, 5) + ".png"; // Unique filename for the complete polyglot image.

	const uint32_t
		// Search vector "ImageVec" for the index location of the first "IDAT" chunk (start of length field).
		// This value will be used as the insert location within vector "ImageVec" for contents of vector "ScriptVec". 
		// The inserted vector contents will appear just before the first "IDAT" chunk and (if PNG-8) after the "PLTE" chunk. 
		// For Imgur to work correctly the "PLTE" chunk (PNG-8) must be located before the "hIST" chunk (extraction script). 
		FIRST_IDAT_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - ImageVec.begin()) - 4,

		// Set the index insert location within vector "ImageVec" for contents of vector "ZipVec".
		// The insert location is just after the last "IDAT" chuck within the vector "ImageVec".
		LAST_IDAT_INDEX = static_cast<uint32_t>(ImageVec.size() + ScriptVec.size()) - 12;

	std::cout << "\nEmbedding extraction script within the PNG image.\n";

	// Insert contents of vector "ScriptVec" ("hIST" chunk containing extraction script) into vector "ImageVec".	
	ImageVec.insert((ImageVec.begin() + FIRST_IDAT_INDEX), ScriptVec.begin(), ScriptVec.end());

	std::cout << "\nEmbedding zip file within the PNG image.\n";

	// Insert contents of vector "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec".
	// This now becomes the last "IDAT" chunk of the PNG image within vector "ImageVec".
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_INDEX), ZipVec.begin(), ZipVec.end());

	// Before updating the last "IDAT" chunk's crc value, adjust ZIP file offsets within this chunk.
	fixZipOffset(ImageVec, LAST_IDAT_INDEX);

	// Generate crc value for our (new) last "IDAT" chunk.
	// The +4 value points LAST_IDAT_INDEX variable value to the chunk name field, which is where the crc calculation needs to begin/include.
	const uint32_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_INDEX + 4], static_cast<uint32_t>(ZipVec.size()) - 8); // We don't include the length or crc fields (8 bytes).

	uint32_t crcinsertindex = static_cast<uint32_t>(ImageVec.size()) - 16;	// Get index location for last "IDAT" chunk's 4-byte crc field, from vector "ImageVec".

	uint8_t bits = 32;

	// Call function to write new crc value into the last "IDAT" chunk's crc index field, within the vector "ImageVec".
	update->Value(ImageVec, crcinsertindex, LAST_IDAT_CRC, bits, false);

	std::ofstream writeFinal(PDV_FILENAME, std::ios::binary);

	if (!writeFinal) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	std::cout << "\nWriting zip-embedded PNG image out to disk.\n";

	// Write out to file vector "ImageVec" now containing the completed polyglot image (Image + Script + ZIP).
	writeFinal.write((char*)&ImageVec[0], ImageVec.size());

	std::string sizewarning =
		"\n**Warning**\n\nDue to the file size of your zip-embedded PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
		"Flickr, ImgBB, PostImage & ImgPile";

	const uint8_t MSG_LEN = static_cast<uint8_t>(sizewarning.length());

	const uint32_t
		IMG_SIZE = static_cast<uint32_t>(ImageVec.size()),
		IMGUR_TWITTER_SIZE = 	5242880,	// 5MB
		IMG_PILE_SIZE =		8388608,	// 8MB
		POST_IMAGE_SIZE =	25165824,	// 24MB
		IMGBB_SIZE =		33554432;	// 32MB
		// Flickr is 200MB, this programs max size, no need to to make a variable for it.

	sizewarning = (IMG_SIZE > IMG_PILE_SIZE && IMG_SIZE <= POST_IMAGE_SIZE ? sizewarning.substr(0, MSG_LEN - 10)
				: (IMG_SIZE > POST_IMAGE_SIZE && IMG_SIZE <= IMGBB_SIZE ? sizewarning.substr(0, MSG_LEN - 21)
				: (IMG_SIZE > IMGBB_SIZE ? sizewarning.substr(0, MSG_LEN - 28) : sizewarning)));

	if (IMG_SIZE > IMGUR_TWITTER_SIZE) {
		std::cerr << sizewarning << ".\n";
	}

	std::cout << "\nCreated zip-embedded PNG image: \"" + PDV_FILENAME + "\" Size: \"" << ImageVec.size() << " Bytes\"";
	std::cout << "\n\nComplete!\n\nYou can now share your zip-embedded PNG image on the relevant supported platforms.\n\n";
}

void fixZipOffset(std::vector<BYTE>& ImageVec, const uint32_t LAST_IDAT_INDEX) {

	const std::string
		START_CENTRAL_DIR_SIG = "PK\x01\x02",
		END_CENTRAL_DIR_SIG = "PK\x05\x06",
		ZIP_SIG = "PK\x03\x04";

	// Search vector "ImageVec" (start from last "IDAT" chunk index) to get locations for "Start Central Directory" & "End Central Directory".
	const uint32_t
		START_CENTRAL_DIR_INDEX = static_cast<uint32_t>(search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin()),
		END_CENTRAL_DIR_INDEX = static_cast<uint32_t>(search(ImageVec.begin() + START_CENTRAL_DIR_INDEX, ImageVec.end(), END_CENTRAL_DIR_SIG.begin(), END_CENTRAL_DIR_SIG.end()) - ImageVec.begin());

	uint32_t
		ziprecordsindex = END_CENTRAL_DIR_INDEX + 11,		// Index location for ZIP file records value.
		commentlengthindex = END_CENTRAL_DIR_INDEX + 21,	// Index location for ZIP comment length.
		endcentralstartindex = END_CENTRAL_DIR_INDEX + 19,	// Index location for End Central Start offset.
		centrallocalindex = START_CENTRAL_DIR_INDEX - 1,	// Initialise variable to just before Start Central index location.
		newoffset = LAST_IDAT_INDEX;				// Initialise variable to last "IDAT" chunk's index location.

	uint16_t ziprecords = (ImageVec[ziprecordsindex] << 8) | ImageVec[ziprecordsindex - 1];	// Get ZIP file records value from index location of vector "ImageVec".

	uint8_t bits = 32;

	// Starting from the last "IDAT" chunk, search for ZIP file record offsets and update them to their new offset location.
	while (ziprecords--) {
		newoffset = static_cast<uint32_t>(search(ImageVec.begin() + newoffset + 1, ImageVec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - ImageVec.begin()),
		centrallocalindex = 45 + static_cast<uint32_t>(search(ImageVec.begin() + centrallocalindex, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin());
		update->Value(ImageVec, centrallocalindex, newoffset, bits, true);
	}

	// Write updated "Start Central Directory" offset into End Central Directory's "Start Central Directory" index location within vector "ImageVec".
	update->Value(ImageVec, endcentralstartindex, START_CENTRAL_DIR_INDEX, bits, true);

	// JAR file support. Get global comment length value from ZIP file within vector "ImageVec" and increase it by 16 bytes to cover end of PNG file.
	// To run a JAR file, you will need to rename the '.png' extension to '.jar'.  
	// or run the command: java -jar your_image_file_name.png
	uint16_t commentlength = 16 + (ImageVec[commentlengthindex] << 8) | ImageVec[commentlengthindex - 1];

	bits = 16;

	// Write the ZIP comment length value into the comment length index location within vector "ImageVec".
	update->Value(ImageVec, commentlengthindex, commentlength, bits, true);
}

// The following code (slightly modified) to compute CRC32 (for "IDAT" & "PLTE" chunks) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint32_t crcUpdate(const uint32_t crc, BYTE* buf, const uint32_t len)
{
	// Table of CRCs of all 8-bit messages.
	std::vector<uint32_t>CrcTableVec{
		0x00,	    0x77073096, 0xEE0E612C, 0x990951BA, 0x76DC419,  0x706AF48F, 0xE963A535, 0x9E6495A3, 0xEDB8832,  0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x9B64C2B,  0x7EB17CBD,
		0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
		0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
		0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x1DB7106,  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x6B6B51F,
		0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0xF00F934,	0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x86D3D2D,	0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
		0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE,
		0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
		0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x3B6E20C,  0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x4DB2615,  0x73DC1683, 0xE3630B12, 0x94643B84, 0xD6D6A3E,  0x7A6A5AA8,
		0xE40ECF0B, 0x9309FF9D, 0xA00AE27,  0x7D079EB1, 0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0,
		0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703,
		0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x26D930A,
		0x9C0906A9, 0xEB0E363F, 0x72076785, 0x5005713,	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0xCB61B38,	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0xBDBDF21,	0x86D3D2D4, 0xF1D4E242,
		0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5,
		0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
		0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
	uint32_t c = crc;
	
	for (uint32_t n  = 0; n < len; n++) {
		c = CrcTableVec[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

// Return the CRC of the bytes buf[0..len-1].
uint32_t crc(BYTE* buf, const uint32_t len)
{
	return crcUpdate(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle ZIP Edition (PDVZIP v1.4). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.
		
PDVZIP enables you to embed a ZIP file within a *tweetable and "executable" PNG image.  		
		
The hosting sites will retain the embedded arbitrary data within the PNG image.  
		
PNG image size limits are platform dependant:  
Flickr (200MB), Imgbb (32MB), PostImage (24MB), ImgPile (8MB), *Twitter & Imgur (5MB).

Once the ZIP file has been embedded within a PNG image, it's ready to be shared on your chosen
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

PNG-32 (Truecolour with alpha [6])
PNG-24 (Truecolour [2])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 899 x 899.

Note: Images that are created & saved within your image editor as PNG-32/24 that are either 
black & white/grayscale, images with 256 colours or less, will be converted by Twitter to 
PNG-8 and you will lose the embedded content. If you want to use a simple "single" colour PNG-32/24 image,
then fill an area with a gradient colour instead of a single solid colour.
Twitter should then keep the image as PNG-32/24.

PNG-8 (Indexed-colour [3])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 4096 x 4096.

Chunks:

PNG chunks that you can insert arbitrary data, in which the hosting site will preserve in conjuction
with the above dimensions & file size limits.

bKGD, cHRM, gAMA, hIST,
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
PLTE, (Use only with PNG-32 & PNG-24 for arbitrary data).
pHYs, sBIT, sPLT, sRGB,
tRNS. (Not recommended, may distort image).

This program uses hIST & IDAT chunk names for storing arbitrary data.

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
