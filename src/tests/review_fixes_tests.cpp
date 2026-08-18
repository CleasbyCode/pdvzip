// Regression tests for the four review findings: reserved Windows device
// names, Linux pwsh -File, leftover output on write failure, and version
// string consistency.
//
// g++ -std=c++23 -O0 -g -I.. -DLODEPNG_NO_COMPILE_DISK \
//   -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CRC \
//   review_fixes_tests.cpp ../archive_analysis.cpp ../binary_utils.cpp \
//   ../crc32.cpp ../script_text_builder.cpp ../script_builder.cpp \
//   ../file_io.cpp ../display_info.cpp ../program_args.cpp ../user_input.cpp \
//   ../image_processing.cpp ../image_resize.cpp ../polyglot_assembly.cpp \
//   ../lodepng/lodepng.cpp -lz -o review_fixes_tests

#include "pdvzip.h"
#include "script_builder_internal.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

#include <zlib.h>

namespace {

int g_failures = 0;

void expectTrue(bool condition, std::string_view label) {
	if (!condition) {
		std::println(std::cerr, "FAIL: {}", label);
		++g_failures;
	}
}

void expectContains(std::string_view actual, std::string_view expected, std::string_view label) {
	if (actual.find(expected) == std::string_view::npos) {
		std::println(std::cerr, "FAIL: {} (missing \"{}\")", label, expected);
		++g_failures;
	}
}

void expectThrows(const auto& fn, std::string_view label) {
	try {
		fn();
		std::println(std::cerr, "FAIL: {} (expected exception)", label);
		++g_failures;
	}
	catch (const std::exception&) {
	}
	catch (...) {
		std::println(std::cerr, "FAIL: {} (non-std exception)", label);
		++g_failures;
	}
}

void appendLe16(vBytes& out, uint16_t value) {
	out.push_back(static_cast<Byte>(value & 0xFF));
	out.push_back(static_cast<Byte>((value >> 8) & 0xFF));
}

void appendLe32(vBytes& out, uint32_t value) {
	out.push_back(static_cast<Byte>(value & 0xFF));
	out.push_back(static_cast<Byte>((value >> 8) & 0xFF));
	out.push_back(static_cast<Byte>((value >> 16) & 0xFF));
	out.push_back(static_cast<Byte>((value >> 24) & 0xFF));
}

void appendBytes(vBytes& out, std::string_view s) {
	out.insert(out.end(), s.begin(), s.end());
}

vBytes makeWrappedSingleFileZip(std::string_view entry_name, std::string_view payload) {
	const uint32_t crc = static_cast<uint32_t>(::crc32(
		::crc32(0L, Z_NULL, 0),
		reinterpret_cast<const Bytef*>(payload.data()),
		static_cast<uInt>(payload.size())));
	const uint32_t comp_size = static_cast<uint32_t>(payload.size());
	const uint32_t uncomp_size = comp_size;
	const uint16_t name_len = static_cast<uint16_t>(entry_name.size());

	vBytes local;
	appendLe32(local, ZIP_LOCAL_FILE_HEADER_SIGNATURE);
	appendLe16(local, 20);
	appendLe16(local, 0);
	appendLe16(local, 0);
	appendLe16(local, 0);
	appendLe16(local, 0);
	appendLe32(local, crc);
	appendLe32(local, comp_size);
	appendLe32(local, uncomp_size);
	appendLe16(local, name_len);
	appendLe16(local, 0);
	appendBytes(local, entry_name);
	appendBytes(local, payload);

	vBytes central;
	appendLe32(central, ZIP_CENTRAL_DIRECTORY_SIGNATURE);
	appendLe16(central, 0x0314);
	appendLe16(central, 20);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe32(central, crc);
	appendLe32(central, comp_size);
	appendLe32(central, uncomp_size);
	appendLe16(central, name_len);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe32(central, static_cast<uint32_t>(0100644) << 16);
	appendLe32(central, 0);
	appendBytes(central, entry_name);

	vBytes eocd;
	appendLe32(eocd, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE);
	appendLe16(eocd, 0);
	appendLe16(eocd, 0);
	appendLe16(eocd, 1);
	appendLe16(eocd, 1);
	appendLe32(eocd, static_cast<uint32_t>(central.size()));
	appendLe32(eocd, static_cast<uint32_t>(local.size()));
	appendLe16(eocd, 0);

	vBytes zip;
	zip.insert(zip.end(), local.begin(), local.end());
	zip.insert(zip.end(), central.begin(), central.end());
	zip.insert(zip.end(), eocd.begin(), eocd.end());

	vBytes wrapped(8 + zip.size() + 4, 0);
	wrapped[4] = 'I';
	wrapped[5] = 'D';
	wrapped[6] = 'A';
	wrapped[7] = 'T';
	std::copy(zip.begin(), zip.end(), wrapped.begin() + 8);
	writeValueAt(wrapped, 0, zip.size(), 4);
	return wrapped;
}

void testWindowsDeviceNamesAreRejected() {
	const vBytes safe = makeWrappedSingleFileZip("docs/readme.txt", "hi");
	try {
		validateArchiveEntryPaths(safe);
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "FAIL: safe path rejected: {}", e.what());
		++g_failures;
	}

