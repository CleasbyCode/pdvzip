// 	PNG Data Vehicle, ZIP Edition (PDVZIP v2.3.7)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux):
// 	$ g++ main.cpp -O2 -s -o pdvzip

// 	Run it:
// 	$ ./pdvzip

#include "pdvzip.h"

int main(int argc, char** argv) {
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	} else if (argc == 3) {
		bool
			isFileCheckSuccess = false,
			isZipFile = true;

		std::string
			IMAGE_FILENAME = argv[1],
			ZIP_FILENAME = argv[2];

		const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

		const std::string
			IMAGE_FILE_EXTENSION = IMAGE_FILENAME.length() > 2 ? IMAGE_FILENAME.substr(IMAGE_FILENAME.length() - 3) : IMAGE_FILENAME,
			ZIP_FILE_EXTENSION = ZIP_FILENAME.length() > 2 ? ZIP_FILENAME.substr(ZIP_FILENAME.length() - 3) : ZIP_FILENAME;

		isZipFile = ZIP_FILE_EXTENSION == "jar" ? false : isZipFile;

		if (IMAGE_FILE_EXTENSION != "png" || (ZIP_FILE_EXTENSION != "zip" && ZIP_FILE_EXTENSION != "jar")) {
			std::cerr << (IMAGE_FILE_EXTENSION != "png"
				? "\nImage File Error: Invalid file extension. Expecting only \"png\""
				: "\nZIP File Error: Invalid file extension. Expecting only \"zip/jar\"")
				<< ".\n\n";
		} else if (!regex_match(IMAGE_FILENAME, REG_EXP) || !regex_match(ZIP_FILENAME, REG_EXP)) {
			std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		} else if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(ZIP_FILENAME)) {
			std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
				? "\nImage"
				: "\nZIP")
				<< " File Error: File not found. Check the filename and try again.\n\n";
		} else {
			isFileCheckSuccess = true;
		}
		isFileCheckSuccess ? startPdv(IMAGE_FILENAME, ZIP_FILENAME, isZipFile) : std::exit(EXIT_FAILURE);		
	} else {
		std::cout << "\nUsage: pdvzip <cover_image> <zip/jar>\n\t\bpdvzip --info\n\n";
	}
}
