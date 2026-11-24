// Output descriptor regression tests. Linux / GNU linker wrappers inject
// filesystem races and I/O failures without adding production test hooks.
//
/*
g++ -std=c++23 -O0 -g -I. tests/output_descriptor_tests.cpp file_io.cpp \
  binary_utils.cpp \
  -Wl,--wrap=open,--wrap=write,--wrap=fchmod,--wrap=fsync,--wrap=close,--wrap=linkat,--wrap=renameat2,--wrap=link \
  -o /tmp/pdvzip-output-descriptor-tests
*/

#include "pdvzip.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int __real_open(const char*, int, ...);
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" int __real_fchmod(int, mode_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_linkat(int, const char*, int, const char*, int);
extern "C" int __real_renameat2(int, const char*, int, const char*, unsigned int);
extern "C" int __real_link(const char*, const char*);

namespace {

// How the output is staged before it is published.
enum class Staging {
	anonymous,        // O_TMPFILE, published with linkat
	named,            // hidden temporary, published with renameat2(RENAME_NOREPLACE)
	named_link_only   // hidden temporary on a filesystem without RENAME_NOREPLACE
};

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
	publish_error,
	name_taken_at_publish
};

struct Interception {
	Staging staging{Staging::anonymous};
	Injection injection{Injection::none};
	bool active{};
	bool injected{};
	int output_fd{-1};
	unsigned write_calls{};
	unsigned sync_calls{};
	unsigned close_calls{};
	unsigned publish_calls{};
	bool visible_before_publish{};
	bool saw_output_fd{};
	fs::path sentinel_path;
	fs::path planted_path;
};

Interception interception;
int failures{};

void expect(bool condition, std::string_view label) {
	if (!condition) {
		std::println(stderr, "FAIL: {}", label);
		++failures;
	}
}

std::string_view stagingName(Staging staging) {
	switch (staging) {
	case Staging::anonymous: return "O_TMPFILE";
	case Staging::named: return "named temporary";
	case Staging::named_link_only: return "named temporary, link fallback";
	}
	return "?";
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

// Regular pzip_/pjar_ files in the working directory (planted symlinks excluded).
std::vector<fs::path> outputPaths() {
	std::vector<fs::path> paths;
	for (const auto& entry : fs::directory_iterator(fs::current_path())) {
		const std::string name = entry.path().filename().string();
		if ((name.starts_with("pzip_") || name.starts_with("pjar_")) && !entry.is_symlink()) {
			paths.push_back(entry.path());
		}
	}
	return paths;
}

bool hasStagingLeftover() {
	return std::ranges::any_of(fs::directory_iterator(fs::current_path()), [](const auto& entry) {
		return entry.path().filename().string().starts_with(".pdvzip_tmp_");
	});
}

// Includes zero and high bytes, and spans many injected short writes.
vBytes payload() {
	vBytes bytes(1027);
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		bytes[i] = static_cast<Byte>((i * 31) & 0xff);
	}
	return bytes;
}

void begin(Staging staging, Injection injection) {
	interception = {};
	interception.staging = staging;
	interception.injection = injection;
	interception.active = true;
}

void testSuccessfulOutput(Staging staging, bool is_zip, Injection injection) {
	TemporaryDirectory directory;
	ScopedUmask mask;
	begin(staging, injection);
	const vBytes bytes = payload();
	try {
		writePolyglotFile(bytes, is_zip);
	}
	catch (const std::exception& error) {
		expect(false, std::format("[{}] successful output threw: {}", stagingName(staging), error.what()));
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
	expect(interception.saw_output_fd, "staged output descriptor was observed");
	expect(!interception.visible_before_publish, "no output name exists before publication");
	expect(!hasStagingLeftover(), "no hidden staging file is left behind");
	expect(interception.sync_calls >= 1, "successful output was fsynced");
	if (injection == Injection::short_write_and_eintr) {
		expect(interception.write_calls > 2, "short writes were completed through repeated writes");
	}
}

void testFailure(Staging staging, Injection injection) {
	TemporaryDirectory directory;
	begin(staging, injection);
	bool threw{};
	try {
		writePolyglotFile(payload(), true);
	}
	catch (const std::runtime_error&) {
		threw = true;
	}
	interception.active = false;
	expect(interception.injected, std::format("[{}] requested output failure was exercised", stagingName(staging)));
	expect(threw, "output failure is reported to the caller");
	expect(outputPaths().empty(), "failed output is removed");
	expect(!hasStagingLeftover(), "failed output leaves no hidden staging file");
	if (injection == Injection::close_error || injection == Injection::close_eintr) {
		expect(interception.close_calls == 1, "failed close is never retried on a released descriptor");
	}
}

// Another process takes the chosen name -- here with a symlink to a private
// file -- between the name being picked and the file being published. The
// publish must not follow or replace it, and must move on to another name.
void testNameTakenAtPublish(Staging staging) {
	TemporaryDirectory directory;
	begin(staging, Injection::name_taken_at_publish);
	interception.sentinel_path = fs::current_path() / "private.txt";
	const vBytes private_bytes{'p', 'r', 'i', 'v', 'a', 't', 'e'};
	{
		std::ofstream sentinel(interception.sentinel_path, std::ios::binary);
		sentinel.write(reinterpret_cast<const char*>(private_bytes.data()), private_bytes.size());
	}
	if (::chmod(interception.sentinel_path.c_str(), 0600) != 0) {
		throw std::runtime_error("Cannot protect test sentinel");
	}
	const vBytes bytes = payload();
	bool threw{};
	try {
		writePolyglotFile(bytes, true);
	}
	catch (const std::runtime_error&) {
		threw = true;
	}
	interception.active = false;
	expect(!threw, std::format("[{}] a taken name is retried, not fatal", stagingName(staging)));
	expect(interception.injected, "output name was taken during publication");
	expect(interception.publish_calls >= 2, "publication moved on to another name");
	expect(fs::is_symlink(interception.planted_path), "the entry that took the name is not replaced");
	expect(permissions(interception.sentinel_path) == 0600, "planted symlink cannot change private target permissions");
	expect(readBytes(interception.sentinel_path) == private_bytes, "private target bytes are unchanged");
	const auto paths = outputPaths();
	expect(paths.size() == 1, "exactly one output is published under a free name");
	if (paths.size() == 1) {
		expect(readBytes(paths[0]) == bytes, "published output retains all requested bytes");
		expect(permissions(paths[0]) == 0755, "published output is executable");
	}
	expect(!hasStagingLeftover(), "no hidden staging file is left behind");
}

bool isOutputDescriptor(int fd) {
	return interception.active && fd > STDERR_FILENO && fd == interception.output_fd;
}

// Shared by the three publish primitives: plant a symlink at the target name
// first, or fail outright, when the test asks for it.
bool interceptPublish(const char* name, int& result) {
	if (!interception.active) {
		return false;
	}
	++interception.publish_calls;
	if (interception.injection == Injection::name_taken_at_publish && !interception.injected) {
		interception.planted_path = fs::current_path() / name;
		fs::create_symlink(interception.sentinel_path, interception.planted_path);
		interception.injected = true;
	}
	if (interception.injection == Injection::publish_error && !interception.injected) {
		interception.injected = true;
		errno = EIO;
		result = -1;
		return true;
	}
	return false;
}

} // namespace

