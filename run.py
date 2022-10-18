import time
import argparse
import os
import sys
import subprocess
import signal
import threading
import queue
from ruamel.yaml import YAML

sys.path.append("preprocessing")
from generate_chirp import generate_from_yaml_filename
sys.path.append("postprocessing")
from save_data import save_data

"""
Provides a simple interface to build, run, and manage data outputs from the SDR code

Should generally be used like this:

runner = RadarProcessRunner(yaml_filename)
runner.setup() # Build the binary and get everything ready
runner.run() # Run the radar program
runner.wait() # Wait for it to finish (if `num_pulses` == -1, then you need to call runner.stop() before this will return)
runner.stop() # Wrap everything up and save data -- you need to call this even if the radar program finishes on its own
"""
class RadarProcessRunner():
    """
    yaml_filename -- path to the YAML config file that provides all the required settings
    output_log_path -- (temporary) location to store stdout of the C++ code
    log_processing_function -- optional function if you need to read and process the stdout data in real time
    output_to_stdout -- set True to also print the output, in addition to storing it to a file
    """
    def __init__(self, yaml_filename, output_log_path="uhd_stdout.log", log_processing_function=None, output_to_stdout=True):
        self.yaml_filename = yaml_filename
        self.output_log_path = output_log_path
        self.log_processing_function = log_processing_function
        self.output_to_stdout = output_to_stdout

        self.setup_complete = False
        self.is_running = False

        self.file_queue = queue.Queue()
        self.output_file = None
        self.output_file_path = None

    """
    Manage the stdout of the radar program, including logging it to a file and optionally sending it for additional processing
    """
    def process_usrp_output(self, out, file_out, also_print=True):
        for line in iter(out.readline, ''):
            # Save output to specified file with timestamp
            t = time.time()
            file_out.write(f"[{t:0.3f}] \t{line}")

            # If provided, pass output to external function for processing
            if self.log_processing_function is not None:
                log_processing_function(line)

            # If specified, also print to stdout
            if also_print:
                print(f"[{t:0.3f}] \t{line}", end="")

            # Enqueue for saving somewhere else
            if line.startswith("[CLOSE FILE]"):
                filename = (line[13:]).strip()
                if filename.startswith("../../"): # Automatically added to escape cwd of binary
                    filename = filename[6:] # Strip it out
                self.file_queue.put(filename)

        out.close()
        file_out.close()

    """
    Build the radar program, generate the chirp, and get ready to run
    """
    def setup(self):

        # Verify CWD
        git_root = subprocess.check_output(["git", "rev-parse", "--show-toplevel"]).decode('utf-8')
        git_root = "".join(git_root.split())
        cwd = os.getcwd()
        if os.path.normpath(git_root) != os.path.normpath(cwd):
            print(f"This script should ONLY be run from the root of the git repo ({git_root}). Detected CWD {cwd}")
            exit(1)

        # Load YAML
        yaml = YAML()
        with open(self.yaml_filename) as stream:
            self.config = yaml.load(stream)

        # Chirp generation
        try:
            generate_from_yaml_filename(self.yaml_filename)
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

        self.setup_complete = True

    """
    Start the radar program
    """
    def run(self):
        if not self.setup_complete:
            raise Exception("Must call setup() before calling run(). If setup() does not complete successfully, you cannot call run().")
        
        self.uhd_process = subprocess.Popen(["./radar", self.yaml_filename], stdout=subprocess.PIPE, bufsize=1, close_fds=True, text=True, cwd="sdr/build")
        self.uhd_output_reader_thread = threading.Thread(target=self.process_usrp_output, args=(self.uhd_process.stdout, open('uhd_stdout.log', 'w'), self.output_to_stdout))
        self.uhd_output_reader_thread.daemon = True # thread dies with the program
        self.uhd_output_reader_thread.start()
        self.is_running = True

    """
    Wait (up to `timeout` seconds, if not None) for the process to complete
    """
    def wait(self, timeout = None):
        t = time.time()
        while self.uhd_process.returncode is None:
            self.uhd_process.poll()
            time.sleep(1)
            if (timeout is not None) and (time.time() - t > timeout):
                self.stop()

    """
    Ends the radar program (if not already terminated) and saves data
    """
    def stop(self, timeout = 10):
        if not self.is_running:
            return 0

        was_force_killed = False

        print("Attemping to stop UHD process")
        self.uhd_process.send_signal(signal.SIGINT)
        print(f"Waiting up to {timeout} seconds for the process to quit")
        t = time.time()
        try:
            self.uhd_process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as e:
            print(f"UHD process did not terminate within time limit. Killing...")
            self.uhd_process.kill()
            was_force_killed = True
        self.is_running = False

        self.uhd_output_reader_thread.join()
        if self.config['RUN_MANAGER']['copy_mode'] == 1:
            self.save_from_queue()
            self.output_file.close()

        # Save output
        print("Copying data files...")
        file_prefix = save_data(self.yaml_filename, alternative_rx_samps_loc=self.output_file_path, extra_files={"uhd_stdout.log": "uhd_stdout.log"})
        print("Finished copying data.")

        self.output_file = None

        return file_prefix
    
    """
    Copy data from split files into a single data output file
    """
    def save_from_queue(self):
        if self.output_file is None:
            self.output_file_path = self.config['RUN_MANAGER']['final_save_loc']
            self.output_file = open(self.output_file_path, 'wb')

        while(not self.file_queue.empty()):
            with open(self.file_queue.get(), 'rb') as f:
                self.output_file.write(f.read())


if __name__ == "__main__":

    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    args = parser.parse_args()
    yaml_filename = args.yaml_file

    # Build and run UHD radar code

    runner = RadarProcessRunner(yaml_filename)

    def sigint_handler(signum, frame):
        runner.stop() # On Ctrl-C, attempt to stop radar process

    runner.setup()
    runner.run()
    signal.signal(signal.SIGINT, sigint_handler)
    runner.wait()
    runner.stop()

