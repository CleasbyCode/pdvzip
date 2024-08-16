// A range of characters that may appear within the image width/height dimension fields of the IHDR chunk or within the 4-byte IHDR chunk's CRC field,
// will break the Linux extraction script. If any of these characters are detected, the user will have to modify the image manually or try another image.
void checkIhdrChunk(std::vector<uint8_t>& Vec) {

	constexpr uint8_t
			LINUX_PROBLEM_CHARACTERS[] { 0x22, 0x27, 0x28, 0x29, 0x3B, 0x3E, 0x60 },
			MAX_INDEX = 0x20;
	
	uint8_t ihdr_chunk_index = 0x12;

	while (ihdr_chunk_index++ != MAX_INDEX) {
		if (std::find(std::begin(LINUX_PROBLEM_CHARACTERS), std::end(LINUX_PROBLEM_CHARACTERS),
			Vec[ihdr_chunk_index]) != std::end(LINUX_PROBLEM_CHARACTERS)) {
				std::cerr << "\nImage File Error:\n\nThe IHDR chunk of this image contains a character that will break the Linux extraction script."
				"\nTry modifying image width & height dimensions (1% increase or decrease) to resolve the issue.\nRepeat if necessary or try another image.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}
	checkImageSpecs(Vec);
}
