#include "pdvzip.h"

bool hasBalancedQuotes(std::string_view str) {
	std::size_t single_count = 0, double_count = 0;

	for (std::size_t i = 0; i < str.size(); ++i) {
		const char c = str[i];
		const bool escaped = (i > 0 && str[i - 1] == '\\');

		if (c == '\'' && !escaped) ++single_count;
		else if (c == '"' && !escaped) ++double_count;
	}
	return (single_count % 2 == 0) && (double_count % 2 == 0);
}

UserArguments promptForArguments(FileType file_type) {
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
	}
	if (file_type != FileType::LINUX_EXECUTABLE) {
		std::print("\nWindows: ");
		std::getline(std::cin, args.windows_args);
	}

	if (!hasBalancedQuotes(args.linux_args) || !hasBalancedQuotes(args.windows_args)) {
		throw std::runtime_error("Arguments Error: Quotes mismatch. Check arguments and try again.");
	}
	return args;
}
