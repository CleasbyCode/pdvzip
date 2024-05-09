#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <regex>
#include <fstream>

#include "extraction_scripts.cpp"
#include "four_bytes.cpp"
#include "crc32.cpp"
#include "value_updater.cpp"
#include "adjust_zip.cpp"
#include "erase_chunks.cpp"
#include "information.cpp"

size_t
	getFourByteValue(const std::vector<uchar>&, size_t),
	eraseChunks(std::vector<uchar>&, size_t);

void 
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uchar>&, size_t, const size_t, uint8_t, bool),
	adjustZipOffsets(std::vector<uchar>&, const size_t, bool),
	displayInfo();
