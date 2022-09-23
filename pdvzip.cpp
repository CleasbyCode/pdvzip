//	PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP) v1.1. Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

// The following code to compute IDAT CRC was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
//___________________________________________________________________________________________________________________

// Table of CRCs of all 8-bit messages.
unsigned long crcTable[256];

// Flag: has the table been computed? Initially false.
int crcTableComputed = 0;

// Make the table for a fast CRC.
void makeCrcTable(void)
{
	unsigned long c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (unsigned long)n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crcTable[n] = c;
	}
	crcTableComputed = 1;
}
// Update a running CRC with the bytes buf[0..len-1]--the CRC should be initialized to all 1's, 
// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
unsigned long updateCrc(const unsigned long& crc, unsigned char* buf, const size_t& len)
{
	unsigned long c = crc;
	int n;

	if (!crcTableComputed)
		makeCrcTable();
	for (n = 0; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}
// Return the CRC of the bytes buf[0..len-1].
unsigned long crc(unsigned char* buf, const size_t& len)
{
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}
//  End of IDAT CRC Function 
//__________________________________________________________________________________________________________________


// Open user files for reading, check size of PNG & ZIP File. Display error & quit program if any file fails to open or exceeds size limits.
int openFilesCheckSize(char* []);

// Read & store PNG image & ZIP file into separate vectors, check for relevant file requirements. Display error message and quit program if requirement checks fail.
int readFilesIntoVectorsCheckSpecs(const string&, const string&, const ptrdiff_t&, const ptrdiff_t&);

// Select & insert the correct elements from "extApp" string array into the vector "ScriptVec", to complete the script build.
int buildScript(vector<unsigned char>&, vector<unsigned char>&, const string&);

// Combine three vectors into one.
void combineVectors(vector<unsigned char>&, vector<unsigned char>&, vector<unsigned char>&, const string&);

// Update ZIP file record offsets to new location
void fixZipOffset(vector<unsigned char>&, const ptrdiff_t&);

// Write out to file complete PNG vector (Image + Script + ZIP). Display error & quit program if write fails.
int writeFile(vector<unsigned char>&, const string&);

// Inserts updated chunk length (or CRC) value into vector index location; Also used to insert adjusted ZIP offset values for "fixZipOffset" function.
void insertChunkLength(vector<unsigned char>&, ptrdiff_t, const size_t&, int, bool);

// Display program infomation
void displayInfo(void);


// ID header strings for vector search and file type validation.
const string
	PNG_ID = "\x89PNG",
	ZIP_ID = "PK\x03\x04",
	PLTE_ID = "PLTE",
	START_CENTRAL_ID = "PK\x01\x02",
	END_CENTRAL_ID = "PK\x05\x06",
	FIRST_IDAT_ID = "IDAT",
	LAST_IDAT_ID = "IDATPK";

const unsigned int
	MAX_MULTIPLIED_DIMS = 5500000,	// Maximum multiplied Width x Height dimensions value;
	MAX_PNG = 5242880,		// Twitter's 5MB PNG file size limit;
	MAX_SCRIPT_SIZE = 400;		// Script size limit, bytes.


int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	else if (argc < 3 || argc > 3) {
		cout << "\nUsage:  pdvzip  <png_image>  <zip_file>\n\tpdvzip  --info\n\n";
	}
	else {
		openFilesCheckSize(argv);
	}
	return 0;
}

