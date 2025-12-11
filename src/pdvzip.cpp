// 	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v4.1)
//	Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022

//	To compile program (Linux), from the extracted repo :~/Downloads/pdvzip-main/src
//
// 	$ chmod +x compile_pdvzip.sh
//	$ ./compile_pdvzip.sh
//	$ Compilation successful. Executable 'pdvzip' created.
//	$ sudo cp pdvzip /usr/bin
//
// 	Run it:
// 	$ pdvzip

#include "lodepng/lodepng.h"
// LodePNG version 20250506
// Copyright (c) 2005-2025 Lode Vandevenne
// https://github.com/lvandeve/lodepng

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <random>
#include <unordered_map>

#ifdef __unix__
	#include <sys/stat.h>      
#endif

namespace fs = std::filesystem;

using Byte   = std::uint8_t;
using vBytes = std::vector<Byte>;

using std::operator""sv;

enum class FileTypeCheck : Byte {
	cover_image  = 1,
    archive_file = 2
};

constexpr auto LINUX_PROBLEM_CHARACTERS = std::to_array<Byte>({ 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 });

static void displayInfo() {
	std::print(R"(
PNG Data Vehicle ZIP/JAR Edition (PDVZIP v4.1).
Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.

Use PDVZIP to embed a ZIP/JAR file within a PNG image, 
to create a tweetable and "executable" PNG-ZIP/JAR polyglot file.
		
The supported hosting sites will retain the embedded archive within the PNG image.  
		
PNG image size limits are platform dependant:  

X/Twitter (5MB), Flickr (200MB), Imgbb (32MB), PostImage (32MB), ImgPile (8MB).

Once the ZIP file has been embedded within a PNG image, it can be shared on your chosen
hosting site or 'executed' whenever you want to access the embedded file(s).

pdvzip (Linux) will attempt to automatically set executable permissions on newly created polyglot image files.
You will need to manually set executable permissions using chmod on these polyglot images downloaded from hosting sites.
		
From a Linux terminal: ./pzip_image.png (chmod +x pzip_image.png, if required).
From a Windows terminal: First, rename the '.png' file extension to '.cmd', then .\pzip_image.cmd 

For common video/audio files, Linux uses the media player vlc or mpv. Windows uses the set default media player.
PDF, Linux uses either evince or firefox. Windows uses the set default PDF viewer.
Python, Linux & Windows use python3 to run these programs.
PowerShell, Linux uses pwsh command (if PowerShell installed). 
Depending on the installed version of PowerShell, Windows uses either pwsh.exe or powershell.exe, to run these scripts.
Folder, Linux uses xdg-open, Windows uses powershell.exe with II (Invoke-Item) command, to open zipped folders.

For any other media type/file extension, Linux & Windows will rely on the operating system's method or set default application for those files.

PNG Image Requirements for Arbitrary Data Preservation

PNG file size (image + embedded content) must not exceed the hosting site's size limits.
The site will either refuse to upload your image or it will convert your image to jpg, such as X/Twitter.

Dimensions:

The following dimension size limits are specific to pdvzip and not necessarily the extact hosting site's size limits.

PNG-32/24 (Truecolor)

Image dimensions can be set between a minimum of 68x68 and a maximum of 900x900.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

Note: Images that are created & saved within your image editor as PNG-32/24 that are either 
black & white/grayscale, images with 256 colours or less, will be converted by X/Twitter to 
PNG-8 and you will lose the embedded content. If you want to use a simple "single" colour PNG-32/24 image,
then fill an area with a gradient colour instead of a single solid colour.
X/Twitter should then keep the image as PNG-32/24.

PNG-8 (Indexed-colour)

Image dimensions can be set between a minimum of 68x68 and a maximum of 4096x4096.

PNG Chunks:

For example, with X/Twitter, you can overfill the following PNG chunks with arbitrary data, 
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

Other platforms may differ in what chunks they preserve and which you can overfill.

bKGD, cHRM, gAMA, hIST,
iCCP, (Only 10KB max. with X/Twitter).
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
PLTE, (Use only with PNG-32 & PNG-24 for arbitrary data).
pHYs, sBIT, sPLT, sRGB,
tRNS. (PNG-32 only).

This program uses the iCCP (extraction script) and IDAT (zip file) chunk names for storing arbitrary data.

ZIP File Size & Other Information

To work out the maximum ZIP file size, start with the hosting site's size limit,  
minus your PNG image size, minus 1500 bytes (extraction script size).   

X/Twitter example: (5MB Image Limit) 5,242,880 - (image size 307,200 + extraction script size 1500) = 4,934,180 bytes available for your ZIP file.

Make sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
Do not include other .zip files within the main ZIP archive. (.rar files are ok).
Do not include other pdvzip created PNG image files within the main ZIP archive, as they are essentially .zip files.
Use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.
A file without an extension will be treated as a Linux executable.
Paint.net application is recommended for easily creating compatible PNG image files.
 
)");
}

struct ProgramArgs {
    std::string image_file_path;
    std::string archive_file_path;

    static ProgramArgs parse(int argc, char** argv) {
        if (argc < 1 || argv[0] == nullptr) {
            throw std::runtime_error("Invalid program invocation: missing program name");
        }

		constexpr std::string_view PREFIX = "Usage: ";
        const std::string
			PROG = fs::path(argv[0]).filename().string(),
			INDENT(PREFIX.size(), ' '),
			USAGE = std::format("{}{} <cover_image> <zip/jar>\n""{}{} --info",PREFIX, PROG, INDENT, PROG);

        auto die = [&USAGE]() -> void {
			throw std::runtime_error(USAGE);
			std::unreachable();
		};

        if (argc < 2) die();

        if (argc == 2 && std::string_view(argv[1]) == "--info") {
            displayInfo();
            std::exit(0);
        }

        if (argc != 3) die();

        return ProgramArgs{
			.image_file_path   = argv[1],
			.archive_file_path = argv[2]
        };
    }
};

