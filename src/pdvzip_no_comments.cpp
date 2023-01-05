
// PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.2). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "ScriptVec.hpp"
	
unsigned long updateCrc(const unsigned long&, unsigned char*, const size_t&);
unsigned long crc(unsigned char*, const size_t&);
void openFiles(char* []);
void checkFileSize(std::ifstream&, std::ifstream&, const std::string&, const std::string&);
void readFilesIntoVectors(std::ifstream&, std::ifstream&, const std::string&, const std::string&, const ptrdiff_t&, const ptrdiff_t&);
void checkFileRequirements(std::vector<unsigned char>&, std::vector<unsigned char>&);
void eraseChunks(std::vector<unsigned char>&);
void fixPaletteChunk(std::vector<unsigned char>&);
void completeScript(std::vector<unsigned char>&);
void combineVectors(std::vector<unsigned char>&, std::vector<unsigned char>&, std::vector<unsigned char>&);
void fixZipOffset(std::vector<unsigned char>&, const ptrdiff_t&);
void writeFile(std::vector<unsigned char>&, const std::string&);
void insertChunkLength(std::vector<unsigned char>&, ptrdiff_t, const size_t&, int, bool);
void displayInfo();

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	else if (argc < 3 || argc > 3) {
		std::cout << "\nUsage:  pdvzip  <png_image>  <zip_file>\n\tpdvzip  --info\n\n";
	}
	else {
		openFiles(argv);
	}
	return 0;
}

void openFiles(char* argv[]) {

	const std::string
		IMG_FILE = argv[1],
		ZIP_FILE = argv[2];

	std::ifstream
		readImg(IMG_FILE, std::ios::binary),
		readZip(ZIP_FILE, std::ios::binary);

	if (!readImg || !readZip) {
		
		const std::string
			READ_ERR_MSG = "Read Error: Unable to open/read file: ",
			ERR_MSG = !readImg ? "\nPNG " + READ_ERR_MSG + "'" + IMG_FILE + "'\n\n" : "\nZIP " + READ_ERR_MSG + "'" + ZIP_FILE + "'\n\n";

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}
	
	checkFileSize(readImg, readZip, IMG_FILE, ZIP_FILE);
}

