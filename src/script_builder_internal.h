#pragma once

#include "pdvzip.h"

namespace script_builder_internal {

std::string buildScriptText(FileType file_type, const std::string& first_filename, const UserArguments& user_args);

}  // namespace script_builder_internal