[[nodiscard]] static std::expected<std::size_t, std::string> searchSig(std::span<const Byte> data, std::span<const Byte> sig, std::size_t start_index = 0, std::size_t increment = 0) {
	if (start_index >= data.size()) {
		return std::unexpected("start_index out of bounds");
	}

    auto search_range = std::views::drop(data, start_index + increment);
    auto result = std::ranges::search(search_range, sig);

    if (result.empty()) {
		return std::unexpected("signature not found");
	}
    return static_cast<std::size_t>(result.begin() - data.begin());
}

static void updateValue(std::span<Byte> data, std::size_t index, std::size_t value, std::size_t length, bool isBigEndian = true) {
    std::size_t write_index = isBigEndian ? index : index - (length - 1);

    if (write_index + length > data.size()) { 
        throw std::out_of_range("updateValue: Index out of bounds.");
    }

    switch (length) {
        case 2: {
            uint16_t val = static_cast<uint16_t>(value);
            if (isBigEndian) {
                val = std::byteswap(val);
            }
            std::memcpy(data.data() + write_index, &val, length);
            break;
        }
        case 4: {
            uint32_t val = static_cast<uint32_t>(value);
            if (isBigEndian) {
                val = std::byteswap(val);
            }
            std::memcpy(data.data() + write_index, &val, length);
            break;
        }
        case 8: {
            uint64_t val = static_cast<uint64_t>(value);
            if (isBigEndian) {
                val = std::byteswap(val);
            }
            std::memcpy(data.data() + write_index, &val, length);
            break;
        }
        default:
            throw std::invalid_argument("updateValue: Invalid byte length. Must be 2, 4, or 8.");
    }
}

[[nodiscard]] static std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length, bool isBigEndian = true) {
    std::size_t read_index = isBigEndian ? index : index - (length - 1);

    if (read_index + length > data.size()) {
        throw std::out_of_range("getValue: Index out of bounds");
    }

    switch (length) {
        case 2: {
            uint16_t value;
            std::memcpy(&value, data.data() + read_index, length);
            if (isBigEndian) {
                value = std::byteswap(value);
            }
            return value;
        }
        case 4: {
            uint32_t value;
            std::memcpy(&value, data.data() + read_index, length);
            if (isBigEndian) {
                value = std::byteswap(value);
            }
            return value;
        }
        case 8: {
            uint64_t value;
            std::memcpy(&value, data.data() + read_index, length);
            if (isBigEndian) {
                value = std::byteswap(value);
            }
            return value;
        }
        default:
            throw std::out_of_range("getValue: Invalid bytes value. 2, 4 or 8 only.");
    }
}

[[nodiscard]] static bool hasValidFilename(const fs::path& p) {
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

[[nodiscard]] static bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
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

[[nodiscard]] static vBytes readFile(const fs::path& path, FileTypeCheck FileType = FileTypeCheck::archive_file) {
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
    }

    std::size_t file_size = fs::file_size(path);

    if (!file_size) {
        throw std::runtime_error("Error: File is empty.");
    }

	if (!hasValidFilename(path)) {
        throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
    }

    if (FileType == FileTypeCheck::cover_image) {
        if (!hasFileExtension(path, {".png"})) {
            throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".png\".");
        }

		constexpr std::size_t
			MINIMUM_IMAGE_SIZE = 87,
			MAX_IMAGE_SIZE	   = 4 * 1024 * 1024;

		if (MINIMUM_IMAGE_SIZE > file_size) {
			throw std::runtime_error("File Error: Invalid image file size.");
		}

		if (file_size > MAX_IMAGE_SIZE) {
			throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
		}
    }

	if (FileType == FileTypeCheck::archive_file) {
		constexpr std::size_t
			MAX_ARCHIVE_SIZE 	 = 2ULL * 1024 * 1024 * 1024,
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
		vec.insert(vec.end(),   IDAT_CRC_BYTES.begin(), 	 IDAT_CRC_BYTES.end());

		constexpr std::size_t INDEX_DIFF = 8;

		constexpr auto ARCHIVE_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 });

		if (!std::equal(ARCHIVE_SIG.begin(), ARCHIVE_SIG.end(), std::begin(vec) + INDEX_DIFF)) {
			throw std::runtime_error("Archive File Error: Signature check failure. Not a valid archive file.");
		}
	}
    return vec;
}