void checkFileSize(std::ifstream& readImg, std::ifstream& readZip, const std::string& IMG_FILE, const std::string& ZIP_FILE) {

	const int
		MAX_PNG_SIZE_BYTES = 5242880,	
		MAX_SCRIPT_SIZE_BYTES = 750;	

	readImg.seekg(0, readImg.end),
	readZip.seekg(0, readZip.end);

	const ptrdiff_t
		IMG_SIZE = readImg.tellg(),
		ZIP_SIZE = readZip.tellg(),
		COMBINED_SIZE = IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES;

	if ((IMG_SIZE + MAX_SCRIPT_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| ZIP_SIZE > MAX_PNG_SIZE_BYTES
		|| COMBINED_SIZE > MAX_PNG_SIZE_BYTES) {

		const ptrdiff_t
			EXCEED_SIZE = (IMG_SIZE + ZIP_SIZE + MAX_SCRIPT_SIZE_BYTES) - MAX_PNG_SIZE_BYTES,
			AVAILABLE_SIZE = MAX_PNG_SIZE_BYTES - (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES);

		const std::string
			SIZE_ERR_MSG = "Size Error: File must not exceed Twitter's file size limit of 5MB (5,242,880 bytes).\n\n",
			COMBINED_SIZE_ERR_MSG = "\nSize Error: " + std::to_string(COMBINED_SIZE) +
			" bytes is the combined size of your PNG image + ZIP file + Script (750 bytes), \nwhich exceeds Twitter's 5MB size limit by "
			+ std::to_string(EXCEED_SIZE) + " bytes. Available ZIP file size is " + std::to_string(AVAILABLE_SIZE) + " bytes.\n\n",

			ERR_MSG = (IMG_SIZE + MAX_SCRIPT_SIZE_BYTES > MAX_PNG_SIZE_BYTES) ? "\nPNG " + SIZE_ERR_MSG
			: (ZIP_SIZE > MAX_PNG_SIZE_BYTES ? "\nZIP " + SIZE_ERR_MSG : COMBINED_SIZE_ERR_MSG);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}
	
	readFilesIntoVectors(readImg, readZip, IMG_FILE, ZIP_FILE, IMG_SIZE, ZIP_SIZE);
}

void readFilesIntoVectors(std::ifstream& readImg, std::ifstream& readZip, const std::string& IMG_FILE, const std::string& ZIP_FILE, const ptrdiff_t& IMG_SIZE, const ptrdiff_t& ZIP_SIZE) {

	std::vector<unsigned char>
		ZipVec{ 0,0,0,0,73,68,65,84,0,0,0,0 },
		ImageVec{ 0 / sizeof(unsigned char) };

	readImg.seekg(0, readImg.beg),
		readZip.seekg(0, readZip.beg);

	ImageVec.resize(IMG_SIZE / sizeof(unsigned char));
	readImg.read((char*)&ImageVec[0], IMG_SIZE);

	ZipVec.resize(ZIP_SIZE + ZipVec.size() / sizeof(unsigned char));
	readZip.read((char*)&ZipVec[8], ZIP_SIZE);

	checkFileRequirements(ImageVec, ZipVec);

	eraseChunks(ImageVec);

	if (ImageVec[25] == 3) {
		fixPaletteChunk(ImageVec);
	}
	
	int idatZipChunkLengthIndex = 1;
	
	insertChunkLength(ZipVec, idatZipChunkLengthIndex, ZIP_SIZE, 24, true);

	completeScript(ZipVec);
	
	combineVectors(ImageVec, ZipVec, ScriptVec);

	writeFile(ImageVec, ZIP_FILE);
}

void checkFileRequirements(std::vector<unsigned char>& ImageVec, std::vector<unsigned char>& ZipVec) {

	const std::string
		PNG_ID = "\x89PNG",
		ZIP_ID = "PK\x03\x04",
		IMG_HDR{ ImageVec.begin(), ImageVec.begin() + PNG_ID.length() },	
		ZIP_HDR{ ZipVec.begin() + 8, ZipVec.begin() + 8 + ZIP_ID.length() };

	const int
		DIMS_WIDTH = ImageVec[18] << 8 | ImageVec[19],	
		DIMS_HEIGHT = ImageVec[22] << 8 | ImageVec[23],
		PNG24_MAX_DIMS = 900,			
		PNG8_MAX_DIMS = 4096,			
		PNG_MIN_DIMS = 65,			
		COLOR_TYPE = ImageVec[25],		
		INZIP_NAME_LENGTH = ZipVec[34], 
		PNG8 = 3,				
		PNG24 = 2,				
		PNG32 = 6,				
		MIN_NAME_LENGTH = 4;	

	const bool	
		VALID_COLOR_TYPE = (COLOR_TYPE == PNG8) ? true
		: (COLOR_TYPE == PNG24) ? true
		: ((COLOR_TYPE == PNG32) ? true : false),

		VALID_IMAGE_DIMS = (COLOR_TYPE == PNG24 || COLOR_TYPE == PNG32
			&& DIMS_WIDTH <= PNG24_MAX_DIMS
			&& DIMS_HEIGHT <= PNG24_MAX_DIMS
			&& DIMS_WIDTH >= PNG_MIN_DIMS
			&& DIMS_HEIGHT >= PNG_MIN_DIMS) ? true
		: ((COLOR_TYPE == PNG8 && DIMS_WIDTH <= PNG8_MAX_DIMS
			&& DIMS_HEIGHT <= PNG8_MAX_DIMS
			&& DIMS_WIDTH >= PNG_MIN_DIMS
			&& DIMS_HEIGHT >= PNG_MIN_DIMS) ? true : false);

	if (IMG_HDR != PNG_ID
		|| ZIP_HDR != ZIP_ID
		|| VALID_COLOR_TYPE != true
		|| VALID_IMAGE_DIMS != true
		|| MIN_NAME_LENGTH > INZIP_NAME_LENGTH) {

		const std::string
			HEADER_ERR_MSG = "Header Error: File does not appear to be a valid",
			IMAGE_ERR_MSG1 = "\nPNG Image Error: Dimensions of PNG image do not meet program requirements.\nSee 'pdvzip --info' for more details.\n\n",
			IMAGE_ERR_MSG2 = "\nPNG Image Error: Colour type of PNG image does not meet program requirements.\nSee 'pdvzip --info' for more details.\n\n",
			ZIP_ERR_MSG = "\nZIP Error: Media filename length within ZIP archive is too short (or file is corrupt)."
			"\n\t   Increase the length of the media filename and make sure it contains a valid extension.\n\n",

			ERR_MSG = (IMG_HDR != PNG_ID) ? "\nPNG " + HEADER_ERR_MSG + " PNG image.\n\n"
			: (ZIP_HDR != ZIP_ID) ? "\nZIP " + HEADER_ERR_MSG + " ZIP archive.\n\n"
			: (VALID_IMAGE_DIMS != true) ? IMAGE_ERR_MSG1
			: ((VALID_COLOR_TYPE != true) ? IMAGE_ERR_MSG2 : ZIP_ERR_MSG);

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	const char
		BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' };  
	
	int pos = 18;
	for (int i{}; i < 16; i++) {
		for (int j{}; j < 7; j++) {
			if (ImageVec[pos] == BAD_CHAR[j]) {
				std::cerr << 
					"\nError:  The IHDR header of this image contains a character that will break the Linux extraction script.\n"
					"\tModifying the image dimensions (increase or decrease) may resolve the issue for this image.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}
		pos++;
	}
}

void eraseChunks(std::vector<unsigned char>& ImageVec) {
	
	const std::string
		IDAT_ID = "IDAT",
		CHUNKS_TO_REMOVE[14]{ "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	ptrdiff_t firstIdatIndex = search(ImageVec.begin(), ImageVec.end(), IDAT_ID.begin(), IDAT_ID.end()) - ImageVec.begin() - 4;
	int chunk = sizeof(CHUNKS_TO_REMOVE) / sizeof(std::string);

	while (chunk--) {
		const ptrdiff_t CHUNK_INDEX = search(ImageVec.begin(), ImageVec.end(), CHUNKS_TO_REMOVE[chunk].begin(), CHUNKS_TO_REMOVE[chunk].end())-ImageVec.begin()-4;
		if (firstIdatIndex > CHUNK_INDEX) {
			int chunkLength = (ImageVec[CHUNK_INDEX + 2] << 8) | ImageVec[CHUNK_INDEX + 3];
			ImageVec.erase(ImageVec.begin() + CHUNK_INDEX, ImageVec.begin() + CHUNK_INDEX + (chunkLength + 12));
			firstIdatIndex = search(ImageVec.begin(), ImageVec.end(), IDAT_ID.begin(), IDAT_ID.end()) - ImageVec.begin() - 4;
			chunk++;
		}
	}
}

unsigned long updateCrc(const unsigned long& crc, unsigned char* buf, const size_t& len)
{

	unsigned long crcTable[256]{ 0,1996959894,3993919788,2567524794,124634137,1886057615,3915621685,2657392035,249268274,2044508324,3772115230,2547177864,162941995,
					2125561021,3887607047,2428444049,498536548,1789927666,4089016648,2227061214,450548861,1843258603,4107580753,2211677639,325883990,
					1684777152,4251122042,2321926636,335633487,1661365465,4195302755,2366115317,997073096,1281953886,3579855332,2724688242,1006888145,
					1258607687,3524101629,2768942443,901097722,1119000684,3686517206,2898065728,853044451,1172266101,3705015759,2882616665,651767980,
					1373503546,3369554304,3218104598,565507253,1454621731,3485111705,3099436303,671266974,1594198024,3322730930,2970347812,795835527,
					1483230225,3244367275,3060149565,1994146192,31158534,2563907772,4023717930,1907459465,112637215,2680153253,3904427059,2013776290,
					251722036,2517215374,3775830040,2137656763,141376813,2439277719,3865271297,1802195444,476864866,2238001368,4066508878,1812370925,
					453092731,2181625025,4111451223,1706088902,314042704,2344532202,4240017532,1658658271,366619977,2362670323,4224994405,1303535960,
					984961486,2747007092,3569037538,1256170817,1037604311,2765210733,3554079995,1131014506,879679996,2909243462,3663771856,1141124467,
					855842277,2852801631,3708648649,1342533948,654459306,3188396048,3373015174,1466479909,544179635,3110523913,3462522015,1591671054,
					702138776,2966460450,3352799412,1504918807,783551873,3082640443,3233442989,3988292384,2596254646,62317068,1957810842,3939845945,
					2647816111,81470997,1943803523,3814918930,2489596804,225274430,2053790376,3826175755,2466906013,167816743,2097651377,4027552580,
					2265490386,503444072,1762050814,4150417245,2154129355,426522225,1852507879,4275313526,2312317920,282753626,1742555852,4189708143,
					2394877945,397917763,1622183637,3604390888,2714866558,953729732,1340076626,3518719985,2797360999,1068828381,1219638859,3624741850,
					2936675148,906185462,1090812512,3747672003,2825379669,829329135,1181335161,3412177804,3160834842,628085408,1382605366,3423369109,
					3138078467,570562233,1426400815,3317316542,2998733608,733239954,1555261956,3268935591,3050360625,752459403,1541320221,2607071920,
					3965973030,1969922972,40735498,2617837225,3943577151,1913087877,83908371,2512341634,3803740692,2075208622,213261112,2463272603,
					3855990285,2094854071,198958881,2262029012,4057260610,1759359992,534414190,2176718541,4139329115,1873836001,414664567,2282248934,
					4279200368,1711684554,285281116,2405801727,4167216745,1634467795,376229701,2685067896,3608007406,1308918612,956543938,2808555105,
					3495958263,1231636301,1047427035,2932959818,3654703836,1088359270,936918000,2847714899,3736837829,1202900863,817233897,3183342108,
					3401237130,1404277552,615818150,3134207493,3453421203,1423857449,601450431,3009837614,3294710456,1567103746,711928724,3020668471,
					3272380065,1510334235,755167117 };

	unsigned long c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

unsigned long crc(unsigned char* buf, const size_t& len) {
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void fixPaletteChunk(std::vector<unsigned char>& ImageVec) {
	
	const std::string PLTE_ID = "PLTE";
	
	const ptrdiff_t
		PLTE_CHUNK_START_INDEX = search(ImageVec.begin(), ImageVec.end(), PLTE_ID.begin(), PLTE_ID.end()) - ImageVec.begin(),
		PLTE_CHUNK_LENGTH_INDEX = PLTE_CHUNK_START_INDEX - 4,
		PLTE_CHUNK_LENGTH = (ImageVec[PLTE_CHUNK_LENGTH_INDEX + 2] << 8) | ImageVec[PLTE_CHUNK_LENGTH_INDEX + 3];

	const char
		BAD_CHAR[7]{ '(', ')', '\'', '`', '"', '>', ';' }, 
		GOOD_CHAR[7]{ '*', '&', '=', '}', 'a', '?', ':' }; 

	int twoCount{};

	for (ptrdiff_t i = PLTE_CHUNK_START_INDEX; i < (PLTE_CHUNK_START_INDEX + (PLTE_CHUNK_LENGTH + 4)); i++) {

		ImageVec[i] = (ImageVec[i] == BAD_CHAR[0]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[1]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[2]) ? GOOD_CHAR[1]
			: (ImageVec[i] == BAD_CHAR[3]) ? GOOD_CHAR[4]
			: (ImageVec[i] == BAD_CHAR[4]) ? GOOD_CHAR[0]
			: (ImageVec[i] == BAD_CHAR[5]) ? GOOD_CHAR[5]
			: ((ImageVec[i] == BAD_CHAR[6]) ? GOOD_CHAR[6] : ImageVec[i]);
		
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
			}
		}
		else {
			twoCount = 0;
		}
		
		int j = 1, k = 2;
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

	int modCrcVal = 255;
	bool redoCrc{};

	do {
		redoCrc = false;
		
		const uint32_t PLTE_CHUNK_CRC = crc(&ImageVec[PLTE_CHUNK_START_INDEX], PLTE_CHUNK_LENGTH + 4);

		ptrdiff_t
			plteCrcInsertIndex = PLTE_CHUNK_START_INDEX + (PLTE_CHUNK_LENGTH + 4),
			plteModCrcInsertIndex = plteCrcInsertIndex - 1;
		
		insertChunkLength(ImageVec, plteCrcInsertIndex, PLTE_CHUNK_CRC, 32, true);
		
		for (int i{}; i < 5; i++) {
			for (int j{}; j < 7; j++) {
				if (i > 3) break;
				if (ImageVec[plteCrcInsertIndex] == BAD_CHAR[j]) {
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

void completeScript(std::vector<unsigned char>& ZipVec) {
	
	std::vector<std::string> ExtApp{ "aac","mp3","mp4","avi","asf","flv","ebm","mkv","peg","wav","wmv","wma","mov","3gp","ogg","pdf",".py","ps1","exe",
					".sh","vlc --play-and-exit --no-video-title-show ","evince ","python3 ","pwsh ","./","xdg-open ","powershell;Invoke-Item ",
					" &> /dev/null","start /b \"\"","pause&","powershell","chmod +x ",";" };

	const int
		INZIP_NAME_LENGTH_INDEX = 34,				
		INZIP_NAME_INDEX = 38,					
		INZIP_NAME_LENGTH = ZipVec[INZIP_NAME_LENGTH_INDEX],	
		MAX_SCRIPT_SIZE_BYTES = 750,
		VIDEO_AUDIO = 20,
		PDF = 21,
		PYTHON = 22,
		LINUX_PWSH = 23,
		EXECUTABLE = 24,
		BASH_XDG_OPEN = 25,
		FOLDER_INVOKE_ITEM = 26,
		WIN_POWERSHELL = 30,
		MOD_INZIP_FILENAME = 36;

	std::string
		inzipName{ ZipVec.begin() + INZIP_NAME_INDEX, ZipVec.begin() + INZIP_NAME_INDEX + INZIP_NAME_LENGTH },	
		inzipNameExt = inzipName.substr(inzipName.length() - 3, 3),	
		argsLinux{}, argsWindows{};					

	size_t findExtension = inzipName.find_last_of('.');

	ExtApp.push_back(inzipName);
	
	int InsertSequence[52]{
				236,234,116,115,114, 33,28,27,33,20, 	
				236,234,115,114, 33,28,33,21,		
				259,237,236,234,116,115,114, 29,35,33,22,34,33,22, 
				259,237,236,234,116,115,114,114,114,114, 29,35,33,28,34,33,24,32,33,31 }, 

				appIndex{},
				insertIndex{},
				extAppElement{},
				sequenceLimit{};
	
	for (; appIndex != 26; appIndex++) {
		if (ExtApp[appIndex] == inzipNameExt) {
			appIndex = appIndex <= 14 ? 20 : appIndex += 6; 
			break;						
		}
	}
	
	if (findExtension == 0 || findExtension > inzipName.length()) {
		appIndex = ZipVec[INZIP_NAME_INDEX + INZIP_NAME_LENGTH - 1] == '/' ? FOLDER_INVOKE_ITEM : EXECUTABLE;
	}

	if (appIndex > 21 && appIndex < 26) {
		std::cout << "\nFor this file type you can provide command-line arguments here, if required.\n\nLinux: ";
		std::getline(std::cin, argsLinux);
		std::cout << "\nWindows: ";
		std::getline(std::cin, argsWindows);
		argsLinux.insert(0, "\x20"), argsWindows.insert(0, "\x20");
		ExtApp.push_back(argsLinux), ExtApp.push_back(argsWindows); 
	}

	switch (appIndex) {
	case VIDEO_AUDIO:		
		extAppElement = 5;	
		break;
	case PDF:					
		insertIndex = 10, extAppElement = 14;	
		break;
	case PYTHON:			
	case LINUX_PWSH:		
		insertIndex = 18, extAppElement = 25;	
		if (appIndex == LINUX_PWSH) {
			inzipName.insert(0, ".\\");	
			ExtApp.push_back(inzipName);
			InsertSequence[31] = LINUX_PWSH,
				InsertSequence[28] = WIN_POWERSHELL;
			InsertSequence[27] = MOD_INZIP_FILENAME; 
		}
		break;
	case EXECUTABLE:
		insertIndex = 32, extAppElement = 42;
		break;
	case BASH_XDG_OPEN:
		insertIndex = 33, extAppElement = 43;
		break;
	case FOLDER_INVOKE_ITEM:
		insertIndex = 10, extAppElement = 14;
		InsertSequence[15] = FOLDER_INVOKE_ITEM, InsertSequence[17] = BASH_XDG_OPEN;
		break;
	default:	
		insertIndex = 10, extAppElement = 14;
		InsertSequence[17] = BASH_XDG_OPEN; 
	}
	
	sequenceLimit = { appIndex == BASH_XDG_OPEN ? extAppElement - 1 : extAppElement };

	while (insertIndex < sequenceLimit) {
		ScriptVec.insert(ScriptVec.begin() + InsertSequence[insertIndex], ExtApp[InsertSequence[extAppElement]].begin(), 
		ExtApp[InsertSequence[extAppElement]].end());
		insertIndex++, extAppElement++;
	}

	bool redoChunkLength{};

	do {

		redoChunkLength = false;
		
		const ptrdiff_t HIST_CHUNK_LENGTH = ScriptVec.size() - 12;

		if (HIST_CHUNK_LENGTH > MAX_SCRIPT_SIZE_BYTES) {
			std::cerr << "\nScript Error: Script exceeds maximum size of 750 bytes.\n\n";
			std::exit(EXIT_FAILURE);
		}

		int histChunkLengthInsertIndex = 2;
		
		insertChunkLength(ScriptVec, histChunkLengthInsertIndex, HIST_CHUNK_LENGTH, 16, true);
		
		if (ScriptVec[3] == '('
			|| ScriptVec[3] == ')'
			|| ScriptVec[3] == '\''
			|| ScriptVec[3] == '`'
			|| ScriptVec[3] == '"'
			|| ScriptVec[3] == '>'
			|| ScriptVec[3] == ';') {
			
			ScriptVec.insert(ScriptVec.begin() + (HIST_CHUNK_LENGTH + 10), '.');
			redoChunkLength = true;
		}
	} while (redoChunkLength);
}

void combineVectors(std::vector<unsigned char>& ImageVec, std::vector<unsigned char>& ZipVec, std::vector<unsigned char>& ScriptVec) {

	const std::string
		IDAT_ID = "IDAT",
		IDAT_ZIP_ID = "IDATPK";
	
	const ptrdiff_t
		FIRST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), IDAT_ID.begin(), IDAT_ID.end()) - ImageVec.begin() - 4,
		HIST_SCRIPT_CHUNK_INSERT_INDEX = FIRST_IDAT_START_INDEX;
	
	ImageVec.insert((ImageVec.begin() + HIST_SCRIPT_CHUNK_INSERT_INDEX), ScriptVec.begin(), ScriptVec.end());
	
	const ptrdiff_t LAST_IDAT_CHUNK_INSERT_INDEX = ImageVec.size() - 12;
	
	ImageVec.insert((ImageVec.begin() + LAST_IDAT_CHUNK_INSERT_INDEX), ZipVec.begin(), ZipVec.end());

	const ptrdiff_t
		LAST_IDAT_START_INDEX = search(ImageVec.begin(), ImageVec.end(), IDAT_ZIP_ID.begin(), IDAT_ZIP_ID.end()) - ImageVec.begin(),
		LAST_IDAT_LENGTH = ImageVec.size() - (LAST_IDAT_START_INDEX + 16);

	fixZipOffset(ImageVec, LAST_IDAT_START_INDEX);

	const uint32_t LAST_IDAT_CRC = crc(&ImageVec[LAST_IDAT_START_INDEX], LAST_IDAT_LENGTH);

	ptrdiff_t lastIdatCrcInsertIndex = ImageVec.size() - 16;
	
	insertChunkLength(ImageVec, lastIdatCrcInsertIndex, LAST_IDAT_CRC, 32, true);
}

void fixZipOffset(std::vector<unsigned char>& ImageVec, const ptrdiff_t& LAST_IDAT_INDEX) {

	const std::string
		START_CENTRAL_ID = "PK\x01\x02",
		END_CENTRAL_ID = "PK\x05\x06",
		ZIP_ID = "PK\x03\x04";
	
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

void writeFile(std::vector<unsigned char>& ImageVec, const std::string& ZIP_FILE) {

	const size_t SLASH_POS = ZIP_FILE.find_last_of("\\/") + 1;
	
	const std::string COMPLETE_PDV_FILE = ZIP_FILE.substr(0, SLASH_POS) + "pdv" + "_" + ZIP_FILE.substr(SLASH_POS, ZIP_FILE.length()) + ".png";

	std::ofstream writeFinal(COMPLETE_PDV_FILE, std::ios::binary);

	if (!writeFinal) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFinal.write((char*)&ImageVec[0], ImageVec.size());
	writeFinal.close();

	std::cout << "\nCreated output file " << "'" << COMPLETE_PDV_FILE << "' " << ImageVec.size() << " bytes." << "\n\nAll Done!\n\n";
}

void insertChunkLength(std::vector<unsigned char>& vec, ptrdiff_t lengthInsertIndex, const size_t& CHUNK_LENGTH, int bits, bool isBig) {

	if (isBig) {
		while (bits) vec[lengthInsertIndex++] = (CHUNK_LENGTH >> (bits -= 8)) & 0xff;
	}
	else {
		while (bits) vec[lengthInsertIndex--] = (CHUNK_LENGTH >> (bits -= 8)) & 0xff;
	}
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.2). Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.

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

PNG Image Requirements for Arbitrary Data Preservation

PNG file size (image + embedded content) must not exceed 5MB (5,242,880 bytes).
Twitter will convert image to jpg if you exceed this size.

Dimensions:

PNG_32 (Truecolour with alpha [6])
PNG_24 (Truecolour [2])

Image dimensions can be set between a minimum of 65 x 65 (Approx.) and a maximum of 900 x 900 (Exct).

Twitter will convert the image to jpg if you exceed the maximum dimensions, and/or the PNG file size exceeds 5MB.
Twitter will convert the image to webp if you go below the minimum dimensions.
Twitter will convert images with 256 colours or less to PNG_8 (indexed-colour, 8bit).

PNG_8 (Indexed-colour [3])

Image dimensions can be set between a minimum of 65 x 65 (Approx.) and a maximum of 4096 x 4096 (Exct).

Twitter will convert the image to jpg if you exceed the maximum dimensions, and/or the PNG file size exceeds 5MB.
Twitter will convert the image to webp if you go below the minimum dimensions.

Chunks:

PNG chunk types that Twitter will preserve arbitrary data.

bKGD, cHRM, gAMA, hIST, iCCP,
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
pHYs, sBIT, sPLT, sRGB,
tRNS. (Not recommended as it will distort image).

This program uses hIST & IDAT chunk names and removes the others (if found).

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
