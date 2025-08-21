#!/bin/bash

# compile_pdvzip.sh

g++ -std=c++20 main.cpp lodepng/lodepng.cpp programArgs.cpp fileChecks.cpp information.cpp extractionScripts.cpp -Wall -O3 -s -o pdvzip

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvzip' created."
else
    echo "Compilation failed."
    exit 1
fi
