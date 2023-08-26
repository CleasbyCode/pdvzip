// PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.3). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

typedef unsigned char BYTE;
typedef	unsigned short SBYTE;

// Update values, such as chunk lengths, crc, file sizes and other values.
// Writes them into the relevant vector index locations. Overwrites previous values.
class ValueUpdater {
public:
	void Value(std::vector<BYTE>& vec, size_t valueInsertIndex, const size_t VALUE, SBYTE bits, bool isBig) {
		if (isBig) {
			while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
		}
		else {
			while (bits) vec[valueInsertIndex--] = (VALUE >> (bits -= 8)) & 0xff;
		}
	}
} *update;

// Read-in user PNG image and ZIP file. Store the data in vectors.
void storeFiles(std::ifstream&, std::ifstream&, const std::string&, const std::string&);

// Make sure user PNG image and ZIP file fulfil valid program requirements. Display relevant error message and exit program if checks fail.
void checkFileRequirements(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]);

// Search and remove all unnecessary PNG chunks found before the first "IDAT" chunk.
void eraseChunks(std::vector<BYTE>&);

// Search for and change characters within the PNG "PLTE" chunk to prevent the Linux extraction script breaking.
void modifyPaletteChunk(std::vector<BYTE>&, const char(&)[]);

// Update barebones extraction script determined by embedded content. 
void completeScript(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]);

// Insert contents of vectors storing user ZIP file and the completed extraction script into the vector containing PNG image, then write vector's content to file.
void combineVectors(std::vector< BYTE>&, std::vector<BYTE>&, std::vector<BYTE>&);

// Adjust embedded ZIP file offsets within the PNG image, so that it remains a valid, working ZIP archive.
void fixZipOffset(std::vector<BYTE>&, const size_t);

// Output to screen detailed program usage information.
void displayInfo();

// Code to compute CRC32 (for "IDAT" & "PLTE" chunks within this program) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
size_t crcUpdate(const size_t, BYTE*, const size_t);
size_t crc(BYTE*, const size_t);

int main(int argc, char** argv) {
	
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	else if (argc < 3 || argc > 3) {
		std::cout << "\nUsage:  pdvzip  <png_image>  <zip_file>\n\tpdvzip  --info\n\n";
	}
	else {
		const std::string
			IMG_NAME = argv[1],
			ZIP_NAME = argv[2];

		// Open user files for reading.
		std::ifstream
			IMAGE(IMG_NAME, std::ios::binary),
			ZIP(ZIP_NAME, std::ios::binary);

		// Display relevant error message and exit program if any file fails to open.
		if (!IMAGE || !ZIP) {
			const std::string
				READ_ERR_MSG = "Read Error: Unable to open/read file: ",
				ERR_MSG = !IMAGE ? "\nPNG " + READ_ERR_MSG + "'" + IMG_NAME + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_NAME + "'\n\n";
			std::cerr << ERR_MSG;
			std::exit(EXIT_FAILURE);
		}

		// Open file sucesss, now read files into vectors.
		storeFiles(IMAGE, ZIP, IMG_NAME, ZIP_NAME);
	}
	return 0;
}

