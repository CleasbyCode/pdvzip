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

// Exposed for unit tests.
[[nodiscard]] std::vector<std::string> splitPosixArguments(
	std::string_view input, std::string_view field_name);

}  // namespace script_builder_internal