int openFilesCheckSize(char* argv[]) {

	const string
		IMG_FILE = argv[1],
		ZIP_FILE = argv[2],
		SIZE_ERR_MSG = "Size Error: File must not exceed Twitter's file size limit of 5MB (5,242,880 bytes)", // Repeated error messages.
		READ_ERR_MSG = "Read File Error: Unable to open / read file: ";

	ifstream readImg(IMG_FILE, ios::binary);
	ifstream readZip(ZIP_FILE, ios::binary);

	if (!readImg || !readZip) {
		// Open file failure, display relevant error message and quit program.
		if (!readImg) {
			cerr << "\nPNG " << READ_ERR_MSG << IMG_FILE << "\n\n";
		}
		else {
			cerr << "\nZIP " << READ_ERR_MSG << ZIP_FILE << "\n\n";
		}
		return -1;
	}
	// Open files success. Now get size of files
	readImg.seekg(0, readImg.end),
		readZip.seekg(0, readZip.end);

	const ptrdiff_t
		IMG_SIZE = readImg.tellg(),
		ZIP_SIZE = readZip.tellg();

	if (MAX_PNG >= (IMG_SIZE + MAX_SCRIPT_SIZE)
		&& MAX_PNG >= ZIP_SIZE
		&& MAX_PNG >= (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE)) {

		// File size check success, now read files into vectors.
		readFilesIntoVectorsCheckSpecs(IMG_FILE, ZIP_FILE, IMG_SIZE, ZIP_SIZE);

	}
	else { // File size check failure, display relevant error message and quit program.
		if (IMG_SIZE + MAX_SCRIPT_SIZE > MAX_PNG) {
			cerr << "\nPNG " << SIZE_ERR_MSG << "\n\n";
		}
		else if (ZIP_SIZE > MAX_PNG) {
			cerr << "\nZIP " << SIZE_ERR_MSG << "\n\n";
		}
		else {
			cerr << "\nSize Error: " << (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE) <<
				" bytes is the combined size of your PNG image + ZIP file + Script (400 bytes),\nwhich exceeds Twitter's 5MB size limit by " <<
				(IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE) - MAX_PNG << " bytes. Available ZIP file space is: " <<
				MAX_PNG - (IMG_SIZE + MAX_SCRIPT_SIZE) << " bytes.\n\n";
		}
		return -1;
	}
	return 0;
}

