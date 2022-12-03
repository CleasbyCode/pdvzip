//	PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP) v1.1. Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "ScriptVec.hpp"

using namespace std;

// The following code to compute IDAT CRC was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
//___________________________________________________________________________________________________________________

// Table of CRCs of all 8-bit messages.
unsigned long crcTable[256];

// Flag: has the table been computed? Initially false.
int crcTableComputed = 0;

// Make the table for a fast CRC.
void makeCrcTable()
{
	unsigned long c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = n;
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

// Find and remove chunks in the PNG image. 
void eraseChunks(vector<unsigned char>&);

// Search and replace problem characters in the PLTE chunk that are breaking the Linux extraction shell script. Also updates PLTE CRC.
void fixPaletteChunk(vector<unsigned char>&);

// Select & insert the correct elements from "extApp" string array into the vector "ScriptVec", to complete the shell extraction script.
int buildScript(vector<unsigned char>&, vector<unsigned char>&, const string&);

// Combine three vectors into one.
void combineVectors(vector<unsigned char>&, vector<unsigned char>&, vector<unsigned char>&, const string&);

// Update ZIP file record offsets to their new location.
void fixZipOffset(vector<unsigned char>&, const ptrdiff_t&);

// Write out to file complete PNG vector (Image + Script + ZIP). Display error & quit program if write fails.
int writeFile(vector<unsigned char>&, const string&);

// Inserts updated chunk length (or CRC) value into vector index location; Also used to insert adjusted ZIP offset values for "fixZipOffset" function.
void insertChunkLength(vector<unsigned char>&, ptrdiff_t, const size_t&, int, bool);

// Display program information
void displayInfo();

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
	MAX_SCRIPT_SIZE = 750;		// Script size limit, bytes.

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
		ZIP_FILE = argv[2];
	
	ifstream readImg(IMG_FILE, ios::binary), readZip(ZIP_FILE, ios::binary);

	if (!readImg || !readZip) { // Open file failure, display relevant error message and quit program.
		const string READ_ERR_MSG = "Read Error: Unable to open/read file: ";
		string errMsg = !readImg ? "\nPNG " + READ_ERR_MSG + "'" + IMG_FILE + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_FILE + "'\n\n";
		cerr << errMsg;
		return -1;
	}

	// Open files success. Now get size of files
	readImg.seekg(0, readImg.end),
	readZip.seekg(0, readZip.end);

	const ptrdiff_t
		IMG_SIZE = readImg.tellg(),
		ZIP_SIZE = readZip.tellg(),
	    	COMBINED_SIZE = IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE;

	if (MAX_PNG >= (IMG_SIZE + MAX_SCRIPT_SIZE)
		&& MAX_PNG >= ZIP_SIZE
		&& MAX_PNG >= COMBINED_SIZE) {

		// File size check success, now read files into vectors.
		readFilesIntoVectorsCheckSpecs(IMG_FILE, ZIP_FILE, IMG_SIZE, ZIP_SIZE);
	}
	else { // File size check failure, display relevant error message and quit program.
		const ptrdiff_t
			EXCEED_SIZE = (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE) - MAX_PNG,
			AVAILABLE_SIZE = MAX_PNG - (IMG_SIZE + MAX_SCRIPT_SIZE);

		const string
			SIZE_ERR_MSG = "Size Error: File must not exceed Twitter's file size limit of 5MB (5,242,880 bytes).\n\n",
			COMBINED_SIZE_ERR_MSG = "\nSize Error: " + to_string(COMBINED_SIZE) +
			" bytes is the combined size of your PNG image + ZIP file + Script (750 bytes), \nwhich exceeds Twitter's 5MB size limit by "
			+ to_string(EXCEED_SIZE) + " bytes. Available ZIP file size is " + to_string(AVAILABLE_SIZE) + " bytes.\n\n";
			
		string errMsg = (IMG_SIZE + MAX_SCRIPT_SIZE > MAX_PNG) ? "\nPNG " + SIZE_ERR_MSG : (ZIP_SIZE > MAX_PNG ? "\nZIP " + SIZE_ERR_MSG : COMBINED_SIZE_ERR_MSG);
		cerr << errMsg;

		return -1;
	}
	return 0;
}

