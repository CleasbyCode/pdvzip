// Resource-limit regressions for archive path validation (review issue 7).
// From the source directory:
/*
g++ -std=c++23 -O1 -I. tests/archive_path_resource_tests.cpp \
  archive_analysis.cpp binary_utils.cpp -lz -o /tmp/pdvzip-path-resource-tests
*/

#include "pdvzip.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace allocation_tracking {

// Only count allocations made by the validator, after constructing the ZIP.
// Live bytes include old and new vector buffers during reallocation. An epoch
// avoids counting later frees of allocations retained by library initialization.
struct alignas(std::max_align_t) Header {
	std::size_t size;
	std::size_t epoch;
};

bool enabled = false;
std::size_t epoch = 0;
std::size_t live = 0;
std::size_t peak = 0;
std::size_t largest = 0;

void begin() {
	++epoch;
	live = peak = largest = 0;
	enabled = true;
}

void* allocate(std::size_t size) {
	if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header)) {
		throw std::bad_alloc();
	}
	auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
	if (!header) throw std::bad_alloc();
	header->size = size;
	header->epoch = enabled ? epoch : 0;
	if (enabled) {
		live += size;
		peak = std::max(peak, live);
		largest = std::max(largest, size);
	}
	return header + 1;
}

void deallocate(void* pointer) noexcept {
	if (!pointer) return;
	auto* header = static_cast<Header*>(pointer) - 1;
	if (header->epoch != 0 && header->epoch == epoch) live -= header->size;
	std::free(header);
}

} // namespace allocation_tracking

void* operator new(std::size_t size) { return allocation_tracking::allocate(size); }
void* operator new[](std::size_t size) { return allocation_tracking::allocate(size); }
void operator delete(void* pointer) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete[](void* pointer) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { allocation_tracking::deallocate(pointer); }

