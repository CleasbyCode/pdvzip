// PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.3). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

typedef unsigned char BYTE;
typedef	unsigned short TBYTE;

class ValueUpdater {
public:
	void Value(std::vector<BYTE>& vec, size_t valueInsertIndex, const size_t VALUE, TBYTE bits, bool isBig) {
		if (isBig) {
			while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
		}
		else {
			while (bits) vec[valueInsertIndex--] = (VALUE >> (bits -= 8)) & 0xff;
		}
	}
} *update;

void storeFiles(std::ifstream&, std::ifstream&, const std::string&, const std::string&);
void checkFileRequirements(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]);
void eraseChunks(std::vector<BYTE>&);
void modifyPaletteChunk(std::vector<BYTE>&, const char(&)[]);
void completeScript(std::vector<BYTE>&, std::vector<BYTE>&, const char(&)[]);
void combineVectors(std::vector< BYTE>&, std::vector<BYTE>&, std::vector<BYTE>&);
void fixZipOffset(std::vector<BYTE>&, const size_t);
void displayInfo();

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
		
		std::ifstream
			IMAGE(IMG_NAME, std::ios::binary),
			ZIP(ZIP_NAME, std::ios::binary);

		if (!IMAGE || !ZIP) {
			const std::string
				READ_ERR_MSG = "Read Error: Unable to open/read file: ",
				ERR_MSG = !IMAGE ? "\nPNG " + READ_ERR_MSG + "'" + IMG_NAME + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_NAME + "'\n\n";
			std::cerr << ERR_MSG;
			std::exit(EXIT_FAILURE);
		}
		
		storeFiles(IMAGE, ZIP, IMG_NAME, ZIP_NAME);
	}
	return 0;
}

