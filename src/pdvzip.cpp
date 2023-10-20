// PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.3). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux):
// 	$ g++ pdvzip.cpp -O2 -s -o pdvzip

//	Run it:
//	$ ./pdvzip

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
	void Value(std::vector<BYTE>& vec, uint32_t valueInsertIndex, const uint32_t VALUE, uint8_t Bits, bool isBig) {
		if (isBig) {
			while (Bits) vec[valueInsertIndex++] = (VALUE >> (Bits -= 8)) & 0xff;
		}
		else {
			while (Bits) vec[valueInsertIndex--] = (VALUE >> (Bits -= 8)) & 0xff;
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
void fixZipOffset(std::vector<BYTE>&, const uint32_t);

// Output to screen detailed program usage information.
void displayInfo();

// Code to compute CRC32 (for "IDAT" & "PLTE" chunks within this program) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint32_t crcUpdate(const uint32_t, BYTE*, const uint32_t);
uint32_t crc(BYTE*, const uint32_t);

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
		ZipVec{ 0, 0, 0, 0, 0x49, 0x44, 0x41, 0x54, 0, 0, 0, 0 }, // "IDAT" chunk name with 4-byte chunk length and crc fields.
		ImageVec((std::istreambuf_iterator<char>(IMAGE)), std::istreambuf_iterator<char>());  // Insert user image file into vector "ImageVec".

	// Insert user ZIP file into vector "ZipVec" at index 8, just after "IDAT" chunk name.
	ZipVec.insert(ZipVec.begin() + 8, std::istreambuf_iterator<char>(ZIP), std::istreambuf_iterator<char>());

	// Get size of user ZIP file from vector "ZipVec", minux 0xC bytes. "IDAT" chunk name(4), chunk length field(4), chunk crc field(4).
	const uint64_t ZIP_SIZE = ZipVec.size() - 0xC;

	// Array used by different functions within this program.
	// Occurrence of these characters in the "IHDR" chunk (data/crc), "PLTE" chunk (data/crc) or "hIST" chunk (length), breaks the Linux extraction script.
	const char BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' };

	// Make sure PNG and ZIP file specs meet program requirements. 
	checkFileRequirements(ImageVec, ZipVec, BAD_CHAR);

	// Find and remove unnecessary PNG chunks.
	eraseChunks(ImageVec);

	const uint16_t MAX_SCRIPT_SIZE_BYTES = 0x2EE;	// Extraction script size limit.

	const uint32_t
		IMG_SIZE = static_cast<uint32_t>(ImageVec.size()),
		MAX_PNG_SIZE_BYTES = 0xC800000,	// 200MB PNG "file-embedded" size limit.
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
	if (ImageVec[0x19] == 3) {
		// Alter characters within the "PLTE" chunk to prevent the extraction script from breaking.
		modifyPaletteChunk(ImageVec, BAD_CHAR);
	}

	uint8_t
		Chunk_Length_Index = 0, // Index of chunk length field for vector "ZipVec".
		Bits = 32;

	// Write the updated "IDAT" chunk length of vector "ZipVec" within its length field.
	update->Value(ZipVec, Chunk_Length_Index, static_cast<uint32_t>(ZIP_SIZE), Bits, true);

	// Finish building extraction script.
	completeScript(ZipVec, ImageVec, BAD_CHAR);
}

void checkFileRequirements(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, const char(&BAD_CHAR)[]) {

	const std::string
		PNG_SIG = "\x89PNG",								// Valid file header signature of PNG image.
		ZIP_SIG = "PK\x03\x04",								// Valid file header signature of ZIP file.
		IMG_VEC_HDR{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() },		// Get file header from vector "ImageVec". 
		ZIP_VEC_HDR{ ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_SIG.length() };	// Get file header from vector "ZipVec".

	const uint16_t
		IMAGE_DIMS_WIDTH = ImageVec[0x12] << 8 | ImageVec[0x13],	// Get image width dimensions from vector "ImageVec".
		IMAGE_DIMS_HEIGHT = ImageVec[0x16] << 8 | ImageVec[0x17],	// Get image height dimensions from vector "ImageVec".
		PNG_TRUECOLOUR_MAX_DIMS = 0x383,				// 899 x 899 maximum supported dimensions for PNG truecolour (PNG-32/PNG-24, Colour types 2 & 6).
		PNG_INDEXCOLOUR_MAX_DIMS = 0x1000;				// 4096 x 4096 maximum supported dimensions for PNG indexed-colour (PNG-8, Colour type 3).
	
	const uint8_t
		COLOUR_TYPE = ImageVec[0x19] == 6 ? 2 : ImageVec[0x19],	// Get image colour type value from vector "ImageVec". If value is 6 (Truecolour with alpha), set the value to 2 (Truecolour).
		PNG_MIN_DIMS = 0x44,					// 68 x 68 minimum supported dimensions for PNG indexed-colour and truecolour.
		INZIP_NAME_LENGTH = ZipVec[0x22],			// Get length of zipped media filename from vector "ZipVec".
		PNG_INDEXCOLOUR = 3,					// PNG-8, indexed colour.
		PNG_TRUECOLOUR = 2,					// PNG-24, truecolour. (We also use this for PNG-32 / truecolour with alpha, 6) as we deal with them the same way.
		MIN_NAME_LENGTH = 4;					// Minimum filename length of zipped file.

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
	uint8_t Index = 0x12; // Index search start position within vector "ImageVec".

	// From pos location, increment through 15 bytes of vector "ImageVec" and compare each byte to the 7 characters within "BAD_CHAR" array.
	for (uint8_t i{}; i < 0x10; i++) {
		for (uint8_t j{}; j < 7; j++) {
			if (ImageVec[Index] == BAD_CHAR[j]) { // "BAD_CHAR" character found, display error message and exit program.
				std::cerr <<
					"\nInvalid Character Error:  \n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
					"\nTry modifying image dimensions (1% increase or decrease) to resolve the issue. Repeat if necessary.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}
		Index++;
	}
}

void eraseChunks(std::vector<BYTE>& ImageVec) {

	const std::string CHUNK[0xF]{ "IDAT", "bKGD", "cHRM", "iCCP", "sRGB", "hIST", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	for (uint8_t Chunk_Index = 0xE; Chunk_Index > 0; Chunk_Index--) {
		uint32_t
			// Get the first "IDAT" chunk index location. Don't remove any chunks after this point.
			First_Idat_Index = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), CHUNK[0].begin(), CHUNK[0].end()) - ImageVec.begin()),
			// From last to first, search and get the index location of each chunk to remove.
			Chunk_Found_Index = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), CHUNK[Chunk_Index].begin(), CHUNK[Chunk_Index].end()) - ImageVec.begin()) - 4;
		// If found chunk is located before the first "IDAT" chunk, erase it.
		if (First_Idat_Index > Chunk_Found_Index) {
			uint32_t Chunk_Size = (ImageVec[Chunk_Found_Index + 1] << 16) | ImageVec[Chunk_Found_Index + 2] << 8 | ImageVec[Chunk_Found_Index + 3];
			ImageVec.erase(ImageVec.begin() + Chunk_Found_Index, ImageVec.begin() + Chunk_Found_Index + (Chunk_Size + 0xC));
			Chunk_Index++; // Increment chunkIndex so that we search again for the same chunk name, in case of multiple occurrences.
		}
	}
}

