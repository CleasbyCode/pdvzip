// 	PNG Data Vehicle, ZIP Edition (PDVZIP v2.4.6)
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
	} else if (argc == 3) {
		bool
			isFileCheckSuccess = false,
			isZipFile = true;

		constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
		const std::regex regex_pattern(REG_EXP);

		const std::string
			IMAGE_FILENAME = argv[1],
			ZIP_FILENAME = argv[2];

		std::filesystem::path imagePath(IMAGE_FILENAME);
		std::filesystem::path zipPath(ZIP_FILENAME);

		std::string
			imageExtension = imagePath.extension().string(),
			zipExtension = zipPath.extension().string();

		isZipFile = zipExtension == ".jar" ? false : isZipFile;

		if (imageExtension != ".png" || (zipExtension != ".zip" && zipExtension != ".jar")) {
			std::cerr << (imageExtension != ".png"
				? "\nImage File Error: Invalid file extension. Expecting only \"png\""
				: "\nZIP File Error: Invalid file extension. Expecting only \"zip/jar\"")
				<< ".\n\n";
		} else if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(ZIP_FILENAME, regex_pattern)) {
			std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		} else if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(ZIP_FILENAME)) {
			std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
				? "\nImage"
				: "\nZIP")
				<< " File Error: File not found. Check the filename and try again.\n\n";
		} else {
			isFileCheckSuccess = true;
		}
		if (isFileCheckSuccess) { 
				pdvZip(IMAGE_FILENAME, ZIP_FILENAME, isZipFile); 
		} else { 
			return 1; 
		}
	} else {
		std::cout << "\nUsage: pdvzip <cover_image> <zip/jar>\n\t\bpdvzip --info\n\n";
	}
}
