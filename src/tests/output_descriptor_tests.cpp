// Output descriptor regression tests. Linux / GNU linker wrappers inject
// filesystem races and I/O failures without adding production test hooks.
//
/*
g++ -std=c++23 -O0 -g -I. tests/output_descriptor_tests.cpp file_io.cpp \
  binary_utils.cpp -Wl,--wrap=write,--wrap=fchmod,--wrap=fsync,--wrap=close \
  -o /tmp/pdvzip-output-descriptor-tests
*/

#include "pdvzip.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" int __real_fchmod(int, mode_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);

namespace {

enum class Injection {
	none,
	short_write_and_eintr,
	write_error,
	zero_write,
	fchmod_eintr,
	fchmod_error,
	fsync_eintr,
	fsync_error,
	close_error,
	close_eintr,
	replace_at_write,
	replace_at_sync,
	replace_at_failed_sync
};

struct Interception {
	Injection injection{Injection::none};
	bool active{};
	bool injected{};
	int output_fd{-1};
	unsigned write_calls{};
	unsigned sync_calls{};
	unsigned close_calls{};
	bool synced_original{};
	fs::path original_path;
	fs::path saved_path;
	fs::path sentinel_path;
};

Interception interception;
int failures{};

void expect(bool condition, std::string_view label) {
	if (!condition) {
		std::println(stderr, "FAIL: {}", label);
		++failures;
	}
}

fs::path descriptorPath(int fd) {
	std::array<char, 4096> path{};
	const std::string descriptor = "/proc/self/fd/" + std::to_string(fd);
	const ssize_t size = ::readlink(descriptor.c_str(), path.data(), path.size());
	if (size < 0 || static_cast<std::size_t>(size) == path.size()) {
		throw std::runtime_error("Cannot resolve test descriptor");
	}
	return std::string(path.data(), static_cast<std::size_t>(size));
}

bool isOutputDescriptor(int fd) {
	if (!interception.active || fd <= STDERR_FILENO) {
		return false;
	}
	if (fd == interception.output_fd) {
		return true;
	}
	const fs::path path = descriptorPath(fd);
	const std::string name = path.filename().string();
	if (!name.starts_with("pzip_") && !name.starts_with("pjar_")) {
		return false;
	}
	interception.output_fd = fd;
	interception.original_path = path;
	return true;
}

class TemporaryDirectory {
public:
	TemporaryDirectory() : previous_(fs::current_path()) {
		std::string pattern = (fs::temp_directory_path() / "pdvzip-descriptor-XXXXXX").string();
		const char* directory = ::mkdtemp(pattern.data());
		if (!directory) {
			throw std::runtime_error("Cannot create test directory");
		}
		path_ = directory;
		fs::current_path(path_);
	}

	~TemporaryDirectory() {
		interception.active = false;
		std::error_code ec;
		fs::current_path(previous_, ec);
		fs::remove_all(path_, ec);
	}

private:
	fs::path previous_;
	fs::path path_;
};

class ScopedUmask {
public:
	ScopedUmask() : previous_(::umask(0077)) {}
	~ScopedUmask() { ::umask(previous_); }
private:
	mode_t previous_;
};

vBytes readBytes(const fs::path& path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("Cannot read test output");
	}
	return vBytes(std::istreambuf_iterator<char>(stream), {});
}

mode_t permissions(const fs::path& path) {
	struct stat status{};
	if (::stat(path.c_str(), &status) != 0) {
		throw std::runtime_error("Cannot stat test output");
	}
	return status.st_mode & 07777;
}

std::vector<fs::path> outputPaths() {
	std::vector<fs::path> paths;
	for (const auto& entry : fs::directory_iterator(fs::current_path())) {
		const std::string name = entry.path().filename().string();
		if (name.starts_with("pzip_") || name.starts_with("pjar_")) {
			paths.push_back(entry.path());
		}
	}
	return paths;
}

// Includes zero and high bytes, and spans many injected short writes.
vBytes payload() {
	vBytes bytes(1027);
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		bytes[i] = static_cast<Byte>((i * 31) & 0xff);
	}
	return bytes;
}

