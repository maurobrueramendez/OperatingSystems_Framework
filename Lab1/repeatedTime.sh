#!/bin/bash

echo "=== Compiling ==="
cd src || exit 1
gcc -o main main.c circularBuffer.c -Wall -Wextra
echo "Build complete"
echo ""

# Run once with output, then time N silent runs
run_test () {
  local label="$1"
  local mode="$2"
  local file="$3"
  local buf="$4"
  local reps="$5"

  echo "$label"
  echo "Correctness run:"
  ./main "$mode" "$file" "$buf"
  echo ""

  echo "Timing: $reps repeated runs (output suppressed)"
  # time writes to stderr, redirect both outputs 
  { /usr/bin/time -p bash -c "for i in \$(seq 1 $reps); do ./main $mode $file $buf >/dev/null; done" ; } 2>&1
  echo ""
}

# Crepetitions:
# small text/binary: higher reps to avoid 0.00
# big text: fewer reps since already measurable
SMALL_REPS=500
BIG_TEXT_REPS=50
BIG_BIN_REPS=500

echo "=== Text File Tests ==="
run_test "Small text file (16 bytes buffer):"  text   ../Data/int_text_small.txt 16   $SMALL_REPS
run_test "Small text file (64 bytes buffer):"  text   ../Data/int_text_small.txt 64   $SMALL_REPS
run_test "Small text file (128 bytes buffer):" text   ../Data/int_text_small.txt 128  $SMALL_REPS
run_test "Small text file (1024 bytes buffer):" text  ../Data/int_text_small.txt 1024 $SMALL_REPS

run_test "Large text file (16 bytes buffer):"  text   ../Data/int_text_big.txt 16    $BIG_TEXT_REPS
run_test "Large text file (64 bytes buffer):"  text   ../Data/int_text_big.txt 64    $BIG_TEXT_REPS
run_test "Large text file (128 bytes buffer):" text   ../Data/int_text_big.txt 128   $BIG_TEXT_REPS
run_test "Large text file (1024 bytes buffer):" text  ../Data/int_text_big.txt 1024  $BIG_TEXT_REPS

echo "=== Binary File Tests ==="
run_test "Small binary file (16 bytes buffer):"  binary ../Data/test_small.dat 16    $SMALL_REPS
run_test "Small binary file (64 bytes buffer):"  binary ../Data/test_small.dat 64    $SMALL_REPS
run_test "Small binary file (128 bytes buffer):" binary ../Data/test_small.dat 128   $SMALL_REPS
run_test "Small binary file (1024 bytes buffer):" binary ../Data/test_small.dat 1024 $SMALL_REPS

run_test "Large binary file (16 bytes buffer):"  binary ../Data/test_big.dat 16      $BIG_BIN_REPS
run_test "Large binary file (64 bytes buffer):"  binary ../Data/test_big.dat 64      $BIG_BIN_REPS
run_test "Large binary file (128 bytes buffer):" binary ../Data/test_big.dat 128     $BIG_BIN_REPS
run_test "Large binary file (1024 bytes buffer):" binary ../Data/test_big.dat 1024   $BIG_BIN_REPS

echo "=== All tests completed ==="