int readFilesIntoVectorsCheckSpecs(const string& IMG_FILE, const string& ZIP_FILE, const ptrdiff_t& IMG_SIZE, const ptrdiff_t& ZIP_SIZE) {

	// Vector "ZipVec" is where we will store the user ZIP file. "ZipVec" will later be inserted into the vector "ImageVec" as the last IDAT chunk. 
	// We will need to update both the CRC, last 4 bytes, currently zero, and the chunk length field, first 4 bytes, also zero, within this vector. 
	vector<unsigned char>ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 };

	// Vector "ImageVec" is where we will store the user PNG image, later combining it with vectors "ScriptVec" & "ZipVec", before writing it out to file.
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
		ZIP_HDR(ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_ID.length());	// Get file header from vector "ZipVec".

	const unsigned int
		// Get image dimensions from vector "ImageVec" and multiply Width x Height.
		MULTIPLIED_DIMS = ((ImageVec[18] << 8 | ImageVec[19]) * (ImageVec[22] << 8 | ImageVec[23])), 
		COLOR_TYPE = ImageVec[25],		// Get image colour type value from vector "ImageVec".
		INZIP_NAME_LENGTH = ZipVec[34],   	// Get length of in-zip media filename from vector "ZipVec".
		INDEXED_COLOR_TYPE = 3,			// PNG Indexed colour type has a set value of 3
		MIN_NAME_LENGTH = 4;			// Minimum filename length of inzip media file.

	if (IMG_HDR == PNG_ID
		&& ZIP_HDR == ZIP_ID
		&& MULTIPLIED_DIMS > MAX_PNG
		&& MAX_MULTIPLIED_DIMS >= MULTIPLIED_DIMS
		&& COLOR_TYPE == INDEXED_COLOR_TYPE
		&& INZIP_NAME_LENGTH >= MIN_NAME_LENGTH) {

		// File requirements check success. Now find and remove unwanted PNG chunks.
		eraseChunks(ImageVec);

		// Now check PLTE chunk for character issues that break the Linux extraction shell script.
		fixPaletteChunk(ImageVec);

		// Update IDAT chunk length.
		// "ZipVec" vector's insert index location for IDAT chunk length field.
		int idatZipChunkLengthIndex = 1;

		// Call function to insert new IDAT chunk length value into "ZipVec" vector's index chunk length field. 
		// This IDAT chunk length will never exceed 5MB, so only 3 bytes maximum (bits=24) of the 4 byte length field will be used.
		insertChunkLength(ZipVec, idatZipChunkLengthIndex, ZIP_SIZE, 24, true);

		// Call next function to complete shell extraction script.
		buildScript(ImageVec, ZipVec, ZIP_FILE);
	}
	else { // File requirements check failure, display relevant error message and quit program.
		const string
			HEADER_ERR_MSG = "Header Error: File does not appear to be a valid",
			IMAGE_ERR_MSG1 = "\nPNG Image Error: Dimensions of PNG image do not meet program requirements. See 'pdvzip --info' for more details.\n\n",
			IMAGE_ERR_MSG2 = "\nPNG Image Error: Colour type of PNG image does not meet program requirements. See 'pdvzip --info' for more details.\n\n",
			ZIP_ERR_MSG = "\nZIP Error: Media filename length within ZIP archive is too short (or file is corrupt)."
			"\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n";
		
		string errMsg = (IMG_HDR != PNG_ID) ? "\nPNG " + HEADER_ERR_MSG + " PNG image\n\n" : (ZIP_HDR != ZIP_ID) ? "\nZIP " + HEADER_ERR_MSG + " ZIP archive\n\n"
			: (MAX_PNG > MULTIPLIED_DIMS || MULTIPLIED_DIMS > MAX_MULTIPLIED_DIMS) ? IMAGE_ERR_MSG1
			: ((COLOR_TYPE != INDEXED_COLOR_TYPE) ? IMAGE_ERR_MSG2 : ZIP_ERR_MSG);

		cerr << errMsg;

		return -1;
	}
	return 0;
}

