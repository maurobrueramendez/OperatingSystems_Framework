# file, compile.sh
#!/bin/bash
gcc src/P3_parallel.c src/parsePGM.c -o computeHistogramParallel -pthread