static void resizeImage(vBytes& image_file_vec) {
    constexpr Byte
		MIN_DIMENSION 		= 68,
		DIMENSION_REDUCTION = 1;

    constexpr float SAMPLING_OFFSET = 0.5f;

    vBytes temp_vec;
    lodepng::State decodeState;
    decodeState.decoder.color_convert = 0;

    unsigned
		width  = 0, 
		height = 0,
		error  = lodepng::decode(temp_vec, width, height, decodeState, image_file_vec);

    if (error) {
        throw std::runtime_error("LodePNG decoder error: " + std::to_string(error));
    }
    if (width <= MIN_DIMENSION || height <= MIN_DIMENSION) {
        throw std::runtime_error("LodePNG: Image dimensions too small to reduce.");
    }

    const unsigned
		new_width  = width - DIMENSION_REDUCTION,
		new_height = height - DIMENSION_REDUCTION,
		channels   = lodepng_get_channels(&decodeState.info_raw);

    const bool is_palette = (decodeState.info_raw.colortype == LCT_PALETTE);

    vBytes resized_vec(new_width * new_height * channels);

    const float
		x_ratio = static_cast<float>(width)  / new_width,
		y_ratio = static_cast<float>(height) / new_height;

    for (unsigned y = 0; y < new_height; ++y) {
		for (unsigned x = 0; x < new_width; ++x) {
			const float orig_x = std::clamp(
                (x + SAMPLING_OFFSET) * x_ratio - SAMPLING_OFFSET,
                0.0f,
                static_cast<float>(width - 1)
            );
            const float orig_y = std::clamp(
                (y + SAMPLING_OFFSET) * y_ratio - SAMPLING_OFFSET,
                0.0f,
                static_cast<float>(height - 1)
            );

            if (is_palette) {
                const unsigned
					ix = static_cast<unsigned>(std::round(orig_x)),
					iy = static_cast<unsigned>(std::round(orig_y));
                resized_vec[y * new_width + x] = temp_vec[iy * width + ix];
            } else {
                const unsigned
					x0 = static_cast<unsigned>(orig_x),
					y0 = static_cast<unsigned>(orig_y),
					x1 = std::min(x0 + 1, width - 1),
					y1 = std::min(y0 + 1, height - 1);

                const float
					dx = orig_x - x0,
					dy = orig_y - y0,
					wx = 1.0f - dx,
					wy = 1.0f - dy;

                for (unsigned c = 0; c < channels; ++c) {
                    const float value =
                        wx * wy * temp_vec[channels * (y0 * width + x0) + c] +
                        wx * dy * temp_vec[channels * (y1 * width + x0) + c] +
                        dx * wy * temp_vec[channels * (y0 * width + x1) + c] +
                        dx * dy * temp_vec[channels * (y1 * width + x1) + c];
                    resized_vec[channels * (y * new_width + x) + c] = static_cast<Byte>(std::round(value));
                }
            }
        }
    }

    lodepng::State encodeState;
    encodeState.info_raw.colortype       = decodeState.info_png.color.colortype;
    encodeState.info_raw.bitdepth        = decodeState.info_png.color.bitdepth;
    encodeState.info_png.color.colortype = decodeState.info_png.color.colortype;
    encodeState.info_png.color.bitdepth  = decodeState.info_png.color.bitdepth;
    encodeState.encoder.auto_convert     = 0;

    if (is_palette) {
        constexpr std::size_t RGBA_COMPONENTS = 4;
        for (std::size_t i = 0; i < decodeState.info_png.color.palettesize; ++i) {
            const Byte* p = &decodeState.info_png.color.palette[i * RGBA_COMPONENTS];
            lodepng_palette_add(&encodeState.info_png.color, p[0], p[1], p[2], p[3]);
            lodepng_palette_add(&encodeState.info_raw, p[0], p[1], p[2], p[3]);
        }
    }

    vBytes output_image_vec;
    error = lodepng::encode(output_image_vec, resized_vec, new_width, new_height, encodeState);
    if (error) {
        throw std::runtime_error("LodePNG encoder error: " + std::to_string(error));
    }

    image_file_vec = std::move(output_image_vec);
}

