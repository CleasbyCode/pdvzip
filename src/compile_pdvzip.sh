#!/bin/bash

# compile_pdvzip.sh — Build script for pdvzip (multi-file layout)

set -euo pipefail

CXX="${CXX:-g++}"
CXX_VERSION="$($CXX --version 2>/dev/null || true)"
if [[ "$CXX_VERSION" == *clang* ]]; then
    LINKER_PLUGIN_FLAG=""
else
    LINKER_PLUGIN_FLAG="-fuse-linker-plugin"
fi

CXXFLAGS="-std=c++23 -O3 -mtune=native -pipe -Wall -Wextra -Wpedantic -Wshadow -Wformat -Wformat-security -DNDEBUG -D_FORTIFY_SOURCE=3 -D_GLIBCXX_ASSERTIONS -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CRC -s -flto=auto $LINKER_PLUGIN_FLAG -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -fPIE"
LDFLAGS="-pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code"
TARGET="pdvzip"
SRCDIR="."

SOURCES=(
    "$SRCDIR/main.cpp"
    "$SRCDIR/display_info.cpp"
    "$SRCDIR/program_args.cpp"
    "$SRCDIR/file_io.cpp"
    "$SRCDIR/binary_utils.cpp"
    "$SRCDIR/crc32.cpp"
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