void eraseChunks(vector<unsigned char>& ImageVec) {

	// Chunks to remove. 
	string removeChunk[14] = { "bKGD", "cHRM", "gAMA", "hIST", "iCCP", "pHYs", "sBIT", "sRGB", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };
	
	// Get first IDAT chunk index location. Don't remove chunks after this point.
	const ptrdiff_t FIRST_IDAT = search(ImageVec.begin(), ImageVec.end(), FIRST_IDAT_ID.begin(), FIRST_IDAT_ID.end()) - ImageVec.begin() - 4;

	int chunkNum = sizeof(removeChunk) / sizeof(string);

	// Remove chunks. Make sure we check for multiple occurrences of each chunk we remove.
	while (chunkNum--) {
		const ptrdiff_t REMOVE_ID_INDEX = search(ImageVec.begin(), ImageVec.end(), removeChunk[chunkNum].begin(), removeChunk[chunkNum].end()) - ImageVec.begin() - 4;
		if (FIRST_IDAT > REMOVE_ID_INDEX) {
			int chunkLength = (ImageVec[REMOVE_ID_INDEX + 2] << 8) | ImageVec[REMOVE_ID_INDEX + 3];
			ImageVec.erase(ImageVec.begin() + REMOVE_ID_INDEX, ImageVec.begin() + REMOVE_ID_INDEX + (chunkLength + 12));
			chunkNum++;
		}
	}
}

void fixPaletteChunk(vector<unsigned char>& ImageVec) {

	// Linux issue: Some individual characters, sequence or combination of certain characters that may appear in the PLTE chunk will break the script.
	// The main 'for loop' contains a number of fixes for this issue.
	// For Imgur support, the PLTE chunk has to be before the hIST chunk (extraction shell script). The following would be unnecessary if I did not support Imgur.

	// After modify the PLTE chunk, we need to update its CRC value.
	// Search for index location of PLTE chunk name within vector "ImageVec". When calculating CRC, we include the 4 byte chunk name field + data field.
	// Get PLTE chunk length for CRC. (Chunk name, 4 bytes + data field length).
	const ptrdiff_t
		PLTE_START_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_ID.begin(), PLTE_ID.end()) - ImageVec.begin(),
		PLTE_LENGTH_FIELD_INDEX = PLTE_START_INDEX - 4,
		PLTE_CHUNK_LENGTH = (ImageVec[PLTE_LENGTH_FIELD_INDEX + 2] << 8) | ImageVec[PLTE_LENGTH_FIELD_INDEX + 3];
		
	char 
		badChar[7] = { '(', ')', '\'', '`', '"', '>', ';' },  // These individual characters in the PLTE chunk will break the shell extraction script. 
		altChar[7] = { '*', '&', '=', '}', 'a', '?', ':' };   // Replace them with these characters. 

	int twoCount = 0;

	for (ptrdiff_t i = PLTE_START_INDEX; i < (PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4)); i++) {

		// Replace each of the seven problem characters if found within PLTE chunk.
		ImageVec[i] = (ImageVec[i] == badChar[0]) ? altChar[1] 
				: (ImageVec[i] == badChar[1]) ? altChar[1] : (ImageVec[i] == badChar[2]) ? altChar[1]
				: (ImageVec[i] == badChar[3]) ? altChar[4] : (ImageVec[i] == badChar[4]) ? altChar[0] 
				: (ImageVec[i] == badChar[5]) ? altChar[5] : ((ImageVec[i] == badChar[6]) ? altChar[6] : ImageVec[i]);

		// Character combinations that will break the Linux extraction script. Replace relevant character to prevent script failure. 
		if ((ImageVec[i] == '&' && ImageVec[i + 1] == '!')
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
			if (ImageVec[i] == '\x0a') {
				ImageVec[i + 1] = altChar[0];
			}
			else {
				ImageVec[i] = ImageVec[i] == '<' ? altChar[2] : altChar[0];
			}
		}

		// Two (or more) characters of "&" or "|" in a row will break the extraction script. Replace one of these characters.
		if (ImageVec[i] == '&' || ImageVec[i] == '|') {
			twoCount++;
			if (twoCount > 1) {
				ImageVec[i] = (ImageVec[i] == '&' ? altChar[0] : altChar[3]);
			}
		}
		else {
			twoCount = 0;
		}

		// Character '<' followed by a number (or a sequence of numbers) and ending with the same character '<' will break the script. 
		// Replace character '<' at the beginning of the sequence (of up to 11 digits).
		int j = 1, k = 2;
		if (ImageVec[i] == '<') {
			while (j < 12) {
				if (ImageVec[i + j] > 47 && ImageVec[i + j] < 58 && ImageVec[i + k] == '<')
				{
					ImageVec[i] = altChar[2];
					j = 12;
				}
				j++, k++;
			}
		}
	}

	int modCrcVal = 255;
	bool redoCrc;

	do {
		redoCrc = false;

		// Pass these two values (PLTE_START_INDEX & PLTE_CHUNK_LENGTH + 4) to the CRC function to get correct PLTE CRC.
		const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_START_INDEX], PLTE_CHUNK_LENGTH + 4);

		// Get index location for PLTE CRC field and PLTE mod location.
		ptrdiff_t
			plteCrcInsertIndex = PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4),
			plteModCrcInsertIndex = plteCrcInsertIndex - 1;

		// Call function to insert the updated CRC value into the 4 byte PLTE CRC chunk field (bits=32) within the vector "ImageVec".
		insertChunkLength(ImageVec, plteCrcInsertIndex, PLTE_CHUNK_CRC, 32, true);

		// Linux: Check to make sure the CRC value does not contain any of the seven problem characters.
		// If we find a problem character in the CRC value, modify one byte in the PLTE chunk (mod location), then recalculate CRC. 
		// Repeat until no problem characters found.
		for (int i = 0; i < 5; i++) {
			for (int j = 0; j < 7; j++) {
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
	} while (redoCrc);
}

