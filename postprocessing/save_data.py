import sys
import shutil
import argparse
import numpy as np
import scipy.signal as sp
import processing as pr
import matplotlib.pyplot as plt
from datetime import datetime
from ruamel.yaml import YAML as ym

# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')
args = parser.parse_args()

# Initialize Constants
yaml = ym()
with open(args.yaml_file) as stream:
    config = yaml.load(stream)

file_prefix = datetime.now().strftime("data/%Y%m%d_%H%M%S")

print(f"Copying data to {file_prefix}...")

shutil.copy(args.yaml_file, file_prefix + "_config.yaml")
shutil.copy("./data/" + config['FILES']['save_loc'] + "_rx_samps.bin", file_prefix + "_rx_samps.bin")
shutil.copy("./data/" + config['FILES']['save_loc'] + "_gps.txt", file_prefix + "_gps.txt")
# requires that you tee off the file to terminal_log.txt when running it
shutil.copy("./data/terminal_log.txt", file_prefix + "_log.txt")

print(f"File copying complete.")
