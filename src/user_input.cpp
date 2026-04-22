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

	auto readLine = [](std::string& out, std::string_view label, std::size_t max_len) {
		if (!std::getline(std::cin, out)) {
			throw std::runtime_error(std::format(
				"Input Error: Failed to read {} (stdin closed or unreadable).", label));
		}
		if (out.size() > max_len) {
			throw std::runtime_error(std::format(
				"Input Error: {} exceed maximum length of {} bytes.", label, max_len));
		}
	};

	if (file_type != FileType::WINDOWS_EXECUTABLE) {
		std::print("\nLinux: ");
		readLine(args.linux_args, "Linux arguments", MAX_ARG_LENGTH);
	}
	if (file_type != FileType::LINUX_EXECUTABLE) {
		std::print("\nWindows: ");
		readLine(args.windows_args, "Windows arguments", MAX_ARG_LENGTH);
	}
	return args;
}
