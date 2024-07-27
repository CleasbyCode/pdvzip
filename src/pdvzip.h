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
#include "getVecSize.cpp"
#include "twoBytes.cpp"
#include "fourBytes.cpp"
#include "crc32.cpp"
#include "valueUpdater.cpp"
#include "searchFunc.cpp"
#include "eraseChunks.cpp"
#include "adjustZip.cpp"
#include "pdvzip.cpp"
#include "information.cpp"

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, uint8_t, const uint8_t (&)[N]);

uint16_t getTwoByteValue(const std::vector<uint8_t>&, uint32_t, bool);

uint32_t
	crcUpdate(uint8_t*, uint32_t),
	getVecSize(const std::vector<uint8_t>&),
	eraseChunks(std::vector<uint8_t>&),
	getFourByteValue(const std::vector<uint8_t>&, const uint32_t);

void 
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t, bool),
	adjustZipOffsets(std::vector<uint8_t>&, const uint32_t),
	displayInfo();
