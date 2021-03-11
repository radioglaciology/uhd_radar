#!/bin/bash
set -e
cd build
cmake ..
make
time ./radar
