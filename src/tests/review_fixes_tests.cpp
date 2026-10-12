// Regression tests for archive metadata and reserved Windows device names,
// Linux script/header safety, output creation/write failures, and version consistency.
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
#include <array>
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
#include <fstream>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

struct ZipFixtureOptions {
	uint16_t version_made_by = 0x0314;
	uint32_t external_attributes = static_cast<uint32_t>(0100644) << 16;
	vBytes local_extra;
	vBytes central_extra;
};

vBytes makeWrappedSingleFileZip(std::string_view entry_name, std::string_view payload,
                               const ZipFixtureOptions& options = {}) {
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
	appendLe16(local, static_cast<uint16_t>(options.local_extra.size()));
	appendBytes(local, entry_name);
	local.insert(local.end(), options.local_extra.begin(), options.local_extra.end());
	appendBytes(local, payload);

	vBytes central;
	appendLe32(central, ZIP_CENTRAL_DIRECTORY_SIGNATURE);
	appendLe16(central, options.version_made_by);
	appendLe16(central, 20);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe32(central, crc);
	appendLe32(central, comp_size);
	appendLe32(central, uncomp_size);
	appendLe16(central, name_len);
	appendLe16(central, static_cast<uint16_t>(options.central_extra.size()));
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe16(central, 0);
	appendLe32(central, options.external_attributes);
	appendLe32(central, 0);
	appendBytes(central, entry_name);
	central.insert(central.end(), options.central_extra.begin(), options.central_extra.end());

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

vBytes makeZipExtraField(uint16_t id, const vBytes& data) {
	vBytes extra;
	appendLe16(extra, id);
	appendLe16(extra, static_cast<uint16_t>(data.size()));
	extra.insert(extra.end(), data.begin(), data.end());
	return extra;
}

void expectArchiveRejected(const vBytes& archive, std::string_view reason, std::string_view label) {
	try {
		(void)analyzeArchive(archive, true);
		expectTrue(false, std::format("{} was accepted", label));
	}
	catch (const std::exception& e) {
		expectContains(e.what(), reason, label);
	}
}

void testArchiveFileTypesCannotHideBehindCreatorHost() {
	// Exercise all possible creator IDs, including FAT, VMS, Atari, BeOS,
	// AtheOS, and unknown hosts. Only type bits change; framing and CRC stay valid.
	constexpr std::array<uint32_t, 5> unsafe_types = {
		0120000, 0010000, 0020000, 0060000, 0140000 // link, FIFO, char/block device, socket
	};
	for (unsigned host = 0; host <= 255; ++host) {
		for (const uint32_t type : unsafe_types) {
			const ZipFixtureOptions options{
				.version_made_by = static_cast<uint16_t>((host << 8) | 20),
				// Owner rw permissions match the DOS bits, as Info-ZIP requires for FAT.
				.external_attributes = ((type | 0666U) << 16) | 0x20U,
				.local_extra = {},
				.central_extra = {}
			};
			const auto archive = makeWrappedSingleFileZip("entry.txt", "target.txt", options);
			expectArchiveRejected(archive,
				type == 0120000 ? "Symlink archive entry" : "Special archive entry",
				std::format("host {} rejects file type {:o}", host, type));
		}
	}
}

void testArchiveFileTypeOverrideExtrasAreRejected() {
	// Well-formed ASi Unix data, including its own CRC, with a symlink mode.
	vBytes asi_body;
	appendLe16(asi_body, 0120777);
	appendLe32(asi_body, 10); // link-name length
	appendLe16(asi_body, 0);  // uid
	appendLe16(asi_body, 0);  // gid
	appendBytes(asi_body, "target.txt");
	vBytes asi_data;
	appendLe32(asi_data, static_cast<uint32_t>(::crc32(0, asi_body.data(),
		static_cast<uInt>(asi_body.size()))));
	asi_data.insert(asi_data.end(), asi_body.begin(), asi_body.end());

	// xl supplies UNIX creator metadata and a symlink external-attribute word.
	vBytes xl_data{0x05};
	appendLe16(xl_data, 0x0314);
	appendLe32(xl_data, static_cast<uint32_t>(0120777) << 16);
	const std::array<vBytes, 2> extras = {
		makeZipExtraField(0x756e, asi_data), makeZipExtraField(0x6c78, xl_data)
	};
	for (const auto& extra : extras) {
		for (const uint32_t attributes : {0U, static_cast<uint32_t>(0100644) << 16}) {
			for (const bool in_local_header : {false, true}) {
				ZipFixtureOptions options;
				options.external_attributes = attributes;
				(in_local_header ? options.local_extra : options.central_extra) = extra;
				expectArchiveRejected(makeWrappedSingleFileZip("entry.txt", "target.txt", options),
					"File-type override extra field",
					std::format("reject extra 0x{:04x} in {} with attributes 0x{:08x}",
						readLe16(extra, 0), in_local_header ? "local header" : "central directory", attributes));
			}
		}
	}
}

void testOrdinaryArchiveMetadataRemainsSupported() {
	vBytes safe_extras = makeZipExtraField(0x5455, vBytes{1, 0, 0, 0, 0}); // UT timestamp
	const vBytes ownership = makeZipExtraField(0x7875, vBytes{1, 1, 0, 1, 0}); // ux uid/gid
	safe_extras.insert(safe_extras.end(), ownership.begin(), ownership.end());
	for (const unsigned host : {0U, 2U, 3U, 5U, 10U, 16U, 19U, 30U}) {
		for (const uint32_t attributes : {0U, 0x10U, 0644U << 16, 0100644U << 16, (0040755U << 16) | 0x10U}) {
			const bool directory = (attributes & 0x10U) != 0;
			const ZipFixtureOptions options{
				.version_made_by = static_cast<uint16_t>((host << 8) | 20),
				.external_attributes = attributes,
				.local_extra = safe_extras,
				.central_extra = safe_extras
			};
			try {
				const auto archive = makeWrappedSingleFileZip(
					directory ? "docs/" : "docs/readme.txt", directory ? "" : "hello", options);
				const auto metadata = analyzeArchive(archive, true);
				expectTrue(metadata.file_type == (directory ? FileType::FOLDER : FileType::UNKNOWN_FILE_TYPE),
					"ordinary archive metadata retains classification");
			}
			catch (const std::exception& e) {
				expectTrue(false, std::format("ordinary host {} metadata rejected: {}", host, e.what()));
			}
		}
	}
	// JAR tools commonly omit all external attributes and use a CAFE marker.
	ZipFixtureOptions jar_options;
	jar_options.version_made_by = 20;
	jar_options.external_attributes = 0;
	jar_options.local_extra = makeZipExtraField(0xcafe, {});
	const auto jar = makeWrappedSingleFileZip("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\r\n\r\n", jar_options);
	expectTrue(analyzeArchive(jar, false).file_type == FileType::JAR,
		"JAR manifest without external mode metadata remains supported");
}

void testArchiveClassificationUsesTheBasename() {
	const auto cases = std::to_array<std::pair<std::string_view, FileType>>({
		{"runner", FileType::LINUX_EXECUTABLE},
		{"pkg/runner", FileType::LINUX_EXECUTABLE},
		{"pkg.v1/runner", FileType::LINUX_EXECUTABLE},
		{"pkg.v1/bin/runner", FileType::LINUX_EXECUTABLE},
		{"a.b/c.d/runner", FileType::LINUX_EXECUTABLE},
		{".hidden/runner", FileType::LINUX_EXECUTABLE},
		{"pkg.v1/my runner", FileType::LINUX_EXECUTABLE},
		{"pkg.v1/x", FileType::LINUX_EXECUTABLE},
		{"pkg.v1/main.PY", FileType::PYTHON},
		{"pkg.v1/run.sh", FileType::BASH_SHELL},
		{"pkg.v1/program.exe", FileType::WINDOWS_EXECUTABLE},
		{"pkg.v1/movie.mp4", FileType::VIDEO_AUDIO},
		{"pkg.v1/file.xyz", FileType::UNKNOWN_FILE_TYPE},
		{"pkg.v1/.profile", FileType::UNKNOWN_FILE_TYPE},
		{"pkg.v1/.py", FileType::PYTHON},
		{"docs/", FileType::FOLDER},
		{"pkg.v1/", FileType::FOLDER},
		{"pkg.v1/docs/", FileType::FOLDER},
		{"pkg.v1/docs.v2/", FileType::FOLDER},
		{".hidden/", FileType::FOLDER}
	});
	for (const auto& [filename, expected] : cases) {
		ZipFixtureOptions options;
		const bool directory = filename.ends_with('/');
		options.external_attributes = (directory ? 0040755U : 0100644U) << 16;
		try {
			const auto metadata = analyzeArchive(
				makeWrappedSingleFileZip(filename, directory ? "" : "x", options), true);
			expectTrue(metadata.file_type == expected,
				std::format("{} is classified using only its basename", filename));
			expectTrue(metadata.first_filename == filename,
				"archive classification retains the complete entry path for the launcher");
		}
		catch (const std::exception& e) {
			expectTrue(false, std::format("classification of {} failed: {}", filename, e.what()));
		}
	}
	ZipFixtureOptions directory_options;
	directory_options.external_attributes = 0040755U << 16;
	expectArchiveRejected(makeWrappedSingleFileZip("pkg.v1/dir./", "", directory_options),
		"Unsafe archive entry path", "dotted parent does not bypass invalid directory validation");
	expectArchiveRejected(makeWrappedSingleFileZip(".py", "x"),
		"Name length of first file", "root filename minimum length remains enforced");
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

void testPosixDoubleQuotedBackslashes() {
	using script_builder_internal::splitPosixArguments;
	const std::vector<std::pair<std::string_view, std::vector<std::string>>> cases = {
		{R"("C:\Users\nick")", {R"(C:\Users\nick)"}},
		{R"("\d+\.\w+\s")", {R"(\d+\.\w+\s)"}},
		{R"("a\ b" "it\'s")", {R"(a\ b)", R"(it\'s)"}},
		{R"("\#\;\&\(\)\{\}\!\%\/\-\_")", {R"(\#\;\&\(\)\{\}\!\%\/\-\_)"}},
		{R"("a\$b\`c\"d\\e")", {R"(a$b`c"d\e)"}},
		{R"("\\q" "\\\q" "\\\\q")", {R"(\q)", R"(\\q)", R"(\\q)"}},
		{R"("a\q"'b\z'c\ d)", {R"(a\qb\zc d)"}},
		{R"('C:\Users\nick' one\ two C:\Users\nick)", {R"(C:\Users\nick)", "one two", "C:Usersnick"}},
		{R"("" '' a""b "path\\")", {"", "", "ab", R"(path\)"}},
		{R"("$HOME" "`printf literal`" "*.txt")", {"$HOME", "`printf literal`", "*.txt"}}
	};
	for (const auto& [input, expected] : cases) {
		expectTrue(splitPosixArguments(input, "Linux arguments") == expected,
			std::format("POSIX argument split preserves expected bytes for {}", input));
	}

	for (const std::string_view input : {R"(unfinished\)", R"("unfinished\)", R"("unfinished\")", R"('unfinished)"}) {
		expectThrows([&] { (void)splitPosixArguments(input, "Linux arguments"); },
			"POSIX argument splitter rejects unfinished escapes or quotes");
	}
	for (const char control : {'\n', '\r', '\t'}) {
		UserArguments arguments;
		arguments.linux_args = std::string("\"a\\") + control + "b\"";
		expectThrows([&] {
			(void)script_builder_internal::buildScriptText(FileType::PYTHON, "main.py", arguments);
		}, "script rendering rejects control characters in quoted arguments");
	}
}

void testExtractionTemplatesFitTheChunkLimit() {
	const std::array<std::pair<FileType, std::string_view>, 10> cases = {{
		{FileType::VIDEO_AUDIO, "movie.mp4"},
		{FileType::PDF, "document.pdf"},
		{FileType::PYTHON, "main.py"},
		{FileType::POWERSHELL, "script.ps1"},
		{FileType::BASH_SHELL, "script.sh"},
		{FileType::WINDOWS_EXECUTABLE, "program.exe"},
		{FileType::UNKNOWN_FILE_TYPE, "readme.txt"},
		{FileType::FOLDER, "docs/"},
		{FileType::LINUX_EXECUTABLE, "program"},
		{FileType::JAR, "program.jar"}
	}};
	const UserArguments arguments{"--flag 'two words'", "--flag \"two words\""};
	for (const auto& [type, filename] : cases) {
		try {
			const auto chunk = buildExtractionScript(type, std::string(filename), arguments);
			expectTrue(chunk.size() <= MAX_SCRIPT_SIZE + CHUNK_FIELDS_COMBINED_LENGTH,
				std::format("{} extraction chunk fits with representative arguments", filename));
		}
		catch (const std::exception& e) {
			expectTrue(false, std::format("{} extraction chunk could not be built: {}", filename, e.what()));
		}
	}
}

void testCommentByteInChunkLengthIsPadded() {
	// The iCCP data includes 16 bytes in addition to the extraction script.
	// Vary only the Linux argument to find a length whose low byte is '#'.
	for (std::size_t argument_length = 1; argument_length <= 256; ++argument_length) {
		const UserArguments arguments{std::string(argument_length, 'a'), "fixed"};
		const auto script = script_builder_internal::buildScriptText(
			FileType::JAR, "program.jar", arguments);
		const std::size_t unpadded_length = script.size() + 16;
		if ((unpadded_length & 0xFF) != 0x23) {
			continue;
		}

		const auto chunk = buildExtractionScript(FileType::JAR, "program.jar", arguments);
		const auto length = readValueAt(chunk, 0, 4);
		expectTrue(length == unpadded_length + 8,
			"iCCP length ending in '#' receives padding");
		expectTrue(chunk[3] == 0x2B,
			"padding replaces the comment byte with a safe length byte");
		expectTrue(length + CHUNK_FIELDS_COMBINED_LENGTH == chunk.size(),
			"padded iCCP length matches the chunk size");
		expectTrue(std::search(chunk.begin(), chunk.end(), script.begin(), script.end()) != chunk.end(),
			"length padding preserves the script text");
		expectTrue(readValueAt(chunk, chunk.size() - 4, 4)
			== ::crc32_z(0, chunk.data() + 4, chunk.size() - 8),
			"padded iCCP has a valid CRC");
		return;
	}
	expectTrue(false, "could not construct an iCCP length ending in '#'");
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

void testOutputNameExhaustionPreservesExistingFiles() {
	const fs::path previous = fs::current_path();
	std::string directory_template = (fs::temp_directory_path()
		/ "pdvzip-name-exhaustion-XXXXXX").string();
	const char* created_directory = ::mkdtemp(directory_template.data());
	if (!created_directory) {
		throw std::runtime_error("Could not create output-name regression directory.");
	}

	struct Cleanup {
		fs::path previous;
		fs::path temporary;
		~Cleanup() {
			std::error_code ec;
			fs::current_path(previous, ec);
			fs::remove_all(temporary, ec);
		}
	} cleanup{previous, fs::path(created_directory)};

	for (const bool is_zip_file : {true, false}) {
		const std::string_view prefix = is_zip_file ? "pzip_" : "pjar_";
		const fs::path directory = cleanup.temporary / prefix;
		fs::create_directory(directory);
		fs::current_path(directory);

		// Occupy the complete name space so every random candidate collides.
		// Empty regular files avoid allocating a data block for every fixture.
		constexpr std::size_t EXPECTED_FILES = 90000;
		for (int number = 10000; number <= 99999; ++number) {
			std::ofstream existing(std::format("{}{}.png", prefix, number),
				std::ios::binary | std::ios::out | std::ios::noreplace);
			if (!existing) {
				throw std::runtime_error("Could not populate output-name regression directory.");
			}
			existing.close();
			if (!existing) {
				throw std::runtime_error("Could not finalize output-name regression fixture.");
			}
		}

		std::string error;
		try {
			writePolyglotFile(vBytes{Byte{0x41}}, is_zip_file);
		}
		catch (const std::exception& e) {
			error = e.what();
		}
		expectTrue(error == "Write File Error: Unable to create a unique output file.",
			std::format("{} exhaustion reports creation failure", prefix));

		std::size_t remaining_files = 0;
		bool unchanged_files = true;
		for (const auto& entry : fs::directory_iterator(directory)) {
			++remaining_files;
			unchanged_files = unchanged_files && entry.is_regular_file() && entry.file_size() == 0;
		}
		expectTrue(remaining_files == EXPECTED_FILES,
			std::format("{} exhaustion preserves every pre-existing file", prefix));
		expectTrue(unchanged_files,
			std::format("{} exhaustion leaves existing file contents unchanged", prefix));

		fs::current_path(cleanup.temporary);
		fs::remove_all(directory);
	}
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
		testArchiveFileTypesCannotHideBehindCreatorHost();
		testArchiveFileTypeOverrideExtrasAreRejected();
		testOrdinaryArchiveMetadataRemainsSupported();
		testArchiveClassificationUsesTheBasename();
		testWindowsDeviceNamesAreRejected();
		testLinuxPowershellUsesFileFlag();
		testPosixDoubleQuotedBackslashes();
		testExtractionTemplatesFitTheChunkLimit();
		testCommentByteInChunkLengthIsPadded();
		testInfoBannerUsesSharedVersion();
		testOutputNameExhaustionPreservesExistingFiles();
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
