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

#include "checkFileSizes.cpp"
#include "getVecSize.cpp"
#include "getByteValue.cpp"
#include "searchFunc.cpp"
#include "crc32.cpp"
#include "eraseChunks.cpp"
#include "checkImageSpecs.cpp"
#include "checkIhdrChunk.cpp"
#include "checkImageSignatures.cpp"
#include "checkZipSignature.cpp"
#include "extractionScripts.cpp"
#include "valueUpdater.cpp"
#include "adjustZip.cpp"
#include "writeFile.cpp"
#include "pdvzip.cpp"
#include "information.cpp"

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, uint8_t, const uint8_t (&)[N]);

uint32_t
	crcUpdate(uint8_t*, uint32_t),
	getVecSize(const std::vector<uint8_t>&),
	getByteValue(const std::vector<uint8_t>&, const uint32_t, const uint8_t BYTE_LENGTH, bool isBigEndian);

void 
	checkFileSizes(const std::string&, const std::string&),
	checkImageSpecs(std::vector<uint8_t>&),
	checkIhdrChunk(std::vector<uint8_t>&),
	checkImageSignatures(std::vector<uint8_t>&),
	checkZipSignature(std::vector<uint8_t>&),
	eraseChunks(std::vector<uint8_t>&),
	writeFile(std::vector<uint8_t>&, const uint32_t, bool),
	startPdv(const std::string&, const std::string&, bool),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t, bool),
	adjustZipOffsets(std::vector<uint8_t>&, const uint32_t),
	displayInfo();
