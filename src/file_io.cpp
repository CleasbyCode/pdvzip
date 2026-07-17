#include "pdvzip.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <format>
#include <fstream>
#include <limits>
#include <print>
#include <random>
#include <stdexcept>
#include <system_error>

namespace {

[[nodiscard]] bool equalsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
	return std::ranges::equal(lhs, rhs, [](unsigned char a, unsigned char b) {
		return std::tolower(a) == std::tolower(b);
	});
}

} // anonymous namespace

bool hasValidFilename(const fs::path& p) {
	const auto filename = p.filename().string();
	return !filename.empty() && std::ranges::none_of(filename, [](unsigned char c) {
		return std::iscntrl(c) != 0;
	});
}

bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
	const std::string extension = p.extension().string();
	return std::ranges::any_of(exts, [&extension](std::string_view ext) {
		return equalsIgnoreAsciiCase(extension, ext);
	});
}

namespace {

struct ScopedFd {
	int fd{-1};

	explicit ScopedFd(int file_descriptor) noexcept : fd(file_descriptor) {}
	~ScopedFd() {
		if (fd >= 0) {
			::close(fd);
		}
	}

	ScopedFd(const ScopedFd&) = delete;
	ScopedFd& operator=(const ScopedFd&) = delete;

	ScopedFd(ScopedFd&& other) noexcept : fd(other.fd) {
		other.fd = -1;
	}

	[[nodiscard]] int get() const noexcept { return fd; }
};

[[nodiscard]] ScopedFd openFileForReadOrThrow(const fs::path& path) {
	int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	ScopedFd handle(::open(path.c_str(), flags));
	if (handle.get() < 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format(
			"Failed to open file: {} ({})", path.string(), ec.message()));
	}
	return handle;
}

[[nodiscard]] std::size_t fdFileSizeChecked(int fd, const fs::path& path) {
	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format(
			"Error: Failed to stat \"{}\": {}", path.string(), ec.message()));
	}
	if (!S_ISREG(st.st_mode)) {
		throw std::runtime_error(std::format(
			"Error: File \"{}\" not found or not a regular file.", path.string()));
	}
	if (st.st_size < 0) {
		throw std::runtime_error(std::format(
			"Error: Negative file size reported for \"{}\".", path.string()));
	}

	const auto raw_file_size = static_cast<std::uintmax_t>(st.st_size);
	if (raw_file_size > std::numeric_limits<std::size_t>::max()) {
		throw std::runtime_error("Error: File is too large to process on this platform.");
	}
	const std::size_t file_size = static_cast<std::size_t>(raw_file_size);
	if (!file_size) {
		throw std::runtime_error("Error: File is empty.");
	}
	return file_size;
}

void validateCoverImageConstraints(const fs::path& path, std::size_t file_size) {
	// Smallest representable PNG: 8-byte signature + 25-byte IHDR + 12-byte IEND
	// + a minimal IDAT chunk; this is the practical floor below which the file
	// cannot be a valid PNG.
	constexpr std::size_t MIN_PNG_SIZE         = 87;
	constexpr std::size_t MAX_COVER_IMAGE_SIZE = 4 * 1024 * 1024;

	if (!hasFileExtension(path, {".png"})) {
		throw std::runtime_error("Image File Error: Invalid image extension. Only expecting \".png\".");
	}
	if (file_size < MIN_PNG_SIZE) {
		throw std::runtime_error("Image File Error: Cover image too small. Not a valid PNG.");
	}
	if (file_size > MAX_COVER_IMAGE_SIZE) {
		throw std::runtime_error("Image File Error: Cover image exceeds the 4MB size limit.");
	}
}

void validateArchiveConstraints(const fs::path& path, std::size_t file_size) {
	// PNG chunk lengths are 31-bit per the PNG specification. Keep the ZIP
	// payload within the range this program can emit as the final IDAT chunk.
	constexpr std::size_t MAX_ARCHIVE_SIZE = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());

	if (!hasFileExtension(path, {".zip", ".jar"})) {
		throw std::runtime_error("Archive File Error: Invalid file extension. Only expecting \".zip\" or \".jar\".");
	}
	if (file_size < 30) {
		throw std::runtime_error("Archive File Error: Invalid file size.");
	}
	if (file_size > MAX_ARCHIVE_SIZE) {
		throw std::runtime_error("Archive File Error: File exceeds maximum size limit.");
	}
}

void validateTypeSpecificConstraints(const fs::path& path, std::size_t file_size, FileTypeCheck check_type) {
	switch (check_type) {
		case FileTypeCheck::cover_image:
			validateCoverImageConstraints(path, file_size);
			return;
		case FileTypeCheck::archive_file:
			validateArchiveConstraints(path, file_size);
			return;
	}

	throw std::runtime_error("Internal Error: Unknown file type check.");
}

