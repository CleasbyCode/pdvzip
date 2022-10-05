//	PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP) v1.1. Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

// The following code to compute IDAT CRC was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
//___________________________________________________________________________________________________________________

unsigned long crcTable[256];

int crcTableComputed = 0;

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
unsigned long crc(unsigned char* buf, const size_t& len)
{
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}
//  End of IDAT CRC Function 
//__________________________________________________________________________________________________________________

int openFilesCheckSize(char* []);
int readFilesIntoVectorsCheckSpecs(const string&, const string&, const ptrdiff_t&, const ptrdiff_t&);
void eraseChunks(vector<unsigned char>&);
void fixPaletteChunk(vector<unsigned char>&);
int buildScript(vector<unsigned char>&, vector<unsigned char>&, const string&);
void combineVectors(vector<unsigned char>&, vector<unsigned char>&, vector<unsigned char>&, const string&);
void fixZipOffset(vector<unsigned char>&, const ptrdiff_t&);
int writeFile(vector<unsigned char>&, const string&);
void insertChunkLength(vector<unsigned char>&, ptrdiff_t, const size_t&, int, bool);
void displayInfo(void);

const string
	PNG_ID = "\x89PNG",
	ZIP_ID = "PK\x03\x04",
	PLTE_ID = "PLTE",
	START_CENTRAL_ID = "PK\x01\x02",
	END_CENTRAL_ID = "PK\x05\x06",
	FIRST_IDAT_ID = "IDAT",
	LAST_IDAT_ID = "IDATPK";

const unsigned int
	MAX_MULTIPLIED_DIMS = 5500000,	
	MAX_PNG = 5242880,		
	MAX_SCRIPT_SIZE = 400;		

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
		READ_ERR_MSG = "Read Error: Unable to open/read file: ",
		SIZE_ERR_MSG = "Size Error: File must not exceed Twitter's file size limit of 5MB (5,242,880 bytes).\n\n";

	unsigned int
		combinedSize = 0,
		exceedSize = 0,
		availableSize = 0;
		
	ifstream readImg(IMG_FILE, ios::binary);
	ifstream readZip(ZIP_FILE, ios::binary);

	if (!readImg || !readZip) {
		string errMsg = !readImg ? "\nPNG " + READ_ERR_MSG + "'" + IMG_FILE + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_FILE + "'\n\n";
		cerr << errMsg;
		return -1;
	}
	
	readImg.seekg(0, readImg.end),
	readZip.seekg(0, readZip.end);

	const ptrdiff_t
		IMG_SIZE = readImg.tellg(),
		ZIP_SIZE = readZip.tellg();

	combinedSize = static_cast<unsigned int>(IMG_SIZE) + static_cast<unsigned int>(ZIP_SIZE) + MAX_SCRIPT_SIZE;
	exceedSize = (static_cast<unsigned int>(IMG_SIZE) + static_cast<unsigned int>(ZIP_SIZE) + MAX_SCRIPT_SIZE) - MAX_PNG,
	availableSize = MAX_PNG - (static_cast<unsigned int>(IMG_SIZE) + MAX_SCRIPT_SIZE);

	const string COMBINED_SIZE_ERR = "\nSize Error: " + to_string(combinedSize) +
		" bytes is the combined size of your PNG image + ZIP file + Script (400 bytes), \nwhich exceeds Twitter's 5MB size limit by "
		+ to_string(exceedSize) + " bytes. Available ZIP file space is: " + to_string(availableSize) + " bytes.\n\n";

	if (MAX_PNG >= (IMG_SIZE + MAX_SCRIPT_SIZE)
		&& MAX_PNG >= ZIP_SIZE
		&& MAX_PNG >= combinedSize) {

		readFilesIntoVectorsCheckSpecs(IMG_FILE, ZIP_FILE, IMG_SIZE, ZIP_SIZE);
	}
	else { 
	
		string errMsg = (IMG_SIZE + MAX_SCRIPT_SIZE > MAX_PNG) ? "\nPNG " + SIZE_ERR_MSG : (ZIP_SIZE > MAX_PNG ? "\nZIP " + SIZE_ERR_MSG : COMBINED_SIZE_ERR);
		cerr << errMsg;

		return -1;
	}
	return 0;
}

