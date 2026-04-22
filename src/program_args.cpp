#include "pdvzip.h"

ProgramArgs ProgramArgs::parse(int argc, char** argv) {
	if (argc < 1 || argv[0] == nullptr) {
		throw std::runtime_error("Invalid program invocation: missing program name");
	}

	if (argc == 2 && std::string_view(argv[1]) == "--info") {
		return ProgramArgs{ .image_file_path = {}, .archive_file_path = {}, .info_mode = true };
	}

	if (argc != 3) {
		const std::string prog = fs::path(argv[0]).filename().string();
		throw std::runtime_error(std::format(
			"Usage: {} <cover_image> <zip/jar>\n"
			"       {} --info",
			prog, prog));
	}
	if (argv[1] == nullptr || argv[2] == nullptr) {
		throw std::runtime_error("Invalid program invocation: missing input path.");
	}

	return ProgramArgs{
		.image_file_path   = argv[1],
		.archive_file_path = argv[2],
	};
}
