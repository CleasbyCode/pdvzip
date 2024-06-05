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

#include "extractionScripts.cpp"
#include "fourBytes.cpp"
#include "crc32.cpp"
#include "valueUpdater.cpp"
#include "searchFunc.cpp"
#include "eraseChunks.cpp"
#include "adjustZip.cpp"
#include "pdvzip.cpp"
#include "information.cpp"

template <uint_fast8_t N>
uint_fast32_t searchFunc(std::vector<uint_fast8_t>&, uint_fast32_t, uint_fast8_t, const uint_fast8_t (&)[N]);

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t),
	eraseChunks(std::vector<uint_fast8_t>&),
	getFourByteValue(const std::vector<uint_fast8_t>&, const uint_fast32_t);

void 
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t, bool),
	adjustZipOffsets(std::vector<uint_fast8_t>&, const uint_fast32_t),
	displayInfo();
