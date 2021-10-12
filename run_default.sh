#!/bin/bash
set -e
# "$@" passes all command line arguments to this script on to the other scripts called here
python preprocessing/generate_chirp.py "$@"
cd sdr/build
cmake ..
make
time ./radar "$@"
cd ../..
python postprocessing/plot_samples.py "$@"
