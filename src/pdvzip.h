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

template <uint_fast8_t N>
uint_fast32_t searchFunc(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast8_t, const uint_fast8_t (&)[N]);

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t),
	getByteValue(const std::vector<uint_fast8_t>&, const uint_fast32_t, const uint_fast8_t BYTE_LENGTH, bool isBigEndian);
bool 
	eraseChunks(std::vector<uint_fast8_t>&),
	writeFile(std::vector<uint_fast8_t>&, const uint_fast32_t, bool);
void 
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t, bool),
	adjustZipOffsets(std::vector<uint_fast8_t>&, const uint_fast32_t),
	displayInfo();

uint_fast8_t pdvZip(const std::string&, const std::string&, bool);