void storeFiles(std::ifstream& IMAGE, std::ifstream& ZIP, const std::string& IMG_FILE, const std::string& ZIP_FILE) {

	std::vector<BYTE>
		ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 }, 
		ImageVec((std::istreambuf_iterator<char>(IMAGE)), std::istreambuf_iterator<char>());  

	ZIP.seekg(0, ZIP.end);

	const size_t ZIP_SIZE = ZIP.tellg();

	ZIP.seekg(0, ZIP.beg);

	ZipVec.resize(ZIP_SIZE + ZipVec.size() / sizeof(BYTE));
	ZIP.read((char*)&ZipVec[8], ZIP_SIZE);
	
	static const char BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' };

	checkFileRequirements(ImageVec, ZipVec, BAD_CHAR);

	eraseChunks(ImageVec);

	const size_t
		IMG_SIZE = ImageVec.size(),
		MAX_PNG_SIZE_BYTES = 5242880,	
		MAX_SCRIPT_SIZE_BYTES = 750,	
		COMBINED_SIZE = IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES;
	
	if ((IMG_SIZE + MAX_SCRIPT_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| ZIP_SIZE > MAX_PNG_SIZE_BYTES
		|| COMBINED_SIZE > MAX_PNG_SIZE_BYTES) {

		const size_t
			EXCEED_SIZE_LIMIT = (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES) - MAX_PNG_SIZE_BYTES,
			AVAILABLE_BYTES = MAX_PNG_SIZE_BYTES - (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES);

		const std::string
			SIZE_ERR = "Size Error: File must not exceed Twitter's file size limit of 5MB (5,242,880 bytes).\n\n",
			COMBINED_SIZE_ERR = "\nSize Error: " + std::to_string(COMBINED_SIZE) +
			" bytes is the combined size of your PNG image + ZIP file + Script (750 bytes), \nwhich exceeds Twitter's 5MB size limit by "
			+ std::to_string(EXCEED_SIZE_LIMIT) + " bytes. Available ZIP file size is " + std::to_string(AVAILABLE_BYTES) + " bytes.\n\n",

			ERR_MSG = (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES > MAX_PNG_SIZE_BYTES) ? "\nPNG " + SIZE_ERR
			: (ZIP_SIZE > MAX_PNG_SIZE_BYTES ? "\nZIP " + SIZE_ERR : COMBINED_SIZE_ERR);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	if (ImageVec[25] == 3) {
		modifyPaletteChunk(ImageVec, BAD_CHAR);
	}

	TBYTE chunkLengthIndex = 1;
	
	update->Value(ZipVec, chunkLengthIndex, ZIP_SIZE, 24, true);

	completeScript(ZipVec, ImageVec, BAD_CHAR);
}

void checkFileRequirements(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, const char(&BAD_CHAR)[]) {

	const std::string
		PNG_SIG = "\x89PNG",		
		ZIP_SIG = "PK\x03\x04",		
		IMG_VEC_HDR{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() },		
		ZIP_VEC_HDR{ ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_SIG.length() };	

	const TBYTE
		IMAGE_DIMS_WIDTH = ImageVec[18] << 8 | ImageVec[19],	
		IMAGE_DIMS_HEIGHT = ImageVec[22] << 8 | ImageVec[23],	
		PNG_TRUECOLOUR_MAX_DIMS = 899,		
		PNG_INDEXCOLOUR_MAX_DIMS = 4096,	
		PNG_MIN_DIMS = 68,			
	
		COLOUR_TYPE = ImageVec[25] == 6 ? 2 : ImageVec[25],

		INZIP_NAME_LENGTH = ZipVec[34],	
		PNG_INDEXCOLOUR = 3,		
		PNG_TRUECOLOUR = 2,		
		MIN_NAME_LENGTH = 4;		

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

	if (IMG_VEC_HDR != PNG_SIG
		|| ZIP_VEC_HDR != ZIP_SIG
		|| !VALID_COLOUR_TYPE
		|| !VALID_IMAGE_DIMS
		|| MIN_NAME_LENGTH > INZIP_NAME_LENGTH) { 

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

	TBYTE pos = 18; 
	
	for (TBYTE i{}; i < 16; i++) {
		for (TBYTE j{}; j < 7; j++) {
			if (ImageVec[pos] == BAD_CHAR[j]) { 
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

	for (TBYTE chunkIndex = 14; chunkIndex > 0; chunkIndex--) {
		size_t
			firstIdatIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[0].begin(), CHUNK[0].end()) - ImageVec.begin(),
			chunkFoundIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[chunkIndex].begin(), CHUNK[chunkIndex].end()) - ImageVec.begin() - 4;
		
		if (firstIdatIndex > chunkFoundIndex) {
			int chunkSize = (ImageVec[chunkFoundIndex + 1] << 16) | ImageVec[chunkFoundIndex + 2] << 8 | ImageVec[chunkFoundIndex + 3];
			ImageVec.erase(ImageVec.begin() + chunkFoundIndex, ImageVec.begin() + chunkFoundIndex + (chunkSize + 12));
			chunkIndex++; 
		}
	}
}

void modifyPaletteChunk(std::vector<BYTE>& ImageVec, const char(&BAD_CHAR)[]) {

	const std::string PLTE_SIG = "PLTE";
	
	const size_t
		PLTE_CHUNK_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - ImageVec.begin(),
		PLTE_CHUNK_SIZE = (ImageVec[PLTE_CHUNK_INDEX - 2] << 8) | ImageVec[PLTE_CHUNK_INDEX - 1];
	
	const char GOOD_CHAR[7]{ '*', '&', '=', '}', 'a', '?', ':' };

	TBYTE twoCount{};

	for (size_t i = PLTE_CHUNK_INDEX; i < (PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4)); i++) {
		
		ImageVec[i] = (ImageVec[i] == BAD_CHAR[0]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[1]) ? GOOD_CHAR[1] : (ImageVec[i] == BAD_CHAR[2]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[3]) ? GOOD_CHAR[4] : (ImageVec[i] == BAD_CHAR[4]) ? GOOD_CHAR[0]
			: (ImageVec[i] == BAD_CHAR[5]) ? GOOD_CHAR[5] : ((ImageVec[i] == BAD_CHAR[6]) ? GOOD_CHAR[6] : ImageVec[i]);
		
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
				ImageVec[i + 1] = GOOD_CHAR[0];
			}
			else {
				ImageVec[i] = ImageVec[i] == '<' ? GOOD_CHAR[2] : GOOD_CHAR[0];
			}
		}

		if (ImageVec[i] == '&' || ImageVec[i] == '|') {
			twoCount++;
			if (twoCount > 1) { 
				ImageVec[i] = (ImageVec[i] == '&' ? GOOD_CHAR[0] : GOOD_CHAR[3]);
				twoCount = 0;
			}
		}
		else {
			twoCount = 0;
		}

		TBYTE j = 1, k = 2;
		if (ImageVec[i] == '<') { 
			while (j < 12) {
				if (ImageVec[i + j] > 47 && ImageVec[i + j] < 58 && ImageVec[i + k] == '<') { 
					ImageVec[i] = GOOD_CHAR[2];  
					j = 12;
				}
				j++, k++;
			}
		}
	}

	int modValue = 255;
	bool redoCrc{};

	do {
		redoCrc = false;
		
		const size_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_CHUNK_INDEX], PLTE_CHUNK_SIZE + 4);
		
		size_t
			crcInsertIndex = PLTE_CHUNK_INDEX + (PLTE_CHUNK_SIZE + 4),
			modValueInsertIndex = crcInsertIndex - 1;
		
		update->Value(ImageVec, crcInsertIndex, PLTE_CHUNK_CRC, 32, true);
		
		for (TBYTE i = 0; i < 5; i++) {
			for (TBYTE j = 0; j < 7; j++) {
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

	std::vector<std::string> AppVec{ "aac","mp3","mp4","avi","asf","flv","ebm","mkv","peg","wav","wmv","wma","mov","3gp","ogg","pdf",".py","ps1","exe",
					".sh","vlc --play-and-exit --no-video-title-show ","evince ","python3 ","pwsh ","./","xdg-open ","powershell;Invoke-Item ",
					" &> /dev/null","start /b \"\"","pause&","powershell","chmod +x ",";" };

	const TBYTE
		INZIP_NAME_LENGTH_INDEX = 34,	
		INZIP_NAME_INDEX = 38,		
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	
		MAX_SCRIPT_SIZE_BYTES = 750,	
		VIDEO_AUDIO = 20,	
		PDF = 21,		
		PYTHON = 22,		
		POWERSHELL = 23,	
		EXECUTABLE = 24,	
		BASH_XDG_OPEN = 25,	
		FOLDER_INVOKE_ITEM = 26,	
		START_B = 28,			
		WIN_POWERSHELL = 30,		
		MOD_INZIP_FILENAME = 36;	

	std::string
		inzipName{ ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH },
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),
		argsLinux{},
		argsWindows{};

	size_t checkExtension = inzipName.find_last_of('.');

	AppVec.push_back(inzipName);

	TBYTE sequence[52]{
				236,234,116,115,114, 33,28,27,33,20,	
				236,234,115,114, 33,28,33,21,		
				259,237,236,234,116,115,114, 29,35,33,22,34,33,22,	
				259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31 }, 

				appIndex = 0,		
				insertIndex = 0,		
				sequenceLimit = 0;	
		
	for (; appIndex != 26; appIndex++) {
		if (AppVec[appIndex] == inzipNameExt) {
			appIndex = appIndex <= 14 ? 20 : appIndex += 6;
			break;
		}
	}
		
	if (checkExtension == 0 || checkExtension > inzipName.length()) {
		appIndex = ZipVec[INZIP_NAME_INDEX + INZIP_NAME_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}
		
	if (appIndex > 21 && appIndex < 26) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, argsLinux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, argsWindows);
		argsLinux.insert(0, "\x20"), argsWindows.insert(0, "\x20");
		AppVec.push_back(argsLinux),	
		AppVec.push_back(argsWindows);	
	}

	switch (appIndex) {
	case VIDEO_AUDIO:	
		appIndex = 5;
		break;
	case PDF:		
	case FOLDER_INVOKE_ITEM:
		sequence[15] = appIndex == FOLDER_INVOKE_ITEM ? FOLDER_INVOKE_ITEM : START_B;
		sequence[17] = appIndex == FOLDER_INVOKE_ITEM ? BASH_XDG_OPEN : PDF;
		insertIndex = 10,
		appIndex = 14;
		break;
	case PYTHON:		
	case POWERSHELL:
		if (appIndex == POWERSHELL) {
			inzipName.insert(0, ".\\");		
			AppVec.push_back(inzipName);		
			sequence[31] = POWERSHELL,		
			sequence[28] = WIN_POWERSHELL;		
			sequence[27] = MOD_INZIP_FILENAME;	
		}
		insertIndex = 18,
		appIndex = 25;
		break;
	case EXECUTABLE:	
	case BASH_XDG_OPEN:
		insertIndex = appIndex == EXECUTABLE ? 32 : 33;
		appIndex = insertIndex == 32 ? 42 : 43;
		break;
	default:			
		insertIndex = 10,	
		appIndex = 14;
		sequence[17] = BASH_XDG_OPEN;	
	}
		
	sequenceLimit = insertIndex == 33 ? appIndex - 1 : appIndex;
		
	while (sequenceLimit > insertIndex) {
		ScriptVec.insert(ScriptVec.begin() + sequence[insertIndex], AppVec[sequence[appIndex]].begin(), AppVec[sequence[appIndex]].end());
		insertIndex++, appIndex++;
	}

	bool redoChunkLength{};

	do {

		redoChunkLength = false;
		
		const size_t HIST_LENGTH = ScriptVec.size() - 12;
		
		if (HIST_LENGTH > MAX_SCRIPT_SIZE_BYTES) {
			std::cerr << "\nScript Size Error: Extraction script exceeds maximum size of 750 bytes.\n\n";
			std::exit(EXIT_FAILURE);
		}

		TBYTE chunkLengthIndex = 2;
		
		update->Value(ScriptVec, chunkLengthIndex, HIST_LENGTH, 16, true);

		for (TBYTE i = 0; i < 7; i++) {
			if (ScriptVec[3] == BAD_CHAR[i]) {
				ScriptVec.insert(ScriptVec.begin() + (HIST_LENGTH + 10), '.');
				redoChunkLength = true;
				break;
			}
		}
	} while (redoChunkLength);
		
	combineVectors(ImageVec, ZipVec, ScriptVec);
}