int buildScript(vector<unsigned char>& ImageVec, vector<unsigned char>& ZipVec, const string& ZIP_FILE) {

	/* Vector "ScriptVec" (See seperate file "ScriptVec.hpp").

	hIST is a valid PNG chunk type. It does not require a correct CRC value. First four bytes is the chunk length field.
	This vector stores the shell/batch script commands used for extracting and opening the zipped media file.

	This data chunk is set to an initial (and maximum) size of 750 bytes. 
	
	The barebones script is only about 300 bytes, but with the limit being 750 bytes, that should be more than enough to account 
	for the later addition of filenames, app+arg strings & other required script commands. Real size is updated when script is complete.

	Script supports both Linux & Windows. The completed script will unzip the media file from the PNG image & attempt to open/play
	the in-zip media file using an application command based on a matched file extension, or if no match found, defaulting to the
	operating system making the choice, if possible. The media file needs to be compatible with the operating system you are running it on.

	The completed "hIST" chunk will be inserted after the PLTE chunk of the PNG image (which is stored in the vector "ImageVec") */

	// String vector of file extensions for some popular media types along with several default application commands (+ args) that support those extensions.
	vector<string> extApp{ "aac","mp3","mp4","avi","asf","flv","ebm","mkv","peg","wav","wmv","wma","mov","3gp","ogg","pdf",".py","ps1","exe",
				".sh","vlc --play-and-exit --no-video-title-show ","evince ","python3 ","pwsh ","./","xdg-open ","powershell;Invoke-Item ",
				" &> /dev/null","start /b \"\"","pause&","powershell","chmod +x ",";" };
			
	const int
		INZIP_NAME_LENGTH_INDEX = 34,				// "ZipVec" vector's index location for in-zip media filename length value.
		INZIP_NAME_INDEX = 38,					// "ZipVec" vector's index location for in-zip media filename.
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	// Get length value of in-zip media filename from vector "ZipVec".
		
		// extApp vector index element values. Some extApp elements are added later (push_back), so don't currently appear in the above extApp vector.
		VIDEO_AUDIO = 20, PDF = 21, PYTHON = 22,
		LINUX_PWSH = 23, EXECUTABLE = 24, BASH_XDG_OPEN = 25, 
		FOLDER_INVOKE_ITEM = 26, WIN_POWERSHELL = 30, INZIP_FILENAME = 33,
		LINUX_ARGS = 34, WINDOWS_ARGS = 35, MOD_INZIP_FILENAME = 36;

	string
		inzipName(ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH), // Get in-zip media filename from vector "ZipVec".
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),	// Get file extension from in-zip media filename.
		argsLinux, argsWindows;																					// Optional user arguments string variables.

	size_t findExtension = inzipName.find_last_of('.');

	// Copy inzipName to extApp vector (33)
	extApp.push_back(inzipName);

	/* When inserting string elements from "extApp" into the script within vector "ScriptVec", we are inserting items in the order of last to first.	

	[0](259) Windows: index insert location for "pause&" extApp 29, which is only used when file is python, powershell or exe.
	[1](237) Windows: index insert location for optional command-line args string, (added later into extApp, 35). Used with .py, .ps1, .sh and .exe file types.
	[2](236) Windows: index insert location for inzip media filename (added later into extApp, 33). 
		 File name used with "start /b" extApp 28, "python3" 22, "powershell" 30 & "powershell;Invoke-Item" 26.
			
	[3](234) Windows: index insert location for "start /b" extApp 28. Location also used for "python3" 22, "powershell" 30 & "powershell;Invoke-Item" 26.
	[4](116) Linux: index insert location for "Dev Null" extApp 27, used with vlc. 
		 Location also used for optional Linux command-line args string, (added into extApp, 34).
	[5](115) Linux: index insert location for inzip media filename (added later into extApp, 33).
	[6](114) Linux: index insert location for "vlc" extApp 20. 
		 Location also used for "evince" 21, "python3" 22, "pwsh" 23, "./" 24, "xdg-open" 25, "chmod +x" 31 and ";" 32. */

	// Array extAppInsertSequence contains sequences of ScriptVec index insert location values (high numbers)
	// and the corresponding extApp element index values (low numbers). For example, in the first sequence, 
	// insert location index 236 (insertIndex) corresponds with (extAppElement) extApp element 33 (inzip media filename).

	int extAppInsertSequence[52] = { 
				236,234,116,115,114, 33,28,27,33,20, 	// 1st sequence used for case: VIDEO_AUDIO. 
				236,234,115,114, 33,28,33,21,		// 2nd sequence used for cases: PDF, FOLDER_INVOKE_ITEM, DEFAULT.
				259,237,236,234,116,115,114, 29,35,33,22,34,33,22, // 3rd sequence used for cases: PYTHON, LINUX_PWSH & WIN_POWERSHELL.
				259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31 }, // 4th sequence used for cases: EXECUTABLE & BASH_XDG_OPEN.

	appIndex = 0, insertIndex = -1, extAppElement = 0, sequenceLimit = 0;

	// From a matched file extension "inzipNameExt" we can select which application string (+args) and commands to use in our script, 
	// and to open/play the extracted in-zip media file. Once correct app extention has been matched, insert the relevant strings 
	// into the script within vector "ScriptVec".
	for (appIndex = 0; appIndex != 26; appIndex++) {
		if (extApp[appIndex] == inzipNameExt) {
			appIndex = appIndex <= 14 ? 20 : appIndex += 6; // After an extension match, any appIndex value between 0 & 14 defaults to extApp 20 (vlc), 
			break;					// if over 14, add 6 to its value. 15 + 6 = extApp 21 (evince), 16 + 6 = 22 extApp (python3), etc.
		}
	}

	// If no file extension detected, check if "inzipName" points to a folder (/) or else assume file is a Linux executable.
	if (findExtension == 0 || findExtension > inzipName.length()) {
		appIndex = ZipVec[INZIP_NAME_INDEX + INZIP_NAME_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	// Provide the option to give command-line arguments for .py, .ps1, .sh  and .exe file types.
	if (appIndex > 21 && appIndex < 26) {
		cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		getline(cin, argsLinux);
		cout << "\nWindows: ";
		getline(cin, argsWindows);
		argsLinux.insert(0, "\x20"), argsWindows.insert(0, "\x20");
		extApp.push_back(argsLinux), extApp.push_back(argsWindows); // extApp (34), (35).
	}

	switch (appIndex) {
	case VIDEO_AUDIO:		// vlc for Linux and start /b (default player) for Windows.
		extAppElement = 5;	// Start extAppInsertSequence from index positions [0] (insertIndex) and [5] (extAappElement). 
					// [0] = 236 ScriptVec index insert location, [5] = extApp index 33, vector element inzip media filename.
		break;
	case PDF:					// Evince for Linux and start /b (default viewer) for Windows.
		insertIndex = 9, extAppElement = 14;	// Start extAppInsertSequence from index positions [9] (insertIndex) and [14] (extAppElement).
		break;
	case PYTHON:			// python3 for Linux & Windows.
	case LINUX_PWSH:		// pwsh for Linux, powershell for Windows.
		insertIndex = 17, extAppElement = 25;	// Start extAppInsertSequence from index positions [17] (insertIndex) and [25] (extAppElement).
							// [17] = 259 ScriptVec index insert location, [25] = extApp index 29, vector element "pause&".

		if (appIndex == LINUX_PWSH) {		// Case LINUX_PWSH is similar to case PYTHON, switch index elements to PowerShell (extApp 23 & 30).
			inzipName.insert(0, ".\\"); 	//  ".\"  required for Windows PowerShell command:  powershell ".\filename.ps1".
			extApp.push_back(inzipName);
			extAppInsertSequence[31] = LINUX_PWSH,
			extAppInsertSequence[28] = WIN_POWERSHELL;
			extAppInsertSequence[27] = MOD_INZIP_FILENAME; // Modified inzipName (".\filename) for Windows powershell command. 
		}
		break;
	case EXECUTABLE:
		insertIndex = 31, extAppElement = 42;
		break;
	case BASH_XDG_OPEN:
		insertIndex = 32, extAppElement = 43;
		break;
	case FOLDER_INVOKE_ITEM:
	
		// If inzipName points to a folder and not a file, just open the folder displaying unzipped file(s), 
		// this is done with "xdg-open" <folder1/folder2/> (Linux) and "powershell;Invoke-Item" <folder1/folder2> Windows.
		
		insertIndex = 9, extAppElement = 14;
		extAppInsertSequence[15] = FOLDER_INVOKE_ITEM, extAppInsertSequence[17] = BASH_XDG_OPEN; // Case is similar to case PDF, alter two element numbers.
		break;
	default:	// All other file types drop here. Linux xdg-open and Windows start /b. (Let operating system choose default program for file type).
		insertIndex = 9, extAppElement = 14;
		extAppInsertSequence[17] = BASH_XDG_OPEN;  // Default case similar to case PDF, we just need to alter one element number.
	}

	sequenceLimit = appIndex == BASH_XDG_OPEN ? extAppElement - 1 : extAppElement;

	while (++insertIndex < sequenceLimit)
		ScriptVec.insert(ScriptVec.begin() + extAppInsertSequence[insertIndex], extApp[extAppInsertSequence[extAppElement++]].begin(), extApp[extAppInsertSequence[extAppElement]].end());
	
	bool redoChunkLength;

	do {

		redoChunkLength = false;

		// Script within vector "ScriptVec" is complete, now update its chunk length size.
		// The chunk length counts only the data field, we don't count the fields for chunk length (4 bytes), chunk name (4 bytes) or chunk CRC (4 bytes).	
		const ptrdiff_t HIST_CHUNK_LENGTH = ScriptVec.size() - 12;

		// Make sure script does not exceed maximum size
		if (HIST_CHUNK_LENGTH > MAX_SCRIPT_SIZE) {
			cerr << "\nScript Error: Script exceeds maximum size of 750 bytes.\n\n";
			return -1;
		}

		else {

			// "ScriptVec" vector's index insert location for chunk length field.
			int histChunkLengthInsertIndex = 2;

			// Call function to insert updated chunk length value into "ScriptVec" vector's index chunk length field. 
			// Due to its small size, hIST chunk length will only use 2 bytes maximum (bits=16) of the 4 byte length field.
			insertChunkLength(ScriptVec, histChunkLengthInsertIndex, HIST_CHUNK_LENGTH, 16, true);

			// Check the first byte of the hIST chunk length field to make sure it does not contain a character 
			// that will break the Linux extraction shell script.
			if (ScriptVec[3] == '(' || ScriptVec[3] == ')' 
				|| ScriptVec[3] == '\'' || ScriptVec[3] == '`' 
				|| ScriptVec[3] == '"' || ScriptVec[3] == '>' 
				|| ScriptVec[3] == ';') {
				
				// Found a bad character, so insert a byte at the end of ScriptVec to increase chunk length, 
				// then update chunk length field again. Recheck for bad characters, repeat byte insertion if required until
				// no bad character is generated by the chunk length update.
				ScriptVec.insert(ScriptVec.begin() + (HIST_CHUNK_LENGTH + 10), '.'); 

				redoChunkLength = true;
			}
		}
	} while (redoChunkLength);

	// Script update complete, call next function to combine vectors.
	combineVectors(ImageVec, ZipVec, ScriptVec, ZIP_FILE);
	return 0;
}

void combineVectors(vector<unsigned char>& ImageVec, vector<unsigned char>& ZipVec, vector<unsigned char>& ScriptVec, const string& ZIP_FILE) {

	// Search for index location of first IDAT chunk within vector "ImageVec". 
	const ptrdiff_t FIRST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), FIRST_IDAT_ID.begin(), FIRST_IDAT_ID.end()) - ImageVec.begin() - 4;

	// "ImageVec" vector's index insert location for vector "ScriptVec" (just before first IDAT chunk and after PLTE chunk) within the PNG image. 
	// This location for the hIST (script) chunk is required for Imgur support. 
	// For Imgur to work correctly, the PLTE chunk must be located BEFORE this hIST (shell script) chunk. 
	const ptrdiff_t HIST_SCRIPT_CHUNK_INSERT_INDEX = FIRST_IDAT_START_INDEX;

	// Insert contents of "ScriptVec" vector into "ImageVec" vector, combining shell script with PNG image.
	ImageVec.insert((ImageVec.begin() + HIST_SCRIPT_CHUNK_INSERT_INDEX), ScriptVec.begin(), ScriptVec.end()); // Inserted right after the PLTE chunk.

	// "ImageVec" vector's index insert location for vector "ZipVec", last 12 bytes of the PNG image.
	const ptrdiff_t LAST_IDAT_CHUNK_INSERT_INDEX = ImageVec.size() - 12;

	// Insert contents of "ZipVec" vector into "ImageVec" vector, combining ZIP file with PNG image and shell script.
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
		zipFileRecordsIndex = END_CENTRAL_DIR_INDEX + 11,	  // Vector index location for ZIP file records value.
		commentLengthInsertIndex = END_CENTRAL_DIR_INDEX + 21,	  // Vector index location for ZIP comment length.
		endCentralStartInsertIndex = END_CENTRAL_DIR_INDEX + 19,  // Vector index location for End Central Start offset.
		centralLocalInsertIndex = START_CENTRAL_DIR_INDEX - 1,	  // Initialise variable to just before (-1) Start Central index location.
		newZipOffset = LAST_IDAT_INDEX,				  // Initialise variable to last IDAT chunk's index location.
		
		// Get ZIP file records value from vector "ImageVec" index location.
		zipFileRecords = (ImageVec[zipFileRecordsIndex] << 8) | ImageVec[zipFileRecordsIndex - 1]; 

	// Starting from around the last IDAT chunk index location, traverse through vector "ImageVec",
	// searching for ZIP file record offsets and updating them to their new location.
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