namespace {

constexpr std::size_t MIB = 1024 * 1024;
constexpr std::size_t PATH_LIMIT = MIB;
constexpr std::string_view LIMIT_ERROR =
	"Archive Security Error: Total archive path length exceeds the 1 MiB safety limit.";

void require(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

void append16(vBytes& output, unsigned value) {
	output.push_back(static_cast<Byte>(value));
	output.push_back(static_cast<Byte>(value >> 8));
}

void append32(vBytes& output, std::size_t value) {
	for (unsigned shift = 0; shift < 32; shift += 8) {
		output.push_back(static_cast<Byte>(value >> shift));
	}
}

vBytes wrappedZip(const std::vector<std::string>& names) {
	require(!names.empty() && names.size() < UINT16_MAX, "invalid fixture entry count");
	vBytes output(8, 0);
	std::vector<std::size_t> offsets;
	for (const auto& name : names) {
		require(!name.empty() && name.size() <= UINT16_MAX, "invalid fixture filename length");
		offsets.push_back(output.size() - 8);
		append32(output, ZIP_LOCAL_FILE_HEADER_SIGNATURE);
		append16(output, 20);
		append16(output, 0); // flags
		append16(output, 0); // stored, empty payload
		append32(output, 0); // timestamp
		append32(output, 0); // CRC of empty payload
		append32(output, 0); // compressed size
		append32(output, 0); // uncompressed size
		append16(output, static_cast<unsigned>(name.size()));
		append16(output, 0); // extras
		output.insert(output.end(), name.begin(), name.end());
	}
	const std::size_t central_start = output.size();
	for (std::size_t i = 0; i < names.size(); ++i) {
		const auto& name = names[i];
		append32(output, ZIP_CENTRAL_DIRECTORY_SIGNATURE);
		append16(output, 0x0314); // Unix creator
		append16(output, 20);
		append16(output, 0); // flags
		append16(output, 0); // stored
		append32(output, 0); // timestamp
		append32(output, 0); // CRC
		append32(output, 0); // compressed size
		append32(output, 0); // uncompressed size
		append16(output, static_cast<unsigned>(name.size()));
		append16(output, 0); // extras
		append16(output, 0); // comment
		append16(output, 0); // disk
		append16(output, 0); // internal attributes
		append32(output, static_cast<std::size_t>(name.ends_with('/') ? 0040755U : 0100644U) << 16);
		append32(output, offsets[i]);
		output.insert(output.end(), name.begin(), name.end());
	}
	const std::size_t central_size = output.size() - central_start;
	append32(output, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE);
	append16(output, 0);
	append16(output, 0);
	append16(output, static_cast<unsigned>(names.size()));
	append16(output, static_cast<unsigned>(names.size()));
	append32(output, central_size);
	append32(output, central_start - 8);
	append16(output, 0);
	output.resize(output.size() + 4, 0);
	return output;
}

std::string longPath(std::size_t index, std::size_t length) {
	std::string path = "p" + std::to_string(index) + "/";
	require(path.size() < length, "fixture path too short");
	while (length - path.size() > 240) {
		path.append(239, 'a');
		path += '/';
	}
	path.append(length - path.size(), 'a');
	return path;
}

std::vector<std::string> distinctPaths(std::size_t count, std::size_t length) {
	std::vector<std::string> names;
	names.reserve(count);
	for (std::size_t i = 0; i < count; ++i) names.push_back(longPath(i, length));
	return names;
}

struct Result {
	bool accepted = false;
	std::string error;
	std::size_t peak = 0;
	std::size_t largest = 0;
};

Result validate(const vBytes& archive) {
	Result result;
	allocation_tracking::begin();
	try {
		validateArchiveEntryPaths(archive);
		result.accepted = true;
	} catch (const std::exception& error) {
		allocation_tracking::enabled = false;
		result.error = error.what();
	} catch (...) {
		allocation_tracking::enabled = false;
		throw;
	}
	allocation_tracking::enabled = false;
	result.peak = allocation_tracking::peak;
	result.largest = allocation_tracking::largest;
	return result;
}

void requireBounded(const Result& result) {
	// 1 MiB of paths permits at most 1 MiB+1 nodes (24 bytes on 64-bit).
	// Allow both vector buffers during growth, plus bounded entry bookkeeping.
	require(result.largest <= (PATH_LIMIT + 1) * 24,
		"single validation allocation exceeds the node-capacity bound");
	require(result.peak <= 50 * MIB, "live validation allocations exceed the growth bound");
}

void testLongDistinctPaths() {
	const auto archive = wrappedZip(distinctPaths(512, 3850));
	const auto result = validate(archive);
	std::cout << "Long-path fixture: " << archive.size() << " archive bytes; "
		<< result.peak << " peak live validation bytes; "
		<< result.largest << " largest allocation.\n";
	require(!result.accepted && result.error == LIMIT_ERROR,
		"oversized distinct paths did not report the path safety limit");
	requireBounded(result);
}

void testBoundary(bool directory, bool over_limit) {
	auto names = distinctPaths(PATH_LIMIT / 1024, 1024);
	if (over_limit) names.back() += 'b';
	if (directory) for (auto& name : names) name += '/';
	const auto result = validate(wrappedZip(names));
	require(over_limit ? (!result.accepted && result.error == LIMIT_ERROR) : result.accepted,
		"normalized path-byte boundary was not enforced exactly");
	requireBounded(result);
}

void testSharedPrefixBudget() {
	const std::string prefix = longPath(0, 4090) + '/';
	std::vector<std::string> names;
	for (unsigned i = 0; i < 512; ++i) names.push_back(prefix + "file" + std::to_string(i));
	const auto result = validate(wrappedZip(names));
	require(!result.accepted && result.error == LIMIT_ERROR,
		"shared prefixes bypassed the aggregate path budget");
	require(result.peak < MIB, "shared-prefix fixture unexpectedly grew a large trie");
}

void testMaximumLengthPath() {
	std::string path;
	path.reserve(UINT16_MAX);
	while (path.size() + 2 <= UINT16_MAX) path += "a/";
	path += 'a';
	const auto result = validate(wrappedZip({path}));
	require(result.accepted, "single maximum-length deeply nested path was rejected");
	requireBounded(result);
}

void testMaximumEntryCount() {
	std::vector<std::string> names;
	names.reserve(UINT16_MAX - 1);
	for (unsigned i = 0; i < UINT16_MAX - 1; ++i) names.push_back("file" + std::to_string(i));
	const auto result = validate(wrappedZip(names));
	require(result.accepted, "many ordinary short filenames were rejected");
	requireBounded(result);
}

void testConflictsStillRejected() {
	for (const auto& names : {
		std::vector<std::string>{"Dir/File.txt", "dir/file.TXT"},
		std::vector<std::string>{"node", "node/child"},
		std::vector<std::string>{"node/child", "node"}}) {
		const auto result = validate(wrappedZip(names));
		require(!result.accepted && result.error.starts_with("Archive Security Error:")
			&& result.error != LIMIT_ERROR, "path conflict was not rejected correctly");
	}
	const auto result = validate(wrappedZip({"node/child", "node/"}));
	require(result.accepted, "explicit directory following its child was rejected");
}

} // namespace

int main() {
	unsigned failures = 0;
	const auto run = [&failures](std::string_view label, const auto& test) {
		try {
			test();
			std::cout << "PASS: " << label << '\n';
		} catch (const std::exception& error) {
			++failures;
			std::cerr << "FAIL: " << label << ": " << error.what() << '\n';
		}
	};
	run("long distinct path limit", testLongDistinctPaths);
	run("exact file path budget", [] { testBoundary(false, false); });
	run("file path budget plus one", [] { testBoundary(false, true); });
	run("exact directory path budget", [] { testBoundary(true, false); });
	run("directory path budget plus one", [] { testBoundary(true, true); });
	run("shared prefix path budget", testSharedPrefixBudget);
	run("maximum-length nested path", testMaximumLengthPath);
	run("65,534 short filenames", testMaximumEntryCount);
	run("path conflict detection", testConflictsStillRejected);
	return failures ? 1 : 0;
}
