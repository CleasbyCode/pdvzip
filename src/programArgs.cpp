enum class ArchiveType {
	ZIP,
	JAR
};

struct ProgramArgs {
	ArchiveType thisArchiveType = ArchiveType::ZIP;
    	std::string image_file;
    	std::string archive_file;

    	static ProgramArgs parse(int argc, char** argv);
};

ProgramArgs ProgramArgs::parse(int argc, char** argv) {
	ProgramArgs args;
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
        	std::exit(0);
	}

	if (argc != 3) {
        	throw std::runtime_error("Usage: pdvzip <cover_image> <zip/jar>\n\t\bpdvzip --info");
    	}
	
    	args.image_file = argv[1];
    	args.archive_file = argv[2];

	std::filesystem::path archive_path(args.archive_file);
	if (archive_path.extension().string() == ".jar") {
		args.thisArchiveType = ArchiveType::JAR;
	}
	
    	return args;
}
