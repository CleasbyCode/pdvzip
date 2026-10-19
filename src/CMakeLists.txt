cmake_minimum_required(VERSION 3.20)

# Take the version from pdvzip.h so build metadata matches what the program reports.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/pdvzip.h" PDVZIP_VERSION_LINE
  REGEX "^constexpr std::string_view PDVZIP_VERSION = \"[0-9.]+\";")
if(NOT PDVZIP_VERSION_LINE MATCHES "\"([0-9.]+)\"")
  message(FATAL_ERROR "Could not read PDVZIP_VERSION from pdvzip.h.")
endif()
project(pdvzip VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "pdvzip requires Linux.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "pdvzip requires GCC 14 or newer.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
  message(FATAL_ERROR "pdvzip requires Clang 18 or newer with libstdc++ 14 or newer.")
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

# ---------------------------------------------------------------------------
# Regression tests (opt-in):
#   cmake -S . -B build/tests -DPDVZIP_BUILD_TESTS=ON -DPDVZIP_ENABLE_LTO=OFF
#   cmake --build build/tests && ctest --test-dir build/tests --output-on-failure
# ---------------------------------------------------------------------------
option(PDVZIP_BUILD_TESTS "Build and register the regression tests with CTest" OFF)

if(PDVZIP_BUILD_TESTS)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # The program sources without main(), built once for every test suite.
  get_target_property(PDVZIP_SOURCES pdvzip SOURCES)
  list(REMOVE_ITEM PDVZIP_SOURCES main.cpp)
  add_library(pdvzip_test_core STATIC ${PDVZIP_SOURCES})
  target_include_directories(pdvzip_test_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(pdvzip_test_core PRIVATE -O1 -g)
  target_compile_definitions(pdvzip_test_core PUBLIC
    LODEPNG_NO_COMPILE_DISK
    LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
    LODEPNG_NO_COMPILE_CRC
  )
  target_link_libraries(pdvzip_test_core PUBLIC ZLIB::ZLIB)

  # Tests that create files run in a scratch directory inside the build tree.
  set(PDVZIP_TEST_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-work")
  file(MAKE_DIRECTORY "${PDVZIP_TEST_WORK_DIR}")

  file(GLOB PDVZIP_TEST_SUITES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_tests.cpp")
  foreach(suite IN LISTS PDVZIP_TEST_SUITES)
    get_filename_component(name "${suite}" NAME_WE)
    add_executable(${name} "${suite}")
    target_compile_options(${name} PRIVATE -O1 -g)
    target_link_libraries(${name} PRIVATE pdvzip_test_core)
    if(name STREQUAL "output_descriptor_tests")
      target_link_options(${name} PRIVATE "-Wl,--wrap=write,--wrap=fchmod,--wrap=fsync,--wrap=close")
    endif()
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${PDVZIP_TEST_WORK_DIR}")
  endforeach()

  foreach(script IN ITEMS linux_extraction_tests indexed_image_memory_test)
    add_test(NAME ${script}
      COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/${script}.py" $<TARGET_FILE:pdvzip>
      WORKING_DIRECTORY "${PDVZIP_TEST_WORK_DIR}")
  endforeach()

  add_test(NAME upcase_table_matches_generator
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_upcase_table.py"
      --check "${CMAKE_CURRENT_SOURCE_DIR}/archive_analysis.cpp")
endif()
