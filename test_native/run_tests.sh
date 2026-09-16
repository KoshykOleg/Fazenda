#!/bin/sh
# Native tests for src/climate.cpp, src/logger.cpp, src/display.cpp (host g++, no ESP32).
# ./run_tests.sh [--suite climate|display|logger|fuzz] [--seeds N] [--hours H] [-v] [--echo]
set -e
cd "$(dirname "$0")"
SRC=../src
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I stubs -I "$SRC" \
    main.cpp tf.cpp sim.cpp test_climate.cpp test_display.cpp test_logger.cpp test_fuzz.cpp \
    "$SRC/climate.cpp" "$SRC/logger.cpp" "$SRC/display.cpp" \
    -o build/fazenda_tests
./build/fazenda_tests "$@"
