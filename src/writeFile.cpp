bool writeFile(std::vector<uint8_t>& Vec, const uint_fast32_t FILE_SIZE, bool isZipFile) {

	srand((unsigned)time(NULL));

	const std::string
		RANDOM_NAME_VALUE = std::to_string(rand()),
		PREFIX = isZipFile ? "pzip_" : "pjar_",
		POLYGLOT_FILENAME = PREFIX + RANDOM_NAME_VALUE.substr(0, 5) + ".png";
	
	std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return false;
	}

	file_ofs.write((char*)&Vec[0], FILE_SIZE);
	
	std::vector<uint8_t>().swap(Vec);
	
	std::cout << "\nCreated " 
		<< (isZipFile 
			? "PNG-ZIP" 
			: "PNG-JAR") 
		<< " polyglot image file: " + POLYGLOT_FILENAME + '\x20' + std::to_string(FILE_SIZE) + " Bytes.\n\nComplete!\n\n";
	
	// Attempt to set executable permissions for the newly created polyglot image file.
	#ifdef __unix__
	    if (chmod(POLYGLOT_FILENAME.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        	std::cerr << "\nWarning: Could not set executable permissions for " << POLYGLOT_FILENAME << ".\nYou will need do this manually using chmod.\n" << std::endl;
	    }
	#endif
	
	return true;
}