int readFilesIntoVectorsCheckSpecs(const string& IMG_FILE, const string& ZIP_FILE, const ptrdiff_t& IMG_SIZE, const ptrdiff_t& ZIP_SIZE) {

	vector<unsigned char>ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 };
	vector<unsigned char>ImageVec(0 / sizeof(unsigned char));

	ifstream readImg(IMG_FILE, ios::binary);
	ifstream readZip(ZIP_FILE, ios::binary);
	
	ImageVec.resize(IMG_SIZE / sizeof(unsigned char));
	readImg.read((char*)&ImageVec[0], IMG_SIZE);

	ZipVec.resize(ZIP_SIZE + ZipVec.size() / sizeof(unsigned char));
	readZip.read((char*)&ZipVec[8], ZIP_SIZE);

	const string
		IMG_HDR(ImageVec.begin(), ImageVec.begin() + PNG_ID.length()),		
		ZIP_HDR(ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_ID.length()),	
		HEADER_ERR_MSG = "\nHeader Error : File does not appear to be a valid",
		IMAGE_ERR_MSG1 = "\nPNG Image Error: Dimensions of PNG image do not meet program requirements. See 'pdvzip --info' for more details.\n\n",
		IMAGE_ERR_MSG2 = "\nPNG Image Error: Colour type of PNG image does not meet program requirements. See 'pdvzip --info' for more details.\n\n",
		ZIP_ERR_MSG = "\nZIP Error: Media filename length within ZIP archive is too short (or file is corrupt)." 
					  "\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n";
					  
	const unsigned int
		MULTIPLIED_DIMS = ((ImageVec[18] << 8 | ImageVec[19]) * (ImageVec[22] << 8 | ImageVec[23])), 
		COLOR_TYPE = ImageVec[25],	
		INZIP_NAME_LENGTH = ZipVec[34], 
		INDEXED_COLOR_TYPE = 3,			
		MIN_NAME_LENGTH = 4;			

	if (IMG_HDR == PNG_ID
		&& ZIP_HDR == ZIP_ID
		&& MULTIPLIED_DIMS > MAX_PNG
		&& MAX_MULTIPLIED_DIMS >= MULTIPLIED_DIMS
		&& COLOR_TYPE == INDEXED_COLOR_TYPE
		&& INZIP_NAME_LENGTH >= MIN_NAME_LENGTH) {

		eraseChunks(ImageVec);

		fixPaletteChunk(ImageVec);

		int idatZipChunkLengthIndex = 1;
		
		insertChunkLength(ZipVec, idatZipChunkLengthIndex, ZIP_SIZE, 24, true);

		buildScript(ImageVec, ZipVec, ZIP_FILE);
	}
	else { 
		
		string errMsg = (IMG_HDR != PNG_ID) ? "PNG " + HEADER_ERR_MSG + " PNG image\n\n" 
				: (ZIP_HDR != ZIP_ID) ? "ZIP " + HEADER_ERR_MSG + "ZIP archive\n\n"
				: (MAX_PNG > MULTIPLIED_DIMS || MULTIPLIED_DIMS > MAX_MULTIPLIED_DIMS) ? IMAGE_ERR_MSG1
				: ((COLOR_TYPE != INDEXED_COLOR_TYPE) ? IMAGE_ERR_MSG2 : ZIP_ERR_MSG);

		cerr << errMsg;

		return -1;
	}
	return 0;
}

void eraseChunks(vector<unsigned char>& ImageVec) {

	string removeChunk[14] = { "bKGD", "cHRM", "gAMA", "hIST", "iCCP", "pHYs", "sBIT", "sRGB", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };
	
	int chunkNum = sizeof(removeChunk) / sizeof(string);

	// Remove chunks. Make sure we check for multiple occurrences of each chunk we remove.
	while (chunkNum--) {
		const ptrdiff_t REMOVE_ID_INDEX = search(ImageVec.begin(), ImageVec.end(), removeChunk[chunkNum].begin(), removeChunk[chunkNum].end()) - ImageVec.begin() - 4;
		if (REMOVE_ID_INDEX != ImageVec.size() - 4) {
			int chunkLength = (ImageVec[REMOVE_ID_INDEX + 2] << 8) | ImageVec[REMOVE_ID_INDEX + 3];
			ImageVec.erase(ImageVec.begin() + REMOVE_ID_INDEX, ImageVec.begin() + REMOVE_ID_INDEX + (chunkLength + 12));
			chunkNum++;
		}
	}
}