[[nodiscard]] vBytes makeReadBuffer(std::size_t file_size, bool wrap_archive) {
	const std::size_t buffer_size = wrap_archive
		? checkedAdd(file_size, CHUNK_FIELDS_COMBINED_LENGTH, "Archive File Error: Wrapped archive size overflow.")
		: file_size;

	vBytes vec(buffer_size, 0);
	if (wrap_archive) {
		constexpr auto IDAT_MARKER_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 });
		std::copy(IDAT_MARKER_BYTES.begin(), IDAT_MARKER_BYTES.end(), vec.begin());
	}

	return vec;
}

void readFileContents(int fd, const fs::path& path, vBytes& vec, std::size_t file_size, bool wrap_archive) {
	const std::size_t prefix_size = wrap_archive ? 8 : 0;
	std::size_t total_read = 0;
	while (total_read < file_size) {
		const std::size_t remaining = file_size - total_read;
		const std::size_t chunk = std::min<std::size_t>(
			remaining,
			static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
		const ssize_t rc = ::read(fd, vec.data() + prefix_size + total_read, chunk);
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"Failed to read file: {} ({})", path.string(), ec.message()));
		}
		if (rc == 0) {
			throw std::runtime_error("Failed to read full file: partial read");
		}
		total_read += static_cast<std::size_t>(rc);
	}
}

void validateWrappedArchiveSignature(const vBytes& vec, bool wrap_archive) {
	if (wrap_archive && !hasLe32Signature(vec, 8, ZIP_LOCAL_FILE_HEADER_SIGNATURE)) {
		throw std::runtime_error("Archive File Error: Signature check failure. Not a valid archive file.");
	}
}

} // anonymous namespace

vBytes readFile(const fs::path& path, FileTypeCheck check_type) {
	if (!hasValidFilename(path)) {
		throw std::runtime_error("Invalid Input Error: Filename contains unsupported control characters.");
	}

	// Open the file first, then validate via fstat on the resulting fd. This
	// avoids a TOCTOU race where a stat-then-open pair could observe a
	// different file than the one ultimately read.
	const ScopedFd handle = openFileForReadOrThrow(path);
	const std::size_t file_size = fdFileSizeChecked(handle.get(), path);
	validateTypeSpecificConstraints(path, file_size, check_type);

	const bool wrap_archive = (check_type == FileTypeCheck::archive_file);
	vBytes vec = makeReadBuffer(file_size, wrap_archive);
	readFileContents(handle.get(), path, vec, file_size, wrap_archive);
	validateWrappedArchiveSignature(vec, wrap_archive);

	return vec;
}

void writePolyglotFile(const vBytes& image_vec, bool is_zip_file) {
	if (image_vec.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
		throw std::runtime_error("Write File Error: Output exceeds maximum writable size.");
	}

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dist(10000, 99999);

	const std::string_view prefix = is_zip_file ? "pzip_" : "pjar_";

	constexpr std::size_t MAX_NAME_ATTEMPTS = 256;
	std::string filename;
	std::ofstream ofs;

	for (std::size_t i = 0; i < MAX_NAME_ATTEMPTS; ++i) {
		filename = std::format("{}{}.png", prefix, dist(gen));

		// noreplace (C++23) atomically fails if the file already exists,
		// eliminating the TOCTOU race between exists() and open().
		ofs.open(filename, std::ios::binary | std::ios::out | std::ios::noreplace);
		if (ofs) {
			break;
		}
		ofs.clear();
	}

	if (!ofs) {
		throw std::runtime_error("Write File Error: Unable to create a unique output file.");
	}

	ofs.write(reinterpret_cast<const char*>(image_vec.data()),
		static_cast<std::streamsize>(image_vec.size()));
	if (!ofs) {
		throw std::runtime_error("Write File Error: Failed while writing output file.");
	}
	ofs.close();
	if (!ofs) {
		throw std::runtime_error("Write File Error: Failed while finalizing output file.");
	}

	// Fsync the output to disk so the polyglot survives a power loss between
	// here and the kernel's writeback. The fd is reopened just for the sync —
	// std::ofstream doesn't expose its underlying descriptor portably.
	{
		ScopedFd sync_fd(::open(filename.c_str(), O_RDONLY | O_CLOEXEC));
		if (sync_fd.get() >= 0) {
			::fsync(sync_fd.get());
		}
	}

	std::print("\nCreated {} polyglot image file: {} ({} bytes).\n\nComplete!\n\n",
		is_zip_file ? "PNG-ZIP" : "PNG-JAR", filename, image_vec.size());

	// 0755 so ./pzip_….png is runnable. Documented in --info; multi-user hosts
	// may tighten with chmod 700 after creation.
	std::error_code ec;
	fs::permissions(
		filename,
		fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec
			| fs::perms::others_read | fs::perms::others_exec,
		ec);
	if (ec) {
		std::println(stderr,
			"\nWarning: Could not set executable permissions for {}.\n"
			"You may need to do this manually with: chmod +x \"{}\"",
			filename, filename);
	}
}