void storeFiles(std::ifstream& IMAGE, std::ifstream& ZIP, const std::string& IMG_FILE, const std::string& ZIP_FILE) {

	// Vector "ZipVec" will store the user's ZIP file. The contents of "ZipVec" will later be inserted into the vector "ImageVec" as the last "IDAT" chunk. 
	// We will need to update the crc value (last 4-bytes) and the chunk length field (first 4-bytes), within this vector. Both fields currently set to zero. 
	// Vector "ImageVec" stores the user's PNG image. Combined later with the vectors "ScriptVec" and "ZipVec", before writing complete vector out to file.
	std::vector<BYTE>
		ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 }, // "IDAT" chunk name with 4-byte chunk length and crc fields.
		ImageVec((std::istreambuf_iterator<char>(IMAGE)), std::istreambuf_iterator<char>());  // Insert user image file into vector "ImageVec".

	// Insert user ZIP file into vector "ZipVec" at index 8, just after "IDAT" chunk name.
	ZipVec.insert(ZipVec.begin() + 8, std::istreambuf_iterator<char>(ZIP), std::istreambuf_iterator<char>());

	// Get size of user ZIP file from vector "ZipVec", minux 12 bytes. "IDAT" chunk name(4), chunk length field(4), chunk crc field(4).
	const size_t ZIP_SIZE = ZipVec.size() - 12;

	// Array used by different functions within this program.
	// Occurrence of these characters in the "IHDR" chunk (data/crc), "PLTE" chunk (data/crc) or "hIST" chunk (length), breaks the Linux extraction script.
	static const char BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' };

	// Make sure PNG and ZIP file specs meet program requirements. 
	checkFileRequirements(ImageVec, ZipVec, BAD_CHAR);

	// Find and remove unnecessary PNG chunks.
	eraseChunks(ImageVec);

	const size_t
		IMG_SIZE = ImageVec.size(),
		MAX_PNG_SIZE_BYTES = 209715200,	// 200MB PNG "file-embedded" size limit.
		MAX_SCRIPT_SIZE_BYTES = 750,	// Extraction script size limit.
		COMBINED_SIZE = IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES;

	// Check size of files. Display relevant error message and exit program if files exceed size limits.
	if ((IMG_SIZE + MAX_SCRIPT_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| ZIP_SIZE > MAX_PNG_SIZE_BYTES
		|| COMBINED_SIZE > MAX_PNG_SIZE_BYTES) {

		const size_t
			EXCEED_SIZE_LIMIT = (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES) - MAX_PNG_SIZE_BYTES,
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

	// Index of chunk length field for vector "ZipVec".
	SBYTE chunkLengthIndex = 0;

	// Write the updated "IDAT" chunk length of vector "ZipVec" within its length field.
	update->Value(ZipVec, chunkLengthIndex, ZIP_SIZE, 32, true); 

	// Finish building extraction script.
	completeScript(ZipVec, ImageVec, BAD_CHAR);
}

void checkFileRequirements(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, const char(&BAD_CHAR)[]) {

	const std::string
		PNG_SIG = "\x89PNG",		// Valid file header signature of PNG image.
		ZIP_SIG = "PK\x03\x04",		// Valid file header signature of ZIP file.
		IMG_VEC_HDR{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() },		// Get file header from vector "ImageVec". 
		ZIP_VEC_HDR{ ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_SIG.length() };	// Get file header from vector "ZipVec".

	const SBYTE
		IMAGE_DIMS_WIDTH = ImageVec[18] << 8 | ImageVec[19],	// Get image width dimensions from vector "ImageVec".
		IMAGE_DIMS_HEIGHT = ImageVec[22] << 8 | ImageVec[23],	// Get image height dimensions from vector "ImageVec".
		PNG_TRUECOLOUR_MAX_DIMS = 899,		// 899 x 899 maximum supported dimensions for PNG truecolour (PNG-32/PNG-24, Colour types 2 & 6).
		PNG_INDEXCOLOUR_MAX_DIMS = 4096,	// 4096 x 4096 maximum supported dimensions for PNG indexed-colour (PNG-8, Colour type 3).
		PNG_MIN_DIMS = 68,			// 68 x 68 minimum supported dimensions for PNG indexed-colour and truecolour.

		// Get image colour type value from vector "ImageVec". If value is 6 (Truecolour with alpha), set the value to 2 (Truecolour).
		COLOUR_TYPE = ImageVec[25] == 6 ? 2 : ImageVec[25],

		INZIP_NAME_LENGTH = ZipVec[34],	// Get length of zipped media filename from vector "ZipVec".
		PNG_INDEXCOLOUR = 3,		// PNG-8, indexed colour.
		PNG_TRUECOLOUR = 2,		// PNG-24, truecolour. (We also use this for PNG-32 / truecolour with alpha, 6) as we deal with them the same way.
		MIN_NAME_LENGTH = 4;		// Minimum filename length of zipped file.

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
		|| MIN_NAME_LENGTH > INZIP_NAME_LENGTH) { // Requirements check failure, display relevant error message and exit program.

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
	SBYTE pos = 18; // Index search start position within vector "ImageVec".

	// From pos location, increment through 15 bytes of vector "ImageVec" and compare each byte to the 7 characters within "BAD_CHAR" array.
	for (SBYTE i{}; i < 16; i++) {
		for (SBYTE j{}; j < 7; j++) {
			if (ImageVec[pos] == BAD_CHAR[j]) { // "BAD_CHAR" character found, display error message and exit program.
				std::cerr <<
					"\nInvalid Character Error:  \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
					"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue. Repeat if necessary.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}
		pos++;
	}
}

void eraseChunks(std::vector<BYTE>& ImageVec) {

	const std::string CHUNK[15]{ "IDAT", "bKGD", "cHRM", "iCCP", "sRGB", "hIST", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	for (SBYTE chunkIndex = 14; chunkIndex > 0; chunkIndex--) {
		size_t
			// Get the first "IDAT" chunk index location. Don't remove any chunks after this point.
			firstIdatIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[0].begin(), CHUNK[0].end()) - ImageVec.begin(),
			// From last to first, search and get the index location of each chunk to remove.
			chunkFoundIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[chunkIndex].begin(), CHUNK[chunkIndex].end()) - ImageVec.begin() - 4;
		// If found chunk is located before the first "IDAT" chunk, erase it.
		if (firstIdatIndex > chunkFoundIndex) {
			int chunkSize = (ImageVec[chunkFoundIndex + 1] << 16) | ImageVec[chunkFoundIndex + 2] << 8 | ImageVec[chunkFoundIndex + 3];
			ImageVec.erase(ImageVec.begin() + chunkFoundIndex, ImageVec.begin() + chunkFoundIndex + (chunkSize + 12));
			chunkIndex++; // Increment chunkIndex so that we search again for the same chunk name, in case of multiple occurrences.
		}
	}
}

void modifyPaletteChunk(std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	// PNG-8, Imgur (image hosting site), Linux issue. 
	// Some individual characters, a sequence or combination of certain characters that may appear within the PLTE chunk, will break the extraction script.
	// This function contains a number of fixes for this issue.
	// The "PLTE" chunk must be located before the "hIST" chunk (extraction script) for pdv images to work correctly on the Imgur image hosting site.
	const std::string PLTE_SIG = "PLTE";

	const size_t
		// Search for index location of "PLTE" chunk name within vector "ImageVec".
		PLTE_CHUNK_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - ImageVec.begin(),
		// Get "PLTE" chunk size from vector "ImageVec".
		PLTE_CHUNK_SIZE = (ImageVec[PLTE_CHUNK_INDEX - 2] << 8) | ImageVec[PLTE_CHUNK_INDEX - 1];

	// Replace "BAD_CHAR" characters with "GOOD_CHAR" characters.
	// While image corruption is possible when altering the "PLTE" chunk, the majority of images should be fine with these replacement characters.
	const char GOOD_CHAR[7]{ '*', '&', '=', '}', 'a', '?', ':' };

	SBYTE twoCount{};

	for (size_t i = PLTE_CHUNK_INDEX; i < (PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4)); i++) {

		// Individual "BAD_CHAR" characters within "PLTE" chunk that will break the extraction script.
		// Replace matched "BAD_CHAR" with the relevant "GOOD_CHAR".
		ImageVec[i] = (ImageVec[i] == BAD_CHAR[0]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[1]) ? GOOD_CHAR[1] : (ImageVec[i] == BAD_CHAR[2]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[3]) ? GOOD_CHAR[4] : (ImageVec[i] == BAD_CHAR[4]) ? GOOD_CHAR[0]
			: (ImageVec[i] == BAD_CHAR[5]) ? GOOD_CHAR[5] : ((ImageVec[i] == BAD_CHAR[6]) ? GOOD_CHAR[6] : ImageVec[i]);

		// Character combinations that will break the extraction script. 
		if ((ImageVec[i] == '&' && ImageVec[i + 1] == '!')		// e.g. "&!"
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '}')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '{')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == ')')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == '(')
			|| (ImageVec[i] == '\x0a' && ImageVec[i + 1] == '&')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '\x0')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '#')
			|| (ImageVec[i] == '&' && ImageVec[i + 1] == '|')
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

		// Two or more characters of "&" or "|" in a row will break the script.
		if (ImageVec[i] == '&' || ImageVec[i] == '|') {
			twoCount++;
			if (twoCount > 1) { // Once we find two of those characters in a row, replace one of them with the relevant "GOOD_CHAR".
				ImageVec[i] = (ImageVec[i] == '&' ? GOOD_CHAR[0] : GOOD_CHAR[3]);
				twoCount = 0;
			}
		}
		else {
			twoCount = 0;
		}

		// Character "<" followed by a number (or a sequence of numbers up to 11 digits) and ending with the same character "<", breaks the extraction script. 
		// e.g. "<1<, <4356<, <293459< <35242369849<".
		SBYTE num_pos = 1, arrow_pos = 2, max_num_pos = 12;
		if (ImageVec[i] == '<') { // Found this "<" character, now check if it is followed by a number and ends with another "<" character.
			while (num_pos < max_num_pos) {
				if (ImageVec[i + num_pos] > 47 && ImageVec[i + num_pos] < 58 && ImageVec[i + arrow_pos] == '<') { // Found pattern sequence 
					ImageVec[i] = GOOD_CHAR[2];  // Replace character "<" at the beginning of the string with the relevant "GOOD_CHAR" character.
					num_pos = max_num_pos;
				}
				num_pos++, arrow_pos++;
			}
		}
	}

	int modValue = 255;
	bool redoCrc{};

	do {
		redoCrc = false;

		// After modifying the "PLTE" chunk, we need to update the chunk's crc value.
		// Pass these two values (PLTE_CHUNK_INDEX & PLTE_CHUNK_SIZE + 4) to the crc fuction to get correct "PLTE" chunk crc value.
		const size_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_CHUNK_INDEX], PLTE_CHUNK_SIZE + 4);

		// Get vector index locations for "PLTE" crc chunk field and "PLTE" modValue.
		size_t
			crcInsertIndex = PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4),
			modValueInsertIndex = crcInsertIndex - 1;

		// Write the updated crc value into the "PLTE" chunk's crc field (bits=32) within vector "ImageVec".
		update->Value(ImageVec, crcInsertIndex, PLTE_CHUNK_CRC, 32, true);

		// Make sure the new crc value does not contain any of the "BAD_CHAR" characters, which will break the extraction script.
		// If we find a "BAD_CHAR" in the new crc value, modify one byte in the "PLTE" chunk ("PLTE" mod location), then recalculate crc value. 
		// Repeat process until no BAD_CHAR found.
		for (SBYTE i = 0; i < 5; i++) {
			for (SBYTE j = 0; j < 7; j++) {
				if (i > 3) break;
				if (ImageVec[crcInsertIndex] == BAD_CHAR[j]) {
					ImageVec[modValueInsertIndex] = modValue;
					modValue--;
					redoCrc = true;
					break;
				}
			}
			crcInsertIndex++;
		}
	} while (redoCrc);
}

