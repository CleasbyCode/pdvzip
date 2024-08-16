void checkZipSignature(std::vector<uint8_t>& Vec) {
	constexpr uint8_t ZIP_SIG[] { 0x50, 0x4B, 0x03, 0x04 };

	if (!std::equal(std::begin(ZIP_SIG), std::end(ZIP_SIG), std::begin(Vec) + 8)) {
		std::cerr << "\nZIP File Error: File does not appear to be a valid ZIP archive.\n\n";
		std::exit(EXIT_FAILURE);
	}
}
