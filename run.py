import time
import argparse
import os
import sys
import subprocess
import signal
import threading

# TODO TODO WORK IN PROGRESS

sys.path.append("preprocessing")
from generate_chirp import generate_from_yaml_filename
sys.path.append("postprocessing")
from save_data import save_data

# Output logging
def log_output_from_usrp(out, file_out, also_print=True):
    for line in iter(out.readline, ''):
        file_out.write(f"[{time.time():0.3f}] \t{line}")
        if also_print:
            print(f"UHD output: \t{line}", end="")
    out.close()
    file_out.close()


if __name__ == "__main__":

    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    args = parser.parse_args()
    yaml_filename = args.yaml_file

    # Verify CWD
    git_root = subprocess.check_output(["git", "rev-parse", "--show-toplevel"]).decode('utf-8')
    git_root = "".join(git_root.split())
    cwd = os.getcwd()
    if os.path.normpath(git_root) != os.path.normpath(cwd):
        print(f"This script should ONLY be run from the root of the git repo ({git_root}). Detected CWD {cwd}")
        exit(1)

    # Chirp generation
    try:
        generate_from_yaml_filename(yaml_filename)
    except Exception as e:
        print(e)
        exit(1)

    # Compile UHD program
    def run_and_fail_on_nonzero(cmd):
        retval = os.system(cmd)
        if retval != 0:
            print(f"Running '{cmd}' produced non-zero return value {retval}. Quitting...")
            error_and_quit()

    os.chdir("sdr/build")
    run_and_fail_on_nonzero("cmake ..")
    run_and_fail_on_nonzero("make")
    os.chdir("../..")

    uhd_process = subprocess.Popen(["./radar", yaml_filename], stdout=subprocess.PIPE, bufsize=1, close_fds=True, text=True, cwd="sdr/build")
    uhd_output_reader_thread = threading.Thread(target=log_output_from_usrp, args=(uhd_process.stdout, open('uhd_stdout.log', 'w')))
    uhd_output_reader_thread.daemon = True # thread dies with the program
    uhd_output_reader_thread.start()

    while(uhd_output_reader_thread.poll() == None){
        # TODO Move files around
    }

