#include "pdvzip.h"

namespace {

[[nodiscard]] char toLowerAscii(unsigned char c) {
	return static_cast<char>(std::tolower(c));
}

[[nodiscard]] bool equalsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}

	for (std::size_t i = 0; i < lhs.size(); ++i) {
		if (toLowerAscii(static_cast<unsigned char>(lhs[i])) != toLowerAscii(static_cast<unsigned char>(rhs[i]))) {
			return false;
		}
	}
	return true;
}

} // anonymous namespace

bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
		return false;
	}
	const auto filename = p.filename().string();
	if (filename.empty()) {
		return false;
	}
	return std::ranges::none_of(filename, [](unsigned char c) {
		return std::iscntrl(c) != 0;
	});
}

bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
	const std::string extension = p.extension().string();
	return std::ranges::any_of(exts, [&extension](std::string_view ext) {
		return equalsIgnoreAsciiCase(extension, ext);
	});
}

vBytes readFile(const fs::path& path, FileTypeCheck check_type) {
	if (!hasValidFilename(path)) {
		throw std::runtime_error("Invalid Input Error: Filename contains unsupported control characters.");
	}

	if (!fs::exists(path) || !fs::is_regular_file(path)) {
		throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
	}

	const auto raw_file_size = fs::file_size(path);
	if (raw_file_size > std::numeric_limits<std::size_t>::max()) {
		throw std::runtime_error("Error: File is too large to process on this platform.");
	}

	const std::size_t file_size = static_cast<std::size_t>(raw_file_size);

	if (!file_size) {
		throw std::runtime_error("Error: File is empty.");
	}

	if (check_type == FileTypeCheck::cover_image) {
		if (!hasFileExtension(path, {".png"})) {
			throw std::runtime_error("Image File Error: Invalid image extension. Only expecting \".png\".");
		}

		constexpr std::size_t
			MINIMUM_IMAGE_SIZE = 87,
			MAX_IMAGE_SIZE     = 4 * 1024 * 1024;

		if (MINIMUM_IMAGE_SIZE > file_size) {
			throw std::runtime_error("Image File Error: Cover image too small. Not a valid PNG.");
		}

		if (file_size > MAX_IMAGE_SIZE) {
			throw std::runtime_error("Image File Error: Cover image exceeds the 4MB size limit.");
		}
	}

	if (check_type == FileTypeCheck::archive_file) {
		constexpr std::size_t
			MAX_ARCHIVE_SIZE     = 2ULL * 1024 * 1024 * 1024,
			MINIMUM_ARCHIVE_SIZE = 30;

		if (!hasFileExtension(path, {".zip", ".jar"})) {
			throw std::runtime_error("Archive File Error: Invalid file extension. Only expecting \".zip\" or \".jar\".");
		}

		if (MINIMUM_ARCHIVE_SIZE > file_size) {
			throw std::runtime_error("Archive File Error: Invalid file size.");
		}

		if (file_size > MAX_ARCHIVE_SIZE) {
			throw std::runtime_error("Archive File Error: File exceeds maximum size limit.");
		}
	}

	std::ifstream file(path, std::ios::binary);

	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}

	if (file_size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
		throw std::runtime_error("Error: File is too large to read in a single operation.");
	}

	const bool wrap_archive = (check_type == FileTypeCheck::archive_file);
	const std::size_t prefix_size = wrap_archive ? 8 : 0;
	const std::size_t buffer_size = wrap_archive
		? checkedAdd(file_size, CHUNK_FIELDS_COMBINED_LENGTH, "Archive File Error: Wrapped archive size overflow.")
		: file_size;

	vBytes vec(buffer_size, 0);
	if (wrap_archive) {
		constexpr auto IDAT_MARKER_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 });
		std::copy(IDAT_MARKER_BYTES.begin(), IDAT_MARKER_BYTES.end(), vec.begin());
	}

	if (!file.read(
			reinterpret_cast<char*>(vec.data() + prefix_size),
			static_cast<std::streamsize>(file_size))
		|| file.gcount() != static_cast<std::streamsize>(file_size)) {
		throw std::runtime_error("Failed to read full file: partial read");
	}

	if (wrap_archive) {
		constexpr auto ARCHIVE_SIG       = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 });
		if (vec.size() < prefix_size + ARCHIVE_SIG.size()
			|| !std::equal(ARCHIVE_SIG.begin(), ARCHIVE_SIG.end(), vec.begin() + prefix_size)) {
			throw std::runtime_error("Archive File Error: Signature check failure. Not a valid archive file.");
		}
	}

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
		if (fs::exists(filename)) {
			continue;
		}

		ofs.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
		if (ofs) {
			break;
		}
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

	std::print("\nCreated {} polyglot image file: {} ({} bytes).\n\nComplete!\n\n",
		is_zip_file ? "PNG-ZIP" : "PNG-JAR", filename, image_vec.size());

	std::error_code ec;
	fs::permissions(
		filename,
		fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec
			| fs::perms::others_read | fs::perms::others_exec,
		ec);
	if (ec) {
		std::println(stderr, "\nWarning: Could not set executable permissions for {}.\n"
		             "You may need to do this manually.", filename);
	}
}
