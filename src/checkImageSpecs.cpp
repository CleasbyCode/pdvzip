void checkImageSpecs(std::vector<uint8_t>& Vec) {

	constexpr uint8_t
			IMAGE_WIDTH_INDEX = 0x12,
			IMAGE_HEIGHT_INDEX = 0x16,
			IMAGE_COLOR_TYPE_INDEX = 0x19,
			MIN_DIMS = 68,
			INDEXED_COLOR_TYPE = 3,
			TRUECOLOR_TYPE = 2,
			BYTE_LENGTH = 2;

	constexpr uint16_t
		MAX_TRUECOLOR_DIMS = 899,
		MAX_INDEXED_COLOR_DIMS = 4096;

	const uint16_t
		IMAGE_WIDTH = getByteValue(Vec, IMAGE_WIDTH_INDEX, BYTE_LENGTH, true),
		IMAGE_HEIGHT = getByteValue(Vec, IMAGE_HEIGHT_INDEX, BYTE_LENGTH, true);

	const uint8_t IMAGE_COLOR_TYPE = Vec[IMAGE_COLOR_TYPE_INDEX] == 6 ? 2 : Vec[IMAGE_COLOR_TYPE_INDEX];

	const bool
		VALID_COLOR_TYPE = (IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE) ? true
		: ((IMAGE_COLOR_TYPE == TRUECOLOR_TYPE) ? true : false),

		VALID_IMAGE_DIMS = (IMAGE_COLOR_TYPE == TRUECOLOR_TYPE
			&& MAX_TRUECOLOR_DIMS >= IMAGE_WIDTH
			&& MAX_TRUECOLOR_DIMS >= IMAGE_HEIGHT
			&& IMAGE_WIDTH >= MIN_DIMS
			&& IMAGE_HEIGHT >= MIN_DIMS) ? true
		: ((IMAGE_COLOR_TYPE == INDEXED_COLOR_TYPE
			&& MAX_INDEXED_COLOR_DIMS >= IMAGE_WIDTH
			&& MAX_INDEXED_COLOR_DIMS >= IMAGE_HEIGHT
			&& IMAGE_WIDTH >= MIN_DIMS
			&& IMAGE_HEIGHT >= MIN_DIMS) ? true : false);

	if (!VALID_COLOR_TYPE || !VALID_IMAGE_DIMS) {
		std::cerr << "\nImage File Error: " 
			<< (!VALID_COLOR_TYPE 
				? "Color type of PNG image is not supported.\n\nPNG-32/24 (Truecolor) or PNG-8 (Indexed-Color) only"
				: "Dimensions of PNG image are not within the supported range.\n\nPNG-32/24 Truecolor: [68 x 68]<->[899 x 899].\nPNG-8 Indexed-Color: [68 x 68]<->[4096 x 4096]") 
			<< ".\n\n";

		std::exit(EXIT_FAILURE);
	}
	eraseChunks(Vec);
}