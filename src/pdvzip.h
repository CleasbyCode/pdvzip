#pragma once

#ifdef __unix__
    #include <sys/stat.h> // Required for chmod
#endif

#include <vector>
#include <iterator>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <regex>
#include <fstream>

#include "getByteValue.cpp"
#include "searchFunc.cpp"
#include "crc32.cpp"
#include "eraseChunks.cpp"
#include "extractionScripts.cpp"
#include "valueUpdater.cpp"
#include "adjustZip.cpp"
#include "writeFile.cpp"
#include "pdvzip.cpp"
#include "information.cpp"

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, const uint8_t, const uint8_t (&)[N]);

uint32_t
	crcUpdate(uint8_t*, uint32_t),
	getByteValue(const std::vector<uint8_t>&, const uint32_t, const uint8_t BYTE_LENGTH, bool isBigEndian);

bool writeFile(std::vector<uint8_t>&, const uint32_t, bool);

void 
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t),
	eraseChunks(std::vector<uint8_t>&),
	adjustZipOffsets(std::vector<uint8_t>&, const uint32_t, const uint32_t),
	displayInfo();

uint8_t pdvZip(const std::string&, const std::string&, bool);