void modifyPaletteChunk(std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	// PNG-8, Imgur (image hosting site), Linux issue. 
	// Some individual characters, a sequence or combination of certain characters that may appear within the PLTE chunk, will break the extraction script.
	// This function contains a number of fixes for this issue.
	// The "PLTE" chunk must be located before the "hIST" chunk (extraction script) for pdv images to work correctly on the Imgur image hosting site.
	const std::string PLTE_SIG = "PLTE";

	const uint32_t PLTE_CHUNK_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - ImageVec.begin()); // Search for index location of "PLTE" chunk name within vector "ImageVec".
		
	const uint16_t PLTE_CHUNK_SIZE = (ImageVec[PLTE_CHUNK_INDEX - 2] << 8) | ImageVec[PLTE_CHUNK_INDEX - 1]; // Get "PLTE" chunk size from vector "ImageVec".

	// Replace "BAD_CHAR" characters with "GOOD_CHAR" characters.
	// While image corruption is possible when altering the "PLTE" chunk, the majority of images should be fine with these replacement characters.
	const char GOOD_CHAR[7]{ '*', '&', '=', '}', 'a', '?', ':' };

	uint8_t Two_Count{};

	for (uint32_t i = PLTE_CHUNK_INDEX; i < (PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4)); i++) {

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
			Two_Count++;
			if (Two_Count > 1) { // Once we find two of those characters in a row, replace one of them with the relevant "GOOD_CHAR".
				ImageVec[i] = (ImageVec[i] == '&' ? GOOD_CHAR[0] : GOOD_CHAR[3]);
				Two_Count = 0;
			}
		}
		else {
			Two_Count = 0;
		}

		// Character "<" followed by a number (or a sequence of numbers up to 11 digits) and ending with the same character "<", breaks the extraction script. 
		// e.g. "<1<, <4356<, <293459< <35242369849<".
		uint8_t Num_Pos = 1, Arrow_Pos = 2, Max_Num_Pos = 0xC;
		if (ImageVec[i] == '<') { // Found this "<" character, now check if it is followed by a number and ends with another "<" character.
			while (Num_Pos < Max_Num_Pos) {
				if (ImageVec[i + Num_Pos] > 0x2F && ImageVec[i + Num_Pos] < 0x3A && ImageVec[i + Arrow_Pos] == '<') { // Found pattern sequence 
					ImageVec[i] = GOOD_CHAR[2];  // Replace character "<" at the beginning of the string with the relevant "GOOD_CHAR" character.
					Num_Pos = Max_Num_Pos;
				}
				Num_Pos++, Arrow_Pos++;
			}
		}
	}

	uint8_t
		Mod_Value = 0xFF,
		Bits = 32;

	bool Redo_Crc{};

	do {
		Redo_Crc = false;

		// After modifying the "PLTE" chunk, we need to update the chunk's crc value.
		// Pass these two values (PLTE_CHUNK_INDEX & PLTE_CHUNK_SIZE + 4) to the crc fuction to get correct "PLTE" chunk crc value.
		const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_CHUNK_INDEX], PLTE_CHUNK_SIZE + 4);

		// Get vector index locations for "PLTE" crc chunk field and "PLTE" modValue.
		uint32_t
			Crc_Insert_Index = PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4),
			Mod_Value_Insert_Index = Crc_Insert_Index - 1;

		// Write the updated crc value into the "PLTE" chunk's crc field (bits=32) within vector "ImageVec".
		update->Value(ImageVec, Crc_Insert_Index, PLTE_CHUNK_CRC, Bits, true);

		// Make sure the new crc value does not contain any of the "BAD_CHAR" characters, which will break the extraction script.
		// If we find a "BAD_CHAR" in the new crc value, modify one byte in the "PLTE" chunk ("PLTE" mod location), then recalculate crc value. 
		// Repeat process until no BAD_CHAR found.
		for (uint8_t i = 0; i < 5; i++) {
			for (uint8_t j = 0; j < 7; j++) {
				if (i > 3) break;
				if (ImageVec[Crc_Insert_Index] == BAD_CHAR[j]) {
					ImageVec[Mod_Value_Insert_Index] = Mod_Value;
					Mod_Value--;
					Redo_Crc = true;
					break;
				}
			}
			Crc_Insert_Index++;
		}
	} while (Redo_Crc);
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
	std::vector<std::string> AppVec{ "aac", "mp3", "mp4", "avi", "asf", "flv", "ebm", "mkv", "peg", "wav", "wmv", "wma", "mov", "3gp", "ogg", "pdf", ".py", "ps1", "exe",
		".sh", "vlc --play-and-exit --no-video-title-show ", "evince ", "python3 ", "pwsh ", "./", "xdg-open ", "powershell;Invoke-Item ",
		" &> /dev/null", "start /b \"\"", "pause&", "powershell", "chmod +x ", ";" };

	const uint16_t MAX_SCRIPT_SIZE_BYTES = 0x2EE;	// 750 bytes extraction script size limit.

	const uint8_t
		INZIP_NAME_LENGTH_INDEX = 0x22,				// "ZipVec" vector's index location for the length value of the zipped media filename.
		INZIP_NAME_INDEX = 0x26,				// "ZipVec" start index location for the zipped media filename.
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	// Get character length of the zipped media filename from vector "ZipVec".
		
		// "AppVec" vector element index values. 
		// Some "AppVec" vector string elements are added later (via push_back), so don't currently appear in the above string vector.
		VIDEO_AUDIO = 0x14,		// "vlc" app command for Linux. 
		PDF = 0x15,			// "evince" app command for Linux. 
		PYTHON = 0x16,			// "python3" app command for Linux & Windows.
		POWERSHELL = 0x17,		// "pwsh" app command for Linux, for starting PowerShell scripts.
		EXECUTABLE = 0x18,		// "./" prepended to filename. Required when running Linux executables.
		BASH_XDG_OPEN = 0x19,		// "xdg-open" Linux command, runs shell scripts (.sh), opens folders & unmatched file extensions.
		FOLDER_INVOKE_ITEM = 0x1A,	// "powershell;Invoke-Item" command used in Windows for opening folders.
		START_B = 0x1C,			// "start /b" Windows command used to open most file types. Windows uses set default app for file type.
		WIN_POWERSHELL = 0x1E,		// "powershell" commmand used by Windows for running PowerShell scripts.
		MOD_INZIP_FILENAME = 0x24;	// inzipName with ".\" prepend characters. Required for Windows PowerShell, e.g. powershell ".\filename.ps1".

	std::string
		// Get the zipped media filename string from vector "ZipVec".
		In_Zip_Name{ ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH },
		// Get the file extension from zipped media filename.
		In_Zip_Name_Ext = In_Zip_Name.substr(In_Zip_Name.length() - 3, 3),
		// Variables for optional command-line arguments.
		Args_Linux{},
		Args_Windows{};

	uint32_t Check_Extension = static_cast<uint32_t>(In_Zip_Name.find_last_of('.'));

	// Store inzipName in "AppVec" vector (33).
	AppVec.push_back(In_Zip_Name);

	// When inserting string elements from vector "AppVec" into the script (within vector "ScriptVec"), we are adding items in 
	// the order of last to first. The Windows script is completed first, followed by Linux. 
	// This order prevents the vector insert locations from changing every time we add a new string element into the vector.

	// The array "sequence" can be split into four sequences containing "ScriptVec" index values (high numbers) used by the 
	// "insertIndex" variable and the corresponding "AppVec" index values (low numbers) used by the "appIndex" variable.

	// For example, in the 1st sequence, sequence[0] = index 0xEC of "ScriptVec" ("insertIndex") corresponds with
	// sequence[5] = "AppVec" index 0x21 ("appIndex"), which is the zipped media filename string element. 
	// "AppVec" string element 0x21 (zipped media filename) will be inserted into the script (vector "ScriptVec") at index 0xEC.

	uint8_t 
		App_Index = 0,		// Uses the "AppVec" index values from the sequence array.
		Insert_Index = 0,	// Uses the "ScriptVec" index values from the sequence array.
		Sequence_Limit = 0;	// Stores the length limit of each of the four sequences. 

	uint16_t Sequence[0x34]{
				0xEC, 0xEA, 0x74, 0x73, 0x72, 0x21, 0x1C, 0x1B ,0x21, 0x14,	// 1st sequence for case "VIDEO_AUDIO".
				0xEC, 0xEA, 0x73, 0x72,	0x21, 0x1C, 0x21, 0x15,			// 2nd sequence for cases "PDF, FOLDER_INVOKE_ITEM, DEFAULT".
				0x103, 0xED, 0xEC, 0xEA, 0x74, 0x73, 0x72, 0x1D, 0x23, 0x21, 0x16, 0x22, 0x21, 0x16,	// 3rd sequence for cases "PYTHON, POWERSHELL".
				0x103, 0xED, 0xEC, 0xEA, 0x74 ,0x73, 0x72, 0x72, 0x72 ,0x72, 0x1D, 0x23, 0x21, 0x1C, 0x22, 0x21, 0x18, 0x20, 0x21, 0x1F }; // 4th sequence for cases "EXECUTABLE & BASH_XDG_OPEN".
		
	/*  [sequence](insertIndex)[sequence](appIndex)
		Build script example below is using the first sequence (see "sequence" array above).

		VIDEO_AUDIO:
		[0]>(0xEC)[5]>(0x21) Windows: "ImageVec" 0xEC insert index for zipped media filename, "AppVec" 0x21.
		[1]>(0xEA)[6]>(0x1C) Windows: "ImageVec" 0xEA insert index for "start /b", "AppVec" 0x1C.
		[2]>(0x74)[7]>(0x1B) Linux: "ImageVec" 0x74 insert index for "Dev Null", "AppVec" 0x1B.
		[3]>(0x73)[8]>(0x21) Linux: "ImageVec" 0x73 insert index for zipped media filename, "AppVec" 0x21.
		[4]>(0x72)[9]>(0x14) Linux: "ImageVec" 0x72 insert index for "vlc", "AppVec" 0x14.
		Sequence limit is 5 (value taken from first appIndex value for each sequence).

		Matching a file extension from "In_Zip_Name_Ext" within "AppVec", we can select which application string and commands to use
		in our extraction script, which, when executed, will open/play/run (depending on file type) the extracted zipped media file.

		Once the correct app extension has been matched by the "for loop" below, it passes the App_Index result to the switch statement.
		The relevant Case sequence is then used in completing the extraction script within vector "ScriptVec".	*/

	for (; App_Index != 0x1A; App_Index++) {
		if (AppVec[App_Index] == In_Zip_Name_Ext) {
			// After a file extension match, any App_Index value between 0 & 0xE defaults to "AppVec" 0x14 (vlc / VIDEO_AUDIO).
			// If over 0x14, we add 6 to the value. 0xF + 6 = "AppVec" 0x15 (evince/PDF), 0x10 + 6 = "AppVec" 0x16 (python3/.py), etc.
			App_Index = App_Index <= 0xE ? 0x14 : App_Index += 6;
			break;
		}
	}

	// If no file extension detected, check if "In_Zip_Name" points to a folder (/), else assume file is a Linux executable.
	if (Check_Extension == 0 || Check_Extension > In_Zip_Name.length()) {
		App_Index = ZipVec[INZIP_NAME_INDEX + INZIP_NAME_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	// Provide the user with the option to add command-line arguments for file types: .py, .ps1, .sh and .exe (no extension, defaults to .exe).
	if (App_Index > 0x15 && App_Index < 0x1A) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, Args_Linux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, Args_Windows);

		Args_Linux.insert(0, "\x20"), Args_Windows.insert(0, "\x20");

		AppVec.push_back(Args_Linux),	// "AppVec" (0x22).
		AppVec.push_back(Args_Windows);	// "AppVec" (0x23).
	}

	switch (App_Index) {
	case VIDEO_AUDIO:	// Case uses 1st sequence: 0xEC, 0xEA, 0x74, 0x73, 0x72, 0x21, 0x1C, 0x1B ,0x21, 0x14.
		App_Index = 5;
		break;
	case PDF:		// These two cases (with minor changes) use the 2nd sequence: 0xEC, 0xEA, 0x73, 0x72,	0x21, 0x1C, 0x21, 0x15.
	case FOLDER_INVOKE_ITEM:
		Sequence[0xF] = App_Index == FOLDER_INVOKE_ITEM ? FOLDER_INVOKE_ITEM : START_B;
		Sequence[0x11] = App_Index == FOLDER_INVOKE_ITEM ? BASH_XDG_OPEN : PDF;
		Insert_Index = 0xA,
		App_Index = 0xE;
		break;
	case PYTHON:		// These two cases (with some changes) use the 3rd sequence: 0x103, 0xED, 0xEC, 0xEA, 0x74, 0x73, 0x72, 0x1D, 0x23, 0x21, 0x16, 0x22, 0x21, 0x16.
	case POWERSHELL:
		if (App_Index == POWERSHELL) {
			In_Zip_Name.insert(0, ".\\");		//  ".\" prepend to inzipName. Required for Windows PowerShell, e.g. powershell ".\filename.ps1".
			AppVec.push_back(In_Zip_Name);		// Add the modified filename to the "AppVec" vector (0x24).
			Sequence[0x1F] = POWERSHELL,		// Swap index number to Linux PowerShell (pwsh 0x17)
			Sequence[0x1C] = WIN_POWERSHELL;	// Swap index number to Windows PowerShell (powershell 0x1E)
			Sequence[0x1B] = MOD_INZIP_FILENAME;	// Swap index number to MOD_INZIP_FILENAME (0x24), used with the Windows powershell command.
		}
		Insert_Index = 0x12,
		App_Index = 0x19;
		break;
	case EXECUTABLE:	// These two cases (with minor changes) use the 4th sequence: 0x103, 0xED, 0xEC, 0xEA, 0x74 ,0x73, 0x72, 0x72, 0x72 ,0x72, 0x1D, 0x23, 0x21, 0x1C, 0x22, 0x21, 0x18, 0x20, 0x21, 0x1F.
	case BASH_XDG_OPEN:
		Insert_Index = App_Index == EXECUTABLE ? 0x20 : 0x21;
		App_Index = Insert_Index == 0x20 ? 0x2A : 0x2B;
		break;
	default:			// Unmatched file extensions. Rely on operating system to use the set default program for dealing with file type.
		Insert_Index = 0xA,	// Case uses 2nd sequence, we just need to alter one index number.
		App_Index = 0xE;
		Sequence[0x11] = BASH_XDG_OPEN;	// Swap index number to BASH_XDG_OPEN (0x19)
	}

	// Set the Sequence_Limit variable using the first App_Index value from each switch case sequence.
	// Reduce Sequence_Limit variable value by 1 if Insert_Index is 0x21 (case BASH_XDG_OPEN).
	Sequence_Limit = Insert_Index == 0x21 ? App_Index - 1 : App_Index;

	// With just a single vector insert command within the "while loop", we can insert all the required strings into the extraction script (vector "ScriptVec"), 
	// based on the sequence array, which is selected by the relevant case, from the above switch statement after the extension match. 
	while (Sequence_Limit > Insert_Index) {
		ScriptVec.insert(ScriptVec.begin() + Sequence[Insert_Index], AppVec[Sequence[App_Index]].begin(), AppVec[Sequence[App_Index]].end());
		Insert_Index++, App_Index++;
	}

	bool Redo_Chunk_Length{};

	do {
		Redo_Chunk_Length = false;

		// Extraction script within the "hIST" chunk of vector "ScriptVec", is now complete. 
		// Update "hIST" chunk length.
		const uint16_t HIST_LENGTH = static_cast<uint16_t>(ScriptVec.size()) - 0xC;

		// Display relevant error message and exit program if extraction script exceeds size limit.
		if (HIST_LENGTH > MAX_SCRIPT_SIZE_BYTES) {
			std::cerr << "\nScript Size Error: Extraction script exceeds maximum size of 750 bytes.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// "ScriptVec" vector's index location for the "hIST" chunk length field.
		uint8_t
			Chunk_Length_Index = 2,
			Bits = 16;

		// Call function to write updated chunk length value for "hIST" chunk into its length field. 
		// Due to its small size, the "hIST" chunk will only use 2 bytes maximum (Bits=16) of the 4 byte length field.
		update->Value(ScriptVec, Chunk_Length_Index, HIST_LENGTH, Bits, true);

		// Check the first byte of the "hIST" chunk length field to make sure the updated chunk length does not match 
		// any of the "BAD_CHAR" characters that will break the Linux extraction script.
		for (uint8_t i = 0; i < 7; i++) {
			if (ScriptVec[3] == BAD_CHAR[i]) {
				// "BAD_CHAR" found. Insert a byte at the end of "ScriptVec" to increase chunk length. Update chunk length field. 
				// Check again for a "BAD_CHAR" match. Repeat the byte insertion and chunk length update, until no "BAD_CHAR" match.
				ScriptVec.insert(ScriptVec.begin() + (HIST_LENGTH + 0xA), '.');
				Redo_Chunk_Length = true;
				break;
			}
		}
	} while (Redo_Chunk_Length);

	// Insert vectors "ScripVec" ("hIST" chunk with completed extraction script) & "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec" (PNG image).
	combineVectors(ImageVec, ZipVec, ScriptVec);
}

