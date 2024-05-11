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

uint32_t
	crcUpdate(uchar*, uint32_t, uint32_t, uint32_t),
	getFourByteValue(const std::vector<uchar>&, uint32_t),
	eraseChunks(std::vector<uchar>&, uint32_t);

void 
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uchar>&, uint32_t, const uint32_t, uint8_t, bool),
	adjustZipOffsets(std::vector<uchar>&, const uint32_t),
	displayInfo();
