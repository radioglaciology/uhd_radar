#!/bin/bash
set -e
# "$@" passes all command line arguments to this script on to the other scripts called here
python preprocessing/generate_chirp.py "$@"
cd sdr/build
cmake ..
make
./radar "$@" 2>&1 | tee ../../data/terminal_log.txt
cd ../..
python postprocessing/save_data.py "$@"
