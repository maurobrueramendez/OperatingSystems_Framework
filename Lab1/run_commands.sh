#!/bin/bash

echo "=== Compiling ==="
cd src || exit 1
gcc -o main main.c circularBuffer.c -Wall -Wextra
echo "Build complete"
echo ""

#text tests
echo "=== Text File Tests ==="

echo "Small text file (16 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_small.txt 16 ; } 2>&1
echo ""

echo "Small text file (64 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_small.txt 64 ; } 2>&1
echo ""

echo "Small text file (128 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_small.txt 128 ; } 2>&1
echo ""

echo "Small text file (1024 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_small.txt 1024 ; } 2>&1
echo ""

echo "Large text file (16 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_big.txt 16 ; } 2>&1
echo ""

echo "Large text file (64 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_big.txt 64 ; } 2>&1
echo ""

echo "Large text file (128 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_big.txt 128 ; } 2>&1
echo ""

echo "Large text file (1024 bytes buffer):"
{ /usr/bin/time -p ./main text ../Data/int_text_big.txt 1024 ; } 2>&1
echo ""

#binary tests
echo "=== Binary File Tests ==="

echo "Small binary file (16 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_small.dat 16 ; } 2>&1
echo ""

echo "Small binary file (64 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_small.dat 64 ; } 2>&1
echo ""

echo "Small binary file (128 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_small.dat 128 ; } 2>&1
echo ""

echo "Small binary file (1024 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_small.dat 1024 ; } 2>&1
echo ""

echo "Large binary file (16 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_big.dat 16 ; } 2>&1
echo ""

echo "Large binary file (64 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_big.dat 64 ; } 2>&1
echo ""

echo "Large binary file (128 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_big.dat 128 ; } 2>&1
echo ""

echo "Large binary file (1024 bytes buffer):"
{ /usr/bin/time -p ./main binary ../Data/test_big.dat 1024 ; } 2>&1
echo ""

echo "=== All tests completed ==="