void completeScript(std::vector<BYTE>& ZipVec, std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	/* Vector "ScriptVec" (See "script_info.txt" in this repo).

	First 4 bytes of the vector is the chunk length field, followed by chunk name "hIST" then our barebones extraction script.
	"hIST" is a valid ancillary PNG chunk type/name. It does not require a correct crc value, so no update needed.

	This vector stores the shell/batch script used for extracting and opening the embedded zipped media file.
	The barebones script is about 300 bytes. The script size limit is 750 bytes, which should be more than enough to account
	for the later addition of filenames, application, argument strings & other required script commands.

	Script supports both Linux & Windows. The completed script, when executed, will unzip the archive within
	the PNG image & attempt to open/play/run (depending on file type) the zipped media file(s), using an application
	command based on a matched file extension, or if no match found, defaulting to the operating system making the choice.

	The zipped media file needs to be compatible with the operating system you are running it on.
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
	// Stores file extensions for some popular media types along with several default application commands (& args) that support those extensions.
	// These vector string elements will be used in the completion of our extraction script.
	std::vector<std::string> AppVec{ "aac","mp3","mp4","avi","asf","flv","ebm","mkv","peg","wav","wmv","wma","mov","3gp","ogg","pdf",".py","ps1","exe",
					".sh","vlc --play-and-exit --no-video-title-show ","evince ","python3 ","pwsh ","./","xdg-open ","powershell;Invoke-Item ",
					" &> /dev/null","start /b \"\"","pause&","powershell","chmod +x ",";" };

	const SBYTE
		INZIP_NAME_LENGTH_INDEX = 34,	// "ZipVec" vector's index location for the length value of the zipped media filename.
		INZIP_NAME_INDEX = 38,		// "ZipVec" start index location for the zipped media filename.
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	// Get character length of the zipped media filename from vector "ZipVec".
		MAX_SCRIPT_SIZE_BYTES = 750,	// Extraction script size limit.

		// "AppVec" vector element index values. 
		// Some "AppVec" vector string elements are added later (via push_back), so don't currently appear in the above string vector.
		VIDEO_AUDIO = 20,		// "vlc" app command for Linux. 
		PDF = 21,			// "evince" app command for Linux. 
		PYTHON = 22,			// "python3" app command for Linux & Windows.
		POWERSHELL = 23,		// "pwsh" app command for Linux, for starting PowerShell scripts.
		EXECUTABLE = 24,		// "./" prepended to filename. Required when running Linux executables.
		BASH_XDG_OPEN = 25,		// "xdg-open" Linux command, runs shell scripts (.sh), opens folders & unmatched file extensions.
		FOLDER_INVOKE_ITEM = 26,	// "powershell;Invoke-Item" command used in Windows for opening folders.
		START_B = 28,			// "start /b" Windows command used to open most file types. Windows uses set default app for file type.
		WIN_POWERSHELL = 30,		// "powershell" commmand used by Windows for running PowerShell scripts.
		MOD_INZIP_FILENAME = 36;	// inzipName with ".\" prepend characters. Required for Windows PowerShell, e.g. powershell ".\filename.ps1".

	std::string
		// Get the zipped media filename string from vector "ZipVec".
		inzipName{ ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH },
		// Get the file extension from zipped media filename.
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),
		// Variables for optional command-line arguments.
		argsLinux{},
		argsWindows{};

	size_t checkExtension = inzipName.find_last_of('.');

	// Store inzipName in "AppVec" vector (33).
	AppVec.push_back(inzipName);

	// When inserting string elements from vector "AppVec" into the script (within vector "ScriptVec"), we are adding items in 
	// the order of last to first. The Windows script is completed first, followed by Linux. 
	// This order prevents the vector insert locations from changing every time we add a new string element into the vector.

	// The array "sequence" can be split into four sequences containing "ScriptVec" index values (high numbers) used by the 
	// "insertIndex" variable and the corresponding "AppVec" index values (low numbers) used by the "appIndex" variable.

	// For example, in the 1st sequence, sequence[0] = index 236 of "ScriptVec" ("insertIndex") corresponds with
	// sequence[5] = "AppVec" index 33 ("appIndex"), which is the zipped media filename string element. 
	// "AppVec" string element 33 (zipped media filename) will be inserted into the script (vector "ScriptVec") at index 236.

	SBYTE sequence[52]{
				236,234,116,115,114, 33,28,27,33,20,	// 1st sequence for case "VIDEO_AUDIO".
				236,234,115,114, 33,28,33,21,		// 2nd sequence for cases "PDF, FOLDER_INVOKE_ITEM, DEFAULT".
				259,237,236,234,116,115,114, 29,35,33,22,34,33,22,	// 3rd sequence for cases "PYTHON, POWERSHELL".
				259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31 }, // 4th sequence for cases "EXECUTABLE & BASH_XDG_OPEN".

				appIndex = 0,		// Uses the "AppVec" index values from the sequence array.
				insertIndex = 0,	// Uses the "ScriptVec" index values from the sequence array.
				sequenceLimit = 0;	// Stores the length limit of each of the four sequences. 

	/*  [sequence](insertIndex)[sequence](appIndex)
		Build script example below is using the first sequence (see "sequence" array above).

		VIDEO_AUDIO:
		[0]>(236)[5]>(33) Windows: "ImageVec" 236 insert index for zipped media filename, "AppVec" 33.
		[1]>(234)[6]>(28) Windows: "ImageVec" 234 insert index for "start /b", "AppVec" 28.
		[2]>(116)[7]>(27) Linux: "ImageVec" 116 insert index for "Dev Null", "AppVec" 27.
		[3]>(115)[8]>(33) Linux: "ImageVec" 115 insert index for zipped media filename, "AppVec" 33.
		[4]>(114)[9]>(20) Linux: "ImageVec" 114 insert index for "vlc", "AppVec" 20.
		Sequence limit is 5 (value taken from first appIndex value for each sequence).

		Matching a file extension from "inzipNameExt" within "AppVec", we can select which application string and commands to use
		in our extraction script, which, when executed, will open/play/run (depending on file type) the extracted zipped media file.

		Once the correct app extension has been matched by the "for loop" below, it passes the appIndex result to the switch statement.
		The relevant Case sequence is then used in completing the extraction script within vector "ScriptVec".	*/

	for (; appIndex != 26; appIndex++) {
		if (AppVec[appIndex] == inzipNameExt) {
			// After a file extension match, any appIndex value between 0 & 14 defaults to "AppVec" 20 (vlc / VIDEO_AUDIO).
			// If over 14, we add 6 to the value. 15 + 6 = "AppVec" 21 (evince/PDF), 16 + 6 = "AppVec" 22 (python3/.py), etc.
			appIndex = appIndex <= 14 ? 20 : appIndex += 6;
			break;
		}
	}

	// If no file extension detected, check if "inzipName" points to a folder (/), else assume file is a Linux executable.
	if (checkExtension == 0 || checkExtension > inzipName.length()) {
		appIndex = ZipVec[INZIP_NAME_INDEX + INZIP_NAME_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	// Provide the user with the option to add command-line arguments for file types: .py, .ps1, .sh and .exe (no extension, defaults to .exe).
	if (appIndex > 21 && appIndex < 26) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, argsLinux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, argsWindows);
		argsLinux.insert(0, "\x20"), argsWindows.insert(0, "\x20");
		AppVec.push_back(argsLinux),	// "AppVec" (34).
		AppVec.push_back(argsWindows);	// "AppVec" (35).
	}

	switch (appIndex) {
	case VIDEO_AUDIO:	// Case uses 1st sequence: 236,234,116,115,114, 33,28,27,33,20.
		appIndex = 5;
		break;
	case PDF:		// These two cases (with minor changes) use the 2nd sequence: 236,234,115,114, 33,28,33,21.
	case FOLDER_INVOKE_ITEM:
		sequence[15] = appIndex == FOLDER_INVOKE_ITEM ? FOLDER_INVOKE_ITEM : START_B;
		sequence[17] = appIndex == FOLDER_INVOKE_ITEM ? BASH_XDG_OPEN : PDF;
		insertIndex = 10,
		appIndex = 14;
		break;
	case PYTHON:		// These two cases (with some changes) use the 3rd sequence: 259,237,236,234,116,115,114, 29,35,33,22,34,33,22.
	case POWERSHELL:
		if (appIndex == POWERSHELL) {
			inzipName.insert(0, ".\\");		//  ".\" prepend to inzipName. Required for Windows PowerShell, e.g. powershell ".\filename.ps1".
			AppVec.push_back(inzipName);		// Add the modified filename to the "AppVec" vector (36).
			sequence[31] = POWERSHELL,		// Swap index number to Linux PowerShell (pwsh 23)
			sequence[28] = WIN_POWERSHELL;		// Swap index number to Windows PowerShell (powershell 30)
			sequence[27] = MOD_INZIP_FILENAME;	// Swap index number to MOD_INZIP_FILENAME (36), used with the Windows powershell command.
		}
		insertIndex = 18,
		appIndex = 25;
		break;
	case EXECUTABLE:	// These two cases (with minor changes) use the 4th sequence: 259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31
	case BASH_XDG_OPEN:
		insertIndex = appIndex == EXECUTABLE ? 32 : 33;
		appIndex = insertIndex == 32 ? 42 : 43;
		break;
	default:			// Unmatched file extensions. Rely on operating system to use the set default program for dealing with file type.
		insertIndex = 10,	// Case uses 2nd sequence, we just need to alter one index number.
		appIndex = 14;
		sequence[17] = BASH_XDG_OPEN;	// Swap index number to BASH_XDG_OPEN (25)
	}

	// Set the sequenceLimit variable using the first appIndex value from each switch case sequence.
	// Reduce sequenceLimit variable value by 1 if insertIndex is 33 (case BASH_XDG_OPEN).
	sequenceLimit = insertIndex == 33 ? appIndex - 1 : appIndex;

	// With just a single vector insert command within the "while loop", we can insert all the required strings into the extraction script (vector "ScriptVec"), 
	// based on the sequence array, which is selected by the relevant case, from the above switch statement after the extension match. 
	while (sequenceLimit > insertIndex) {
		ScriptVec.insert(ScriptVec.begin() + sequence[insertIndex], AppVec[sequence[appIndex]].begin(), AppVec[sequence[appIndex]].end());
		insertIndex++, appIndex++;
	}

	bool redoChunkLength{};

	do {
		redoChunkLength = false;

		// Extraction script within the "hIST" chunk of vector "ScriptVec", is now complete. 
		// Update "hIST" chunk length.
		const size_t HIST_LENGTH = ScriptVec.size() - 12;

		// Display relevant error message and exit program if extraction script exceeds size limit.
		if (HIST_LENGTH > MAX_SCRIPT_SIZE_BYTES) {
			std::cerr << "\nScript Size Error: Extraction script exceeds maximum size of 750 bytes.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// "ScriptVec" vector's index location for the "hIST" chunk length field.
		SBYTE chunkLengthIndex = 2;

		// Call function to write updated chunk length value for "hIST" chunk into its length field. 
		// Due to its small size, the "hIST" chunk will only use 2 bytes maximum (bits=16) of the 4 byte length field.
		update->Value(ScriptVec, chunkLengthIndex, HIST_LENGTH, 16, true);

		// Check the first byte of the "hIST" chunk length field to make sure the updated chunk length does not match 
		// any of the "BAD_CHAR" characters that will break the Linux extraction script.
		for (SBYTE i = 0; i < 7; i++) {
			if (ScriptVec[3] == BAD_CHAR[i]) {
				// "BAD_CHAR" found. Insert a byte at the end of "ScriptVec" to increase chunk length. Update chunk length field. 
				// Check again for a "BAD_CHAR" match. Repeat the byte insertion and chunk length update, until no "BAD_CHAR" match.
				ScriptVec.insert(ScriptVec.begin() + (HIST_LENGTH + 10), '.');
				redoChunkLength = true;
				break;
			}
		}
	} while (redoChunkLength);

	// Insert vectors "ScripVec" ("hIST" chunk with completed extraction script) & "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec" (PNG image).
	combineVectors(ImageVec, ZipVec, ScriptVec);
}

void combineVectors(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, std::vector<BYTE>& ScriptVec) {

	srand((unsigned)time(NULL));  // For output filename.

	const std::string
		IDAT_SIG = "IDAT",
		TXT_NUM = std::to_string(rand()),
		PDV_FILENAME = "pdvzip_image_" + TXT_NUM.substr(0, 5) + ".png"; // Output unique filename for the complete polyglot image.

	const size_t
		// Search vector "ImageVec" for the index location of the first "IDAT" chunk (start of length field).
		// This value will be used as the insert location within vector "ImageVec" for contents of vector "ScriptVec". 
		// The inserted vector contents will appear just before the first "IDAT" chunk and (if PNG-8) after the "PLTE" chunk. 
		// For Imgur to work correctly the "PLTE" chunk (PNG-8) must be located before the "hIST" chunk (extraction script). 
		FIRST_IDAT_INDEX = search(ImageVec.begin(), ImageVec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - ImageVec.begin() - 4,

		// Set the index insert location within vector "ImageVec" for contents of vector "ZipVec".
		// The insert location is just after the last "IDAT" chuck within the vector "ImageVec".
		LAST_IDAT_INDEX = (ImageVec.size() + ScriptVec.size()) - 12;

	// Insert contents of vector "ScriptVec" ("hIST" chunk containing extraction script) into vector "ImageVec".	
	ImageVec.insert((ImageVec.begin() + FIRST_IDAT_INDEX), ScriptVec.begin(), ScriptVec.end());

	// Insert contents of vector "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec".
	// This now becomes the last "IDAT" chunk of the PNG image within vector "ImageVec".
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_INDEX), ZipVec.begin(), ZipVec.end());

	// Before updating the last "IDAT" chunk's crc value, adjust ZIP file offsets within this chunk.
	fixZipOffset(ImageVec, LAST_IDAT_INDEX);

	// Generate crc value for our (new) last "IDAT" chunk.
	// The +4 value points LAST_IDAT_INDEX variable value to the chunk name location, which is where the crc calculation needs to begin/include.
	const size_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_INDEX + 4], ZipVec.size() - 8); // We don't include the 4 bytes length field or the 4 bytes crc field (8 bytes).

	size_t crcInsertIndex = ImageVec.size() - 16;	// Get index location for last "IDAT" chunk's 4-byte crc field, from vector "ImageVec".

	// Call function to write new crc value into the last "IDAT" chunk's crc index field, within the vector "ImageVec".
	update->Value(ImageVec, crcInsertIndex, LAST_IDAT_CRC, 32, true);

	std::ofstream writeFinal(PDV_FILENAME, std::ios::binary);

	if (!writeFinal) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Write out to file vector "ImageVec" now containing the completed polyglot image (Image + Script + ZIP).
	writeFinal.write((char*)&ImageVec[0], ImageVec.size());

	std::string msgSizeWarning =
		"\n**Warning**\n\nDue to the file size of your \"zip-embedded\" PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
		"Flickr, ImgBB, ImageShack, PostImage & ImgPile";

	const size_t
		msgLen = msgSizeWarning.length(),
		imgSize = ImageVec.size(),
		imgurTwitter = 5242880,		// 5MB
		imgPile = 8388608,		// 8MB
		postImageSize = 25165824,	// 24MB
		imageShackSize = 26214400,	// 25MB 
		imgbbSize = 33554432;		// 32MB

	msgSizeWarning = (imgSize > imgPile && imgSize <= postImageSize ? msgSizeWarning.substr(0, msgLen - 10)
					: (imgSize > postImageSize && imgSize <= imageShackSize ? msgSizeWarning.substr(0, msgLen - 21)
					: (imgSize > imageShackSize && imgSize <= imgbbSize ? msgSizeWarning.substr(0, msgLen - 33)
					: (imgSize > imgbbSize ? msgSizeWarning.substr(0, msgLen - 40) : msgSizeWarning))));

	if (imgSize > imgurTwitter) {
		std::cerr << msgSizeWarning << ".\n";
	}

	std::cout << "\nCreated output file " << "'" << PDV_FILENAME << "' " << ImageVec.size() << " bytes." << "\n\nAll Done!\n\n";
}