void combineVectors(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, std::vector<BYTE>& ScriptVec) {

	srand((unsigned)time(NULL));  // For output filename.

	const std::string
		IDAT_SIG = "IDAT",
		TXT_NUM = std::to_string(rand()),
		PDV_FILENAME = "pdvzip_image_" + TXT_NUM.substr(0, 5) + ".png"; // Output unique filename for the complete polyglot image.

	const uint32_t
		// Search vector "ImageVec" for the index location of the first "IDAT" chunk (start of length field).
		// This value will be used as the insert location within vector "ImageVec" for contents of vector "ScriptVec". 
		// The inserted vector contents will appear just before the first "IDAT" chunk and (if PNG-8) after the "PLTE" chunk. 
		// For Imgur to work correctly the "PLTE" chunk (PNG-8) must be located before the "hIST" chunk (extraction script). 
		FIRST_IDAT_INDEX = static_cast<uint32_t>(search(ImageVec.begin(), ImageVec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - ImageVec.begin()) - 4,

		// Set the index insert location within vector "ImageVec" for contents of vector "ZipVec".
		// The insert location is just after the last "IDAT" chuck within the vector "ImageVec".
		LAST_IDAT_INDEX = static_cast<uint32_t>(ImageVec.size() + ScriptVec.size()) - 0xC;

	// Insert contents of vector "ScriptVec" ("hIST" chunk containing extraction script) into vector "ImageVec".	
	ImageVec.insert((ImageVec.begin() + FIRST_IDAT_INDEX), ScriptVec.begin(), ScriptVec.end());

	// Insert contents of vector "ZipVec" ("IDAT" chunk with ZIP file) into vector "ImageVec".
	// This now becomes the last "IDAT" chunk of the PNG image within vector "ImageVec".
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_INDEX), ZipVec.begin(), ZipVec.end());

	// Before updating the last "IDAT" chunk's crc value, adjust ZIP file offsets within this chunk.
	fixZipOffset(ImageVec, LAST_IDAT_INDEX);

	// Generate crc value for our (new) last "IDAT" chunk.
	// The +4 value points LAST_IDAT_INDEX variable value to the chunk name location, which is where the crc calculation needs to begin/include.
	const uint32_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_INDEX + 4], static_cast<uint32_t>(ZipVec.size()) - 8); // We don't include the 4 bytes length field or the 4 bytes crc field (8 bytes).

	uint32_t Crc_Insert_Index = static_cast<uint32_t>(ImageVec.size()) - 0x10;	// Get index location for last "IDAT" chunk's 4-byte crc field, from vector "ImageVec".

	uint8_t Bits = 32;

	// Call function to write new crc value into the last "IDAT" chunk's crc index field, within the vector "ImageVec".
	update->Value(ImageVec, Crc_Insert_Index, LAST_IDAT_CRC, Bits, true);

	std::ofstream writeFinal(PDV_FILENAME, std::ios::binary);

	if (!writeFinal) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Write out to file vector "ImageVec" now containing the completed polyglot image (Image + Script + ZIP).
	writeFinal.write((char*)&ImageVec[0], ImageVec.size());

	std::string Msg_Size_Warning =
		"\n**Warning**\n\nDue to the file size of your \"zip-embedded\" PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
		"Flickr, ImgBB, ImageShack, PostImage & ImgPile";

	const uint8_t Msg_Len = static_cast<uint8_t>(Msg_Size_Warning.length());

	const uint32_t
		Img_Size = static_cast<uint32_t>(ImageVec.size()),
		Imgur_Twitter_Size = 0x500000,	// 5MB
		Img_Pile_Size =	0x800000,	// 8MB
		Post_Image_Size = 0x1800000,	// 24MB
		Image_Shack_Size = 0x1900000,	// 25MB 
		Imgbb_Size = 0x2000000;		// 32MB
		// Flickr is 200MB, this programs max size, no need to to make a variable for it.

	Msg_Size_Warning = (Img_Size > Img_Pile_Size && Img_Size <= Post_Image_Size ? Msg_Size_Warning.substr(0, Msg_Len - 0xA)
		: (Img_Size > Post_Image_Size && Img_Size <= Image_Shack_Size ? Msg_Size_Warning.substr(0, Msg_Len - 0x15)
			: (Img_Size > Image_Shack_Size && Img_Size <= Imgbb_Size ? Msg_Size_Warning.substr(0, Msg_Len - 0x21)
				: (Img_Size > Imgbb_Size ? Msg_Size_Warning.substr(0, Msg_Len - 0x28) : Msg_Size_Warning))));

	if (Img_Size > Imgur_Twitter_Size) {
		std::cerr << Msg_Size_Warning << ".\n";
	}

	std::cout << "\nCreated output file " << "'" << PDV_FILENAME << "' " << ImageVec.size() << " bytes." << "\n\nAll Done!\n\n";
}

