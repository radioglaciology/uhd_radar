import sys
import shutil
import argparse
import numpy as np
import scipy.signal as sp
import processing as pr
import matplotlib.pyplot as plt
from datetime import datetime
from ruamel.yaml import YAML as ym

def save_data(yaml_filename, extra_files={}, alternative_rx_samps_loc=None):
    # Initialize Constants
    yaml = ym()
    with open(yaml_filename) as stream:
        config = yaml.load(stream)

    file_prefix = datetime.now().strftime("data/%Y%m%d_%H%M%S")

    print(f"Copying data to {file_prefix}...")

    shutil.copy(yaml_filename, file_prefix + "_config.yaml")
    if alternative_rx_samps_loc is None:
        shutil.copy(config['FILES']['save_loc'], file_prefix + "_rx_samps.bin")
    else:
        shutil.copy(alternative_rx_samps_loc, file_prefix + "_rx_samps.bin")

    for source_file, dest_tag in extra_files.items():
        shutil.copy(source_file, file_prefix + "_" + dest_tag)

    print(f"File copying complete.")
    
    return file_prefix

if __name__ == "__main__":
    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    args = parser.parse_args()
    yaml_filename = args.yaml_file

    save_data(yaml_filename)