static void imageCheck(vBytes& image_file_vec) {
    constexpr uint16_t
		MIN_RGB_COLORS 		   = 257,
		MAX_INDEXED_PLTE_DIMS  = 4096,
		MAX_TRUECOLOR_RGB_DIMS = 900;

    constexpr std::size_t
		COLOR_TYPE_INDEX = 0x19,
		RGBA_COMPONENTS  = 4,
		RGB_COMPONENTS   = 3;

    constexpr Byte
		ALPHA_OPAQUE 	  = 255,
		MIN_DIMENSION 	  = 68,
		TRUECOLOR_RGBA    = 6,
		INDEXED_PLTE 	  = 3,
		TRUECOLOR_RGB 	  = 2,
		PALETTE_BIT_DEPTH = 8;

    vBytes image;

    lodepng::State state;
    state.decoder.zlibsettings.ignore_adler32 = 1;

    unsigned
		width  = 0, 
		height = 0,
		error  = lodepng::decode(image, width, height, state, image_file_vec);

    if (error) {
        throw std::runtime_error(std::format("LodePNG Error: {}: {}", error, lodepng_error_text(error)));
    }

    LodePNGColorStats stats;
    lodepng_color_stats_init(&stats);
    stats.allow_palette = 1;
    stats.allow_greyscale = 0;
    error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);

    if (error) {
        throw std::runtime_error("LodePNG stats error: " + std::to_string(error));
    }

    Byte color_type = static_cast<Byte>(state.info_png.color.colortype);

    if ((color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA) &&
        (MIN_RGB_COLORS > stats.numcolors)) {

        const std::size_t
			palette_size = stats.numcolors,
			channels = (state.info_raw.colortype == LCT_RGBA) ? RGBA_COMPONENTS : RGB_COMPONENTS;

        vBytes palette(palette_size * RGBA_COMPONENTS);
        std::unordered_map<uint32_t, Byte> color_to_index;

        for (std::size_t i = 0; i < palette_size; ++i) {
            const Byte* p = &stats.palette[i * RGBA_COMPONENTS];

            palette[i * RGBA_COMPONENTS]     = p[0];
            palette[i * RGBA_COMPONENTS + 1] = p[1];
            palette[i * RGBA_COMPONENTS + 2] = p[2];
            palette[i * RGBA_COMPONENTS + 3] = p[3];

            const uint32_t key = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            color_to_index[key] = static_cast<Byte>(i);
        }

        vBytes indexed_image(width * height);
        for (std::size_t i = 0; i < width * height; ++i) {
            const Byte
				r = image[i * channels],
				g = image[i * channels + 1],
				b = image[i * channels + 2],
				a = (channels == RGBA_COMPONENTS) ? image[i * channels + 3] : ALPHA_OPAQUE;

            const uint32_t key = (r << 24) | (g << 16) | (b << 8) | a;
            indexed_image[i] = color_to_index[key];
        }

        lodepng::State encodeState;
        encodeState.info_raw.colortype 		 = LCT_PALETTE;
        encodeState.info_raw.bitdepth 		 = PALETTE_BIT_DEPTH;
        encodeState.info_png.color.colortype = LCT_PALETTE;
        encodeState.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
        encodeState.encoder.auto_convert 	 = 0;

        for (std::size_t i = 0; i < palette_size; ++i) {
            const Byte* p = &palette[i * RGBA_COMPONENTS];
            lodepng_palette_add(&encodeState.info_png.color, p[0], p[1], p[2], p[3]);
            lodepng_palette_add(&encodeState.info_raw, p[0], p[1], p[2], p[3]);
        }

        vBytes output;
        error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
        if (error) {
            throw std::runtime_error("LodePNG encode error: " + std::to_string(error));
        }
        image_file_vec = std::move(output);
    } else {
		constexpr auto
			PLTE_SIG = std::to_array<Byte>({ 0x50, 0x4C, 0x54, 0x45 }),
			TRNS_SIG = std::to_array<Byte>({ 0x74, 0x52, 0x4E, 0x53 }),
			IDAT_SIG = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });

		constexpr auto IEND_SIG = std::to_array<Byte>({ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 });
		
		if (auto pos_opt = searchSig(image_file_vec, IEND_SIG)) {
			constexpr std::size_t NAME_FIELD_CRC_FIELD_COMBIMBED_LENGTH = 8;

			const std::size_t PNG_STANDARD_END_INDEX = *pos_opt + NAME_FIELD_CRC_FIELD_COMBIMBED_LENGTH;

			if (PNG_STANDARD_END_INDEX <= image_file_vec.size()) {
				image_file_vec.erase(image_file_vec.begin() + static_cast<std::ptrdiff_t>(PNG_STANDARD_END_INDEX), image_file_vec.end());
			}
		}

		constexpr Byte
			PNG_FIRST_BYTES = 33,
			PNG_IEND_BYTES	= 12;

		vBytes copied_png;
		copied_png.reserve(image_file_vec.size());

		std::copy_n(image_file_vec.begin(), PNG_FIRST_BYTES, std::back_inserter(copied_png));

		auto copy_chunk_type = [&](const auto& chunk_signature) {
			constexpr std::size_t
				CHUNK_FIELDS_COMBINED_LENGTH = 12, 
				CHUNK_LENGTH_FIELD_SIZE		 = 4;

			std::size_t
				chunk_length 	   			= 0,
				new_chunk_index    			= 0,
				chunk_length_index 			= 0,
				increment_next_search_index = 0;

			while (auto chunk_opt = searchSig(image_file_vec, chunk_signature, new_chunk_index, increment_next_search_index)) {
				new_chunk_index = *chunk_opt;
				if (image_file_vec[new_chunk_index + 4] == 0x78 && image_file_vec[new_chunk_index + 5] == 0x5E && image_file_vec[new_chunk_index + 6] == 0x5C) { 
					break;
				}
				chunk_length_index = new_chunk_index - CHUNK_LENGTH_FIELD_SIZE;
				chunk_length = getValue(image_file_vec, chunk_length_index, CHUNK_LENGTH_FIELD_SIZE) + CHUNK_FIELDS_COMBINED_LENGTH;
				std::copy_n(image_file_vec.begin() + chunk_length_index, chunk_length, std::back_inserter(copied_png));
				new_chunk_index = new_chunk_index;
				increment_next_search_index = 5;
			}
		};

		if (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB) {
			if (color_type == INDEXED_PLTE) {
				copy_chunk_type(PLTE_SIG);
			}
			copy_chunk_type(TRNS_SIG);
		}

		copy_chunk_type(IDAT_SIG);

		std::copy_n(image_file_vec.end() - PNG_IEND_BYTES, PNG_IEND_BYTES, std::back_inserter(copied_png));

		image_file_vec = std::move(copied_png);
	}
	
	constexpr std::size_t
		IHDR_START_INDEX = 0x12,
		IHDR_STOP_INDEX  = 0x21;

	auto hasProblemCharacter = [&]() {
		return std::any_of(
			image_file_vec.begin() + IHDR_START_INDEX,
			image_file_vec.begin() + IHDR_STOP_INDEX,
			[](Byte b) {
				return std::find(LINUX_PROBLEM_CHARACTERS.begin(), LINUX_PROBLEM_CHARACTERS.end(), b) != LINUX_PROBLEM_CHARACTERS.end();
			}
		);
	};

	while (hasProblemCharacter()) {
		resizeImage(image_file_vec);
	}

	color_type = image_file_vec[COLOR_TYPE_INDEX] == TRUECOLOR_RGBA ? TRUECOLOR_RGB : image_file_vec[COLOR_TYPE_INDEX];   
	bool hasValidColorType = (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB);

	auto checkDimensions = [&](Byte COLOR_TYPE, uint16_t MAX_DIMS) {
		return (color_type == COLOR_TYPE && width <= MAX_DIMS && height <= MAX_DIMS && width >= MIN_DIMENSION && height >= MIN_DIMENSION);
	};

	bool hasValidDimensions = checkDimensions(TRUECOLOR_RGB, MAX_TRUECOLOR_RGB_DIMS) || checkDimensions(INDEXED_PLTE, MAX_INDEXED_PLTE_DIMS);

	if (!hasValidColorType || !hasValidDimensions) {
		if (!hasValidColorType) {
			std::print(stderr, "\nImage File Error: Color type of cover image is not supported.\n\n"
								"Supported types: PNG-32/24 (Truecolor) or PNG-8 (Indexed-Color).");
		} else {
			std::print(stderr, "\nImage File Error: Dimensions of cover image are not within the supported range.\n\n"
								"Supported ranges:\n"
								" - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n"
								" - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]\n");
		}
		throw std::runtime_error("Incompatible image. Aborting.");
	}
}

