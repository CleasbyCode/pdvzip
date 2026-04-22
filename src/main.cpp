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

		const bool is_zip_file = hasFileExtension(*args.archive_file_path, {".zip"});

		// Update the IDAT chunk length to include the archive.
		writeValueAt(archive_vec, 0, static_cast<uint32_t>(archive_file_size - CHUNK_FIELDS_COMBINED_LENGTH), 4);

		// Reject unsafe archive entry paths (zip-slip style traversal/absolute paths).
		validateArchiveEntryPaths(archive_vec);

		// Determine what kind of file is embedded and capture the first archive path.
		const ArchiveMetadata archive_metadata = analyzeArchive(archive_vec, is_zip_file);

		// Prompt for optional arguments (scripts, executables, JAR).
		const UserArguments user_args = promptForArguments(archive_metadata.file_type);

		// Build the iCCP chunk containing the extraction script.
		vBytes script_vec = buildExtractionScript(archive_metadata.file_type, archive_metadata.first_filename, user_args);

		// Assemble the polyglot: embed script + archive, fix offsets, finalize CRC.
		embedChunks(image_vec, std::move(script_vec), std::move(archive_vec), original_image_size);

		// Write output.
		writePolyglotFile(image_vec, is_zip_file);

		return 0;
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "\n{}\n", e.what());
		return 1;
	}
}
