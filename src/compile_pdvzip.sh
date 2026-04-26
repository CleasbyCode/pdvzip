#!/bin/bash

# compile_pdvzip.sh — Build script for pdvzip (multi-file layout)

set -euo pipefail

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++23 -O3 -mtune=native -pipe -Wall -Wextra -Wpedantic -Wshadow -DNDEBUG -D_FORTIFY_SOURCE=3 -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -s -flto=auto -fuse-linker-plugin -fstack-protector-strong -fPIE"
LDFLAGS="-pie -Wl,-z,relro,-z,now"
TARGET="pdvzip"
SRCDIR="."

SOURCES=(
    "$SRCDIR/main.cpp"
    "$SRCDIR/display_info.cpp"
    "$SRCDIR/program_args.cpp"
    "$SRCDIR/file_io.cpp"
    "$SRCDIR/binary_utils.cpp"
    "$SRCDIR/image_processing.cpp"
    "$SRCDIR/image_resize.cpp"
    "$SRCDIR/archive_analysis.cpp"
    "$SRCDIR/user_input.cpp"
    "$SRCDIR/script_builder.cpp"
    "$SRCDIR/script_text_builder.cpp"
    "$SRCDIR/polyglot_assembly.cpp"
    "$SRCDIR/lodepng/lodepng.cpp"
)

echo "Compiling $TARGET..."
$CXX $CXXFLAGS "${SOURCES[@]}" $LDFLAGS -o "$TARGET"
echo "Compilation successful. Executable '$TARGET' created."
