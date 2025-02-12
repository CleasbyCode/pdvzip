bool writeFile(std::vector<uint8_t>& Vec, const uint32_t FILE_SIZE, bool isZipFile) {
	std::random_device rd;
    	std::mt19937 gen(rd());
    	std::uniform_int_distribution<> dist(10000, 99999);  

	const std::string
		PREFIX = isZipFile ? "pzip_" : "pjar_",
		POLYGLOT_FILENAME = PREFIX + std::to_string(dist(gen)) + ".png";

	std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return false;
	}
	
	file_ofs.write(reinterpret_cast<const char*>(Vec.data()), FILE_SIZE);
	
	std::vector<uint8_t>().swap(Vec);

	std::cout << "\nCreated " 
		<< (isZipFile 
			? "PNG-ZIP" 
			: "PNG-JAR") 
		<< " polyglot image file: " << POLYGLOT_FILENAME << " (" << FILE_SIZE << " bytes).\n\nComplete!\n\n";

	// Attempt to set executable permissions for the newly created polyglot image file.
	#ifdef __unix__
	    if (chmod(POLYGLOT_FILENAME.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        	std::cerr << "\nWarning: Could not set executable permissions for " << POLYGLOT_FILENAME << ".\nYou will need do this manually using chmod.\n" << std::endl;
	    }
	#endif

	return true;
}