void testSuccessfulOutput(bool is_zip, Injection injection) {
	TemporaryDirectory directory;
	ScopedUmask mask;
	interception = {};
	interception.injection = injection;
	interception.active = true;
	const vBytes bytes = payload();
	try {
		writePolyglotFile(bytes, is_zip);
	}
	catch (const std::exception& error) {
		expect(false, std::string("successful output threw: ") + error.what());
	}
	interception.active = false;
	const auto paths = outputPaths();
	expect(paths.size() == 1, "successful write creates exactly one output");
	if (paths.size() == 1) {
		expect(paths[0].filename().string().starts_with(is_zip ? "pzip_" : "pjar_"),
			"output uses the requested ZIP/JAR prefix");
		expect(readBytes(paths[0]) == bytes, "all output bytes survive interrupted and short writes");
		expect(permissions(paths[0]) == (injection == Injection::fchmod_error ? 0600 : 0755),
			"output permissions respect finalization, including warning-only chmod failure");
	}
	if (injection != Injection::none) {
		expect(interception.injected, "requested success-path fault was exercised");
	}
	expect(interception.sync_calls >= 1, "successful output was fsynced");
	if (injection == Injection::short_write_and_eintr) {
		expect(interception.write_calls > 2, "short writes were completed through repeated writes");
	}
}

void testFailure(Injection injection) {
	TemporaryDirectory directory;
	interception = {};
	interception.injection = injection;
	interception.active = true;
	bool threw{};
	try {
		writePolyglotFile(payload(), true);
	}
	catch (const std::runtime_error&) {
		threw = true;
	}
	interception.active = false;
	expect(interception.injected, "requested output failure was exercised");
	expect(threw, "output failure is reported to the caller");
	expect(outputPaths().empty(), "failed output is removed");
	if (injection == Injection::close_error || injection == Injection::close_eintr) {
		expect(interception.close_calls == 1, "failed close is never retried on a released descriptor");
	}
}

void testPathReplacement(bool fail_sync, bool at_write = false) {
	TemporaryDirectory directory;
	interception = {};
	interception.sentinel_path = fs::current_path() / "private.txt";
	interception.saved_path = fs::current_path() / "original-output.saved";
	const vBytes private_bytes{'p', 'r', 'i', 'v', 'a', 't', 'e'};
	{
		std::ofstream sentinel(interception.sentinel_path, std::ios::binary);
		sentinel.write(reinterpret_cast<const char*>(private_bytes.data()), private_bytes.size());
	}
	if (::chmod(interception.sentinel_path.c_str(), 0600) != 0) {
		throw std::runtime_error("Cannot protect test sentinel");
	}
	interception.injection = at_write ? Injection::replace_at_write
		: fail_sync ? Injection::replace_at_failed_sync : Injection::replace_at_sync;
	interception.active = true;
	const vBytes bytes = payload();
	bool threw{};
	try {
		writePolyglotFile(bytes, true);
	}
	catch (const std::runtime_error&) {
		threw = true;
	}
	interception.active = false;
	expect(threw == fail_sync, "replacement preserves the expected sync result");
	expect(interception.injected, "output pathname was replaced during finalization");
	expect(interception.synced_original, "fsync receives the original output inode after replacement");
	expect(permissions(interception.sentinel_path) == 0600, "substituted symlink cannot change private target permissions");
	expect(readBytes(interception.sentinel_path) == private_bytes, "private target bytes are unchanged");
	expect(fs::is_symlink(interception.original_path), "cleanup preserves a replacement it did not create");
	expect(readBytes(interception.saved_path) == bytes, "original output retains all requested bytes");
	expect(permissions(interception.saved_path) == 0755, "permission change applies to the original output inode");
}

} // namespace

extern "C" ssize_t __wrap_write(int fd, const void* buffer, size_t count) {
	if (!isOutputDescriptor(fd)) {
		return __real_write(fd, buffer, count);
	}
	++interception.write_calls;
	if (interception.injection == Injection::replace_at_write && !interception.injected) {
		const ssize_t result = __real_write(fd, buffer, count);
		fs::rename(interception.original_path, interception.saved_path);
		fs::create_symlink(interception.sentinel_path, interception.original_path);
		interception.injected = true;
		return result;
	}
	if (interception.injection == Injection::short_write_and_eintr) {
		if (!interception.injected) {
			interception.injected = true;
			errno = EINTR;
			return -1;
		}
		return __real_write(fd, buffer, std::min<std::size_t>(count, 7));
	}
	if (interception.injection == Injection::write_error || interception.injection == Injection::zero_write) {
		if (interception.write_calls == 1) {
			return __real_write(fd, buffer, std::min<std::size_t>(count, 7));
		}
		interception.injected = true;
		errno = EIO;
		return interception.injection == Injection::zero_write ? 0 : -1;
	}
	return __real_write(fd, buffer, count);
}

