// 	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v3.1)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux), from the extracted repo :~/Downloads/pdvzip-main/src
//
// 	$ chmod +x compile_pdvzip.sh
//	$ ./compile_pdvzip.sh
//	$ Compilation successful. Executable 'pdvzip' created.
//	$ sudo cp pdvzip /usr/bin
//
// 	Run it:
// 	$ pdvzip

#include "programArgs.h"    
#include "fileChecks.h"      
#include "pdvZip.h"         

#include <iostream>         
#include <stdexcept>        

int main(int argc, char** argv) {
	try {
		ProgramArgs args = ProgramArgs::parse(argc, argv);
		if (!hasValidFilename(args.image_file) || !hasValidFilename(args.archive_file)) {
            		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        	}
        	validateFiles(args.image_file, args.archive_file);
        	pdvZip(args.image_file, args.archive_file, args.thisArchiveType);
    	}
	catch (const std::runtime_error& e) {
        	std::cerr << "\n" << e.what() << "\n\n";
        	return 1;
    	}
}
