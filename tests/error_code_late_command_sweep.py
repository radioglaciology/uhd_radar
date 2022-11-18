import gpiozero
import time
import argparse
import os
import sys
import subprocess
import signal
import threading
from ruamel.yaml import YAML
import matplotlib.pyplot as plt
import numpy as np

sys.path.append("preprocessing")
from generate_chirp import generate_from_yaml_filename
sys.path.append("postprocessing")
from save_data import save_data

def run_and_fail_on_nonzero(cmd):
    retval = os.system(cmd)
    if retval != 0:
        print(f"Running '{cmd}' produced non-zero return value {retval}. Quitting...")
        exit(retval)

# Output logging
def log_output_from_usrp(out, file_out):
    global current_state
    for line in iter(out.readline, ''):
        file_out.write(f"[{time.time():0.3f}] \t{line}")
        print(f"UHD output: \t{line}", end="")
    out.close()
    file_out.close()

def test_with_pulse_rep_int(yaml_filename, pulse_rep_int, timeout_s=60*2, tmp_yaml_filename='tmp_config.yaml.tmp'):
    # Load YAML file
    yaml = YAML(typ='safe')
    stream = open(yaml_filename)
    config = yaml.load(stream)

    # Modify
    config['CHIRP']['pulse_rep_int'] = pulse_rep_int

    with open(tmp_yaml_filename, 'w') as f:
        yaml.dump(config, f)

    uhd_process = subprocess.Popen(["./radar", tmp_yaml_filename], stdout=subprocess.PIPE, bufsize=1, close_fds=True, text=True, cwd="sdr/build")
    uhd_output_reader_thread = threading.Thread(target=log_output_from_usrp, args=(uhd_process.stdout, open('uhd_stdout.log', 'w')))
    uhd_output_reader_thread.daemon = True # thread dies with the program
    uhd_output_reader_thread.start()

    print(f"Waiting up to {timeout_s} seconds for the process to quit")
    try:
        uhd_process.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired as e:
        print(f"UHD process did not terminate within time limit. Killing...")
        uhd_process.kill()

    # Save output
    print("Copying data files...")
    file_prefix = save_data(yaml_filename, extra_files={"uhd_stdout.log": "uhd_stdout.log"})
    print("Finished copying data.")

    with open("uhd_stdout.log", "r") as f:
        n_errors = sum(line.count("ERROR_CODE_LATE_COMMAND") for line in f)

    print(f"{n_errors} ERROR_CODE_LATE_COMMAND errors detected")

    return {'file_prefix': file_prefix, 'pulse_rep_int': pulse_rep_int, 'n_errors': n_errors}
    #return {'file_prefix': "", 'pulse_rep_int': pulse_rep_int, 'n_errors': n_errors}

expected_cwd = "/home/ubuntu/uhd_radar"

if __name__ == "__main__":

    # Check for correct working directory
    if os.getcwd() != expected_cwd:
        print(f"This script should ONLY be run from {expected_cwd}. Detected CWD {os.getcwd()}")
        error_and_quit()

    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    args = parser.parse_args()
    yaml_filename = args.yaml_file

    # Chirp generation
    try:
        generate_from_yaml_filename(yaml_filename)
    except Exception as e:
        print(e)
        error_and_quit()

    # Compile UHD program

    os.chdir("sdr/build")
    run_and_fail_on_nonzero("cmake ..")
    run_and_fail_on_nonzero("make")
    os.chdir("../..")

    # Run sweep
    values = np.arange(500e-6, 4000e-6, 200e-6) #[1000e-6, 2000e-6, 3000e-6, 4000e-6, 5000e-6]
    results = {}

    for v in values:
        results[v] = test_with_pulse_rep_int(yaml_filename, pulse_rep_int = float(v))

    for k, v in results.items():
        print(f"pulse_rep_int: {k} \tn_errors: {v['n_errors']} \tprefix: {v['file_prefix']}")

    n_error_list = [results[v]['n_errors'] for v in values]

    fig, ax = plt.subplots(figsize=(10,6), facecolor='white')
    ax.scatter(values * 1e3, n_error_list)
    ax.set_xlabel('pulse_rep_int [ms]')
    ax.set_ylabel('# of ERROR_CODE_LATE_COMMAND')
    ax.grid()
    fig.savefig('error_code_late_command.png')