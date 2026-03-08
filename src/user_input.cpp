#include "pdvzip.h"

UserArguments promptForArguments(FileType file_type) {
	constexpr std::size_t MAX_ARG_LENGTH = 1024;

	UserArguments args;

	const bool needs_args =
		(file_type > FileType::PDF && file_type < FileType::UNKNOWN_FILE_TYPE)
		|| file_type == FileType::LINUX_EXECUTABLE
		|| file_type == FileType::JAR;

	if (!needs_args) {
		return args;
	}

	std::println("\nFor this file type, if required, you can provide command-line arguments here.");

	if (file_type != FileType::WINDOWS_EXECUTABLE) {
		std::print("\nLinux: ");
		std::getline(std::cin, args.linux_args);
		if (args.linux_args.size() > MAX_ARG_LENGTH) {
			throw std::runtime_error("Input Error: Linux arguments exceed maximum length.");
		}
	}
	if (file_type != FileType::LINUX_EXECUTABLE) {
		std::print("\nWindows: ");
		std::getline(std::cin, args.windows_args);
		if (args.windows_args.size() > MAX_ARG_LENGTH) {
			throw std::runtime_error("Input Error: Windows arguments exceed maximum length.");
		}
	}
	return args;
}