int readFilesIntoVectorsCheckSpecs(const string& IMG_FILE, const string& ZIP_FILE, const ptrdiff_t& IMG_SIZE, const ptrdiff_t& ZIP_SIZE) {

	// Vector "ZipVec" is where we will store the user ZIP file. "ZipVec" will later be inserted into the vector "ImageVec" as the last IDAT chunk. 
	// We will need to update both the CRC, last 4 bytes, currently zero, and the chunk length field, first 4 bytes, also zero, within this vector. 
	vector<unsigned char>ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 };

	// Vector "ImageVec" is where we will store the user PNG image, later combining it with the vectors "ScriptVec" and "ZipVec", before writing it out to file.
	vector<unsigned char>ImageVec(0 / sizeof(unsigned char));

	ifstream readImg(IMG_FILE, ios::binary);
	ifstream readZip(ZIP_FILE, ios::binary);

	// Read PNG image file into vector "ImageVec", begining at index location 0.
	ImageVec.resize(IMG_SIZE / sizeof(unsigned char));
	readImg.read((char*)&ImageVec[0], IMG_SIZE);

	// Read ZIP file into vector "ZipVec", beginning at index location 8, right after the "IDAT" chunk name.
	ZipVec.resize(ZIP_SIZE + ZipVec.size() / sizeof(unsigned char));
	readZip.read((char*)&ZipVec[8], ZIP_SIZE);

	const string
		IMG_HDR(ImageVec.begin(), ImageVec.begin() + PNG_ID.length()),		// Get file header from vector "ImageVec". 
		ZIP_HDR(ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_ID.length()),	// Get file header from vector "ZipVec".
		FORMAT_ERR_MSG = "Format Error: File does not appear to be a valid", 	// Repeated error/info messages.	
		INFO_MSG = "See pdvzip --info for more details.";

	const unsigned int
		MULTIPLIED_DIMS = ((ImageVec[18] << 8 | ImageVec[19]) * (ImageVec[22] << 8 | ImageVec[23])), // Get image dimensions from vector "ImageVec" and multiply Width x Height.
		COLOR_TYPE = ImageVec[25],		// Get image colour type value from vector "ImageVec".
		INZIP_NAME_LENGTH = ZipVec[34], 	// Get length of in-zip media filename from vector "ZipVec".
		INDEXED_COLOR_TYPE = 3,			// PNG Indexed colour type has a set value of 3
		MIN_NAME_LENGTH = 4;			// Minimum filename length of inzip media file.

	if (IMG_HDR == PNG_ID
		&& ZIP_HDR == ZIP_ID
		&& MULTIPLIED_DIMS > MAX_PNG
		&& MAX_MULTIPLIED_DIMS >= MULTIPLIED_DIMS
		&& COLOR_TYPE == INDEXED_COLOR_TYPE
		&& INZIP_NAME_LENGTH >= MIN_NAME_LENGTH) {

		// File requirements check success. Now find and remove unwanted chunks.

		string removeChunk;
		string chunkName[9] = { "bKGD", "cHRM", "gAMA", "iCCP", "pHYs", "sBIT", "sRGB", "sPLT", "tRNS" };

		int chunkCount = sizeof(chunkName) / sizeof(string);

		while (chunkCount--) {
			removeChunk = chunkName[chunkCount];
			const ptrdiff_t REMOVE_ID_INDEX = search(ImageVec.begin(), ImageVec.end(), removeChunk.begin(), removeChunk.end()) - ImageVec.begin() - 4;
			if (REMOVE_ID_INDEX != ImageVec.size() - 4) {
				int chunkLength = (ImageVec[REMOVE_ID_INDEX + 2] << 8) | ImageVec[REMOVE_ID_INDEX + 3];
				ImageVec.erase(ImageVec.begin() + REMOVE_ID_INDEX, ImageVec.begin() + REMOVE_ID_INDEX + (chunkLength + 12));
			}
		}

		// Search for index location of PLTE chunk name within vector "ImageVec". When calculating CRC, we include the 4 byte chunk name field + data field.
		// Next, get PLTE chunk length for CRC. (Chunk name, 4 bytes + data field length).
		const ptrdiff_t
			PLTE_START_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_ID.begin(), PLTE_ID.end()) - ImageVec.begin(),
			PLTE_LENGTH_FIELD_INDEX = PLTE_START_INDEX - 4,
			PLTE_CHUNK_LENGTH = (ImageVec[PLTE_LENGTH_FIELD_INDEX + 2] << 8) | ImageVec[PLTE_LENGTH_FIELD_INDEX + 3];

		// Linux: Some characters that may appear in the PLTE chunk will cause the script to crash out (or other unwanted behaviour). 
		// I've auto tested many images, only one image (so far) has displayed any sign of distortion/corruption after changing the character.
		char badChar[6] = { '(', ')', '\'', '`', '"', '>' };

		int twoCount = 0;

		// Linux: Two (or more) of "&", "|", ";" characters in a row will crash out the script. Alter one of these characters.
		for (int i = static_cast<int>(PLTE_START_INDEX); i < (PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4)); i++) {
			if (ImageVec[i] == '&' || ImageVec[i] == '|' || ImageVec[i] == ';') {
				twoCount++;
				if (twoCount > 1) {
					ImageVec[i] = '*';
				}
			}
			else {
				twoCount = 0;
			}

			 // Linux: '&' followed by '#' , '|' or ';' will crash out the script. Alter these characters if right after '&'.
			if ((ImageVec[i] == '#' && ImageVec[i-1] == '&') || (ImageVec[i] == '|' && ImageVec[i-1] == '&') || (ImageVec[i] == ';' && ImageVec[i-1] == '&')) {
				ImageVec[i] = '*';
			}

			// Linux:  Search for any of the six bad characters and alter them if found.
			for (int j = 0; j < 6; j++) {
				if (ImageVec[i] == badChar[j]) {
					ImageVec[i] = '*';
					break;
				}
			}
		}

		int modCrcVal = 255;
		bool redoCrc;

		do {
			redoCrc = false;

			// Pass these two values to the CRC fuction to get correct PLTE CRC.  https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
			const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_START_INDEX], PLTE_CHUNK_LENGTH + 4);

			// Index location for PLTE CRC field and PLTE mod location.
			ptrdiff_t 
				plteCrcInsertIndex = PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4),
				plteModCrcInsertIndex = plteCrcInsertIndex - 1;

			// Call function to insert the updated CRC value into the 4 byte PLTE CRC chunk field (bits=32) within the vector "ImageVec".
			insertChunkLength(ImageVec, plteCrcInsertIndex, PLTE_CHUNK_CRC, 32, true);

			// Linux: Check to make sure the CRC value does not contain any of the six bad characters.
			// If we find a bad character in the CRC, modify one byte in the PLTE chunk (mod location), then recalculate CRC. 
			// Repeat until no bad characters found.
			for (int i = 0; i < 5; i++) {
				for (int j = 0; j < 6; j++) {
					if (i > 3) break;
					if (ImageVec[plteCrcInsertIndex] == badChar[j])
					{
						ImageVec[plteModCrcInsertIndex] = modCrcVal;
						modCrcVal--;
						redoCrc = true;
						break;
					}
				}
				plteCrcInsertIndex++;
			}
		} 
		while (redoCrc);
		
		// Update IDAT chunk length.
		// "ZipVec" vector's insert index location for IDAT chunk length field.
		int idatZipChunkLengthIndex = 1;

		// Call function to insert new IDAT chunk length value into "ZipVec" vector's index chunk length field. 
		// This IDAT chunk length will never exceed 5MB, so only 3 bytes maximum (bits=24) of the 4 byte length field will be used.
		insertChunkLength(ZipVec, idatZipChunkLengthIndex, ZIP_SIZE, 24, true);

		// Call next function to complete script.
		buildScript(ImageVec, ZipVec, ZIP_FILE);

	}
	else { // File requirements check failure, display relevant error message and quit program.
		if (IMG_HDR != PNG_ID) {
			cerr << "\nPNG " << FORMAT_ERR_MSG << " PNG image.\n\n";
		}
		else if (ZIP_HDR != ZIP_ID) {
			cerr << "\nZIP " << FORMAT_ERR_MSG << " ZIP archive.\n\n";
		}
		else if (MAX_PNG > MULTIPLIED_DIMS || MULTIPLIED_DIMS > MAX_MULTIPLIED_DIMS) {
			cerr << "\nPNG Image Error: Dimensions of PNG image do not meet program requirements. " << INFO_MSG << "\n\n";
		}
		else if (COLOR_TYPE != INDEXED_COLOR_TYPE) {
			cerr << "\nPNG Image Error: Color type of PNG image does not meet program requirements. " << INFO_MSG << "\n\n";
		}
		else {
			cerr << "\nZIP Error: Media filename length within ZIP archive is too short (or file is corrupt)." <<
				"\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n";
		}
		return -1;
	}
	return 0;
}

