// 	PNG Data Vehicle, ZIP/JAR Edition (PDVZIP v3.5)
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

#include "programArgs.h"    
#include "fileChecks.h" 
#include "extractionScripts.h"
#include "lodepng/lodepng.h"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <cstddef>
#include <random>   
#include <array>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

#ifdef __unix__
	#include <sys/stat.h>      
#endif

int main(int argc, char** argv) {
	try {
			ProgramArgs args = ProgramArgs::parse(argc, argv);

			std::vector<uint8_t> 
        			image_vec,
        			archive_vec;
		
			uintmax_t 
				image_size = 0,
				archive_file_size = 0;
	
			validateFiles(args.cover_image, args.archive_file, image_size, archive_file_size, image_vec, archive_vec);

			auto searchSig = []<typename T, size_t N>(std::vector<uint8_t>& vec, uint32_t start_index, const uint8_t INCREMENT_SEARCH_INDEX, const std::array<T, N>& SIG) -> uint32_t {
    			return static_cast<uint32_t>(std::search(vec.begin() + start_index + INCREMENT_SEARCH_INDEX, vec.end(), SIG.begin(), SIG.end()) - vec.begin());
			};
		
			auto updateValue = [](std::vector<uint8_t>& vec, uint32_t insert_index, const uint32_t NEW_VALUE, uint8_t bits, bool isBigEndian) {
				while (bits) {
					static_cast<uint_fast32_t>(vec[isBigEndian ? insert_index++ : insert_index--] = (NEW_VALUE >> (bits -= 8)) & 0xff);
				}
			};
		
			// Check cover image for supported image dimensions and color type values.
			constexpr uint8_t
				COLOR_TYPE_INDEX 	= 0x19,
				MIN_DIMS 			= 68,
				TRUECOLOR_RGBA		= 6,
				INDEXED_PLTE 		= 3,
				TRUECOLOR_RGB 		= 2;
			
			constexpr uint16_t
				MAX_INDEXED_PLTE_DIMS 	= 4096,
				MAX_TRUECOLOR_RGB_DIMS 	= 900;
			
			unsigned
				width = 0,
				height = 0;
					
			std::vector<unsigned char> image; 
   			lodepng::State state;
    		unsigned error = lodepng::decode(image, width, height, state, image_vec);
    		if (error) {
    			throw std::runtime_error("Lodepng decoder error: " + std::to_string(error));
    		}
   		
   			uint8_t color_type = static_cast<uint8_t>(state.info_png.color.colortype);
   		
   			// Make sure Truecolor image has more than 256 colors, if not, attempt to convert it to PNG-8 for compatibility requirements. 
   			if (color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA) {
	    		LodePNGColorStats stats;
    			lodepng_color_stats_init(&stats);
    			stats.allow_palette = 1; 
    			stats.allow_greyscale = 0; 

    			error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
    			if (error) {
    				throw std::runtime_error("Lodepng stats error: " + std::to_string(error));
    			}

    			// Check if the image has 256 or fewer unique colors
    			if (stats.numcolors <= 256) {
        			std::vector<unsigned char> indexed_image(width * height);
        			std::vector<unsigned char> palette;
        			size_t palette_size = stats.numcolors > 0 ? stats.numcolors : 1;

        			if (stats.numcolors > 0) {
            			for (size_t i = 0; i < palette_size; ++i) {
                			palette.push_back(stats.palette[i * 4]);     // R
                			palette.push_back(stats.palette[i * 4 + 1]); // G
                			palette.push_back(stats.palette[i * 4 + 2]); // B
                			palette.push_back(stats.palette[i * 4 + 3]); // A
            			}
        			} else {
            				// Handle edge case: single color or empty palette
            				unsigned char r = 0, g = 0, b = 0, a = 255; // Default: black, opaque
            				if (!image.empty()) {
                				r = image[0];
                				g = image[1];
                				b = image[2];
                				if (state.info_raw.colortype == LCT_RGBA) a = image[3];
            				}
            				palette.push_back(r);
            				palette.push_back(g);
            				palette.push_back(b);
            				palette.push_back(a);
        			}
        			size_t channels = (state.info_raw.colortype == LCT_RGBA) ? 4 : 3;
        			for (size_t i = 0; i < width * height; ++i) {
            			unsigned char r = image[i * channels];
            			unsigned char g = image[i * channels + 1];
            			unsigned char b = image[i * channels + 2];
            			unsigned char a = (channels == 4) ? image[i * channels + 3] : 255;

            			size_t index = 0;
            			for (size_t j = 0; j < palette_size; ++j) {
                			if (palette[j * 4] == r && palette[j * 4 + 1] == g && palette[j * 4 + 2] == b && palette[j * 4 + 3] == a) {
                    			index = j;
                    			break;
                			}
            			}
            			indexed_image[i] = static_cast<unsigned char>(index);
        			}
        			lodepng::State encodeState;
        			encodeState.info_raw.colortype = LCT_PALETTE; 
        			encodeState.info_raw.bitdepth = 8; 
        			encodeState.info_png.color.colortype = LCT_PALETTE;
        			encodeState.info_png.color.bitdepth = 8;
        			encodeState.encoder.auto_convert = 0; 

        			for (size_t i = 0; i < palette_size; ++i) {
            			lodepng_palette_add(&encodeState.info_png.color, palette[i * 4], palette[i * 4 + 1], palette[i * 4 + 2], palette[i * 4 + 3]);
            			lodepng_palette_add(&encodeState.info_raw, palette[i * 4], palette[i * 4 + 1], palette[i * 4 + 2], palette[i * 4 + 3]);
        			}
        			std::vector<unsigned char> output;
        			error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
        			if (error) {
            			throw std::runtime_error("Lodepng encode error: " + std::to_string(error));
        			}
        			image_vec.swap(output);
        			std::vector<uint8_t>().swap(output);	
        			std::vector<uint8_t>().swap(image);
        			std::vector<uint8_t>().swap(indexed_image);
        			std::vector<uint8_t>().swap(palette);
   	 			}		 
			}
		
			color_type = image_vec[COLOR_TYPE_INDEX] == TRUECOLOR_RGBA ? TRUECOLOR_RGB : image_vec[COLOR_TYPE_INDEX];   // Color type may of changed, update variable.
		
			bool hasValidColorType = (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB);

			auto checkDimensions = [&](uint8_t COLOR_TYPE, uint16_t MAX_DIMS) {
				return (color_type == COLOR_TYPE && width <= MAX_DIMS && height <= MAX_DIMS && width >= MIN_DIMS && height >= MIN_DIMS);
			};
			
			bool hasValidDimensions = checkDimensions(TRUECOLOR_RGB, MAX_TRUECOLOR_RGB_DIMS) || checkDimensions(INDEXED_PLTE, MAX_INDEXED_PLTE_DIMS);
			
			if (!hasValidColorType || !hasValidDimensions) {
    			std::cerr << "\nImage File Error: ";
    			if (!hasValidColorType) {
        			std::cerr << "Color type of cover image is not supported.\n\nSupported types: PNG-32/24 (Truecolor) or PNG-8 (Indexed-Color).";
    			} else {
        			std::cerr << "Dimensions of cover image are not within the supported range.\n\n";
        			std::cerr << "Supported ranges:\n - PNG-32/24 Truecolor: [68 x 68] to [900 x 900]\n - PNG-8 Indexed-Color: [68 x 68] to [4096 x 4096]\n";
    			}
    			throw std::runtime_error("Incompatible image. Aborting.");
			}
		
			// A selection of "problem characters" may appear within the cover image's width/height dimension fields of the IHDR chunk or within the 4-byte IHDR chunk's CRC field.
			// These characters will break the Linux extraction script. If any of these characters are detected, the program will attempt to decrease the width/height dimension size
			// of the image by 1 pixel value, repeated if necessary, until no problem characters are found within the dimension size fields or the IHDR chunk's CRC field.
			auto resizeImage = [](std::vector<uint8_t>& image_vec) {
    			std::vector<unsigned char> temp_vec;
    			unsigned width, height;
    			lodepng::State decodeState;

    			decodeState.decoder.color_convert = 0; 

    			unsigned error = lodepng::decode(temp_vec, width, height, decodeState, image_vec);
    			if (error) {
        			throw std::runtime_error("LodePNG decoder error: " + std::to_string(error));
    			}

    			if (width <= 1 || height <= 1) {
        			throw std::runtime_error("LodePNG: Image dimensions too small to reduce.");
    			}

    			unsigned 
    				new_width = width - 1,
    				new_height = height - 1;

    			unsigned channels = lodepng_get_channels(&decodeState.info_raw);

    			std::vector<unsigned char> resized_vec(new_width * new_height * channels);
    			float x_ratio = static_cast<float>(width) / new_width;
    			float y_ratio = static_cast<float>(height) / new_height;

    			if (decodeState.info_raw.colortype == LCT_PALETTE) {
        			for (unsigned y = 0; y < new_height; ++y) {
            			for (unsigned x = 0; x < new_width; ++x) {
                			float orig_x = (x + 0.5f) * x_ratio - 0.5f;
                			float orig_y = (y + 0.5f) * y_ratio - 0.5f;
                			orig_x = std::max(0.0f, std::min(orig_x, static_cast<float>(width - 1)));
                			orig_y = std::max(0.0f, std::min(orig_y, static_cast<float>(height - 1)));
                			unsigned ix = static_cast<unsigned>(std::round(orig_x));
                			unsigned iy = static_cast<unsigned>(std::round(orig_y));
                			resized_vec[y * new_width + x] = temp_vec[iy * width + ix];
            			}
        			}
    			} else {
					for (unsigned y = 0; y < new_height; ++y) {
            			for (unsigned x = 0; x < new_width; ++x) {
                			float orig_x = (x + 0.5f) * x_ratio - 0.5f;
                			float orig_y = (y + 0.5f) * y_ratio - 0.5f;
                			orig_x = std::max(0.0f, std::min(orig_x, static_cast<float>(width - 1)));
                			orig_y = std::max(0.0f, std::min(orig_y, static_cast<float>(height - 1)));
                			unsigned x0 = static_cast<unsigned>(std::floor(orig_x));
                			unsigned y0 = static_cast<unsigned>(std::floor(orig_y));
                			unsigned x1 = std::min(x0 + 1, width - 1);
                			unsigned y1 = std::min(y0 + 1, height - 1);
                			float dx = orig_x - x0;
                			float dy = orig_y - y0;
                			float wx = 1.0f - dx;
                			float wy = 1.0f - dy;
                			for (unsigned c = 0; c < channels; ++c) {
                    			float value = wx * wy * temp_vec[channels * (y0 * width + x0) + c] + 
                    						  wx * dy * temp_vec[channels * (y1 * width + x0) + c] +
                                  			  dx * wy * temp_vec[channels * (y0 * width + x1) + c] +
                                  			  dx * dy * temp_vec[channels * (y1 * width + x1) + c];
                    			resized_vec[channels * (y * new_width + x) + c] = static_cast<unsigned char>(std::round(value));
                			}	
            			}
        			}
    			}
    			lodepng::State encodeState;
    			encodeState.info_raw.colortype = decodeState.info_png.color.colortype;
    			encodeState.info_raw.bitdepth = decodeState.info_png.color.bitdepth;
    			encodeState.info_png.color.colortype = decodeState.info_png.color.colortype;
    			encodeState.info_png.color.bitdepth = decodeState.info_png.color.bitdepth;
    			encodeState.encoder.auto_convert = 0; 

    			if (decodeState.info_png.color.colortype == LCT_PALETTE) {
        			for (size_t i = 0; i < decodeState.info_png.color.palettesize; ++i) {
            			lodepng_palette_add(&encodeState.info_png.color,
                        	decodeState.info_png.color.palette[i * 4],
                            decodeState.info_png.color.palette[i * 4 + 1],
                            decodeState.info_png.color.palette[i * 4 + 2],
                            decodeState.info_png.color.palette[i * 4 + 3]);
                                		
            			lodepng_palette_add(&encodeState.info_raw,
                            decodeState.info_png.color.palette[i * 4],
                            decodeState.info_png.color.palette[i * 4 + 1],
                        	decodeState.info_png.color.palette[i * 4 + 2],
                            decodeState.info_png.color.palette[i * 4 + 3]);
        			}
    			}
    			std::vector<unsigned char> output_image_vec;
    			error = lodepng::encode(output_image_vec, resized_vec, new_width, new_height, encodeState);
    			if (error) {
        			throw std::runtime_error("Lodepng encoder error: " + std::to_string(error));
    			}
    			
    			image_vec.swap(output_image_vec);
    			std::vector<uint8_t>().swap(output_image_vec);	
    			std::vector<uint8_t>().swap(temp_vec);	
    			std::vector<uint8_t>().swap(resized_vec);
			};

			constexpr uint8_t 
				TOTAL_BAD_CHARS  = 7,
				IHDR_START_INDEX = 0x12,
				IHDR_STOP_INDEX  = 0x21;

			constexpr std::array<uint8_t, TOTAL_BAD_CHARS> LINUX_PROBLEM_CHARACTERS { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 }; // This list could grow.
	
			bool isBadImage = true;

			while (isBadImage) {
    			isBadImage = false;
    			for (uint8_t index = IHDR_START_INDEX; IHDR_STOP_INDEX > index; ++index) { // Check IHDR chunk section where problem characters may occur.
        			if (std::find(LINUX_PROBLEM_CHARACTERS.begin(), LINUX_PROBLEM_CHARACTERS.end(), image_vec[index]) != LINUX_PROBLEM_CHARACTERS.end()) {
            			resizeImage(image_vec);
            			isBadImage = true;
            			break;
        			}
    			}
			}
	
			constexpr std::array<uint8_t, 4> 
				PLTE_SIG {0x50, 0x4C, 0x54, 0x45},
				TRNS_SIG {0x74, 0x52, 0x4E, 0x53},
				IDAT_SIG {0x49, 0x44, 0x41, 0x54};

    		constexpr uint8_t
				PNG_FIRST_BYTES = 33,
				PNG_IEND_BYTES = 12,
				CHUNK_FIELDS_COMBINED_LENGTH = 12,
				INCREMENT_NEXT_SEARCH_POS = 5;
			
			image_size = static_cast<uint32_t>(image_vec.size());
	
    		const uint32_t FIRST_IDAT_INDEX = searchSig(image_vec, 0, 0, IDAT_SIG);
    	
    		if (FIRST_IDAT_INDEX == image_size) {
    			throw std::runtime_error("Image Error: Invalid or corrupt image file. Expected IDAT chunk not found!");
    		}
    	
    		std::vector<uint8_t> copied_image_vec;
    		copied_image_vec.reserve(image_size);     
  
    		std::copy_n(image_vec.begin(), PNG_FIRST_BYTES, std::back_inserter(copied_image_vec));	

    		auto copy_chunk_type = [&](const auto& chunk_signature) {
				constexpr uint8_t CHUNK_LENGTH_FIELD_SIZE = 4;
				
        		uint32_t 
				chunk_search_pos = 0,
				chunk_length_pos = 0,
				chunk_count = 0,
				chunk_length = 0;
			
        		while (true) {
            		chunk_search_pos = searchSig(image_vec, chunk_search_pos, INCREMENT_NEXT_SEARCH_POS, chunk_signature);
            		
					if (chunk_signature != IDAT_SIG && chunk_search_pos > FIRST_IDAT_INDEX) {
						if (chunk_signature == PLTE_SIG && !chunk_count) {
							throw std::runtime_error("Image Error: Invalid or corrupt image file. Expected PLTE chunk not found!");
						} else {
							break;
						}
					} else if (chunk_search_pos == image_size) {
						break;
					}
			
					++chunk_count;
				
					chunk_length_pos = chunk_search_pos - CHUNK_LENGTH_FIELD_SIZE;
            			
            		for (uint8_t i = 0; i < CHUNK_LENGTH_FIELD_SIZE; ++i) {
        				chunk_length = (chunk_length << 8) | static_cast<uint64_t>(image_vec[chunk_length_pos + i]);
    				}
            			
            		chunk_length += CHUNK_FIELDS_COMBINED_LENGTH;
            			
	    			std::copy_n(image_vec.begin() + chunk_length_pos, chunk_length, std::back_inserter(copied_image_vec));
        		}
    		};

			if (image_vec[COLOR_TYPE_INDEX] == INDEXED_PLTE || image_vec[COLOR_TYPE_INDEX] == TRUECOLOR_RGB) {
    			if (image_vec[COLOR_TYPE_INDEX] == INDEXED_PLTE) {
    				copy_chunk_type(PLTE_SIG); 
    			}
    			copy_chunk_type(TRNS_SIG);
			}
	
    		copy_chunk_type(IDAT_SIG);

    		std::copy_n(image_vec.end() - PNG_IEND_BYTES, PNG_IEND_BYTES, std::back_inserter(copied_image_vec));
    		
    		image_vec.swap(copied_image_vec);
    		std::vector<uint8_t>().swap(copied_image_vec);
    		
			image_size = image_vec.size(); 
				
			const uint32_t IDAT_CHUNK_ARCHIVE_FILE_SIZE = static_cast<uint32_t>(archive_vec.size());

			uint8_t 
				idat_chunk_length_index = 0,
				value_bit_length = 32;
	
			bool isBigEndian = true;
		
			updateValue(archive_vec, idat_chunk_length_index, (IDAT_CHUNK_ARCHIVE_FILE_SIZE - CHUNK_FIELDS_COMBINED_LENGTH), value_bit_length, isBigEndian);

			// The following section completes & embeds the extraction script, which is determined by archive type (ZIP/JAR). If ZIP, the file type of the first ZIP file record within the archive.
			constexpr uint8_t 	
				ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX 	= 0x22, 
				ARC_RECORD_FIRST_FILENAME_INDEX 		= 0x26,
				ARC_RECORD_FIRST_FILENAME_MIN_LENGTH 	= 4,
				INDEX_DIFF 								= 8,
				TOTAL_FILE_EXTENSIONS 					= 35;
			
			const uint8_t ARC_RECORD_FIRST_FILENAME_LENGTH = archive_vec[ARC_RECORD_FIRST_FILENAME_LENGTH_INDEX];

			if (ARC_RECORD_FIRST_FILENAME_MIN_LENGTH > ARC_RECORD_FIRST_FILENAME_LENGTH) {
				throw std::runtime_error("File Error:\n\nName length of first file within archive is too short.\nIncrease length (minimum 4 characters). Make sure it has a valid extension.");
			}

			constexpr std::array<const char*, TOTAL_FILE_EXTENSIONS> EXTENSION_LIST  { 
				"mp4", "mp3", "wav", "mpg", "webm", "flac", "3gp", "aac", "aiff", "aif", "alac", "ape", "avchd", "avi",
				"dsd", "divx", "f4v", "flv", "m4a", "m4v", "mkv", "mov", "midi", "mpeg", "ogg", "pcm", "swf", "wma", "wmv",
				"xvid", "pdf", "py", "ps1", "sh", "exe" 
			};
						    
			const std::string ARC_RECORD_FIRST_FILENAME{archive_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX, archive_vec.begin() + ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH};

			enum FileType {VIDEO_AUDIO = 29, PDF, PYTHON, POWERSHELL, BASH_SHELL, WINDOWS_EXECUTABLE, UNKNOWN_FILE_TYPE, FOLDER, LINUX_EXECUTABLE, JAR};

			bool isZipFile = (args.thisArchiveType == ArchiveType::ZIP);

			uint8_t extension_list_index = (isZipFile) ? 0 : JAR;
						    
			if (extension_list_index == JAR && (ARC_RECORD_FIRST_FILENAME != "META-INF/MANIFEST.MF" && ARC_RECORD_FIRST_FILENAME != "META-INF/")) {
				throw std::runtime_error("File Type Error: Archive file does not appear to be a valid JAR file.");
			}
	
			const size_t EXTENSION_POS = ARC_RECORD_FIRST_FILENAME.rfind('.');
		
			const std::string ARC_RECORD_FIRST_FILENAME_EXTENSION = (EXTENSION_POS != std::string::npos) ? ARC_RECORD_FIRST_FILENAME.substr(EXTENSION_POS + 1) : "?";
	
			// Deal with filenames that don't have extensions. Folders and Linux executables.
			if (isZipFile && ARC_RECORD_FIRST_FILENAME_EXTENSION  == "?") {
				extension_list_index = archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/' ? FOLDER : LINUX_EXECUTABLE;
			}
						    
			// Even though we found a period character, indicating a file extension, it could still be a folder that just has a "." somewhere within its name, check for it here.
			// Linux allows a zipped folder to have a "." for the last character of its name (e.g. "my_folder."), but this will cause issues with Windows, so also check for it here.
			if (isZipFile && extension_list_index != FOLDER && archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 1] == '/') {
				if (archive_vec[ARC_RECORD_FIRST_FILENAME_INDEX + ARC_RECORD_FIRST_FILENAME_LENGTH - 2] != '.') {
					extension_list_index = FOLDER; 
				} else {
					throw std::runtime_error("ZIP File Error: Invalid folder name within ZIP archive."); 
				}
			}
	
			auto toLowerCase = [](std::string str) -> std::string {
    			std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    			return str;
			};
	
			// Try to match the file extension of the first file of the archive with the array list of file extensions (Extension_List).
			// This will determine what extraction script to embed within the image, so that it correctly deals with the file type.
			while (UNKNOWN_FILE_TYPE > extension_list_index) {
				if (EXTENSION_LIST[extension_list_index] == toLowerCase(ARC_RECORD_FIRST_FILENAME_EXTENSION)) {
					extension_list_index = VIDEO_AUDIO >= extension_list_index ? VIDEO_AUDIO : extension_list_index;
					break;
				}
				extension_list_index++;
			}

			auto hasBalancedQuotes = [](const std::string& args_string) {
        		uint8_t 
        			singleCount = 0,
        			doubleCount = 0;

       			for (size_t i = 0; i < args_string.size(); ++i) {
            		char c = args_string[i];
            		if (c == '\'' && (i == 0 || args_string[i-1] != '\\')) {
                		singleCount++;
            		} else if (c == '"' && (i == 0 || args_string[i-1] != '\\')) {
                		doubleCount++;
            		}
        		}
        		return (singleCount % 2 == 0) && (doubleCount % 2 == 0);
    		};

			std::string
				args_linux,
				args_windows,
				arguments;

			if ((extension_list_index > PDF && UNKNOWN_FILE_TYPE > extension_list_index) || extension_list_index == LINUX_EXECUTABLE || extension_list_index == JAR) {
				std::cout << "\nFor this file type, if required, you can provide command-line arguments here.\n";
				if (extension_list_index != WINDOWS_EXECUTABLE) {
					std::cout << "\nLinux: ";
					std::getline(std::cin, args_linux);
					arguments = args_linux;
				}
				if (extension_list_index != LINUX_EXECUTABLE) {
					std::cout << "\nWindows: ";
					std::getline(std::cin, args_windows);
					arguments = args_windows;
				}
			}
		
			if (!hasBalancedQuotes(args_linux) || !hasBalancedQuotes(args_windows)) {
        		throw std::runtime_error("Arguments Error: Quotes mismatch. Check arguments and try again.");
    		}	
    		
			std::vector<uint8_t> script_vec { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 0x5F, 0x00, 0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00 };

			constexpr uint16_t MAX_SCRIPT_SIZE = 1500;

			script_vec.reserve(script_vec.size() + MAX_SCRIPT_SIZE);
	
			std::unordered_map<uint8_t, std::vector<uint16_t>> case_map = {
			// The single digit integer is the extraction script id (see Extraction_Scripts_Vec), the hex values are insert index locations
			// within the extraction script vector. We use these index locations to insert additional items into the script in order to complete it.
				{VIDEO_AUDIO,			{ 0, 0x1E4, 0x1C }}, 
				{PDF,					{ 1, 0x196, 0x1C }}, 
				{PYTHON,				{ 2, 0x10B, 0x101, 0xBC, 0x1C }},
				{POWERSHELL,			{ 3, 0x105, 0xFB,  0xB6, 0x33 }},
				{BASH_SHELL,			{ 4, 0x134, 0x132, 0x8E, 0x1C }},
				{WINDOWS_EXECUTABLE,	{ 5, 0x116, 0x114 }},
				{FOLDER,				{ 6, 0x149, 0x1C }},
				{LINUX_EXECUTABLE,		{ 7, 0x8E,  0x1C }},
				{JAR,					{ 8, 0xA6,  0x61 }},
				{UNKNOWN_FILE_TYPE,		{ 9, 0x127, 0x1C }} // Fallback/placeholder. Unknown file type, unmatched file extension case.
			};

			auto it = case_map.find(extension_list_index);

			if (it == case_map.end()) {
    			extension_list_index = UNKNOWN_FILE_TYPE;
			}

			std::vector<uint16_t> case_values_vec = (it != case_map.end()) ? it->second : case_map[extension_list_index];

			constexpr uint8_t 
				EXTRACTION_SCRIPT_ELEMENT_INDEX = 0,
				SCRIPT_INDEX = 0x16;
			
			const uint16_t EXTRACTION_SCRIPT = case_values_vec[EXTRACTION_SCRIPT_ELEMENT_INDEX];

			script_vec.insert(script_vec.begin() + SCRIPT_INDEX, extraction_scripts_vec[EXTRACTION_SCRIPT].begin(), extraction_scripts_vec[EXTRACTION_SCRIPT].end());
		
			std::vector<std::vector<uint8_t>>().swap(extraction_scripts_vec);
		
			if (extension_list_index == WINDOWS_EXECUTABLE || extension_list_index == LINUX_EXECUTABLE) {
				script_vec.insert(script_vec.begin() + case_values_vec[1], arguments.begin(), arguments.end());
				script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());	
			} else if (extension_list_index == JAR) {
				script_vec.insert(script_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
				script_vec.insert(script_vec.begin() + case_values_vec[2], args_linux.begin(), args_linux.end());
			} else if (extension_list_index > PDF && WINDOWS_EXECUTABLE > extension_list_index) { 
				script_vec.insert(script_vec.begin() + case_values_vec[1], args_windows.begin(), args_windows.end());
				script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
				script_vec.insert(script_vec.begin() + case_values_vec[3], args_linux.begin(), args_linux.end());
				script_vec.insert(script_vec.begin() + case_values_vec[4], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
			} else { 
				script_vec.insert(script_vec.begin() + case_values_vec[1], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
		 		script_vec.insert(script_vec.begin() + case_values_vec[2], ARC_RECORD_FIRST_FILENAME.begin(), ARC_RECORD_FIRST_FILENAME.end());
			}
	
			std::unordered_map<uint8_t, std::vector<uint16_t>>().swap(case_map);
			std::vector<uint16_t>().swap(case_values_vec);
		
			uint8_t 
				iccp_chunk_length_index = 0,
				iccp_chunk_length_first_byte_index = 3;
	
			uint16_t iccp_chunk_script_size = static_cast<uint16_t>(script_vec.size()) - CHUNK_FIELDS_COMBINED_LENGTH;  

			updateValue(script_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, isBigEndian);

			const uint8_t ICCP_CHUNK_LENGTH_FIRST_BYTE_VALUE = script_vec[iccp_chunk_length_first_byte_index];
	
			// If a problem character (which breaks the Linux extraction script) is found within the first byte of the updated iCCP chunk length field, 
			// insert a short string to the end of the iCCP chunk to increase its length, avoiding the problem characters when chunk length is updated.
			if (std::find(LINUX_PROBLEM_CHARACTERS.begin(), LINUX_PROBLEM_CHARACTERS.end(), 
				ICCP_CHUNK_LENGTH_FIRST_BYTE_VALUE) != LINUX_PROBLEM_CHARACTERS.end()) {
					const std::string INCREASE_CHUNK_LENGTH_STRING = "........";
					script_vec.insert(script_vec.begin() + iccp_chunk_script_size + INDEX_DIFF, INCREASE_CHUNK_LENGTH_STRING.begin(), INCREASE_CHUNK_LENGTH_STRING.end());
					iccp_chunk_script_size = static_cast<uint16_t>(script_vec.size()) - CHUNK_FIELDS_COMBINED_LENGTH;
					updateValue(script_vec, iccp_chunk_length_index, iccp_chunk_script_size, value_bit_length, isBigEndian);
			}
	
			constexpr uint8_t
				ICCP_CHUNK_INDEX 						= 0x21,
				ICCP_CHUNK_NAME_INDEX 					= 0x04,
				LAST_IDAT_CHUNK_CRC_INDEX_DIFF 			= 16,
				ICCP_CHUNK_CRC_INDEX_DIFF 				= 8,
				EXCLUDE_SIZE_FIELD_AND_CRC_FIELD_LENGTH = 8,
				LAST_IDAT_INDEX_DIFF 					= 4,
				ICCP_CHUNK_NAME_FIELD_LENGTH 			= 4;

			if (iccp_chunk_script_size > MAX_SCRIPT_SIZE) {
				throw std::runtime_error("Script Size Error: Extraction script exceeds size limit.");
			}

			auto crcUpdate = [](uint8_t* buf, uint32_t buf_length) -> uint32_t {
	    		constexpr std::array<uint32_t, 256> CRC_TABLE = {
        			0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        			0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	        		0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        			0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        			0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        			0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        			0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        			0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        			0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        			0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        			0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        			0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        			0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        			0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        			0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        			0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        			0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        			0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        			0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        			0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        			0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        			0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        			0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        			0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        			0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        			0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        			0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        			0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        			0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        			0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        			0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        			0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    			};

    			uint32_t 
    				buf_index = 0,
    				crc_value = 0xFFFFFFFF;

    			while (buf_length--) {
        			crc_value = CRC_TABLE[(crc_value ^ buf[buf_index++]) & 0xFF] ^ (crc_value >> 8);
    			}

    			return crc_value ^ 0xFFFFFFFF;
			};	

			const uint16_t ICCP_CHUNK_LENGTH = iccp_chunk_script_size + ICCP_CHUNK_NAME_FIELD_LENGTH;

			const uint32_t ICCP_CHUNK_CRC = crcUpdate(&script_vec[ICCP_CHUNK_NAME_INDEX], ICCP_CHUNK_LENGTH);

			uint16_t iccp_chunk_crc_index = iccp_chunk_script_size + ICCP_CHUNK_CRC_INDEX_DIFF;

			updateValue(script_vec, iccp_chunk_crc_index, ICCP_CHUNK_CRC, value_bit_length, isBigEndian);

			image_vec.insert((image_vec.begin() + ICCP_CHUNK_INDEX), script_vec.begin(), script_vec.end());
			std::vector<uint8_t>().swap(script_vec);
		
			image_vec.insert((image_vec.end() - PNG_IEND_BYTES), archive_vec.begin(), archive_vec.end());
			std::vector<uint8_t>().swap(archive_vec);
	
			const uint32_t 
				LAST_IDAT_INDEX = static_cast<uint32_t>(image_size) + iccp_chunk_script_size + LAST_IDAT_INDEX_DIFF, 	// Important to use the old image size before the above inserts.
				COMPLETE_IMAGE_SIZE = static_cast<uint32_t>(image_vec.size());  	      								// Image size updated to include the inserts.

			constexpr uint8_t
				CENTRAL_LOCAL_INDEX_DIFF 		= 45,
				ZIP_COMMENT_LENGTH_INDEX_DIFF 	= 21,
				END_CENTRAL_START_INDEX_DIFF 	= 19,
				ZIP_RECORDS_INDEX_DIFF 			= 11,
				PNG_IEND_LENGTH 				= 16,
				ZIP_LOCAL_INDEX_DIFF 			= 4,
				ZIP_SIG_LENGTH 					= 4;
		
			constexpr std::array<uint8_t, ZIP_SIG_LENGTH>
				ZIP_LOCAL_SIG			{ 0x50, 0x4B, 0x03, 0x04 },
				END_CENTRAL_DIR_SIG		{ 0x50, 0x4B, 0x05, 0x06 },
				START_CENTRAL_DIR_SIG	{ 0x50, 0x4B, 0x01, 0x02 };
						 
			// Starting from the end of the vector storing the archive, a single reverse search of the contents finds the "end_central directory" index. 	
			const uint32_t END_CENTRAL_DIR_INDEX = COMPLETE_IMAGE_SIZE - static_cast<uint32_t>(std::distance(image_vec.rbegin(), std::search(image_vec.rbegin(), image_vec.rend(), END_CENTRAL_DIR_SIG.rbegin(), END_CENTRAL_DIR_SIG.rend()))) - ZIP_SIG_LENGTH;
		
			uint32_t
				total_zip_records_index  = END_CENTRAL_DIR_INDEX + ZIP_RECORDS_INDEX_DIFF,
				end_central_start_index  = END_CENTRAL_DIR_INDEX + END_CENTRAL_START_INDEX_DIFF,
				zip_comment_length_index = END_CENTRAL_DIR_INDEX + ZIP_COMMENT_LENGTH_INDEX_DIFF,
				zip_record_local_index 	 = LAST_IDAT_INDEX + ZIP_LOCAL_INDEX_DIFF, // First ZIP record index/offset.
				start_central_dir_index  = 0; 
		
			value_bit_length = 16;
			isBigEndian = false;
	
			uint16_t 	
				total_zip_records = (image_vec[total_zip_records_index] << 8) | image_vec[total_zip_records_index - 1],
				zip_comment_length = ((image_vec[zip_comment_length_index] << 8) | image_vec[zip_comment_length_index - 1]) + PNG_IEND_LENGTH, // Extend comment length. Required for JAR.
				record_count = 0;
						 
			updateValue(image_vec, zip_comment_length_index, zip_comment_length, value_bit_length, isBigEndian); 

			// Find the first, top "start_central directory" index location by searching the vector, working backwards from the vector's content.
			// By starting the search from the end of the vector, we know we are already within the record section data of the archive and this
			// helps in avoiding the increased probability of a "false-positive" signature match if we were to search the vector from the beginning and read through image & file data first.
			auto search_it = image_vec.rbegin();
			while (total_zip_records > record_count++) {
				search_it = std::search(search_it, image_vec.rend(), START_CENTRAL_DIR_SIG.rbegin(), START_CENTRAL_DIR_SIG.rend());
				start_central_dir_index = COMPLETE_IMAGE_SIZE - static_cast<uint32_t>(std::distance(image_vec.rbegin(), search_it++)) - ZIP_SIG_LENGTH;	
			}
	
			uint32_t central_dir_local_index = start_central_dir_index + CENTRAL_LOCAL_INDEX_DIFF; // First central dir index location.

			value_bit_length = 32;
			updateValue(image_vec, end_central_start_index, start_central_dir_index, value_bit_length, isBigEndian);  // Update end_central index location with start_central directory offset.
	
			while (total_zip_records--) {
				updateValue(image_vec, central_dir_local_index, zip_record_local_index, value_bit_length, isBigEndian); // Write new zip_record offset to the current central_dir_local index.
				zip_record_local_index  = searchSig(image_vec, zip_record_local_index, INCREMENT_NEXT_SEARCH_POS, ZIP_LOCAL_SIG); 										// Get the next zip_record index/offset.
				central_dir_local_index = searchSig(image_vec, central_dir_local_index, INCREMENT_NEXT_SEARCH_POS, START_CENTRAL_DIR_SIG) + CENTRAL_LOCAL_INDEX_DIFF; 	// Get the Next central_dir index.
			}

			const uint32_t LAST_IDAT_CHUNK_CRC = crcUpdate(&image_vec[LAST_IDAT_INDEX], IDAT_CHUNK_ARCHIVE_FILE_SIZE - EXCLUDE_SIZE_FIELD_AND_CRC_FIELD_LENGTH);
	
			uint32_t last_idat_chunk_crc_index = COMPLETE_IMAGE_SIZE - LAST_IDAT_CHUNK_CRC_INDEX_DIFF;
	
			value_bit_length = 32;
			isBigEndian = true;
	
			updateValue(image_vec, last_idat_chunk_crc_index, LAST_IDAT_CHUNK_CRC, value_bit_length, isBigEndian);
	
			std::random_device rd;
    			std::mt19937 gen(rd());
    			std::uniform_int_distribution<> dist(10000, 99999);  // Five-digit random number

			const std::string
				PREFIX = isZipFile ? "pzip_" : "pjar_",
				POLYGLOT_FILENAME = PREFIX + std::to_string(dist(gen)) + ".png";

			std::ofstream file_ofs(POLYGLOT_FILENAME, std::ios::binary);
	
			if (!file_ofs) {
				throw std::runtime_error("Write File Error: Unable to write to file.");
			}
	
			file_ofs.write(reinterpret_cast<const char*>(image_vec.data()), COMPLETE_IMAGE_SIZE);
	
			std::vector<uint8_t>().swap(image_vec);

			std::cout << "\nCreated " 
				<< (isZipFile 
					? "PNG-ZIP" 
					: "PNG-JAR") 
				<< " polyglot image file: " << POLYGLOT_FILENAME << " (" << COMPLETE_IMAGE_SIZE << " bytes).\n\nComplete!\n\n";

			// Attempt to set executable permissions for the newly created polyglot image file.
			#ifdef __unix__
	    		if (chmod(POLYGLOT_FILENAME.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        			std::cerr << "\nWarning: Could not set executable permissions for " << POLYGLOT_FILENAME << ".\nYou will need do this manually using chmod.\n" << std::endl;
	    		}
			#endif

			return 0;		
    }
    catch (const std::runtime_error& e) {
    	std::cerr << "\n" << e.what() << "\n\n";
     	return 1;
	}
}	
