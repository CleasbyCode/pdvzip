#include "pdvzip.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cctype>
#include <format>
#include <limits>
#include <print>
#include <random>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {

[[nodiscard]] bool equalsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
	return std::ranges::equal(lhs, rhs, [](unsigned char a, unsigned char b) static {
		return std::tolower(a) == std::tolower(b);
	});
}

[[nodiscard]] bool hasValidFilename(const fs::path& p) {
	const auto filename = p.filename().string();
	return !filename.empty() && std::ranges::none_of(filename, [](unsigned char c) static {
		return std::iscntrl(c) != 0;
	});
}

} // anonymous namespace

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
	// Let fstat reject FIFOs without waiting for a writer. On Linux,
	// O_NONBLOCK leaves regular-file reads unchanged.
	int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	ScopedFd handle(::open(path.c_str(), flags));
	if (handle.get() < 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format(
			"Failed to open file: {} ({})", sanitizeForDisplay(path.string()), ec.message()));
	}
	return handle;
}

[[nodiscard]] std::size_t fdFileSizeChecked(int fd, const fs::path& path) {
	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format(
			"Error: Failed to stat \"{}\": {}", sanitizeForDisplay(path.string()), ec.message()));
	}
	if (!S_ISREG(st.st_mode)) {
		throw std::runtime_error(std::format(
			"Error: File \"{}\" not found or not a regular file.", sanitizeForDisplay(path.string())));
	}
	if (st.st_size < 0) {
		throw std::runtime_error(std::format(
			"Error: Negative file size reported for \"{}\".", sanitizeForDisplay(path.string())));
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
				"Failed to read file: {} ({})", sanitizeForDisplay(path.string()), ec.message()));
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

namespace {

constexpr const char* OUTPUT_CREATE_ERROR = "Write File Error: Unable to create output file";

[[noreturn]] void throwErrnoError(std::string_view message, int error_number) {
	const std::error_code ec(error_number, std::generic_category());
	throw std::runtime_error(std::format("{}: {}", message, ec.message()));
}

// The finished image is staged where no other process can see it under its
// final name, then given that name in one step that never replaces an existing
// entry. Where the filesystem supports O_TMPFILE the staged file has no name at
// all: an interrupted or killed run (pdvzip installs no signal handlers, so
// Ctrl-C included) leaves nothing behind. Otherwise it is written under a
// hidden ".pdvzip_tmp_NNNNNN" name, removed on every path that unwinds; there,
// Ctrl-C, SIGKILL, a crash or power loss mid-write can leave that hidden 0600
// file behind. A partial image never appears under the final name.
class StagedOutput {
public:
	explicit StagedOutput(std::mt19937& gen) {
		do {
			fd_ = ::open(".", O_TMPFILE | O_WRONLY | O_CLOEXEC, 0600);
		} while (fd_ < 0 && errno == EINTR);
		if (fd_ >= 0) {
			return;
		}
		// EOPNOTSUPP: this filesystem has no O_TMPFILE. EISDIR: a pre-3.11
		// kernel that does not know the flag and saw only its O_DIRECTORY bit.
		if (errno != EOPNOTSUPP && errno != EISDIR) {
			throwErrnoError(OUTPUT_CREATE_ERROR, errno);
		}

		std::uniform_int_distribution<> dist(100000, 999999);
		constexpr std::size_t MAX_TEMP_ATTEMPTS = 1024;
		for (std::size_t i = 0; i < MAX_TEMP_ATTEMPTS; ++i) {
			std::string candidate = std::format(".pdvzip_tmp_{}", dist(gen));
			do {
				fd_ = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
			} while (fd_ < 0 && errno == EINTR);
			if (fd_ >= 0) {
				staged_path_ = std::move(candidate);
				return;
			}
			if (errno != EEXIST) {
				throwErrnoError(OUTPUT_CREATE_ERROR, errno);
			}
		}
		throw std::runtime_error("Write File Error: Unable to create a unique temporary output file.");
	}

	~StagedOutput() {
		if (fd_ >= 0) {
			::close(fd_);
		}
		if (!published_ && !staged_path_.empty()) {
			(void)::unlink(staged_path_.c_str());
		}
	}

	StagedOutput(const StagedOutput&) = delete;
	StagedOutput& operator=(const StagedOutput&) = delete;

	[[nodiscard]] int fd() const noexcept { return fd_; }

