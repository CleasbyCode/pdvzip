#include "pdvzip.h"

#include <format>
#include <stdexcept>

namespace {

[[nodiscard]] bool isInfoModeRequest(int argc, char** argv) {
	return argc == 2 && argv[1] != nullptr && std::string_view(argv[1]) == "--info";
}

[[nodiscard]] std::string usageFor(std::string_view program_name) {
	return std::format(
		"Usage: {} <cover_image> <zip/jar>\n"
		"       {} --info",
		program_name, program_name);
}

} // anonymous namespace

ProgramArgs ProgramArgs::parse(int argc, char** argv) {
	if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
		throw std::runtime_error("Invalid program invocation: missing program name");
	}

	if (isInfoModeRequest(argc, argv)) {
		ProgramArgs args;
		args.info_mode = true;
		return args;
	}

	if (argc != 3) {
		const std::string prog = fs::path(argv[0]).filename().string();
		throw std::runtime_error(usageFor(prog));
	}
	if (argv[1] == nullptr || argv[2] == nullptr) {
		throw std::runtime_error("Invalid program invocation: missing input path.");
	}

	return ProgramArgs{
		.image_file_path   = argv[1],
		.archive_file_path = argv[2],
	};
}
