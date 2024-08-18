bool writeFile(std::vector<uint8_t>& Vec, const uint32_t FILE_SIZE, bool isZipFile) {
	srand((unsigned)time(NULL));

	const std::string
		RANDOM_NAME_VALUE = std::to_string(rand()),
		POLYGLOT_FILENAME = "pzip_" + RANDOM_NAME_VALUE.substr(0, 5) + ".png";

	std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return false;
	}

	file_ofs.write((char*)&Vec[0], FILE_SIZE);

	std::cout << "\nCreated " 
		<< (isZipFile 
			? "PNG-ZIP" 
			: "PNG-JAR") 
		<< " polyglot image file: " + POLYGLOT_FILENAME + '\x20' + std::to_string(FILE_SIZE) + " Bytes.\n\nComplete!\n\n";
	return true;
}
