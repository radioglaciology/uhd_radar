#!/bin/bash
set -e
python generate_chirp.py
cd build
cmake ..
make
time ./radar
cd ..
python plot_samples.py
