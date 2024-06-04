#pragma once

#include <vector>
#include <filesystem>
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
#include "erase_chunks.cpp"
#include "adjust_zip.cpp"
#include "pdvzip.cpp"
#include "information.cpp"

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t),
	eraseChunks(std::vector<uint_fast8_t>&),
	getFourByteValue(const std::vector<uint_fast8_t>&, uint_fast32_t);

void 
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t, bool),
	adjustZipOffsets(std::vector<uint_fast8_t>&, const uint_fast32_t),
	displayInfo();