void fixZipOffset(std::vector<BYTE>& ImageVec, const size_t LAST_IDAT_INDEX) {

	const std::string
		START_CENTRAL_DIR_SIG = "PK\x01\x02",
		END_CENTRAL_DIR_SIG = "PK\x05\x06",
		ZIP_SIG = "PK\x03\x04";

	// Search vector "ImageVec" (start from last "IDAT" chunk) to get index locations for "Start Central Directory" & "End Central Directory".
	const size_t
		START_CENTRAL_DIR_INDEX = search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin(),
		END_CENTRAL_DIR_INDEX = search(ImageVec.begin() + START_CENTRAL_DIR_INDEX, ImageVec.end(), END_CENTRAL_DIR_SIG.begin(), END_CENTRAL_DIR_SIG.end()) - ImageVec.begin();

	size_t
		zipRecordsIndex = END_CENTRAL_DIR_INDEX + 11,		// Index location for ZIP file records value.
		commentLengthIndex = END_CENTRAL_DIR_INDEX + 21,	// Index location for ZIP comment length.
		endCentralStartIndex = END_CENTRAL_DIR_INDEX + 19,	// Index location for End Central Start offset.
		centralLocalIndex = START_CENTRAL_DIR_INDEX - 1,	// Initialise variable to just before Start Central index location.
		newOffset = LAST_IDAT_INDEX,				// Initialise variable to last "IDAT" chunk's index location.
		zipRecords = (ImageVec[zipRecordsIndex] << 8) | ImageVec[zipRecordsIndex - 1];	// Get ZIP file records value from index location of vector "ImageVec".

	// Starting from the last "IDAT" chunk, search for ZIP file record offsets and update them to their new offset location.
	while (zipRecords--) {
		newOffset = search(ImageVec.begin() + newOffset + 1, ImageVec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - ImageVec.begin(),
		centralLocalIndex = 45 + search(ImageVec.begin() + centralLocalIndex, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin();
		update->Value(ImageVec, centralLocalIndex, newOffset, 32, false);
	}

	// Write updated "Start Central Directory" offset into End Central Directory's "Start Central Directory" index location within vector "ImageVec".
	update->Value(ImageVec, endCentralStartIndex, START_CENTRAL_DIR_INDEX, 32, false);

	// JAR file support. Get global comment length value from ZIP file within vector "ImageVec" and increase it by 16 bytes to cover end of PNG file.
	// To run a JAR file, you will need to rename the '.png' extension to '.jar'.  
	// or run the command: java -jar your_image_file.png
	SBYTE commentLength = 16 + (ImageVec[commentLengthIndex] << 8) | ImageVec[commentLengthIndex - 1];

	// Write the comment length value into the comment length index location within vector "ImageVec".
	update->Value(ImageVec, commentLengthIndex, commentLength, 16, false);
}

// The following code (slightly modified) to compute CRC32 (for "IDAT" & "PLTE" chunks) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
size_t crcUpdate(const size_t crc, BYTE* buf, const size_t len)
{
	// Table of CRCs of all 8-bit messages.
	size_t crcTable[256]
	{ 0, 1996959894, 3993919788, 2567524794, 124634137, 1886057615, 3915621685, 2657392035, 249268274, 2044508324, 3772115230, 2547177864, 162941995, 2125561021,
		 3887607047, 2428444049, 498536548, 1789927666, 4089016648, 2227061214, 450548861, 1843258603, 4107580753, 2211677639, 325883990, 1684777152, 4251122042,
		 2321926636, 335633487, 1661365465, 4195302755, 2366115317, 997073096, 1281953886, 3579855332, 2724688242, 1006888145, 1258607687, 3524101629, 2768942443,
		 901097722, 1119000684, 3686517206, 2898065728, 853044451, 1172266101, 3705015759, 2882616665, 651767980, 1373503546, 3369554304, 3218104598, 565507253,
		 1454621731, 3485111705, 3099436303, 671266974, 1594198024, 3322730930, 2970347812, 795835527, 1483230225, 3244367275, 3060149565, 1994146192, 31158534,
		 2563907772, 4023717930, 1907459465, 112637215, 2680153253, 3904427059, 2013776290, 251722036, 2517215374, 3775830040, 2137656763, 141376813, 2439277719,
		 3865271297, 1802195444, 476864866, 2238001368, 4066508878, 1812370925, 453092731, 2181625025, 4111451223, 1706088902, 314042704, 2344532202, 4240017532,
		 1658658271, 366619977, 2362670323, 4224994405, 1303535960, 984961486, 2747007092, 3569037538, 1256170817, 1037604311, 2765210733, 3554079995, 1131014506,
		 879679996, 2909243462, 3663771856, 1141124467, 855842277, 2852801631, 3708648649, 1342533948, 654459306, 3188396048, 3373015174, 1466479909, 544179635,
		 3110523913, 3462522015, 1591671054, 702138776, 2966460450, 3352799412, 1504918807, 783551873, 3082640443, 3233442989, 3988292384, 2596254646, 62317068,
		 1957810842, 3939845945, 2647816111, 81470997, 1943803523, 3814918930, 2489596804, 225274430, 2053790376, 3826175755, 2466906013, 167816743, 2097651377,
		 4027552580, 2265490386, 503444072, 1762050814, 4150417245, 2154129355, 426522225, 1852507879, 4275313526, 2312317920, 282753626, 1742555852, 4189708143,
		 2394877945, 397917763, 1622183637, 3604390888, 2714866558, 953729732, 1340076626, 3518719985, 2797360999, 1068828381, 1219638859, 3624741850, 2936675148,
		 906185462, 1090812512, 3747672003, 2825379669, 829329135, 1181335161, 3412177804, 3160834842, 628085408, 1382605366, 3423369109, 3138078467, 570562233,
		 1426400815, 3317316542, 2998733608, 733239954, 1555261956, 3268935591, 3050360625, 752459403, 1541320221, 2607071920, 3965973030, 1969922972, 40735498,
		 2617837225, 3943577151, 1913087877, 83908371, 2512341634, 3803740692, 2075208622, 213261112, 2463272603, 3855990285, 2094854071, 198958881, 2262029012,
		 4057260610, 1759359992, 534414190, 2176718541, 4139329115, 1873836001, 414664567, 2282248934, 4279200368, 1711684554, 285281116, 2405801727, 4167216745,
		 1634467795, 376229701, 2685067896, 3608007406, 1308918612, 956543938, 2808555105, 3495958263, 1231636301, 1047427035, 2932959818, 3654703836, 1088359270,
		 936918000, 2847714899, 3736837829, 1202900863, 817233897, 3183342108, 3401237130, 1404277552, 615818150, 3134207493, 3453421203, 1423857449, 601450431,
		 3009837614, 3294710456, 1567103746, 711928724, 3020668471, 3272380065, 1510334235, 755167117 };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
	size_t c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

// Return the CRC of the bytes buf[0..len-1].
size_t crc(BYTE* buf, const size_t len)
{
	return crcUpdate(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle ZIP Edition (PDVZIP v1.3). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.
		
PDVZIP enables you to embed a ZIP file within a *tweetable and "executable" PNG image.  		
		
The hosting sites will retain the embedded arbitrary data within the PNG image.  
		
PNG image size limits are platform dependant:  
Flickr (200MB), Imgbb (32MB), ImageShack (25MB), PostImage (24MB), ImgPile (8MB), *Twitter & Imgur (5MB).

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
