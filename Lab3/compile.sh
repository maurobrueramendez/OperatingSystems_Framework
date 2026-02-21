#!/bin/bash

# Create build directory
mkdir -p src/build

# --- Sequential ---
echo "Compiling Sequential..."
gcc src/P3_sequential.c src/parsePGM.c -o src/build/computeHistogramSequential

echo "Running Sequential..."
./src/build/computeHistogramSequential Data/heart.pgm Data/histogram_heart_sequential.txt

# --- Parallel ---
echo "Compiling Parallel..."
gcc src/P3_parallel.c src/parsePGM.c -o src/build/computeHistogramParallel -pthread

echo "Running Parallel..."
./src/build/computeHistogramParallel Data/heart.pgm Data/histogram_heart_parallel.txt 4

# --- Python & Comparison ---
echo "Python commands to visualize:"
echo "python3 src/showHistogram.py Data/histogram_heart_sequential.txt"
echo "python3 src/showHistogram.py Data/histogram_heart_parallel.txt"

echo "Comparing results:"
git diff --no-index Data/histogram_heart_sequential.txt Data/histogram_heart_parallel.txt