void fixPaletteChunk(vector<unsigned char>& ImageVec) {

	const ptrdiff_t
		PLTE_START_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_ID.begin(), PLTE_ID.end()) - ImageVec.begin(),
		PLTE_LENGTH_FIELD_INDEX = PLTE_START_INDEX - 4,
		PLTE_CHUNK_LENGTH = (ImageVec[PLTE_LENGTH_FIELD_INDEX + 2] << 8) | ImageVec[PLTE_LENGTH_FIELD_INDEX + 3];

	char 
		badChar[7] = { '(', ')', '\'', '`', '"', '>', ';' }, 
		altChar[7] = { '*', '&', '=', '}', 'a', '?', ':' };   

	int twoCount = 0;

	for (int i = static_cast<int>(PLTE_START_INDEX); i < (PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4)); i++) {
	
		ImageVec[i] = (ImageVec[i] == badChar[0]) ? altChar[1] 
				: (ImageVec[i] == badChar[1]) ? altChar[1] : (ImageVec[i] == badChar[2]) ? altChar[1]
				: (ImageVec[i] == badChar[3]) ? altChar[4] : (ImageVec[i] == badChar[5]) ? altChar[5] 
				: ((ImageVec[i] == badChar[6]) ? altChar[6] : ImageVec[i]);
				
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
		
		if (ImageVec[i] == '&' || ImageVec[i] == '|') {
			twoCount++;
			if (twoCount > 1) {
				ImageVec[i] = (ImageVec[i] == '&' ? altChar[0] : altChar[3]);
			}
		}
		else {
			twoCount = 0;
		}

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
		
		const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_START_INDEX], PLTE_CHUNK_LENGTH + 4);
		
		ptrdiff_t
			plteCrcInsertIndex = PLTE_START_INDEX + (PLTE_CHUNK_LENGTH + 4),
			plteModCrcInsertIndex = plteCrcInsertIndex - 1;

		insertChunkLength(ImageVec, plteCrcInsertIndex, PLTE_CHUNK_CRC, 32, true);

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

	vector<unsigned char>ScriptVec{ 0,0,'\x13','\x08','h','I','S','T','\x0d','R','E','M',';','c','l','e','a','r',';','u','n','z','i','p','\x20','-','q','o','\x20',
		'"','$','0','"',';','c','l','e','a','r',';','"','"',';','e','x','i','t',';','\x0d','\x0a','#','&','c','l','s','&','t','a','r','\x20','-','x','f','\x20',
		'"','%','~','n','0','%','~','x','0','"','&','\x20','"','.','\\','"','&','r','e','n','\x20','"','%','~','n','0','%','~','x','0','"','\x20',
		'*','.','p','n','g','&','a','t','t','r','i','b','\x20','+','h','\x20','"','"','&','e','x','i','t','\x0d','\x0a' };

	string extApp[29] = { "aac", "mp3", "mp4", "avi", "asf", "flv", "ebm", "mkv", "peg", "wav", "wmv", "wma","mov", "3gp", "ogg", "pdf", ".py", ".sh", "ps1",
			"vlc --play-and-exit --no-video-title-show ", "evince ", "python3 ", "sh ", "pwsh ", "xdg-open "," &> /dev/null", "powershell","start /b \"\"","pause&" };
	
	const int
		VLC = 19, DEV_NULL = 25, START_B = 27, PAUSE = 28,	
		LINUX_INSERT_INDEX = 40, WINDOWS_INSERT_INDEX = 75,	
		INZIP_NAME_LENGTH_INDEX = 34,				
		INZIP_NAME_INDEX = 38,					
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	
		FILENAME_INSERT_INDEX[3] = { 80, 42, 8 };		

	ZipVec[INZIP_NAME_INDEX] = '.';
	ZipVec[search(ZipVec.begin(), ZipVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ZipVec.begin() + 46] = '.';

	string
		inzipName(ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH),	
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),	
		argsLinux, argsWindows;						

	for (int offset : FILENAME_INSERT_INDEX)
		ScriptVec.insert(ScriptVec.end() - offset, inzipName.begin(), inzipName.end());

	int appIndex;

	for (appIndex = 0; appIndex != 24; appIndex++) {
		if (extApp[appIndex] == inzipNameExt) {
			appIndex = appIndex <= 14 ? 19 : appIndex += 5; 
			break;						
		}
	}

	int appSwitch = appIndex == 23 ? 26 : appIndex;
	string scriptType = (appIndex == 21) ? "Python" : "PowerShell";

	switch (appIndex) {
	case 19:
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX, extApp[VLC].begin(), extApp[VLC].end());
		ScriptVec.insert(ScriptVec.begin() + extApp[VLC].length() + LINUX_INSERT_INDEX + INZIP_NAME_LENGTH + 2, extApp[DEV_NULL].begin(), extApp[DEV_NULL].end());
		ScriptVec.insert(ScriptVec.begin() + WINDOWS_INSERT_INDEX + extApp[VLC].length() + INZIP_NAME_LENGTH + extApp[DEV_NULL].length(), extApp[START_B].begin(), extApp[START_B].end());
		break;
	case 21: 
	case 23: 
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
	default: 
		ScriptVec.insert(ScriptVec.begin() + LINUX_INSERT_INDEX, extApp[appIndex].begin(), extApp[appIndex].end());
		ScriptVec.insert(ScriptVec.begin() + INZIP_NAME_LENGTH + WINDOWS_INSERT_INDEX + extApp[appIndex].length(), extApp[START_B].begin(), extApp[START_B].end());
	}

	const ptrdiff_t HIST_CHUNK_LENGTH = ScriptVec.size() - 12;

	if (HIST_CHUNK_LENGTH > MAX_SCRIPT_SIZE) {
		cerr << "\nScript Error: Script exceeds maximum size of 400 bytes.\n\n";
		return -1;
	}
	else {
		int histChunkLengthInsertIndex = 2;

		insertChunkLength(ScriptVec, histChunkLengthInsertIndex, HIST_CHUNK_LENGTH, 16, true);
		
		combineVectors(ImageVec, ZipVec, ScriptVec, ZIP_FILE);
	}
	return 0;
}