int buildScript(vector<unsigned char>& ImageVec, vector<unsigned char>& ZipVec, const string& ZIP_FILE) {

	/* Vector "ScriptVec". hIST is a valid PNG chunk type. It does not require a correct CRC value. First four bytes is the chunk length field.
	This vector stores the script commands used for extracting and opening the zipped media file.

	This data chunk is set to an initial (and maximum) size of 400 bytes. The barebones script is only about 120 bytes, but I've added a few hundred more bytes,
	which should be more than enough to account for the later addition of filenames, app+arg strings & other required script commands.
	Real size is updated when script is complete.

	Script supports both Linux & Windows. The completed script will unzip the media file from the PNG image & attempt to open/play
	the in-zip media file using an application command based on a matched file extension, or if no match found, defaulting to the
	operating system making the choice, if possible.

	The completed "hIST" chunk will later be inserted after PLTE chunk and just before the first IDAT chunk of the PNG image (from the vector "ImageVec") */
	vector<unsigned char>ScriptVec{ 0,0,'\x13','\x08','h','I','S','T','\x0d','R','E','M',';','c','l','e','a','r',';','u','n','z','i','p','\x20','-','q','o','\x20',
		'"','$','0','"',';','c','l','e','a','r',';','"','"',';','e','x','i','t',';','\x0d','\x0a','#','&','c','l','s','&','t','a','r','\x20','-','x','f','\x20',
		'"','%','~','n','0','%','~','x','0','"','&','\x20','"','.','\\','"','&','r','e','n','\x20','"','%','~','n','0','%','~','x','0','"','\x20',
		'*','.','p','n','g','&','a','t','t','r','i','b','\x20','+','h','\x20','"','"','&','e','x','i','t','\x0d','\x0a' };

	// Short string array of file extensions for some popular media types along with several default application commands (+args) that support those extensions.
	string extApp[29] = { "aac", "mp3", "mp4", "avi", "asf", "flv", "ebm", "mkv", "peg", "wav", "wmv", "wma","mov", "3gp", "ogg", "pdf", ".py", ".sh", "ps1",
			"vlc --play-and-exit --no-video-title-show ", "evince ", "python3 ", "sh ", "pwsh ", "xdg-open "," &> /dev/null", "powershell","start /b \"\"","pause&" };

	const int
		VLC = 19, DEV_NULL = 25, START_B = 27, PAUSE = 28,	// A few "extApp" string array element names & their index value.
		LINUX_INSERT_INDEX = 40, WINDOWS_INSERT_INDEX = 75,	// "ScriptVec" vector's script insert index location values for Linux & Windows script commands.
		INZIP_NAME_LENGTH_INDEX = 34,				// "ZipVec" vector's index location for in-zip media filename length value.
		INZIP_NAME_INDEX = 38,					// "ZipVec" vector's index location for in-zip media filename.
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	// Get length value of in-zip media filename from vector "ZipVec".
		FILENAME_INSERT_INDEX[3] = { 80, 42, 8 };		// Small int array containing three "ScriptVec" index locations for inserting in-zip media filename.

	// Within the local header and central directory of ZIP file (from vector "ZipVec") change first character of in-zip media filename to '.', so that it is hidden under Linux. 
	ZipVec[INZIP_NAME_INDEX] = '.';
	ZipVec[search(ZipVec.begin(), ZipVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ZipVec.begin() + 46] = '.';

	string
		inzipName(ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH),	// Get in-zip media filename from vector "ZipVec".
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),	// Get file extension from in-zip media filename (last three chars).
		argsLinux, argsWindows;						// Variables to store optional user arguments for Python or PowerShell.

	// Insert in-zip media filename into three index locations of the script within vector "ScriptVec"
	for (int offset : FILENAME_INSERT_INDEX)
		ScriptVec.insert(ScriptVec.end() - offset, inzipName.begin(), inzipName.end());

	int appIndex;	// Variable will store index number for extApp array elements.

	// From a matched file extension "inzipNameExt" we can select which application string (+args) and commands to use in our script, and to open/play the extracted in-zip media file.
	// Once correct app extention has been matched, insert the relevant strings into the script within vector "ScriptVec".
	for (appIndex = 0; appIndex != 24; appIndex++) {
		if (extApp[appIndex] == inzipNameExt) {
			appIndex = appIndex <= 14 ? 19 : appIndex += 5; // After an extension match, any appIndex value between 0 & 14 defaults to 19(vlc), 
			break;						// if over 14, add 5 to its value. 15=20(evince), 16=21(python3), etc.
		}
	}

	// The required ScriptVec.insert commands (case: 21,23) for python3 and pwsh/powershell (21,23,26) are almost identical, 
	// so eliminate repeated code (case:21) by just switching between app number here.
	int appSwitch = appIndex == 23 ? 26 : appIndex;
	string scriptType = (appIndex == 21) ? "Python" : "PowerShell";

	// When inserting string elements from "extApp" into the script within vector "ScriptVec", we need to make sure to keep the script in alignment. 
	switch (appIndex) {
	case 19:// Linux: Insert "vlc" app name + default args. Windows: Insert "start /b". (Use the set default media player for Windows). 
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX, extApp[VLC].begin(), extApp[VLC].end());
		ScriptVec.insert(ScriptVec.begin() + extApp[VLC].length() + LINUX_INSERT_INDEX + INZIP_NAME_LENGTH + 2, extApp[DEV_NULL].begin(), extApp[DEV_NULL].end());
		ScriptVec.insert(ScriptVec.begin() + WINDOWS_INSERT_INDEX + extApp[VLC].length() + INZIP_NAME_LENGTH + extApp[DEV_NULL].length(), extApp[START_B].begin(), extApp[START_B].end());
		break;
	case 21: // Linux: Insert "python3" app name + optional user arguments. Windows: Insert "python3" app name + optional user arguments + "pause" command.
	case 23: // Linux: Insert "pwsh" app name + optional user arguments. Windows: Insert "powershell" app name + optional user arguments + "pause" command.
		cout << "\n" << scriptType << " Script Found...\n\nAdd extra arguments if required.\n\nLinux: ";
		getline(cin, argsLinux);
		cout << "\nWindows: ";
		getline(cin, argsWindows);
		argsLinux.insert(0, "\x20");
		argsWindows.insert(0, "\x20");
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX, extApp[appIndex].begin(), extApp[appIndex].end());
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX + extApp[appIndex].length() + INZIP_NAME_LENGTH + 2, argsLinux.begin(), argsLinux.end());
		ScriptVec.insert(ScriptVec.begin() + argsLinux.length() + INZIP_NAME_LENGTH + WINDOWS_INSERT_INDEX + extApp[appIndex].length(), extApp[appSwitch].begin(), extApp[appSwitch].end());
		ScriptVec.insert(ScriptVec.begin() + argsLinux.length() + (INZIP_NAME_LENGTH * 2) + 5 + WINDOWS_INSERT_INDEX + extApp[appIndex].length() + extApp[appSwitch].length(), argsWindows.begin(), argsWindows.end());
		ScriptVec.insert(ScriptVec.end() - extApp[PAUSE].length(), extApp[PAUSE].begin(), extApp[PAUSE].end());
		break;
	default: // Linux: For ".pdf" insert "evince"(20), for ".sh", insert "sh"(22). Anything else, default to "xdg-open"(24). Windows: Everything here defaults to "start /b"(27).
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX, extApp[appIndex].begin(), extApp[appIndex].end());
		ScriptVec.insert(ScriptVec.begin() + INZIP_NAME_LENGTH + WINDOWS_INSERT_INDEX + extApp[appIndex].length(), extApp[START_B].begin(), extApp[START_B].end());
	}

	// Now that the script within vector "ScriptVec" is complete, update its real chunk length size.
	// The chunk length counts only the data field, we don't count the fields for chunk length (4 bytes), chunk name (4 bytes) or chunk CRC (4 bytes).
	const ptrdiff_t HIST_CHUNK_LENGTH = ScriptVec.size() - 12;

	// Make sure script does not exceed maximum size
	if (HIST_CHUNK_LENGTH > MAX_SCRIPT_SIZE) {
		cerr << "\nScript Error: Script exceeds maximum size of 400 bytes.\n\n";
		return -1;
	}
	else {

		// "ScriptVec" vector's index insert location for chunk length field.
		int histChunkLengthInsertIndex = 2;

		// Call function to insert updated chunk length value into "ScriptVec" vector's index chunk length field. 
		// Due to its small size, hIST chunk length will only use 2 bytes maximum (bits=16) of the 4 byte length field.
		insertChunkLength(ScriptVec, histChunkLengthInsertIndex, HIST_CHUNK_LENGTH, 16, true);

		// Script update complete, call next function to combine vectors.
		combineVectors(ImageVec, ZipVec, ScriptVec, ZIP_FILE);
	}
	return 0;
}