void displayInfo() {

	cout << R"(
PNG Data Vehicle for Twitter, ZIP Edition(PDVZIP) v1.1. Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.

PDVZIP enables you to embed a ZIP archive containing a small media file within a tweetable PNG image.
Twitter will retain the arbitrary data embedded inside the image. Twitter's PNG size limit is 5MB per image.

Once the ZIP file has been embedded within your PNG image, its ready to be shared (tweeted) or 'executed' 
whenever you want to open/play the media file. You can also upload and share your PNG file to *some popular
image hosting sites, such as Flickr, ImgBB, Imgur, ImgPile, ImageShack, PostImage, etc. 
*Not all image hosting sites are compatible, e.g. ImgBox, Reddit.

From a Linux terminal: ./pdv_your_image_file.png (Image file requires executable permissions).
From a Windows terminal: First, rename the '.png' file extension to '.cmd', then .\pdv_your_image_file.cmd 

For some common video & audio files, Linux requires the 'vlc (VideoLAN)' program, Windows uses the default media player.
PDF '.pdf', Linux requires the 'evince' program, Windows uses the set default PDF viewer.
Python '.py', Linux & Windows use the 'python3' command to run these programs.
PowerShell '.ps1', Linux uses the 'pwsh' command (if PowerShell installed), Windows uses 'powershell' to run these scripts.

For any other media type/file extension, Linux & Windows will rely on the operating system's set default application.

PNG Image Requirements

Bit depth, 8-bit or lower (4,2,1) Indexed colour (PNG colour type value: 3).

Image's multiplied dimensions value must be between 5,242,880 & 5,500,000.
Suggested Width x Height Dimensions: 2900 x 1808 = 5,243,200. Example Two: 2290 x 2290 = 5,244,100, etc.

ZIP File Size & Other Information

To work out the maximum ZIP file size, start with Twitter's size limit of 5MB (5,242,880 bytes), minus PNG image size, 
minus 750 bytes (extraction script). Example: 5,242,880 - (307,200 + 750) = 4,934,930 bytes available for ZIP file. 

The less detailed the image, the more space available for the ZIP.

Make sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
Use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.
A file without an extension will be treated as a Linux executable.
Paint.net application is recommended for easily creating compatible PNG image files.

)";
}
