// 	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v2.8)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux), from the extracted repo / user@yourlinux:~/Downloads/pdvzip-main/src$
// 	$ g++ main.cpp -O2 -s -o pdvzip
//	$ sudo cp pdvzip /usr/bin
//
// 	Run it:
// 	$ pdvzip

enum class ArchiveType {
	ZIP,
	JAR
};

#include "pdvzip.h"

int main(int argc, char** argv) {

	ArchiveType thisArchiveType;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
		return 0;
	}

	if (argc != 3) {
		std::cout << "\nUsage: pdvzip <cover_image> <zip/jar>\n\t\bpdvzip --info\n\n";
		return 1;
	}

	const std::string 
		IMAGE_FILENAME(argv[1]),
		ARCHIVE_FILENAME(argv[2]);
	
	constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
	const std::regex regex_pattern(REG_EXP);

	if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(ARCHIVE_FILENAME, regex_pattern)) {
		std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		return 1;
	}

	const std::filesystem::path 
		IMAGE_PATH(IMAGE_FILENAME),
		ARCHIVE_PATH(ARCHIVE_FILENAME);

	const std::string
		IMAGE_EXTENSION = IMAGE_PATH.extension().string(),
		ARCHIVE_EXTENSION = ARCHIVE_PATH.extension().string();

	thisArchiveType = ARCHIVE_EXTENSION == ".zip" ? ArchiveType::ZIP : ArchiveType::JAR;
		
	if (IMAGE_EXTENSION != ".png" || (ARCHIVE_EXTENSION != ".zip" && ARCHIVE_EXTENSION != ".jar")) {
		std::cerr << (IMAGE_EXTENSION != ".png"
			? "\nImage File Error: Invalid file extension. Only expecting \".png\" image extension"
			: "\nArchive File Error: Invalid file extension. Only expecting \".zip or .jar\" archive extensions")
			<< ".\n\n";
		return 1;
	}

	if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(ARCHIVE_FILENAME)) {
		std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
			? "\nImage"
			: "\nArchvie")
			<< " File Error: File not found. Check the filename and try again.\n\n";
		return 1;
	}
	pdvZip(IMAGE_FILENAME, ARCHIVE_FILENAME, thisArchiveType);
}
