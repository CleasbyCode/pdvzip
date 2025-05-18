#pragma once

#include <string>

bool hasValidArchiveExtension(const std::string&);
bool hasValidImageExtension(const std::string&);
bool hasValidFilename(const std::string&);
void validateFiles(const std::string&, const std::string&);
