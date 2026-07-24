#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Create build directory if it doesn't exist
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

# Configure the project if it hasn't been configured yet
if [ ! -f CMakeCache.txt ]; then
    cmake ..
fi

# Build and run
cmake --build . -j
./ruqya_quran_player