#!/bin/bash

echo "Building asm2wasm project..."

if [ ! -d "build" ]; then
    mkdir build
fi

cd build

rm -f CMakeCache.txt

echo "Building with CMake..."
CC=clang CXX=clang++ cmake ..

echo "Building..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Usage:"
    echo "  ./asm2wasm examples/simple_add.asm"
    echo "  ./asm2wasm -o output.ll examples/arithmetic.asm"
    echo "  ./asm2wasm --help"
else
    echo "Build error occurred."
    exit 1
fi