	// Give the staged file `name` without ever replacing an existing entry.
	// Returns false when `name` is taken, so the caller can try another.
	[[nodiscard]] bool publish(const std::string& name) {
		int rc;
		if (staged_path_.empty()) {
			// The supported way to name an O_TMPFILE inode. link() never replaces
			// an entry. The /proc route needs no privilege; AT_EMPTY_PATH covers
			// a system without /proc mounted.
			const std::string proc_path = std::format("/proc/self/fd/{}", fd_);
			rc = ::linkat(AT_FDCWD, proc_path.c_str(), AT_FDCWD, name.c_str(), AT_SYMLINK_FOLLOW);
			if (rc != 0 && errno == ENOENT) {
				rc = ::linkat(fd_, "", AT_FDCWD, name.c_str(), AT_EMPTY_PATH);
			}
		} else {
			rc = ::renameat2(AT_FDCWD, staged_path_.c_str(), AT_FDCWD, name.c_str(), RENAME_NOREPLACE);
			if (rc != 0 && (errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP)) {
				// No RENAME_NOREPLACE here: link() is the other no-replace primitive.
				rc = ::link(staged_path_.c_str(), name.c_str());
				if (rc == 0) {
					(void)::unlink(staged_path_.c_str());
				}
			}
		}
		if (rc == 0) {
			published_ = true;
			return true;
		}
		if (errno == EEXIST) {
			return false;
		}
		throwErrnoError("Write File Error: Failed to publish output file", errno);
	}

	// Linux releases the descriptor even when close reports an error, so it is
	// never retried and the destructor will not close a reused number.
	[[nodiscard]] int close() noexcept {
		const int closing_fd = std::exchange(fd_, -1);
		return closing_fd >= 0 ? ::close(closing_fd) : 0;
	}

private:
	int fd_{-1};
	std::string staged_path_{}; // empty for an unnamed O_TMPFILE file
	bool published_{false};
};

void writeAllParts(int fd, std::span<const std::span<const Byte>> parts) {
	for (const std::span<const Byte> part : parts) {
		std::size_t total_written = 0;
		while (total_written < part.size()) {
			const std::size_t chunk_size = std::min<std::size_t>(
				part.size() - total_written,
				static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
			const ssize_t written = ::write(fd, part.data() + total_written, chunk_size);
			if (written < 0 && errno == EINTR) {
				continue;
			}
			if (written <= 0) {
				throw std::runtime_error("Write File Error: Failed while writing output file.");
			}
			total_written += static_cast<std::size_t>(written);
		}
	}
}

void syncCurrentDirectoryNoThrow() noexcept {
	const int dir_fd = ::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir_fd >= 0) {
		(void)::fsync(dir_fd);
		(void)::close(dir_fd);
	}
}

} // anonymous namespace

void writePolyglotFile(std::span<const std::span<const Byte>> parts, bool is_zip_file) {
	std::size_t output_size = 0;
	for (const std::span<const Byte> part : parts) {
		output_size = checkedAdd(output_size, part.size(), "Write File Error: Output exceeds maximum writable size.");
	}
	if (output_size > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
		throw std::runtime_error("Write File Error: Output exceeds maximum writable size.");
	}

	std::random_device rd;
	std::mt19937 gen(rd());
	StagedOutput output(gen);
	writeAllParts(output.fd(), parts);

	// 0755 so ./pzip_….png is runnable. The staged file is reachable only
	// through this descriptor, so nothing else can be substituted for it.
	int permission_status;
	do {
		permission_status = ::fchmod(output.fd(), 0755);
	} while (permission_status < 0 && errno == EINTR);
	const std::error_code permission_error(
		permission_status < 0 ? errno : 0, std::generic_category());

	// Sync both contents and the final permissions before naming the file:
	// publishing is the commit.
	int sync_status;
	do {
		sync_status = ::fsync(output.fd());
	} while (sync_status < 0 && errno == EINTR);
	if (sync_status < 0) {
		throw std::runtime_error("Write File Error: Failed while syncing output file.");
	}

	const std::string_view prefix = is_zip_file ? "pzip_" : "pjar_";
	std::uniform_int_distribution<> dist(10000, 99999);
	constexpr std::size_t MAX_NAME_ATTEMPTS = 256;
	std::string filename;
	for (std::size_t i = 0; i < MAX_NAME_ATTEMPTS && filename.empty(); ++i) {
		std::string candidate = std::format("{}{}.png", prefix, dist(gen));
		if (output.publish(candidate)) {
			filename = std::move(candidate);
		}
	}
	if (filename.empty()) {
		throw std::runtime_error("Write File Error: Unable to create a unique output file.");
	}

	struct stat published_file{};
	const bool have_identity = ::fstat(output.fd(), &published_file) == 0;
	if (output.close() != 0) {
		// Best-effort cleanup of the name just published: skip a path that has
		// already been replaced. This identity check and unlink are not atomic.
		struct stat current_file{};
		if (have_identity && ::lstat(filename.c_str(), &current_file) == 0
			&& current_file.st_dev == published_file.st_dev
			&& current_file.st_ino == published_file.st_ino) {
			(void)::unlink(filename.c_str());
		}
		throw std::runtime_error("Write File Error: Failed while finalizing output file.");
	}
	syncCurrentDirectoryNoThrow();

	std::print("\nCreated {} polyglot image file: {} ({} bytes).\n\nComplete!\n\n",
		is_zip_file ? "PNG-ZIP" : "PNG-JAR", filename, output_size);

	if (permission_error) {
		std::println(stderr,
			"\nWarning: Could not set executable permissions for {}.\n"
			"You may need to do this manually with: chmod +x \"{}\"",
			filename, filename);
	}
}
