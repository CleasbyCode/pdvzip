#pragma once

#include <string>
#include <vector>
#include <cstdint>

bool hasValidArchiveExtension(const std::string&);
bool hasValidImageExtension(const std::string&);
bool hasValidFilename(const std::string&);
void validateFiles(std::string&, std::string&, uintmax_t&, uintmax_t&, std::vector<uint8_t>&, std::vector<uint8_t>&);
