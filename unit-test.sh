#!/bin/bash
# Run unit tests for the assignment

# Automate these steps from the readme:
# Create a build subdirectory, change into it, run
# cmake .. && make && run the assignment-autotest application
mkdir -p build
cd build
cmake ..
make clean
make
cd ..
echo "Current directory:"
pwd
echo "Directory contents:"
ls -la
./build/assignment-autotest/assignment-autotest
