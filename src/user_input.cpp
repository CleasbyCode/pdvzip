#include "pdvzip.h"

namespace {

constexpr std::size_t MAX_ARG_LENGTH = 1024;

[[nodiscard]] bool fileTypeAcceptsArguments(FileType file_type) {
	return (file_type > FileType::PDF && file_type < FileType::UNKNOWN_FILE_TYPE)
		|| file_type == FileType::LINUX_EXECUTABLE
		|| file_type == FileType::JAR;
}

void readArgumentLine(std::string& out, std::string_view label) {
	out.clear();
	out.reserve(MAX_ARG_LENGTH);

	while (out.size() < MAX_ARG_LENGTH) {
		const int ch = std::cin.get();
		if (ch == std::char_traits<char>::eof()) {
			if (std::cin.bad()) {
				throw std::runtime_error(std::format(
					"Input Error: Failed to read {} (stdin closed or unreadable).", label));
			}
			return;
		}
		if (ch == '\n') {
			return;
		}
		out.push_back(static_cast<char>(ch));
	}

	// We've read MAX_ARG_LENGTH characters without seeing a newline.
	// If the next character is a newline or EOF, the line is exactly at the cap.
	const int next = std::cin.peek();
	if (next == std::char_traits<char>::eof() || next == '\n') {
		if (next == '\n') {
			std::cin.get();
		}
		return;
	}

	throw std::runtime_error(std::format(
		"Input Error: {} exceed maximum length of {} bytes.", label, MAX_ARG_LENGTH));
}

} // anonymous namespace

UserArguments promptForArguments(FileType file_type) {
	UserArguments args;

	if (!fileTypeAcceptsArguments(file_type)) {
		return args;
	}

	std::println("\nFor this file type, if required, you can provide command-line arguments here.");

	if (file_type != FileType::WINDOWS_EXECUTABLE) {
		std::print("\nLinux: ");
		readArgumentLine(args.linux_args, "Linux arguments");
	}
	if (file_type != FileType::LINUX_EXECUTABLE) {
		std::print("\nWindows: ");
		readArgumentLine(args.windows_args, "Windows arguments");
	}
	return args;
}
