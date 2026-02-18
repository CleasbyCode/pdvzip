#include "pdvzip.h"

int main(int argc, char** argv) {
	try {
		auto args = ProgramArgs::parse(argc, argv);

		if (args.info_mode) {
			displayInfo();
			return 0;
		}

		vBytes
			image_vec   = readFile(*args.image_file_path, FileTypeCheck::cover_image),
			archive_vec = readFile(*args.archive_file_path);

		optimizeImage(image_vec);

		const std::size_t
			original_image_size = image_vec.size(),
			archive_file_size   = archive_vec.size();

		const bool isZipFile = hasFileExtension(*args.archive_file_path, {".zip"});

		// Update the IDAT chunk length to include the archive.
		updateValue(archive_vec, 0, static_cast<uint32_t>(archive_file_size - CHUNK_FIELDS_COMBINED_LENGTH), 4);

		// Determine what kind of file is embedded.
		const FileType file_type = determineFileType(archive_vec, isZipFile);
		const std::string first_filename = getArchiveFirstFilename(archive_vec);

		// Prompt for optional arguments (scripts, executables, JAR).
		const UserArguments user_args = promptForArguments(file_type);

		// Build the iCCP chunk containing the extraction script.
		vBytes script_vec = buildExtractionScript(file_type, first_filename, user_args);

		// Assemble the polyglot: embed script + archive, fix offsets, finalize CRC.
		embedChunks(image_vec, std::move(script_vec), std::move(archive_vec), original_image_size);

		// Write output.
		writePolyglotFile(image_vec, isZipFile);

		return 0;
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "\n{}\n", e.what());
		return 1;
	}
}