void combineVectors(vector<unsigned char>& ImageVec, vector<unsigned char>& ZipVec, vector<unsigned char>& ScriptVec, const string& ZIP_FILE) {

	// Search for index location of first IDAT chunk (ZIP) within vector "ImageVec". 
	const ptrdiff_t FIRST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), FIRST_IDAT_ID.begin(), FIRST_IDAT_ID.end()) - ImageVec.begin() - 4;

	// "ImageVec" vector's index insert location for vector "ScriptVec" (just before first IDAT chunk and after PLTE chunk) within the PNG image. 
	// This location for the hIST (script) chunk is required for Imgur support. 
	// For Imgur to work correctly, the PLTE chunk MUST be located BEFORE this hIST (script) chunk. 
	const ptrdiff_t HIST_SCRIPT_CHUNK_INSERT_INDEX = FIRST_IDAT_START_INDEX;

	// Insert contents of "ScriptVec" vector into "ImageVec" vector, combining Script with PNG image.
	ImageVec.insert((ImageVec.begin() + HIST_SCRIPT_CHUNK_INSERT_INDEX), ScriptVec.begin(), ScriptVec.end()); // Inserted just before fisrt IDAT chunk.

	// "ImageVec" vector's index insert location for vector "ZipVec", last 12 bytes of the PNG image.
	const ptrdiff_t LAST_IDAT_CHUNK_INSERT_INDEX = ImageVec.size() - 12;

	// Insert contents of "ZipVec" vector into "ImageVec" vector, combining ZIP file with PNG image and Script.
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_CHUNK_INSERT_INDEX), ZipVec.begin(), ZipVec.end());  // Inserted right after the last IDAT chunk's CRC value.

	// Search for index location of last IDAT chunk (ZIP) within vector "ImageVec". When calculating CRC, we include the 4 byte chunk name field.
	const ptrdiff_t LAST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), LAST_IDAT_ID.begin(), LAST_IDAT_ID.end()) - ImageVec.begin();

	// Get last IDAT chunk length. (Chunk name, 4 bytes + data field length).
	// The last 16 bytes that we DO NOT include in the IDAT CRC calculation consists of the 4 byte IDAT CRC field & the remaining 12 bytes of the PNG footer.
	const ptrdiff_t LAST_IDAT_LENGTH = ImageVec.size() - (LAST_IDAT_START_INDEX + 16);

	// Call function to adjust the ZIP file record offsets (within vector "ImageVec") to their new offset values.
	fixZipOffset(ImageVec, LAST_IDAT_START_INDEX);

	// Pass the above two values to the CRC fuction to get correct IDAT CRC.  https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
	const uint32_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_START_INDEX], LAST_IDAT_LENGTH);

	// Get the last IDAT chunk's CRC insert index field location from the vector "ImageVec". 
	ptrdiff_t lastIdatCrcInsertIndex = ImageVec.size() - 16;

	// Call function to insert the updated CRC value into the 4 byte IDAT CRC chunk field (bits=32) within the vector "ImageVec".
	insertChunkLength(ImageVec, lastIdatCrcInsertIndex, LAST_IDAT_CRC, 32, true);

	// Finally, call function to write out the completed PNG polyglot file.
	writeFile(ImageVec, ZIP_FILE);
}