void combineVectors(vector<unsigned char>& ImageVec, vector<unsigned char>& ZipVec, vector<unsigned char>& ScriptVec, const string& ZIP_FILE) {

	const ptrdiff_t FIRST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), FIRST_IDAT_ID.begin(), FIRST_IDAT_ID.end()) - ImageVec.begin() - 4;
	const ptrdiff_t HIST_SCRIPT_CHUNK_INSERT_INDEX = FIRST_IDAT_START_INDEX;

	ImageVec.insert((ImageVec.begin() + HIST_SCRIPT_CHUNK_INSERT_INDEX), ScriptVec.begin(), ScriptVec.end()); 

	const ptrdiff_t LAST_IDAT_CHUNK_INSERT_INDEX = ImageVec.size() - 12;

	ImageVec.insert((ImageVec.begin() + LAST_IDAT_CHUNK_INSERT_INDEX), ZipVec.begin(), ZipVec.end());  

	const ptrdiff_t LAST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), LAST_IDAT_ID.begin(), LAST_IDAT_ID.end()) - ImageVec.begin();
	const ptrdiff_t LAST_IDAT_LENGTH = ImageVec.size() - (LAST_IDAT_START_INDEX + 16);

	fixZipOffset(ImageVec, LAST_IDAT_START_INDEX);

	const uint32_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_START_INDEX], LAST_IDAT_LENGTH);

	ptrdiff_t lastIdatCrcInsertIndex = ImageVec.size() - 16;

	insertChunkLength(ImageVec, lastIdatCrcInsertIndex, LAST_IDAT_CRC, 32, true);

	writeFile(ImageVec, ZIP_FILE);
}

void fixZipOffset(vector<unsigned char>& ImageVec, const ptrdiff_t& LAST_IDAT_INDEX) {

	const ptrdiff_t
		START_CENTRAL_DIR_INDEX = search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ImageVec.begin(),
		END_CENTRAL_DIR_INDEX = search(ImageVec.begin() + START_CENTRAL_DIR_INDEX, ImageVec.end(), END_CENTRAL_ID.begin(), END_CENTRAL_ID.end()) - ImageVec.begin();

	ptrdiff_t
		zipFileRecordsIndex = END_CENTRAL_DIR_INDEX + 11,		
		commentLengthInsertIndex = END_CENTRAL_DIR_INDEX + 21,		
		endCentralStartInsertIndex = END_CENTRAL_DIR_INDEX + 19,	
		centralLocalInsertIndex = START_CENTRAL_DIR_INDEX - 1,		
		newZipOffset = LAST_IDAT_INDEX,					
		zipFileRecords = (ImageVec[zipFileRecordsIndex] << 8) | ImageVec[zipFileRecordsIndex - 1]; 

	while (zipFileRecords--) {
		newZipOffset = search(ImageVec.begin() + newZipOffset + 1, ImageVec.end(), ZIP_ID.begin(), ZIP_ID.end()) - ImageVec.begin(),
		centralLocalInsertIndex = 45 + search(ImageVec.begin() + centralLocalInsertIndex, ImageVec.end(), START_CENTRAL_ID.begin(), START_CENTRAL_ID.end()) - ImageVec.begin();
		insertChunkLength(ImageVec, centralLocalInsertIndex, newZipOffset, 32, false);
	}
	
	insertChunkLength(ImageVec, endCentralStartInsertIndex, START_CENTRAL_DIR_INDEX, 32, false);

	int commentLength = 16 + (ImageVec[commentLengthInsertIndex] << 8) | ImageVec[commentLengthInsertIndex - 1];

	insertChunkLength(ImageVec, commentLengthInsertIndex, commentLength, 16, false);
}

int writeFile(vector<unsigned char>& ImageVec, const string& ZIP_FILE) {

	const size_t SLASH_POS = ZIP_FILE.find_last_of("\\/") + 1;

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
