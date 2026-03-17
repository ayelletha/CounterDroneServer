#!/bin/bash

# Stop building & running in case of ay error
set -e

echo "Building Counter-Drone Server..."

# Create a folder for all building files (flag -p for skipping if file already exist)
mkdir -p build
cd build

rm -rf *

# Compile with specific compiler
cmake -DCMAKE_CXX_COMPILER=g++ ..

# Use all cpu's cores for fast compliation
make -j$(nproc)

echo "✅ Build successful! Starting server..."
echo "---------------------------------------"

./drone_server
