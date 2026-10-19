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
#include <span>
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
	uint16_t flags = 0;
	uint32_t external_attributes = static_cast<uint32_t>(0100644) << 16;
	vBytes local_extra;
	vBytes central_extra;
	std::string_view entry_comment;
};

struct ZipFixtureEntry {
	std::string_view name;
	std::string_view payload = "x";
	ZipFixtureOptions options = {};
};

vBytes makeWrappedZip(std::span<const ZipFixtureEntry> entries, std::string_view archive_comment = {}) {
	vBytes local;
	vBytes central;
	for (const ZipFixtureEntry& entry : entries) {
		const ZipFixtureOptions& options = entry.options;
		const uint32_t crc = static_cast<uint32_t>(::crc32(
			::crc32(0L, Z_NULL, 0),
			reinterpret_cast<const Bytef*>(entry.payload.data()),
			static_cast<uInt>(entry.payload.size())));
		const uint32_t size = static_cast<uint32_t>(entry.payload.size());
		const uint16_t name_len = static_cast<uint16_t>(entry.name.size());
		const uint32_t local_offset = static_cast<uint32_t>(local.size());

		appendLe32(local, ZIP_LOCAL_FILE_HEADER_SIGNATURE);
		appendLe16(local, 20);
		appendLe16(local, options.flags);
		appendLe16(local, 0);
		appendLe16(local, 0);
		appendLe16(local, 0);
		appendLe32(local, crc);
		appendLe32(local, size);
		appendLe32(local, size);
		appendLe16(local, name_len);
		appendLe16(local, static_cast<uint16_t>(options.local_extra.size()));
		appendBytes(local, entry.name);
		local.insert(local.end(), options.local_extra.begin(), options.local_extra.end());
		appendBytes(local, entry.payload);

		appendLe32(central, ZIP_CENTRAL_DIRECTORY_SIGNATURE);
		appendLe16(central, options.version_made_by);
		appendLe16(central, 20);
		appendLe16(central, options.flags);
		appendLe16(central, 0);
		appendLe16(central, 0);
		appendLe16(central, 0);
		appendLe32(central, crc);
		appendLe32(central, size);
		appendLe32(central, size);
		appendLe16(central, name_len);
		appendLe16(central, static_cast<uint16_t>(options.central_extra.size()));
		appendLe16(central, static_cast<uint16_t>(options.entry_comment.size()));
		appendLe16(central, 0);
		appendLe16(central, 0);
		appendLe32(central, options.external_attributes);
		appendLe32(central, local_offset);
		appendBytes(central, entry.name);
		central.insert(central.end(), options.central_extra.begin(), options.central_extra.end());
		appendBytes(central, options.entry_comment);
	}

	vBytes eocd;
	appendLe32(eocd, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE);
	appendLe16(eocd, 0);
	appendLe16(eocd, 0);
	appendLe16(eocd, static_cast<uint16_t>(entries.size()));
	appendLe16(eocd, static_cast<uint16_t>(entries.size()));
	appendLe32(eocd, static_cast<uint32_t>(central.size()));
	appendLe32(eocd, static_cast<uint32_t>(local.size()));
	appendLe16(eocd, static_cast<uint16_t>(archive_comment.size()));
	appendBytes(eocd, archive_comment);

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

vBytes makeWrappedSingleFileZip(std::string_view entry_name, std::string_view payload,
                               const ZipFixtureOptions& options = {}) {
	const ZipFixtureEntry entry{ .name = entry_name, .payload = payload, .options = options };
	return makeWrappedZip(std::span(&entry, 1));
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

	const std::array<std::string_view, 11> reserved = {
		"CONIN$",
		"CONOUT$.txt",
		"CLOCK$",
		"conin$",
		"ConOut$.log",
		"clock$.dat",
		"COM\xC2\xB9",
		"com\xC2\xB2.txt",
		"LPT\xC2\xB3.log",
		"dir/lpt\xC2\xB9",
		"C:drive.txt",
	};
	for (const std::string_view name : reserved) {
		// Flag names as UTF-8 so superscript-digit names reach the reserved-name check.
		const vBytes bad = makeWrappedSingleFileZip(name, "x", { .flags = 0x0800 });
		expectThrows([&] {
			validateArchiveEntryPaths(bad);
		}, std::format("reject Windows device name \"{}\"", name));
	}

	for (const std::string_view name : {"com\xE2\x81\xB4.txt"sv, "COM0.txt"sv, "company.txt"sv}) {
		try {
			validateArchiveEntryPaths(makeWrappedSingleFileZip(name, "x", { .flags = 0x0800 }));
		}
		catch (const std::exception& e) {
			std::println(std::cerr, "FAIL: non-reserved name \"{}\" rejected: {}", name, e.what());
			++g_failures;
		}
	}
}

vBytes makeOverlappingEntriesZip() {
	// Two entries whose declared compressed payloads overlap. Entry "a" lies
	// about its compressed size so that its payload span runs to the central
	// directory, covering entry "b"'s local header and payload. Those
	// overlapping bytes are not a valid DEFLATE stream, so if payloads were
	// decompressed before the overlap check, the archive would be rejected with
	// a DEFLATE error rather than the overlap error -- which is exactly the
	// "decompress before rejecting overlaps" amplification bug.
	constexpr uint16_t METHOD_DEFLATE = 8;
	constexpr uint32_t UNCOMP = 100;

	const auto appendLocalHeader = [](vBytes& out, std::string_view name,
	                                  uint32_t comp_size, uint32_t uncomp_size) {
		appendLe32(out, ZIP_LOCAL_FILE_HEADER_SIGNATURE);
		appendLe16(out, 20);             // version needed
		appendLe16(out, 0);              // flags
		appendLe16(out, METHOD_DEFLATE); // compression method
		appendLe16(out, 0);              // mod time
		appendLe16(out, 0);              // mod date
		appendLe32(out, 0);              // crc-32
		appendLe32(out, comp_size);
		appendLe32(out, uncomp_size);
		appendLe16(out, static_cast<uint16_t>(name.size()));
		appendLe16(out, 0);              // extra-field length
		appendBytes(out, name);
	};
	const auto appendCentralRecord = [](vBytes& out, std::string_view name, uint32_t comp_size,
	                                    uint32_t uncomp_size, uint32_t local_offset) {
		appendLe32(out, ZIP_CENTRAL_DIRECTORY_SIGNATURE);
		appendLe16(out, 0x0314);         // version made by (UNIX)
		appendLe16(out, 20);             // version needed
		appendLe16(out, 0);              // flags
		appendLe16(out, METHOD_DEFLATE); // compression method
		appendLe16(out, 0);              // mod time
		appendLe16(out, 0);              // mod date
		appendLe32(out, 0);              // crc-32
		appendLe32(out, comp_size);
		appendLe32(out, uncomp_size);
		appendLe16(out, static_cast<uint16_t>(name.size()));
		appendLe16(out, 0);              // extra-field length
		appendLe16(out, 0);              // comment length
		appendLe16(out, 0);              // disk start
		appendLe16(out, 0);              // internal attributes
		appendLe32(out, static_cast<uint32_t>(0100644) << 16); // external attributes
		appendLe32(out, local_offset);
		appendBytes(out, name);
	};

	vBytes local;
	appendLocalHeader(local, "a", 32, UNCOMP); // 31-byte header, declared payload overruns into "b"
	const uint32_t local_offset_b = static_cast<uint32_t>(local.size());
	appendLocalHeader(local, "b", 1, UNCOMP);
	local.push_back(0); // entry "b"'s single payload byte

	vBytes central;
	appendCentralRecord(central, "a", 32, UNCOMP, 0);
	appendCentralRecord(central, "b", 1, UNCOMP, local_offset_b);

	vBytes eocd;
	appendLe32(eocd, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE);
	appendLe16(eocd, 0); // disk number
	appendLe16(eocd, 0); // central-directory disk
	appendLe16(eocd, 2); // records on this disk
	appendLe16(eocd, 2); // total records
	appendLe32(eocd, static_cast<uint32_t>(central.size()));
	appendLe32(eocd, static_cast<uint32_t>(local.size()));
	appendLe16(eocd, 0); // comment length

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

void testOverlappingEntriesRejectedBeforeDecompression() {
	// The overlap must be detected before any payload is inflated. The
	// overlapping bytes are not valid DEFLATE, so decompressing first would
	// surface a DEFLATE error instead.
	expectArchiveRejected(makeOverlappingEntriesZip(),
		"Local ZIP entry payloads overlap",
		"overlapping local entries are rejected before their payloads are decompressed");
}

void testInfoZipVersionSuffixNamesAreRejected() {
	// Info-ZIP's unzip strips a trailing ";<digits>" version suffix unless -V is
	// given, so "main.py;1" extracts as "main.py": it overwrites a sibling
	// "main.py" and diverges from the validated/launched name. Reject such names.
	for (const std::string_view name : {"main.py;1", "run.sh;12", "pkg/app.js;0", "weird;007"}) {
		expectArchiveRejected(makeWrappedSingleFileZip(name, "x"),
			"version suffix", std::format("reject Info-ZIP version suffix \"{}\"", name));
	}
	// Names Info-ZIP would not rewrite stay valid: a ";<digits>" that is not the
	// final characters, a ";" with no trailing digits, trailing digits with no
	// ";", and an interior semicolon.
	for (const std::string_view name : {"main;1.py", "data;.py", "report2024.py", "a;b.py"}) {
		try {
			(void)analyzeArchive(makeWrappedSingleFileZip(name, "x"), true);
		}
		catch (const std::exception& e) {
			expectTrue(false, std::format("portable name \"{}\" was rejected: {}", name, e.what()));
		}
	}
}

void expectArchivePathsAccepted(const vBytes& archive, std::string_view label) {
	try {
		validateArchiveEntryPaths(archive);
	}
	catch (const std::exception& e) {
		expectTrue(false, std::format("{} was rejected: {}", label, e.what()));
	}
}

void expectArchivePathsRejected(const vBytes& archive, std::string_view reason, std::string_view label) {
	try {
		validateArchiveEntryPaths(archive);
		expectTrue(false, std::format("{} was accepted", label));
	}
	catch (const std::exception& e) {
		expectContains(e.what(), reason, label);
	}
}

void testDosDirectoryAttributeMustMatchPath() {
	constexpr uint32_t DOS_DIRECTORY = 0x10;
	// libarchive extracts these as directories, so the launcher would target a
	// directory named main.py instead of the validated Python file.
	expectArchiveRejected(makeWrappedSingleFileZip("main.py", "print(1)",
		{ .version_made_by = 0x0014, .external_attributes = DOS_DIRECTORY }),
		"Directory metadata does not match", "DOS directory attribute on a file path");
	expectArchiveRejected(makeWrappedSingleFileZip("main.py", "print(1)",
		{ .external_attributes = (static_cast<uint32_t>(0100644) << 16) | DOS_DIRECTORY }),
		"Directory metadata does not match", "UNIX file mode with a DOS directory attribute");
	// Directories may carry the attribute, or omit it as Java's jar tool does.
	for (const uint32_t attributes : {DOS_DIRECTORY, 0u}) {
		expectArchivePathsAccepted(makeWrappedSingleFileZip("docs/", "",
			{ .version_made_by = 0x0014, .external_attributes = attributes }),
			std::format("directory entry with DOS attributes 0x{:x}", attributes));
	}
}

void testHiddenEndOfCentralDirectoryIsRejected() {
	using namespace std::string_literals;
	const ZipFixtureEntry main_entry{ .name = "main.txt" };
	const std::span<const ZipFixtureEntry> entries(&main_entry, 1);

	// Info-ZIP selects the last EOCD signature in the file even when its comment
	// length does not reach the end, so a second record in the comment could
	// point it at an unvalidated central directory.
	const std::string hidden_eocd = "PK\x05\x06"s + std::string(18, '\0') + "padding";
	expectArchiveRejected(makeWrappedZip(entries, hidden_eocd),
		"end-of-central-directory signature", "archive comment containing a second EOCD record");
	expectArchiveRejected(makeWrappedZip(entries, "notes PK\x05\x06 more"s),
		"end-of-central-directory signature", "archive comment containing a bare EOCD signature");

	// A ZIP64 locator directly before the EOCD would switch extractors to ZIP64
	// metadata. Here it is smuggled in through the last central record's comment.
	const std::string zip64_locator = "PK\x06\x07"s + std::string(16, '\0');
	const ZipFixtureEntry locator_entry{ .name = "main.txt", .options = { .entry_comment = zip64_locator } };
	expectArchiveRejected(makeWrappedZip(std::span(&locator_entry, 1)),
		"ZIP64 locator", "ZIP64 locator signature preceding the EOCD");

	try {
		(void)analyzeArchive(makeWrappedZip(entries, "an ordinary archive comment"), true);
	}
	catch (const std::exception& e) {
		expectTrue(false, std::format("archive with an ordinary comment was rejected: {}", e.what()));
	}
}

void testLegacyFilenameEncodingsAreRejected() {
	// Unflagged non-ASCII names are decoded per extractor: Info-ZIP turns DOS
	// CP437 "caf\x82" into "caf\xe9", leaving the launcher pointing nowhere.
	expectArchiveRejected(makeWrappedSingleFileZip("caf\x82.py", "x", { .version_made_by = 0x0014 }),
		"UTF-8 flag", "unflagged CP437 filename");
	expectArchiveRejected(makeWrappedSingleFileZip("dir/caf\xC3\xA9.py", "x"),
		"UTF-8 flag", "unflagged UTF-8 bytes from a UNIX host");

	constexpr uint16_t UTF8_FLAG = 0x0800;
	for (const std::string_view name : {"caf\xC3.py", "\xC0\xAF.py", "\xED\xA0\x80.py", "\xF4\x90\x80\x80.py", "\xFF.py"}) {
		expectArchivePathsRejected(makeWrappedSingleFileZip(name, "x", { .flags = UTF8_FLAG }),
			"not valid UTF-8", "malformed UTF-8 filename");
	}
	expectArchivePathsRejected(makeWrappedSingleFileZip("a\xC2\x85.txt", "x", { .flags = UTF8_FLAG }),
		"control characters", "C1 control character in filename");

	// Flagged UTF-8 is fine for entries the script does not name...
	const std::array<ZipFixtureEntry, 2> mixed{{
		{ .name = "main.py" },
		{ .name = "données/naïve.txt", .options = { .flags = UTF8_FLAG } },
	}};
	try {
		const ArchiveMetadata metadata = analyzeArchive(makeWrappedZip(mixed), true);
		expectTrue(metadata.first_filename == "main.py", "ASCII launch target is selected");
	}
	catch (const std::exception& e) {
		expectTrue(false, std::format("flagged UTF-8 secondary entry was rejected: {}", e.what()));
	}
	// ...but the launch target is written into the script, so it must be ASCII.
	expectArchiveRejected(makeWrappedSingleFileZip("café.py", "x", { .flags = UTF8_FLAG }),
		"ASCII characters only", "non-ASCII launch target");
}

void testUnicodeCaseConflictsAreRejected() {
	constexpr uint16_t UTF8_FLAG = 0x0800;
	const auto pair = [](std::string_view first, std::string_view second) {
		return makeWrappedZip(std::array<ZipFixtureEntry, 2>{{
			{ .name = first, .options = { .flags = UTF8_FLAG } },
			{ .name = second, .options = { .flags = UTF8_FLAG } },
		}});
	};
	// These collide on case-insensitive Windows filesystems.
	for (const auto& [first, second] : std::to_array<std::pair<std::string_view, std::string_view>>({
			{"Ä.txt", "ä.txt"}, {"dir/Σ.txt", "dir/ς.txt"}, {"ǅ.txt", "ǆ.txt"},
			{"ÿ.txt", "Ÿ.txt"}, {"Ωmega/a.txt", "ωMEGA/A.TXT"}})) {
		expectArchivePathsRejected(pair(first, second), "case-conflicting",
			std::format("case conflict \"{}\" / \"{}\"", first, second));
	}
	expectArchivePathsRejected(pair("Ä", "ä/x.txt"), "conflicts with an existing file path",
		"Unicode case conflict between a file and a directory");
	// NTFS keeps these distinct: no Kelvin-sign or ß expansion, no
	// normalization, and supplementary characters are not case-folded.
	for (const auto& [first, second] : std::to_array<std::pair<std::string_view, std::string_view>>({
			{"K.txt", "K.txt"}, {"Straße.txt", "STRASSE.txt"}, {"ä.txt", "ä.txt"},
			{"\U00010428.txt", "\U00010400.txt"}})) {
		expectArchivePathsAccepted(pair(first, second),
			std::format("distinct names \"{}\" / \"{}\"", first, second));
	}
}

void testEmbeddedArchiveOffsetsAndCrc() {
	vBytes archive = makeWrappedSingleFileZip("main.py", "print(1)");
	vBytes image(0x21 + 12, Byte{0xAA});
	const vBytes iend{0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
	std::copy(iend.begin(), iend.end(), image.end() - 12);
	vBytes script(20, Byte{0x55});

	const PolyglotImage polyglot = embedChunks(image, script, archive);
	vBytes out;
	for (const auto part : polyglot.parts()) {
		out.insert(out.end(), part.begin(), part.end());
	}
	expectTrue(out.size() == image.size() + script.size() + archive.size(), "output contains every part once");
	expectTrue(std::equal(iend.begin(), iend.end(), out.end() - 12), "IEND stays last");

	const std::size_t idat = image.size() + script.size() - 12;
	const std::size_t zip_start = idat + 8;
	const std::size_t eocd = out.size() - 16 - 22;
	expectTrue(hasLe32Signature(out, eocd, ZIP_END_CENTRAL_DIRECTORY_SIGNATURE), "EOCD found at its new position");
	expectTrue(readLe16(out, eocd + 20) == 16, "EOCD comment covers the PNG trailer");
	const std::size_t central = readLe32(out, eocd + 16);
	expectTrue(hasLe32Signature(out, central, ZIP_CENTRAL_DIRECTORY_SIGNATURE), "central offset is absolute");
	const std::size_t local = readLe32(out, central + 42);
	expectTrue(local == zip_start && hasLe32Signature(out, local, ZIP_LOCAL_FILE_HEADER_SIGNATURE),
		"local offset is absolute");

	const std::size_t crc_index = out.size() - 16;
	const uint32_t expected_crc = static_cast<uint32_t>(::crc32(
		::crc32(0L, Z_NULL, 0), out.data() + idat + 4, static_cast<uInt>(crc_index - idat - 4)));
	expectTrue(readValueAt(out, crc_index, 4) == expected_crc, "final IDAT CRC is correct");

	using namespace std::string_literals;
	const ZipFixtureEntry entry{ .name = "main.py" };
	expectThrows([&] {
		(void)embedChunks(image, script, makeWrappedZip(std::span(&entry, 1), "PK\x05\x06"s + std::string(18, '\0') + "x"));
	}, "assembly re-checks for a hidden EOCD record");
}

void testTemplateDelimitersInArgumentsAreLiteral() {
	// The renderer scans only the template, so placeholder-looking input is
	// emitted as an ordinary quoted argument.
	const UserArguments arguments{"{{WINDOWS_ARGS}} {{x", "{{LINUX_ARGS}}"};
	const std::string script = script_builder_internal::buildScriptText(FileType::PYTHON, "main.py", arguments);
	expectContains(script, "'{{WINDOWS_ARGS}}' '{{x'", "Linux arguments keep literal braces");
	expectContains(script, "\"{{LINUX_ARGS}}\"", "Windows arguments keep literal braces");
}

void testLinuxExtractionRetainsVersionNumbers() {
	// -V keeps Info-ZIP from stripping ";<digits>" suffixes, so extracted names
	// match the validated central-directory names.
	const std::string script = script_builder_internal::buildScriptText(
		FileType::PYTHON, "main.py", UserArguments{});
	expectContains(script, "unzip -qoV", "Linux extraction passes -V to retain version numbers");
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
		testOverlappingEntriesRejectedBeforeDecompression();
		testInfoZipVersionSuffixNamesAreRejected();
		testDosDirectoryAttributeMustMatchPath();
		testHiddenEndOfCentralDirectoryIsRejected();
		testLegacyFilenameEncodingsAreRejected();
		testUnicodeCaseConflictsAreRejected();
		testEmbeddedArchiveOffsetsAndCrc();
		testTemplateDelimitersInArgumentsAreLiteral();
		testLinuxExtractionRetainsVersionNumbers();
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
