#!/bin/bash echo 

"=== Compiling ===" 
cd src 
gcc -o main main.c circularBuffer.c -Wall -Wextra 
echo "Build complete" 
echo "" 

#text files 
echo "=== Text File Tests ===" 
echo "Small text file (16 bytes buffer):" 
./main text ../Data/int_text_small.txt 16 
echo "" 

echo "Small text file (64 bytes buffer):" 
./main text ../Data/int_text_small.txt 64 
echo "" 

echo "Small text file (128 bytes buffer):" 
./main text ../Data/int_text_small.txt 128 
echo "" 

echo "Small text file (1024 bytes buffer):" 
./main text ../Data/int_text_small.txt 1024 
echo "" 

echo "Large text file (16 bytes buffer):" 
./main text ../Data/int_text_big.txt 16 
echo "" 

echo "Large text file (64 bytes buffer):" 
./main text ../Data/int_text_big.txt 64 
echo "" 

echo "Large text file (128 bytes buffer):" 
./main text ../Data/int_text_big.txt 128 
echo "" 

echo "Large text file (1024 bytes buffer):" 
./main text ../Data/int_text_big.txt 1024 
echo "" 

#binary files 
echo "=== Binary File Tests ===" 
echo "Small binary file (16 bytes buffer):" 
./main binary ../Data/test_small.dat 16 
echo "" 
echo "Small binary file (64 bytes buffer):" 
./main binary ../Data/test_small.dat 64 
echo "" 
echo "Small binary file (128 bytes buffer):" 
./main binary ../Data/test_small.dat 128 
echo "" 

echo "Small binary file (1024 bytes buffer):" 
./main binary ../Data/test_small.dat 1024 
echo "" 

echo "Large binary file (16 bytes buffer):" 
./main binary ../Data/test_big.dat 16 
echo "" 

echo "Large binary file (64 bytes buffer):" 
./main binary ../Data/test_big.dat 64 
echo "" 

echo "Large binary file (128 bytes buffer):" 
./main binary ../Data/test_big.dat 128 
echo "" 

echo "Large binary file (1024 bytes buffer):" 
./main binary ../Data/test_big.dat 1024 
echo "" 

echo "=== All tests completed ==="