extern "C" int __wrap_fchmod(int fd, mode_t mode) {
	if (isOutputDescriptor(fd) && !interception.injected
		&& (interception.injection == Injection::fchmod_eintr || interception.injection == Injection::fchmod_error)) {
		interception.injected = true;
		errno = interception.injection == Injection::fchmod_eintr ? EINTR : EACCES;
		return -1;
	}
	return __real_fchmod(fd, mode);
}

extern "C" int __wrap_fsync(int fd) {
	if (!isOutputDescriptor(fd)) {
		return __real_fsync(fd);
	}
	++interception.sync_calls;
	if (!interception.injected && (interception.injection == Injection::replace_at_sync
		|| interception.injection == Injection::replace_at_failed_sync)) {
		fs::rename(interception.original_path, interception.saved_path);
		fs::create_symlink(interception.sentinel_path, interception.original_path);
		interception.injected = true;
	}
	if (interception.injection == Injection::replace_at_write
		|| interception.injection == Injection::replace_at_sync
		|| interception.injection == Injection::replace_at_failed_sync) {
		struct stat descriptor_status{}, original_status{}, sentinel_status{};
		if (::fstat(fd, &descriptor_status) != 0
			|| ::stat(interception.saved_path.c_str(), &original_status) != 0
			|| ::stat(interception.sentinel_path.c_str(), &sentinel_status) != 0) {
			throw std::runtime_error("Cannot inspect replacement race in test");
		}
		interception.synced_original = descriptor_status.st_dev == original_status.st_dev
			&& descriptor_status.st_ino == original_status.st_ino
			&& (descriptor_status.st_dev != sentinel_status.st_dev || descriptor_status.st_ino != sentinel_status.st_ino);
	}
	if ((!interception.injected && (interception.injection == Injection::fsync_eintr
		|| interception.injection == Injection::fsync_error))
		|| interception.injection == Injection::replace_at_failed_sync) {
		interception.injected = true;
		errno = interception.injection == Injection::fsync_eintr ? EINTR : EIO;
		return -1;
	}
	return __real_fsync(fd);
}

extern "C" int __wrap_close(int fd) {
	if (interception.active && fd == interception.output_fd) {
		++interception.close_calls;
		if (!interception.injected && (interception.injection == Injection::close_error
			|| interception.injection == Injection::close_eintr)) {
			interception.injected = true;
			// Linux releases the descriptor even when close reports these errors.
			(void)__real_close(fd);
			errno = interception.injection == Injection::close_eintr ? EINTR : EIO;
			return -1;
		}
	}
	return __real_close(fd);
}

int main(int argc, char** argv) {
	try {
		// Allows demonstrating the original vulnerability without expecting the
		// ofstream implementation to use the new write/fchmod wrappers.
		if (argc == 2 && std::string_view(argv[1]) == "--race-only") {
			testPathReplacement(false);
		}
		else {
			for (bool is_zip : {true, false}) {
				testSuccessfulOutput(is_zip, Injection::none);
			}
			for (Injection injection : {Injection::short_write_and_eintr, Injection::fchmod_eintr,
				Injection::fchmod_error, Injection::fsync_eintr}) {
				testSuccessfulOutput(true, injection);
			}
			for (Injection injection : {Injection::write_error, Injection::zero_write,
				Injection::fsync_error, Injection::close_error, Injection::close_eintr}) {
				testFailure(injection);
			}
			testPathReplacement(false);
			testPathReplacement(false, true);
			testPathReplacement(true);
		}
	}
	catch (const std::exception& error) {
		interception.active = false;
		expect(false, std::string("test fixture failed: ") + error.what());
	}
	std::println("Output descriptor regression tests: {} failure(s).", failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
