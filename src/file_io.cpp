#include "pdvzip.h"

bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
		return false;
	}
	const auto filename = p.filename().string();
	if (filename.empty()) {
		return false;
	}
	return std::ranges::all_of(filename, [](unsigned char c) {
		return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
	});
}

bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
	auto e = p.extension().string();
	std::ranges::transform(e, e.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return std::ranges::any_of(exts, [&e](std::string_view ext) {
		std::string c{ext};
		std::ranges::transform(c, c.begin(), [](unsigned char x) {
			return static_cast<char>(std::tolower(x));
		});
		return e == c;
	});
}

vBytes readFile(const fs::path& path, FileTypeCheck FileType) {
	if (!hasValidFilename(path)) {
		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
	}

	if (!fs::exists(path) || !fs::is_regular_file(path)) {
		throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
	}

	std::size_t file_size = fs::file_size(path);

	if (!file_size) {
		throw std::runtime_error("Error: File is empty.");
	}

	if (FileType == FileTypeCheck::cover_image) {
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

	if (FileType == FileTypeCheck::archive_file) {
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

	vBytes vec(file_size);
	file.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(file_size));

	if (file.gcount() != static_cast<std::streamsize>(file_size)) {
		throw std::runtime_error("Failed to read full file: partial read");
	}

	if (FileType == FileTypeCheck::archive_file) {
		constexpr auto IDAT_MARKER_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 });
		constexpr auto IDAT_CRC_BYTES    = std::to_array<Byte>({ 0x00, 0x00, 0x00, 0x00 });

		vec.insert(vec.begin(), IDAT_MARKER_BYTES.begin(), IDAT_MARKER_BYTES.end());
		vec.insert(vec.end(),   IDAT_CRC_BYTES.begin(),    IDAT_CRC_BYTES.end());

		constexpr std::size_t INDEX_DIFF = 8;

		constexpr auto ARCHIVE_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 });

		if (!std::equal(ARCHIVE_SIG.begin(), ARCHIVE_SIG.end(), std::begin(vec) + INDEX_DIFF)) {
			throw std::runtime_error("Archive File Error: Signature check failure. Not a valid archive file.");
		}
	}
	return vec;
}

void writePolyglotFile(const vBytes& image_vec, bool isZipFile) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dist(10000, 99999);

	const std::string_view prefix = isZipFile ? "pzip_" : "pjar_";
	const std::string filename = std::format("{}{}.png", prefix, dist(gen));

	std::ofstream ofs(filename, std::ios::binary);
	if (!ofs) {
		throw std::runtime_error("Write File Error: Unable to write to file.");
	}

	ofs.write(reinterpret_cast<const char*>(image_vec.data()), image_vec.size());

	std::print("\nCreated {} polyglot image file: {} ({} bytes).\n\nComplete!\n\n",
		isZipFile ? "PNG-ZIP" : "PNG-JAR", filename, image_vec.size());

	#ifdef __unix__
	if (chmod(filename.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		std::println(stderr, "\nWarning: Could not set executable permissions for {}.\n"
		             "You will need to do this manually using chmod.", filename);
	}
	#endif
}