extern "C" int __wrap_open(const char* path, int flags, ...) {
	mode_t mode = 0;
	if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list args;
		va_start(args, flags);
		mode = static_cast<mode_t>(va_arg(args, int));
		va_end(args);
	}
	const bool is_tmpfile = (flags & O_TMPFILE) == O_TMPFILE;
	if (interception.active && is_tmpfile && interception.staging != Staging::anonymous) {
		errno = EOPNOTSUPP; // this "filesystem" has no O_TMPFILE
		return -1;
	}
	const int fd = __real_open(path, flags, mode);
	if (interception.active && fd >= 0
		&& (is_tmpfile || std::string_view(path).starts_with(".pdvzip_tmp_"))) {
		interception.output_fd = fd;
		interception.saw_output_fd = true;
	}
	return fd;
}

extern "C" ssize_t __wrap_write(int fd, const void* buffer, size_t count) {
	if (!isOutputDescriptor(fd)) {
		return __real_write(fd, buffer, count);
	}
	++interception.write_calls;
	if (!outputPaths().empty()) {
		interception.visible_before_publish = true;
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
	if (!outputPaths().empty()) {
		interception.visible_before_publish = true;
	}
	if (!interception.injected && (interception.injection == Injection::fsync_eintr
		|| interception.injection == Injection::fsync_error)) {
		interception.injected = true;
		errno = interception.injection == Injection::fsync_eintr ? EINTR : EIO;
		return -1;
	}
	return __real_fsync(fd);
}

extern "C" int __wrap_close(int fd) {
	if (isOutputDescriptor(fd)) {
		++interception.close_calls;
		// The number may be reused (e.g. by the directory fsync) once closed.
		interception.output_fd = -1;
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

extern "C" int __wrap_linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags) {
	int result = 0;
	if (interceptPublish(newpath, result)) {
		return result;
	}
	return __real_linkat(olddirfd, oldpath, newdirfd, newpath, flags);
}

extern "C" int __wrap_renameat2(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, unsigned int flags) {
	if (interception.active && interception.staging == Staging::named_link_only) {
		errno = EINVAL; // RENAME_NOREPLACE unsupported here
		return -1;
	}
	int result = 0;
	if (interceptPublish(newpath, result)) {
		return result;
	}
	return __real_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
}

extern "C" int __wrap_link(const char* oldpath, const char* newpath) {
	int result = 0;
	if (interceptPublish(newpath, result)) {
		return result;
	}
	return __real_link(oldpath, newpath);
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--race-only") {
			for (Staging staging : {Staging::anonymous, Staging::named, Staging::named_link_only}) {
				testNameTakenAtPublish(staging);
			}
		}
		else {
			for (Staging staging : {Staging::anonymous, Staging::named, Staging::named_link_only}) {
				for (bool is_zip : {true, false}) {
					testSuccessfulOutput(staging, is_zip, Injection::none);
				}
				for (Injection injection : {Injection::short_write_and_eintr, Injection::fchmod_eintr,
					Injection::fchmod_error, Injection::fsync_eintr}) {
					testSuccessfulOutput(staging, true, injection);
				}
				for (Injection injection : {Injection::write_error, Injection::zero_write,
					Injection::fsync_error, Injection::close_error, Injection::close_eintr,
					Injection::publish_error}) {
					testFailure(staging, injection);
				}
				testNameTakenAtPublish(staging);
			}
		}
	}
	catch (const std::exception& error) {
		interception.active = false;
		expect(false, std::string("test fixture failed: ") + error.what());
	}
	std::println("Output descriptor regression tests: {} failure(s).", failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
