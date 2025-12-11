#!/bin/bash

# compile_pdvzip.sh

g++ -std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -DNDEBUG -s -flto=auto -fuse-linker-plugin pdvzip.cpp lodepng/lodepng.cpp -o pdvzip

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvzip' created."
else
    echo "Compilation failed."
    exit 1
fi