void fixZipOffset(vector<unsigned char>& ImageVec, const ptrdiff_t& LAST_IDAT_INDEX) {

	// Search vector "ImageVec" to find index locations of "Start Central Directory" and "End Central Directory".
	const ptrdiff_t
		START_CENTRAL_DIR_INDEX = search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ImageVec.begin(),
		END_CENTRAL_DIR_INDEX = search(ImageVec.begin() + START_CENTRAL_DIR_INDEX, ImageVec.end(), END_CENTRAL_ID.begin(), END_CENTRAL_ID.end()) - ImageVec.begin();

	ptrdiff_t
		zipFileRecordsIndex = END_CENTRAL_DIR_INDEX + 11,		// Vector index location for ZIP file records value.
		commentLengthInsertIndex = END_CENTRAL_DIR_INDEX + 21,		// Vector index location for ZIP comment length.
		endCentralStartInsertIndex = END_CENTRAL_DIR_INDEX + 19,	// Vector index location for End Central Start offset.
		centralLocalInsertIndex = START_CENTRAL_DIR_INDEX - 1,		// Initialise variable to just before (-1) Start Central index location.
		newZipOffset = LAST_IDAT_INDEX,					// Initialise variable to last IDAT chunk's index location.
		zipFileRecords = (ImageVec[zipFileRecordsIndex] << 8) | ImageVec[zipFileRecordsIndex - 1]; // Get ZIP file records value from vector "ImageVec" index location.

	// Starting from around the last IDAT chunk index location, traverse through vector "ImageVec" searching for ZIP file record offsets and updating them to their new location.
	while (zipFileRecords--) {
		newZipOffset = search(ImageVec.begin() + newZipOffset + 1, ImageVec.end(), ZIP_ID.begin(), ZIP_ID.end()) - ImageVec.begin(),
		centralLocalInsertIndex = 45 + search(ImageVec.begin() + centralLocalInsertIndex, ImageVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ImageVec.begin();
		insertChunkLength(ImageVec, centralLocalInsertIndex, newZipOffset, 32, false);
	}

	// Insert updated "Start Central Directory" offset into End Central Directory's "Start Central Directory" index location within vector "ImageVec".
	insertChunkLength(ImageVec, endCentralStartInsertIndex, START_CENTRAL_DIR_INDEX, 32, false);

	// JAR file support. Get global comment length value from ZIP file within vector "ImageVec" and increase it by 16 bytes to cover end of PNG file.
	// To run a JAR file, you will need to rename the '.png' extention to '.jar'.  
	// Also make sure the '.class' file is not the first to appear in the ZIP file records.
	int commentLength = 16 + (ImageVec[commentLengthInsertIndex] << 8) | ImageVec[commentLengthInsertIndex - 1];

	// Insert the increased comment length value into comment index location within vector "ImageVec".
	insertChunkLength(ImageVec, commentLengthInsertIndex, commentLength, 16, false);
}

int writeFile(vector<unsigned char>& ImageVec, const string& ZIP_FILE) {

	const size_t SLASH_POS = ZIP_FILE.find_last_of("\\/") + 1;

	// Create new filename for output file, e.g. "my_music_file.zip" = "pdv_my_music_file.zip.png"
	string outFile = ZIP_FILE.substr(0, SLASH_POS) + "pdv" + "_" + ZIP_FILE.substr(SLASH_POS, ZIP_FILE.length()) + ".png";

	ofstream writeFinal(outFile, ios::binary);

	if (!writeFinal) {
		cerr << "\nWrite Error: Unable to write to file.\n\n";
		return -1;
	}

	writeFinal.write((char*)&ImageVec[0], ImageVec.size());
	writeFinal.close();

	cout << "\nCreated output file " << "'" << outFile << "' " << ImageVec.size() << " bytes." << "\n\nAll Done!\n\n";

	return 0;
}

void insertChunkLength(vector<unsigned char>& vec, ptrdiff_t lengthInsertIndex, const size_t& CHUNK_LENGTH, int bits, bool isBig) {

	if (isBig)
		while (bits) vec.at(lengthInsertIndex++) = (CHUNK_LENGTH >> (bits -= 8)) & 0xff;
	else
		while (bits) vec.at(lengthInsertIndex--) = (CHUNK_LENGTH >> (bits -= 8)) & 0xff;
}

void displayInfo(void) {

	cout << "\nPNG Data Vehicle for Twitter, ZIP Edition (PDVZIP) v1.1. Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.\n\n" <<
		"PDVZIP enables you to embed a ZIP archive containing a small media file within a tweetable PNG image. " <<
		"\nTwitter will retain the arbitrary data embedded inside the image. Twitter's PNG size limit is 5MB per image." <<
		"\n\nYou can also upload your PNG-ZIP file to *some popular image hosting sites, such as Imgur (imgur.com) and\nPostimage (postimages.org), etc. *Not all image" <<
		" hosting sites are compatible.\n\nOnce the ZIP file has been embedded within your PNG image, its ready to be shared (tweeted) or\n'executed' whenever you want to open/play the media file.\n\n" <<
		"From a Linux terminal: ./pdv_your_image_file.png (Image file requires executable permissions).\n\nWindows: First, rename the '.png' file extension to '.cmd'." <<
		" From a Windows terminal: .\\pdv_your_image_file.cmd (Windows may display a\nsecurity warning when running the cmd file from desktop, clear this by clicking 'More info'" <<
		" then select 'Run anyway').\n\nFor some common video & audio files, Linux requires the 'vlc (VideoLAN)' application, Windows uses the set default media player.\n" <<
		"PDF '.pdf', Linux requires the 'evince' application, Windows uses the set default PDF viewer.\nPython '.py', Linux & Windows use the 'python3'" <<
		" command to run these programs.\nPowerShell '.ps1', Linux uses the 'pwsh' command (if PowerShell installed), Windows uses 'powershell' to run these " <<
		"scripts.\n\nFor any other media type/file extension, Linux & Windows will rely on the operating system's set default application.\n\nPNG Image Requirements: " <<
		"Bit depth, 8-bit or lower (4,2,1) Indexed colour (PNG colour type value: 3).\nImage's multiplied dimensions value must be between 5,242,880 & 5,500,000.\nSuggested Width x Height Dimensions: " <<
		"2900 x 1808 = 5,243,200. Example Two: 2290 x 2290 = 5,244,100, etc.\n\nZIP File Size & Other Information: To work out the maximum ZIP file size, start with " <<
		"Twitter's size limit of 5MB (5,242,880 bytes), \nminus your PNG image size, minus 400 bytes. Example: 5,242,880 - (307,200 + 400) = 4,935,280 bytes available for " <<
		"ZIP file.\nThe less detailed the image, the more space available for the ZIP.\n\nMake sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer." <<
		"\nAlways use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.\n" <<
		"Paint.net application is recommended for easily creating compatible PNG image files.\n\n";
}
