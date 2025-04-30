#pragma once

#ifdef __unix__
    #include <sys/stat.h> // Required for chmod
#endif

#include <vector>
#include <array>
#include <iterator>
#include <filesystem>
#include <random>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <fstream>

// https://github.com/lvandeve/lodepng 
#include "lodepng/lodepng.h"
#include "lodepng/lodepng.cpp"
// (Copyright (c) 2005-2024 Lode Vandevenne)

#include "resizeImage.cpp"
#include "information.cpp"
#include "programArgs.cpp"
#include "fileChecks.cpp"
#include "getByteValue.cpp"
#include "searchFunc.cpp"
#include "crc32.cpp"
#include "copyChunks.cpp"
#include "extractionScripts.cpp"
#include "valueUpdater.cpp"
#include "adjustZip.cpp"
#include "writeFile.cpp"
#include "pdvzip.cpp"

template <typename T, size_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, const uint8_t, const std::array<T, N>&);

uint32_t
	crcUpdate(uint8_t*, uint32_t),
	getByteValue(const std::vector<uint8_t>&, const uint32_t, const uint8_t, bool);

bool 
	hasValidFilename(const std::string&),
	writeFile(std::vector<uint8_t>&, const uint32_t, bool);

void 
	validateFiles(const std::string&, const std::string&),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t),
	resizeImage(std::vector<uint8_t>&),
	copyEssentialPngChunks(std::vector<uint8_t>&),
	adjustZipOffsets(std::vector<uint8_t>&, const uint32_t, const uint32_t),
	displayInfo();

uint8_t pdvZip(const std::string&, const std::string&, ArchiveType);
