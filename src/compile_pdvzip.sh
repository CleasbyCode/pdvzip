#!/bin/bash

# compile_pdvzip.sh — Build script for jdvrif (multi-file layout)

set -e

CXX="${CXX:-g++}"
# CXXFLAGS="-std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -Wshadow -DNDEBUG -s -flto=auto -fuse-linker-plugin -fstack-protector-strong"
CXXFLAGS="-std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -Wshadow -DNDEBUG -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -s -flto=auto -fuse-linker-plugin -fstack-protector-strong"
TARGET="pdvzip"
SRCDIR="."

SOURCES=(
    "$SRCDIR/main.cpp"
    "$SRCDIR/display_info.cpp"
    "$SRCDIR/program_args.cpp"
    "$SRCDIR/file_io.cpp"
    "$SRCDIR/binary_utils.cpp"
    "$SRCDIR/image_processing.cpp"
    "$SRCDIR/archive_analysis.cpp"
    "$SRCDIR/user_input.cpp"
    "$SRCDIR/script_builder.cpp"
    "$SRCDIR/polyglot_assembly.cpp"
    "$SRCDIR/lodepng/lodepng.cpp"
)

echo "Compiling $TARGET..."
$CXX $CXXFLAGS "${SOURCES[@]}" -o "$TARGET"
echo "Compilation successful. Executable '$TARGET' created."
 
