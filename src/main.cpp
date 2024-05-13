// 	PNG Data Vehicle, ZIP Edition (PDVZIP v2.3.3). 
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux):
// 	$ g++ pdvzip.cpp -O2 -s -o pdvzip

// 	Run it:
// 	$ ./pdvzip

#include "pdvzip.h"

int main(int argc, char** argv) {
	
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	} else if (argc < 3 || argc > 3) {
		std::cout << "\nUsage: pdvzip <png> <zip/jar>\n\t\bpdvzip --info\n\n";
	} else {
		
		bool isZipFile = true;

		std::string
			IMAGE_NAME = argv[1],
			ZIP_NAME = argv[2];

		const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
		
		const std::string
			GET_PNG_EXT = IMAGE_NAME.length() > 2 ? IMAGE_NAME.substr(IMAGE_NAME.length() - 3) : IMAGE_NAME,
			GET_ZIP_EXT = ZIP_NAME.length() > 2 ? ZIP_NAME.substr(ZIP_NAME.length() - 3) : ZIP_NAME;

		if (GET_PNG_EXT != "png" || GET_ZIP_EXT != "zip" && GET_ZIP_EXT != "jar" || !regex_match(IMAGE_NAME, REG_EXP) || !regex_match(ZIP_NAME, REG_EXP)) {
			std::cerr << (GET_PNG_EXT != "png" || GET_ZIP_EXT != "zip" ? 
			"\nFile Type Error: Invalid file extension found. Expecting only '.png' followed by '.zip/.jar'"
			: "\nInvalid Input Error: Characters not supported by this program found within file name arguments") << ".\n\n";
			std::exit(EXIT_FAILURE);
		}

		isZipFile = GET_ZIP_EXT == "jar" ? false : isZipFile;
		
		startPdv(IMAGE_NAME, ZIP_NAME, isZipFile);
	}
	return 0;
}


