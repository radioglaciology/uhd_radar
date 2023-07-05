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

#expected_cwd = "/home/thomas/Documents/StanfordGrad/RadioGlaciology/sdr"
expected_cwd = "/home/radioglaciolgy/anna/uhd_radar"

if __name__ == "__main__":

    # Check for correct working directory
    if os.getcwd() != expected_cwd:
        raise Exception(f"This script should ONLY be run from {expected_cwd}. Detected CWD {os.getcwd()}")

    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    parser.add_argument("--full_duplex", action='store_true',
                        help='Calculate duty cycle for a full duplex transport layer. By default, assumes half duplex.')
    args = parser.parse_args()
    yaml_filename = args.yaml_file

    yaml = YAML(typ='safe')
    with open(yaml_filename) as stream:
        config = yaml.load(stream)

    # Chirp generation
    generate_from_yaml_filename(yaml_filename)

    # Compile UHD program

    os.chdir("sdr/build")
    run_and_fail_on_nonzero("cmake ..")
    run_and_fail_on_nonzero("make")
    os.chdir("../..")

    # Figure out a reasonable sweep range
    if args.full_duplex:
        active_time = max(config['CHIRP']['tx_duration'], config['CHIRP']['rx_duration'])
        print(f"Full-duplex mode. Using active time of {active_time} seconds, which is the maximum of tx_duration and rx_duration")
    else:
        active_time = (config['CHIRP']['tx_duration'] + config['CHIRP']['rx_duration']) / 2
        print(f"Half-duplex mode. Using active time of {active_time} seconds, which is the average of tx_duration and rx_duration")
    
    # Converters to show pulse_rep_int in terms of the effective duty cycle
    # (averaged across TX and RX)
    # pri is in milliseconds
    def pri_to_duty(pri):
        return 100 * active_time / (pri)
    
    def duty_to_pri(duty):
        return 100 * active_time / (duty)

    duty_cycles = np.arange(pri_to_duty(max(config['CHIRP']['tx_duration'], config['CHIRP']['rx_duration'])), 1.0, -2.5) # in percent
    values = duty_to_pri(duty_cycles)
    results = {}

    # Run sweep
    for v in values:
        results[v] = test_with_pulse_rep_int(yaml_filename, pulse_rep_int = float(v))

    for k, v in results.items():
        print(f"pulse_rep_int: {k} \tn_errors: {v['n_errors']} \tprefix: {v['file_prefix']}")

    n_error_list = np.array([results[v]['n_errors'] for v in values])

    import pickle
    with open('test.pickle', 'wb') as f:
        pickle.dump({'n_error_list': n_error_list, 'values': values, 'config': config}, f)

    fig, ax = plt.subplots(figsize=(10,6), facecolor='white')
    ax.scatter(duty_cycles, n_error_list / config['CHIRP']['num_pulses'] * 100)
    ax.set_xlabel('Effective duty cycle [%]')
    secax = ax.secondary_xaxis('top', functions=(duty_to_pri, pri_to_duty))
    secax.set_xlabel('pulse_rep_int [microseconds]')
    secax.set_xticks(duty_to_pri(ax.get_xticks()))
    secax.set_xticklabels([f"{x*1e6:.2f} us" for x in secax.get_xticks()], rotation="vertical")
    ax.set_ylim(0, 100)
    ax.set_xlim(duty_cycles[0], duty_cycles[-1])

    ax.set_ylabel('Percent of ERROR_CODE_LATE_COMMAND [%]')
    ax.set_title(f"tx_duration: {config['CHIRP']['tx_duration']}, rx_duration: {config['CHIRP']['rx_duration']}, num_pulses: {config['CHIRP']['num_pulses']}")
    ax.grid()
    fig.tight_layout()
    fig.savefig('error_code_late_command.png')