int main(int argc, char** argv) {
	try { 
			ProgramArgs args = ProgramArgs::parse(argc, argv);

			vBytes image_file_vec = readFile(args.image_file_path, FileTypeCheck::cover_image);

			vBytes archive_file_vec = readFile(args.archive_file_path);

			imageCheck(image_file_vec);

			std::size_t image_file_size = image_file_vec.size();

			const std::size_t IDAT_CHUNK_ARCHIVE_FILE_SIZE = archive_file_vec.size();
			constexpr std::size_t 
				IDAT_CHUNK_LENGTH_INDEX 	       	   = 0,
				ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX = 0x22, 
				ARC_RECORD_FIRST_FILENAME_INDEX        = 0x26,
				CHUNK_FIELDS_COMBINED_LENGTH		   = 12,
				ARC_RECORD_FIRST_FILENAME_MIN_LENGTH   = 4;
			
			std::size_t value_byte_length = 4;

			updateValue(archive_file_vec, IDAT_CHUNK_LENGTH_INDEX, (IDAT_CHUNK_ARCHIVE_FILE_SIZE - CHUNK_FIELDS_COMBINED_LENGTH), value_byte_length);

			const std::size_t ARC_RECORD_FIRST_FILENAME_LENGTH = archive_file_vec[ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX];

			if (ARC_RECORD_FIRST_FILENAME_MIN_LENGTH > ARC_RECORD_FIRST_FILENAME_LENGTH) {
				throw std::runtime_error("File Error:\n\nName length of first file within archive is too short.\nIncrease length (minimum 4 characters). Make sure it has a valid extension.");
			}

			constexpr auto EXTENSION_LIST = std::to_array<std::string_view>({
				"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
				"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
				"xvid", "pdf", "py", "ps1", "sh", "exe"
			});
		
			const std::string ARC_RECORD_FIRST_FILENAME{archive_file_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX, archive_file_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH};

			enum FileType { 
				VIDEO_AUDIO = 29, 
				PDF, 
				PYTHON, 
				POWERSHELL, 
				BASH_SHELL, 
				WINDOWS_EXECUTABLE, 
				UNKNOWN_FILE_TYPE, 
				FOLDER, 
				LINUX_EXECUTABLE, 
				JAR 
			};

			bool isZipFile = hasFileExtension(args.archive_file_path, {".zip"});

			std::size_t extension_list_index = (isZipFile) ? 0 : JAR;

			if (extension_list_index == JAR && (ARC_RECORD_FIRST_FILENAME != "META-INF/MANIFEST.MF" && ARC_RECORD_FIRST_FILENAME != "META-INF/")) {
				throw std::runtime_error("File Type Error: Archive file does not appear to be a valid JAR file.");
			}
	
			const std::size_t EXTENSION_POS = ARC_RECORD_FIRST_FILENAME.rfind('.');
		
			const std::string ARC_RECORD_FIRST_FILENAME_EXTENSION = (EXTENSION_POS != std::string::npos) ? ARC_RECORD_FIRST_FILENAME.substr(EXTENSION_POS + 1) : "?";
	
			if (isZipFile && ARC_RECORD_FIRST_FILENAME_EXTENSION  == "?") {
				extension_list_index = archive_file_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
			}
		
			if (isZipFile && extension_list_index != FOLDER && archive_file_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
				if (archive_file_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
					extension_list_index = FOLDER; 
				} else {
					throw std::runtime_error("ZIP File Error: Invalid folder name within ZIP archive."); 
				}
			}
	
			auto toLowerCase = [](std::string str) {
				std::ranges::transform(str, str.begin(), [](unsigned char c) {
					return static_cast<char>(std::tolower(c));
				});
				return str;
			};

			while (UNKNOWN_FILE_TYPE > extension_list_index) {
				if (EXTENSION_LIST[extension_list_index] == toLowerCase(ARC_RECORD_FIRST_FILENAME_EXTENSION)) {
					extension_list_index = std::max(extension_list_index, static_cast<std::size_t>(VIDEO_AUDIO));
					break;
				}
				extension_list_index++;
			}

			auto hasBalancedQuotes = [](std::string_view args_string) {
				std::size_t
					singleCount = 0,
					doubleCount = 0;

				for (std::size_t i = 0; i < args_string.size(); ++i) {
					const char c = args_string[i];
					const bool isEscaped = (i > 0 && args_string[i - 1] == '\\');

					if (c == '\'' && !isEscaped) {
						++singleCount;
					} else if (c == '"' && !isEscaped) {
						++doubleCount;
					}
				}
				return (singleCount % 2 == 0) && (doubleCount % 2 == 0);
			};

			std::string
				args_linux,
				args_windows,
				arguments;

			if ((extension_list_index > PDF && UNKNOWN_FILE_TYPE > extension_list_index) || extension_list_index == LINUX_EXECUTABLE || extension_list_index == JAR) {
				std::println("\nFor this file type, if required, you can provide command-line arguments here.");
				if (extension_list_index != WINDOWS_EXECUTABLE) {
					std::print("\nLinux: ");  
					std::getline(std::cin, args_linux);
					arguments = args_linux;
				}
				if (extension_list_index != LINUX_EXECUTABLE) {
					std::print("\nWindows: ");  
					std::getline(std::cin, args_windows);
					arguments = args_windows;
				}
			}
		
			if (!hasBalancedQuotes(args_linux) || !hasBalancedQuotes(args_windows)) {
        		throw std::runtime_error("Arguments Error: Quotes mismatch. Check arguments and try again.");
    		}	
    		
			vBytes script_file_vec { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 0x5F, 0x00, 0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00 };

			constexpr std::size_t
				EXTRACTION_SCRIPT_ELEMENT_INDEX = 0,
				MAX_SCRIPT_SIZE                 = 1500,
				SCRIPT_INDEX                    = 0x16;

			script_file_vec.reserve(script_file_vec.size() + MAX_SCRIPT_SIZE);

			const std::unordered_map<std::size_t, std::vector<uint16_t>> case_map = {
				{VIDEO_AUDIO,        { 0, 0x1E4, 0x1C }},
				{PDF,                { 1, 0x196, 0x1C }},
				{PYTHON,             { 2, 0x10B, 0x101, 0xBC, 0x1C }},
				{POWERSHELL,         { 3, 0x105, 0xFB,  0xB6, 0x33 }},
				{BASH_SHELL,         { 4, 0x134, 0x132, 0x8E, 0x1C }},
				{WINDOWS_EXECUTABLE, { 5, 0x116, 0x114 }},
				{FOLDER,             { 6, 0x149, 0x1C }},
				{LINUX_EXECUTABLE,   { 7, 0x8E,  0x1C }},
				{JAR,                { 8, 0xA6,  0x61 }},
				{UNKNOWN_FILE_TYPE,  { 9, 0x127, 0x1C }} 
			};

			if (!case_map.contains(extension_list_index)) {
				extension_list_index = static_cast<std::size_t>(UNKNOWN_FILE_TYPE);
			}

			const auto& case_values_vec = case_map.at(extension_list_index);

			const uint16_t EXTRACTION_SCRIPT = case_values_vec[EXTRACTION_SCRIPT_ELEMENT_INDEX];

			static const std::vector<std::vector<Byte>> extraction_scripts_vec = [] {
				constexpr auto CRLF = "\r\n"sv;
				constexpr std::array<std::pair<std::string_view, std::string_view>, 10> scripts = {{
					// VIDEO_AUDIO
					{
						R"(ITEM="";DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v vlc >$NUL 2>&1;then clear;vlc --play-and-exit --no-video-title-show "$ITEM" &> $NUL;elif command -v mpv >$NUL 2>&1;then clear;mpv --quiet "$ITEM" &> $NUL;else clear;fi;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&exit)"sv
					},
					// PDF
					{
						R"(ITEM="";DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v evince >$NUL 2>&1;then clear;evince "$ITEM" &> $NUL;else firefox "$ITEM" &> $NUL;clear;fi;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&exit)"sv
					},
					// PYTHON
					{
						R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v python3 >/dev/null 2>&1;then clear;python3 "$ITEM" ;else clear;fi;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set ITEM=&set ARGS=&set APP=python3&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&where !APP! >nul 2>&1 && (!APP! "!ITEM!" !ARGS! ) || (cls&exit)&echo.&exit)"sv
					},
					// POWERSHELL
					{
						R"(DIR="pdvzip_extracted";ITEM="";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v pwsh >/dev/null 2>&1;then clear;pwsh "$ITEM" ;else clear;fi;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set ITEM=&set ARGS=&set DIR=pdvzip_extracted&set PDIR="%SystemDrive%\Program Files\PowerShell\"&cls&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&IF EXIST !PDIR! (pwsh -ExecutionPolicy Bypass -File "!ITEM!" !ARGS!&echo.&exit) ELSE (powershell -ExecutionPolicy Bypass -File "!ITEM!" !ARGS!&echo.&exit))"sv
					},
					// BASH_SHELL
					{
						R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" ;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&"" &cls&exit)"sv
					},
					// WINDOWS_EXECUTABLE
					{
						R"(DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";clear;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&"" &echo.&exit)"sv
					},
					// FOLDER
					{
						R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM" &> /dev/null;clear;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&powershell "II ''"&cls&exit)"sv
					},
					// LINUX_EXECUTABLE
					{
						R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" ;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&cls&exit)"sv
					},
					// JAR
					{
						R"(clear;hash -r;if command -v java >/dev/null 2>&1;then clear;java -jar "$0" ;else clear;fi;exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set ARGS=&set APP=java&cls&where !APP! >nul 2>&1 && (!APP! -jar "%~dpnx0" !ARGS! ) || (cls)&ren "%~dpnx0" *.png&echo.&exit)"sv
					},
					// UNKNOWN_FILE_TYPE
					{
						R"(ITEM="";DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM";exit;)"sv,
						R"(#&cls&setlocal EnableDelayedExpansion&set DIR=pdvzip_extracted&mkdir .\!DIR!&move "%~dpnx0" .\!DIR!&cd .\!DIR!&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&""&echo.&exit)"sv
					}
				}};

				std::vector<std::vector<Byte>> result;
				result.reserve(scripts.size());
				for (const auto& [linux_part, windows_part] : scripts) {
					vBytes combined;
					combined.reserve(linux_part.size() + CRLF.size() + windows_part.size());
					combined.insert(combined.end(), linux_part.begin(), linux_part.end());
					combined.insert(combined.end(), CRLF.begin(), CRLF.end());
					combined.insert(combined.end(), windows_part.begin(), windows_part.end());
					result.push_back(std::move(combined));
				}
				return result;
			}();

			script_file_vec.insert(script_file_vec.begin() + SCRIPT_INDEX, extraction_scripts_vec[EXTRACTION_SCRIPT].begin(), extraction_scripts_vec[EXTRACTION_SCRIPT].end());

			if (extension_list_index == static_cast<std::size_t>(WINDOWS_EXECUTABLE) || extension_list_index == static_cast<std::size_t>(LINUX_EXECUTABLE)) {
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[1], arguments.begin(), arguments.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
			} else if (extension_list_index == static_cast<std::size_t>(JAR)) {
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[2], args_linux.begin(), args_linux.end());
			} else if (extension_list_index > static_cast<std::size_t>(PDF) && static_cast<std::size_t>(WINDOWS_EXECUTABLE) > extension_list_index) {
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[3], args_linux.begin(), args_linux.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[4], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
			} else {
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[1], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
				script_file_vec.insert(script_file_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
			}

			std::size_t
				iccp_chunk_length_index 	   	   = 0,
				iccp_chunk_length_first_byte_index = 3,
				iccp_chunk_script_size 		       = script_file_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;

			updateValue(script_file_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_byte_length);

			const Byte ICCP_CHUNK_LENGTH_FIRST_BYTE_VALUE = script_file_vec[iccp_chunk_length_first_byte_index];
	
			if (std::ranges::contains(LINUX_PROBLEM_CHARACTERS, ICCP_CHUNK_LENGTH_FIRST_BYTE_VALUE)) {
					constexpr std::string_view INCREASE_CHUNK_LENGTH_STRING = "........";
					constexpr std::size_t INSERT_STRING_INDEX_DIFF = 8;
					script_file_vec.insert(script_file_vec.begin() + iccp_chunk_script_size + INSERT_STRING_INDEX_DIFF, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
					iccp_chunk_script_size = script_file_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
					updateValue(script_file_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_byte_length);
			}
	
			constexpr std::size_t
				ICCP_CHUNK_INDEX 						= 0x21,
				ICCP_CHUNK_NAME_INDEX 					= 0x04,
				LAST_IDAT_CHUNK_CRC_INDEX_DIFF 			= 16,
				ARCHIVE_VEC_INSERT_INDEX_DIFF			= 12,
				ICCP_CHUNK_CRC_INDEX_DIFF 				= 8,
				EXCLUDE_SIZE_FIELD_AND_CRC_FIELD_LENGTH = 8,
				LAST_IDAT_INDEX_DIFF 					= 4,
				ICCP_CHUNK_NAME_FIELD_LENGTH 			= 4;

			if (iccp_chunk_script_size > MAX_SCRIPT_SIZE) {
				throw std::runtime_error("Script Size Error: Extraction script exceeds size limit.");
			}

			const std::size_t 
				ICCP_CHUNK_LENGTH = iccp_chunk_script_size + ICCP_CHUNK_NAME_FIELD_LENGTH,
				ICCP_CHUNK_CRC 	  = lodepng_crc32(&script_file_vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH);

			std::size_t iccp_chunk_crc_index = iccp_chunk_script_size + ICCP_CHUNK_CRC_INDEX_DIFF;

			updateValue(script_file_vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_byte_length);

			image_file_vec.reserve(image_file_vec.size() + script_file_vec.size() + archive_file_vec.size());

			image_file_vec.insert((image_file_vec.begin() + ICCP_CHUNK_INDEX), script_file_vec.begin(), script_file_vec.end());
			script_file_vec = vBytes{};
		
			image_file_vec.insert((image_file_vec.end() - ARCHIVE_VEC_INSERT_INDEX_DIFF), archive_file_vec.begin(), archive_file_vec.end());
			archive_file_vec = vBytes{};
	
			const std::size_t
				LAST_IDAT_INDEX 	= image_file_size + iccp_chunk_script_size + LAST_IDAT_INDEX_DIFF, // Important to use the old image size before the above inserts.
				COMPLETE_IMAGE_SIZE = image_file_vec.size();  	      				  				   // Image size updated to include the inserts.

			constexpr std::size_t
				CENTRAL_LOCAL_INDEX_DIFF 	  = 45,
				ZIP_COMMENT_LENGTH_INDEX_DIFF = 21,
				END_CENTRAL_START_INDEX_DIFF  = 19,
				ZIP_RECORDS_INDEX_DIFF 		  = 11,
				PNG_IEND_LENGTH 			  = 16,
				ZIP_LOCAL_INDEX_DIFF 		  = 4,
				ZIP_SIG_LENGTH 				  = 4;
				
			constexpr auto
				ZIP_LOCAL_SIG 		  = std::to_array<Byte>({ 0x50, 0x4B, 0x03, 0x04 }),
				END_CENTRAL_DIR_SIG	  = std::to_array<Byte>({ 0x50, 0x4B, 0x05, 0x06 }),
				START_CENTRAL_DIR_SIG = std::to_array<Byte>({ 0x50, 0x4B, 0x01, 0x02 });
						 
			auto distance = static_cast<std::size_t>(std::distance(image_file_vec.rbegin(), std::search(image_file_vec.rbegin(), image_file_vec.rend(), END_CENTRAL_DIR_SIG.rbegin(), END_CENTRAL_DIR_SIG.rend())));

			const std::size_t END_CENTRAL_DIR_INDEX = COMPLETE_IMAGE_SIZE - distance - ZIP_SIG_LENGTH;

			std::size_t
				total_zip_records_index  = END_CENTRAL_DIR_INDEX + ZIP_RECORDS_INDEX_DIFF,
				end_central_start_index  = END_CENTRAL_DIR_INDEX + END_CENTRAL_START_INDEX_DIFF,
				zip_comment_length_index = END_CENTRAL_DIR_INDEX + ZIP_COMMENT_LENGTH_INDEX_DIFF,
				zip_record_local_index 	 = LAST_IDAT_INDEX + ZIP_LOCAL_INDEX_DIFF, 
				start_central_dir_index  = 0; 
		
			value_byte_length = 2;
				
			bool isBigEndian = false;
	
			uint16_t 	
				total_zip_records = static_cast<uint16_t>(getValue(image_file_vec, total_zip_records_index,  value_byte_length,  isBigEndian)),
				zip_comment_length = static_cast<uint16_t>(getValue(image_file_vec, zip_comment_length_index, value_byte_length,  isBigEndian)) + PNG_IEND_LENGTH, // Extend comment length. Required for JAR.
				record_count = 0;
						 
			updateValue(image_file_vec, zip_comment_length_index, zip_comment_length, value_byte_length, isBigEndian);

			// Find the first, top "start_central directory" index location by searching the vector, working backwards from the vector's content.
			// By starting the search from the end of the vector, we know we are already within the record section data of the archive and this
			// helps in avoiding the increased probability of a "false-positive" signature match if we were to search the vector from the beginning and read through image & file data first.

			auto search_it = image_file_vec.rbegin();
				
			while (total_zip_records > record_count++) {
				search_it = std::search(search_it, image_file_vec.rend(), START_CENTRAL_DIR_SIG.rbegin(), START_CENTRAL_DIR_SIG.rend());
				start_central_dir_index = COMPLETE_IMAGE_SIZE - static_cast<std::size_t>(std::distance(image_file_vec.rbegin(), search_it++)) - ZIP_SIG_LENGTH;
			}

			std::size_t central_dir_local_index = start_central_dir_index + CENTRAL_LOCAL_INDEX_DIFF; 

			value_byte_length = 4;
			updateValue(image_file_vec, end_central_start_index, start_central_dir_index, value_byte_length, isBigEndian);  
	
			constexpr std::size_t INCREMENT_SEARCH_INDEX = 1;

			while (total_zip_records--) {
				updateValue(image_file_vec, central_dir_local_index, zip_record_local_index, value_byte_length, isBigEndian); 	
				auto zip_result_opt  	= searchSig(image_file_vec, ZIP_LOCAL_SIG, zip_record_local_index, INCREMENT_SEARCH_INDEX); 	  				
				zip_record_local_index  = *zip_result_opt;
				auto central_result_opt = searchSig(image_file_vec, START_CENTRAL_DIR_SIG, central_dir_local_index, INCREMENT_SEARCH_INDEX);			
				central_dir_local_index = *central_result_opt + CENTRAL_LOCAL_INDEX_DIFF;
			}

			const std::size_t LAST_IDAT_CHUNK_CRC = lodepng_crc32(&image_file_vec[LAST_IDAT_INDEX], IDAT_CHUNK_ARCHIVE_FILE_SIZE - EXCLUDE_SIZE_FIELD_AND_CRC_FIELD_LENGTH);

			std::size_t last_idat_chunk_crc_index = COMPLETE_IMAGE_SIZE - LAST_IDAT_CHUNK_CRC_INDEX_DIFF;
	
			updateValue(image_file_vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_byte_length);
	
			std::random_device rd;
    		std::mt19937 gen(rd());
    		std::uniform_int_distribution<> dist(10000, 99999);  

			constexpr std::string_view
				PREFIX_ZIP = "pzip_",
				PREFIX_JAR = "pjar_";

			const auto PREFIX = isZipFile ? PREFIX_ZIP : PREFIX_JAR;
			const std::string POLYGLOT_FILENAME = std::format("{}{}.png", PREFIX, dist(gen));

			std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
			if (!file_ofs) {
				throw std::runtime_error("Write File Error: Unable to write to file.");
			}
	
			file_ofs.write(reinterpret_cast<const char*>(image_file_vec.data()), COMPLETE_IMAGE_SIZE);
	
			image_file_vec = vBytes{};

			std::print("\nCreated {} polyglot image file: {} ({} bytes).\n\nComplete!\n\n", isZipFile ? "PNG-ZIP" : "PNG-JAR", POLYGLOT_FILENAME, COMPLETE_IMAGE_SIZE);

			// Attempt to set executable permissions for the newly created polyglot image file.
			#ifdef __unix__
	    		if (chmod(POLYGLOT_FILENAME.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        			std::println(stderr, "\nWarning: Could not set executable permissions for {}.\nYou will need do this manually using chmod.", POLYGLOT_FILENAME);
	    		}
			#endif

			return 0;		
    }
    catch (const std::runtime_error& e) {
		std::println(stderr, "\n{}\n", e.what());
		return 1;
	}
}