void fixZipOffset(std::vector<BYTE>& ImageVec, const uint32_t LAST_IDAT_INDEX) {

	const std::string
		START_CENTRAL_DIR_SIG = "PK\x01\x02",
		END_CENTRAL_DIR_SIG = "PK\x05\x06",
		ZIP_SIG = "PK\x03\x04";

	// Search vector "ImageVec" (start from last "IDAT" chunk) to get index locations for "Start Central Directory" & "End Central Directory".
	const uint32_t
		START_CENTRAL_DIR_INDEX = static_cast<uint32_t>(search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin()),
		END_CENTRAL_DIR_INDEX = static_cast<uint32_t>(search(ImageVec.begin() + START_CENTRAL_DIR_INDEX, ImageVec.end(), END_CENTRAL_DIR_SIG.begin(), END_CENTRAL_DIR_SIG.end()) - ImageVec.begin());

	uint32_t
		Zip_Records_Index = END_CENTRAL_DIR_INDEX + 0xB,		// Index location for ZIP file records value.
		Comment_Length_Index = END_CENTRAL_DIR_INDEX + 0x15,		// Index location for ZIP comment length.
		End_Central_Start_Index = END_CENTRAL_DIR_INDEX + 0x13,		// Index location for End Central Start offset.
		Central_Local_Index = START_CENTRAL_DIR_INDEX - 1,		// Initialise variable to just before Start Central index location.
		New_Offset = LAST_IDAT_INDEX;					// Initialise variable to last "IDAT" chunk's index location.

	uint16_t Zip_Records = (ImageVec[Zip_Records_Index] << 8) | ImageVec[Zip_Records_Index - 1];	// Get ZIP file records value from index location of vector "ImageVec".

	uint8_t Bits = 32;

	// Starting from the last "IDAT" chunk, search for ZIP file record offsets and update them to their new offset location.
	while (Zip_Records--) {
		New_Offset = static_cast<uint32_t>(search(ImageVec.begin() + New_Offset + 1, ImageVec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - ImageVec.begin()),
		Central_Local_Index = 45 + static_cast<uint32_t>(search(ImageVec.begin() + Central_Local_Index, ImageVec.end(), START_CENTRAL_DIR_SIG.begin(), START_CENTRAL_DIR_SIG.end()) - ImageVec.begin());
		update->Value(ImageVec, Central_Local_Index, New_Offset, Bits, false);
	}

	// Write updated "Start Central Directory" offset into End Central Directory's "Start Central Directory" index location within vector "ImageVec".
	update->Value(ImageVec, End_Central_Start_Index, START_CENTRAL_DIR_INDEX, Bits, false);

	// JAR file support. Get global comment length value from ZIP file within vector "ImageVec" and increase it by 16 bytes to cover end of PNG file.
	// To run a JAR file, you will need to rename the '.png' extension to '.jar'.  
	// or run the command: java -jar your_image_file.png
	uint16_t Comment_Length = 0x10 + (ImageVec[Comment_Length_Index] << 8) | ImageVec[Comment_Length_Index - 1];

	Bits = 16;

	// Write the comment length value into the comment length index location within vector "ImageVec".
	update->Value(ImageVec, Comment_Length_Index, Comment_Length, Bits, false);
}

// The following code (slightly modified) to compute CRC32 (for "IDAT" & "PLTE" chunks) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint32_t crcUpdate(const uint32_t crc, BYTE* buf, const uint32_t len)
{
	// Table of CRCs of all 8-bit messages.
	uint32_t crcTable[0x100]
	{ 0, 	0x77073096, 0xee0e612c, 0x990951ba, 0x76dc419,  0x706af48f, 0xe963a535, 0x9e6495a3, 0xedb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x9b64c2b,  
		0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
		0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1,
		0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac,
		0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87,
		0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x1db7106,  0x98d220bc, 0xefd5102a, 0x71b18589, 0x6b6b51f,  0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2,
		0xf00f934,  0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x86d3d2d,  0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
		0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158,
		0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73,
		0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e,
		0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x3b6e20c,  0x74b1d29a, 0xead54739,
		0x9dd277af, 0x4db2615,  0x73dc1683, 0xe3630b12, 0x94643b84, 0xd6d6a3e,  0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0xa00ae27,  0x7d079eb1, 0xf00f9344,
		0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f,
		0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda,
		0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795,
		0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0,
		0xec63f226, 0x756aa39c, 0x26d930a,  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x5005713,  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0xcb61b38,  0x92d28e9b,
		0xe5d5be0d, 0x7cdcefb7, 0xbdbdf21,  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6,
		0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661,
		0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c,
		0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37,
		0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
	uint32_t c = crc;

	for (uint32_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
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