void combineVectors(std::vector<BYTE>& ImageVec, std::vector<BYTE>& ZipVec, std::vector<BYTE>& ScriptVec) {

	srand((unsigned)time(NULL));

	const std::string
		IDAT_SIG = "IDAT",
		TXT_NUM = std::to_string(rand()),
		PDV_FILENAME = "pdvimg_" + TXT_NUM.substr(0, 5) + ".png"; 

	const size_t
		FIRST_IDAT_INDEX = search(ImageVec.begin(), ImageVec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - ImageVec.begin() - 4,
		LAST_IDAT_INDEX = (ImageVec.size() + ScriptVec.size()) - 12;
	
	ImageVec.insert((ImageVec.begin() + FIRST_IDAT_INDEX), ScriptVec.begin(), ScriptVec.end());
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_INDEX), ZipVec.begin(), ZipVec.end());
	
	fixZipOffset(ImageVec, LAST_IDAT_INDEX);
	
	const size_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_INDEX + 4], ZipVec.size() - 8);

	size_t crcInsertIndex = ImageVec.size() - 16;	
	
	update->Value(ImageVec, crcInsertIndex, LAST_IDAT_CRC, 32, true);

	std::ofstream writeFinal(PDV_FILENAME, std::ios::binary);

	if (!writeFinal) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	writeFinal.write((char*)&ImageVec[0], ImageVec.size());

	std::cout << "\nCreated output file " << "'" << PDV_FILENAME << "' " << ImageVec.size() << " bytes." << "\n\nAll Done!\n\n";
}

