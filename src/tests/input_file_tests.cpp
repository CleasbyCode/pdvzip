// Input descriptor regression tests. FIFO reads run in alarm-bounded child
// processes so a blocking open fails the test without hanging the suite.
/*
g++ -std=c++23 -O0 -g -I. tests/input_file_tests.cpp file_io.cpp \
  binary_utils.cpp -o /tmp/pdvzip-input-file-tests
*/

#include "pdvzip.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int failures{};

void expect(bool condition, std::string_view label) {
	if (!condition) {
		std::println(stderr, "FAIL: {}", label);
		++failures;
	}
}

class TemporaryDirectory {
public:
	TemporaryDirectory() {
		std::string pattern = (fs::temp_directory_path() / "pdvzip-input-XXXXXX").string();
		const char* directory = ::mkdtemp(pattern.data());
		if (!directory) {
			throw std::runtime_error("Cannot create test directory");
		}
		path = directory;
	}

	~TemporaryDirectory() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}

	fs::path path;
};

struct InputCase {
	std::string_view filename;
	FileTypeCheck type;
};

constexpr std::array INPUT_CASES{
	InputCase{"cover.png", FileTypeCheck::cover_image},
	InputCase{"archive.zip", FileTypeCheck::archive_file},
	InputCase{"archive.jar", FileTypeCheck::archive_file}
};

void writeBytes(const fs::path& path, const vBytes& bytes) {
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
	output.close();
	if (!output) {
		throw std::runtime_error("Cannot write test fixture");
	}
}

bool rejects(const fs::path& path, FileTypeCheck type, std::string_view message) {
	try {
		(void)readFile(path, type);
	}
	catch (const std::runtime_error& error) {
		return std::string_view(error.what()).find(message) != std::string_view::npos;
	}
	return false;
}

std::size_t descriptorCount() {
	std::size_t count = 0;
	for ([[maybe_unused]] const auto& entry : fs::directory_iterator("/proc/self/fd")) {
		++count;
	}
	return count;
}

enum class FifoState { without_writer, with_writer, symlink };

void testFifoInput(const TemporaryDirectory& directory, const InputCase& input, FifoState state) {
	const std::string scenario = state == FifoState::without_writer ? "without-writer"
		: state == FifoState::with_writer ? "with-writer" : "symlink";
	const fs::path path = directory.path / ("fifo-" + scenario + "-" + std::string(input.filename));
	if (::mkfifo(path.c_str(), 0600) != 0) {
		throw std::runtime_error("Cannot create FIFO fixture");
	}
	fs::path read_path = path;
	if (state == FifoState::symlink) {
		read_path = directory.path / ("link-" + std::string(input.filename));
		fs::create_symlink(path, read_path);
	}
	// Keep a writer present without sending bytes. Validation must reject the
	// descriptor before attempting a read, even when a blocking open can finish.
	const int peer = state == FifoState::with_writer
		? ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC) : -1;
	if (state == FifoState::with_writer && peer < 0) {
		throw std::runtime_error("Cannot open FIFO peer");
	}
	const pid_t child = ::fork();
	if (child != 0 && peer >= 0) {
		::close(peer);
	}
	if (child < 0) {
		throw std::runtime_error("Cannot fork FIFO test");
	}
	if (child == 0) {
		// Without a writer, the original blocking open hangs until SIGALRM.
		// The peer descriptor, when present, stays open in this child.
		::signal(SIGALRM, SIG_DFL);
		::alarm(2);
		try {
			const std::size_t before = descriptorCount();
			const std::string_view message = state == FifoState::symlink
				? "Failed to open file" : "not a regular file";
			for (unsigned repeat = 0; repeat < 32; ++repeat) {
				if (!rejects(read_path, input.type, message)) {
					::_exit(1);
				}
			}
			::_exit(descriptorCount() == before ? 0 : 2);
		}
		catch (...) {
			::_exit(3);
		}
	}
	int status = 0;
	pid_t result;
	do {
		result = ::waitpid(child, &status, 0);
	} while (result < 0 && errno == EINTR);
	if (result < 0) {
		throw std::runtime_error("Cannot wait for FIFO test");
	}
	expect(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		std::string(input.filename) + ": FIFO " + scenario + " is rejected promptly without leaking descriptors");
}

void testRegularInput(const TemporaryDirectory& directory, const InputCase& input) {
	const fs::path path = directory.path / input.filename;
	vBytes bytes(input.type == FileTypeCheck::cover_image ? 1027 : 65539);
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<Byte>((index * 31) & 0xff);
	}
	if (input.type == FileTypeCheck::archive_file) {
		constexpr std::array<Byte, 4> signature{0x50, 0x4b, 0x03, 0x04};
		std::copy(signature.begin(), signature.end(), bytes.begin());
	}
	else {
		constexpr std::array<Byte, 8> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
		std::copy(signature.begin(), signature.end(), bytes.begin());
	}
	writeBytes(path, bytes);
	vBytes expected = bytes;
	if (input.type == FileTypeCheck::archive_file) {
		expected = {0, 0, 0, 0, 'I', 'D', 'A', 'T'};
		expected.insert(expected.end(), bytes.begin(), bytes.end());
		expected.insert(expected.end(), 4, 0);
	}
	expect(readFile(path, input.type) == expected,
		std::string(input.filename) + ": regular file preserves binary bytes and archive wrapping");

	const fs::path symlink = directory.path / ("symlink-" + std::string(input.filename));
	fs::create_symlink(path, symlink);
	expect(rejects(symlink, input.type, "Failed to open file"),
		std::string(input.filename) + ": symlink to a regular file is rejected");

	const fs::path folder = directory.path / ("directory-" + std::string(input.filename));
	fs::create_directory(folder);
	expect(rejects(folder, input.type, "not a regular file"),
		std::string(input.filename) + ": directory is rejected");
}

} // namespace

int main() {
	try {
		TemporaryDirectory directory;
		for (const InputCase& input : INPUT_CASES) {
			for (const auto state : {FifoState::without_writer, FifoState::with_writer, FifoState::symlink}) {
				testFifoInput(directory, input, state);
			}
			testRegularInput(directory, input);
		}
		for (const auto type : {FileTypeCheck::cover_image, FileTypeCheck::archive_file}) {
			expect(rejects("/dev/null", type, "not a regular file"),
				"character device is rejected before extension or size validation");
		}
	}
	catch (const std::exception& error) {
		std::println(stderr, "FAIL: unexpected test error: {}", error.what());
		return 1;
	}
	if (failures) {
		std::println(stderr, "{} input-file regression(s) failed", failures);
		return 1;
	}
	std::println("All input-file regression tests passed.");
}
