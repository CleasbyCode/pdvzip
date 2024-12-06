// 	PNG Data Vehicle, ZIP Edition (PDVZIP v2.7)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux), from the extracted repo / user@yourlinux:~/Downloads/pdvzip-main/src$
// 	$ g++ main.cpp -O2 -s -o pdvzip
//	$ sudo cp pdvzip /usr/bin
//
// 	Run it:
// 	$ pdvzip

#include "pdvzip.h"

int main(int argc, char** argv) {
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
		return 0;
	}

	if (argc != 3) {
		std::cout << "\nUsage: pdvzip <cover_image> <zip/jar>\n\t\bpdvzip --info\n\n";
		return 1;
	}

	const std::string
		IMAGE_FILENAME = argv[1],
		ZIP_FILENAME = argv[2];

	constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
	const std::regex regex_pattern(REG_EXP);

	if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(ZIP_FILENAME, regex_pattern)) {
		std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		return 1;
	}

	const std::filesystem::path 
		IMAGE_PATH(IMAGE_FILENAME),
		ZIP_PATH(ZIP_FILENAME);

	const std::string
		IMAGE_EXTENSION = IMAGE_PATH.extension().string(),
		ZIP_EXTENSION = ZIP_PATH.extension().string();

	const bool isZipFile = (ZIP_EXTENSION == ".zip");

	if (IMAGE_EXTENSION != ".png" || (!isZipFile && ZIP_EXTENSION != ".jar")) {
		std::cerr << (IMAGE_EXTENSION != ".png"
			? "\nImage File Error: Invalid file extension. Expecting only \"png\" image extension"
			: "\nZIP File Error: Invalid file extension. Expecting only \"zip or jar\" archive extensions")
			<< ".\n\n";
		return 1;
	}

	if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(ZIP_FILENAME)) {
		std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
			? "\nImage"
			: "\nZIP")
			<< " File Error: File not found. Check the filename and try again.\n\n";
		return 1;
	}
	pdvZip(IMAGE_FILENAME, ZIP_FILENAME, isZipFile);
}