void fixZipOffset(std::vector<BYTE>& ImageVec, const size_t LAST_IDAT_INDEX) {

	const std::string
		START_CENTRAL_SIG = "PK\x01\x02",
		END_CENTRAL_SIG = "PK\x05\x06",
		ZIP_SIG = "PK\x03\x04";
	
	const size_t
		START_CENTRAL_INDEX = search(ImageVec.begin() + LAST_IDAT_INDEX, ImageVec.end(), START_CENTRAL_SIG.begin(), START_CENTRAL_SIG.end()) - ImageVec.begin(),
		END_CENTRAL_INDEX = search(ImageVec.begin() + START_CENTRAL_INDEX, ImageVec.end(), END_CENTRAL_SIG.begin(), END_CENTRAL_SIG.end()) - ImageVec.begin();

	size_t
		zipRecordsIndex = END_CENTRAL_INDEX + 11,	
		commentLengthIndex = END_CENTRAL_INDEX + 21,	
		endCentralStartIndex = END_CENTRAL_INDEX + 19,	
		centralLocalIndex = START_CENTRAL_INDEX - 1,	
		newOffset = LAST_IDAT_INDEX,			
		zipRecords = (ImageVec[zipRecordsIndex] << 8) | ImageVec[zipRecordsIndex - 1];
	
	while (zipRecords--) {
		newOffset = search(ImageVec.begin() + newOffset + 1, ImageVec.end(), ZIP_SIG.begin(), ZIP_SIG.end()) - ImageVec.begin(),
		centralLocalIndex = 45 + search(ImageVec.begin() + centralLocalIndex, ImageVec.end(), START_CENTRAL_SIG.begin(), START_CENTRAL_SIG.end()) - ImageVec.begin();
		update->Value(ImageVec, centralLocalIndex, newOffset, 32, false);
	}
	
	update->Value(ImageVec, endCentralStartIndex, START_CENTRAL_INDEX, 32, false);
	
	TBYTE commentLength = 16 + (ImageVec[commentLengthIndex] << 8) | ImageVec[commentLengthIndex - 1];

	update->Value(ImageVec, commentLengthIndex, commentLength, 16, false);
}

