#!/bin/bash

# compile_pdvzip.sh

g++ main.cpp lodepng/lodepng.cpp programArgs.cpp fileChecks.cpp copyChunks.cpp \
    information.cpp adjustZip.cpp writeFile.cpp getByteValue.cpp updateValue.cpp \
    crc32.cpp resizeImage.cpp extractionScripts.cpp pdvZip.cpp \
    -Wall -O2 -s -o pdvzip

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvzip' created."
else
    echo "Compilation failed."
    exit 1
fi
