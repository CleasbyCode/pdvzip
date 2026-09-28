#pragma once

#include "pdvzip.h"

#include <string>
#include <string_view>
#include <vector>

namespace script_builder_internal {

std::string buildScriptText(
	FileType file_type,
	const std::string& first_filename,
	const UserArguments& user_args);

// Exposed for unit tests and shared script rendering.
[[nodiscard]] std::vector<std::string> splitPosixArguments(
	std::string_view input, std::string_view field_name);
[[nodiscard]] std::vector<std::string> splitWindowsArguments(
	std::string_view input, std::string_view field_name);
[[nodiscard]] std::string quotePosixArgument(std::string_view arg);
[[nodiscard]] std::string quoteWindowsArgumentForCmd(std::string_view arg);

}  // namespace script_builder_internal