size_t crcUpdate(const size_t crc, BYTE* buf, const size_t len)
{
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
	
	size_t c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

size_t crc(BYTE* buf, const size_t len)
{
	return crcUpdate(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.3). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.


PDVZIP enables you to embed a ZIP archive containing a small media file within a tweetable PNG image.
Twitter will retain the arbitrary data embedded inside the image. Twitter's PNG size limit is 5MB per image.

Once the ZIP file has been embedded within your PNG image, its ready to be shared (tweeted) or 'executed' 
whenever you want to open/play the media file. You can also upload and share your PNG file to *some popular
image hosting sites, such as Flickr, ImgBB, Imgur, ImgPile, ImageShack, PostImage, etc. 
*Not all image hosting sites are compatible, e.g. ImgBox, Reddit.

From a Linux terminal: ./pdvzip_your_image.png (Image file requires executable permissions).
From a Windows terminal: First, rename the '.png' file extension to '.cmd', then .\pdvzip_your_image.cmd 

For some common video & audio files, Linux requires the 'vlc (VideoLAN)' program, Windows uses the default media player.
PDF '.pdf', Linux requires the 'evince' program, Windows uses the set default PDF viewer.
Python '.py', Linux & Windows use the 'python3' command to run these programs.
PowerShell '.ps1', Linux uses the 'pwsh' command (if PowerShell installed), Windows uses 'powershell' to run these scripts.

For any other media type/file extension, Linux & Windows will rely on the operating system's set default application.

PNG Image Requirements for Arbitrary Data Preservation

PNG file size (image + embedded content) must not exceed 5MB (5,242,880 bytes).
Twitter will convert image to jpg if you exceed this size.

Dimensions:

The following dimension size limits are specific to pdvzip and not necessarily the extact Twitter size limits.

PNG-32 (Truecolour with alpha [6])
PNG-24 (Truecolour [2])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 899 x 899.

PNG-8 (Indexed-colour [3])

Image dimensions can be set between a minimum of 68 x 68 and a maximum of 4096 x 4096.

Chunks:

PNG chunks that you can insert arbitrary data, in which Twitter will preserve in conjuction with the above dimensions & file size limits.

bKGD, cHRM, gAMA, hIST,
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
PLTE, (Use only with PNG-32 & PNG-24 for arbitrary data).
pHYs, sBIT, sPLT, sRGB,
tRNS. (Not recommended, may distort image).

This program uses hIST & IDAT chunk names for storing arbitrary data.

ZIP File Size & Other Information

To work out the maximum ZIP file size, start with Twitter's size limit of 5MB (5,242,880 bytes), minus PNG image size, 
minus 750 bytes (shell extraction script). Example: 5,242,880 - (307,200 + 750) = 4,934,930 bytes available for ZIP file. 

The less detailed the image, the more space available for the ZIP.

Make sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
Use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.
A file without an extension will be treated as a Linux executable.
Paint.net application is recommended for easily creating compatible PNG image files.
 
)";
}