	const std::array<std::string_view, 6> reserved = {
		"CONIN$",
		"CONOUT$.txt",
		"CLOCK$",
		"conin$",
		"ConOut$.log",
		"clock$.dat",
	};
	for (const std::string_view name : reserved) {
		const vBytes bad = makeWrappedSingleFileZip(name, "x");
		expectThrows([&] {
			validateArchiveEntryPaths(bad);
		}, std::format("reject Windows device name \"{}\"", name));
	}
}

void testLinuxPowershellUsesFileFlag() {
	const std::string script = script_builder_internal::buildScriptText(
		FileType::POWERSHELL, "app.ps1", UserArguments{});
	expectContains(script, R"(pwsh -File "$ITEM")",
		"Linux PowerShell template invokes pwsh with -File");
	expectTrue(script.find(R"(pwsh "$ITEM")") == std::string::npos,
		"Linux PowerShell template does not invoke pwsh without -File");
}

void testInfoBannerUsesSharedVersion() {
	const fs::path capture_path = fs::temp_directory_path()
		/ std::format("pdvzip-info-{}.txt", ::getpid());
	const int saved_stdout = ::dup(STDOUT_FILENO);
	if (saved_stdout < 0) {
		std::println(std::cerr, "FAIL: dup(STDOUT_FILENO) failed");
		++g_failures;
		return;
	}

	const int capture_fd = ::open(capture_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (capture_fd < 0) {
		::close(saved_stdout);
		std::println(std::cerr, "FAIL: open() for info-banner capture failed");
		++g_failures;
		return;
	}

	std::fflush(stdout);
	if (::dup2(capture_fd, STDOUT_FILENO) < 0) {
		::close(capture_fd);
		::close(saved_stdout);
		std::println(std::cerr, "FAIL: dup2() for info-banner capture failed");
		++g_failures;
		return;
	}

	displayInfo();
	std::fflush(stdout);

	::dup2(saved_stdout, STDOUT_FILENO);
	::close(saved_stdout);

	if (::lseek(capture_fd, 0, SEEK_SET) < 0) {
		::close(capture_fd);
		fs::remove(capture_path);
		std::println(std::cerr, "FAIL: lseek() for info-banner capture failed");
		++g_failures;
		return;
	}

	std::string banner;
	char buf[4096];
	for (;;) {
		const ssize_t n = ::read(capture_fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (n == 0) {
			break;
		}
		banner.append(buf, static_cast<std::size_t>(n));
	}
	::close(capture_fd);
	fs::remove(capture_path);

	expectContains(banner, std::format("PDVZIP v{}", PDVZIP_VERSION),
		"info banner uses PDVZIP_VERSION");
	expectTrue(banner.find("v4.7") == std::string::npos,
		"info banner does not advertise the previous version");
}

bool directoryHasPolyglotOutput(const fs::path& dir) {
	for (const auto& entry : fs::directory_iterator(dir)) {
		const auto name = entry.path().filename().string();
		if (name.starts_with("pzip_") || name.starts_with("pjar_")) {
			return true;
		}
	}
	return false;
}

void testWriteFailureRemovesPartialFile() {
	const fs::path tmp = fs::temp_directory_path()
		/ std::format("pdvzip-write-test-{}", ::getpid());
	fs::create_directories(tmp);
	const fs::path previous = fs::current_path();
	fs::current_path(tmp);

	struct rlimit old_lim{};
	if (::getrlimit(RLIMIT_FSIZE, &old_lim) != 0) {
		fs::current_path(previous);
		fs::remove_all(tmp);
		std::println(std::cerr, "FAIL: getrlimit(RLIMIT_FSIZE) failed");
		++g_failures;
		return;
	}

	struct sigaction old_sa{};
	struct sigaction ign{};
	std::memset(&ign, 0, sizeof(ign));
	ign.sa_handler = SIG_IGN;
	if (::sigaction(SIGXFSZ, &ign, &old_sa) != 0) {
		fs::current_path(previous);
		fs::remove_all(tmp);
		std::println(std::cerr, "FAIL: sigaction(SIGXFSZ) failed");
		++g_failures;
		return;
	}

	struct rlimit tiny = old_lim;
	tiny.rlim_cur = 1;
	if (::setrlimit(RLIMIT_FSIZE, &tiny) != 0) {
		::sigaction(SIGXFSZ, &old_sa, nullptr);
		fs::current_path(previous);
		fs::remove_all(tmp);
		std::println(std::cerr, "FAIL: setrlimit(RLIMIT_FSIZE) failed");
		++g_failures;
		return;
	}

	bool threw = false;
	try {
		writePolyglotFile(vBytes(64 * 1024, Byte{0x41}), true);
	}
	catch (const std::exception&) {
		threw = true;
	}

	const bool leftover = directoryHasPolyglotOutput(tmp);

	::setrlimit(RLIMIT_FSIZE, &old_lim);
	::sigaction(SIGXFSZ, &old_sa, nullptr);
	fs::current_path(previous);
	fs::remove_all(tmp);

	expectTrue(threw, "writePolyglotFile throws when the write cannot complete");
	expectTrue(!leftover, "writePolyglotFile removes the partial output file");
}

} // namespace

int main() {
	try {
		testWindowsDeviceNamesAreRejected();
		testLinuxPowershellUsesFileFlag();
		testInfoBannerUsesSharedVersion();
		testWriteFailureRemovesPartialFile();
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "Unhandled exception: {}", e.what());
		return 1;
	}

	if (g_failures != 0) {
		std::println(std::cerr, "\n{} test failure(s).", g_failures);
		return 1;
	}

	std::println("All review-fix tests passed.");
	return 0;
}
