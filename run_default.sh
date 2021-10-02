#!/bin/bash
set -e
python preprocessing/generate_chirp.py
cd sdr/build
cmake ..
make
time ./radar
cd ../..
python postprocessing/plot_samples.py
