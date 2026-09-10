cmake_minimum_required(VERSION 3.20)

project(pdvzip VERSION 4.8 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "pdvzip requires Linux.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "pdvzip requires GCC 14 or newer.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
  message(FATAL_ERROR "pdvzip requires Clang 18 or newer with a compatible C++23 standard library.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PDVZIP_OPT_LEVEL "-O3" CACHE STRING "Optimization flag for pdvzip")
set_property(CACHE PDVZIP_OPT_LEVEL PROPERTY STRINGS -O0 -O1 -O2 -O3 -Og -Os -Oz -Ofast)
if(NOT PDVZIP_OPT_LEVEL MATCHES "^-O(0|1|2|3|g|s|z|fast)$")
  message(FATAL_ERROR "Unsupported PDVZIP_OPT_LEVEL: ${PDVZIP_OPT_LEVEL}")
endif()

set(PDVZIP_FORTIFY_LEVEL "3" CACHE STRING "_FORTIFY_SOURCE level")
set_property(CACHE PDVZIP_FORTIFY_LEVEL PROPERTY STRINGS 2 3)
if(NOT PDVZIP_FORTIFY_LEVEL MATCHES "^[23]$")
  message(FATAL_ERROR "PDVZIP_FORTIFY_LEVEL must be 2 or 3.")
endif()

option(PDVZIP_ENABLE_LTO "Enable link-time optimization for the release binary" ON)

find_package(ZLIB REQUIRED)

add_executable(pdvzip
  main.cpp
  display_info.cpp
  program_args.cpp
  file_io.cpp
  binary_utils.cpp
  crc32.cpp
  image_processing.cpp
  image_resize.cpp
  archive_analysis.cpp
  user_input.cpp
  script_builder.cpp
  script_text_builder.cpp
  polyglot_assembly.cpp
  lodepng/lodepng.cpp
)

target_compile_options(pdvzip PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wformat
  -Wformat-security
  "${PDVZIP_OPT_LEVEL}"
  -mtune=native
  -pipe
  -fstack-protector-strong
  -fstack-clash-protection
  -fcf-protection=full
  -fPIE
  -ftrivial-auto-var-init=zero
)

# Keep conversion diagnostics enabled for pdvzip while avoiding warning noise
# from the separately maintained lodepng implementation.
set_source_files_properties(lodepng/lodepng.cpp PROPERTIES
  COMPILE_OPTIONS -Wno-conversion
)

target_compile_definitions(pdvzip PRIVATE
  NDEBUG
  _GLIBCXX_ASSERTIONS
  "_FORTIFY_SOURCE=${PDVZIP_FORTIFY_LEVEL}"
  LODEPNG_NO_COMPILE_DISK
  LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
  LODEPNG_NO_COMPILE_CRC
)

target_link_options(pdvzip PRIVATE
  -s
  -pie
  -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code
)

target_link_libraries(pdvzip PRIVATE ZLIB::ZLIB)

if(PDVZIP_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT PDVZIP_IPO_SUPPORTED OUTPUT PDVZIP_IPO_ERROR LANGUAGES CXX)
  if(NOT PDVZIP_IPO_SUPPORTED)
    message(FATAL_ERROR "The selected compiler does not support LTO: ${PDVZIP_IPO_ERROR}")
  endif()
  set_property(TARGET pdvzip PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
