import gpiozero
import time
import argparse
import os
import sys
import subprocess
import signal
import threading

sys.path.append("preprocessing")
from generate_chirp import generate_from_yaml_filename
sys.path.append("postprocessing")
from save_data import save_data

# Nominal flow:
# setup -> ready -[button press]-> starting -> recording -[button press]-> saving -> ready
#  >> if issue: error
#
# States:
# setup     - Initial one-time setup (includes generating the chirp)
# ready     - Waiting for user to press button to begin -- press button to start
# starting  - USRP is being setup
# recording - USRP is actively recording -- press button to end
# saving    - done recording and saving data
# error     - an issue occurred requiring manual intervention

current_state = "setup"     # Current actual state
displayed_state = None       # Current state displayed on LED
expected_cwd = "/home/ubuntu/uhd_radar"
yaml_filename = None
uhd_process = None
uhd_output_reader_thread = None

# Setup button and button LED
button = gpiozero.Button(4, pull_up=False, hold_time=5)
led = gpiozero.PWMLED(18)

def button_press():
    global current_state

    print("Button pressed")

    if current_state == "ready":
        start_recording()
    elif current_state == "recording":
        stop_recording()

def button_hold():
    global current_state

    print("Button hold")

    if current_state == "recording":
        stop_recording()

    print("Button held down -- asking system to shutdown")
    os.system("systemctl poweroff")
    exit(0)

def update_led_state():
    global current_state, displayed_state
    if current_state != displayed_state:
        try:
            if current_state == "setup":
                led.blink(on_time=0.1, off_time=0.1)
                displayed_state = "setup"
            elif current_state == "ready":
                led.blink(on_time=0.2, off_time=1)
                displayed_state = "ready"
            elif current_state == "starting":
                led.blink(on_time=0.1, off_time=0.1)
                displayed_state = "starting"
            elif current_state == "recording":
                led.pulse()
                displayed_state = "recording"
            elif current_state == "saving":
                led.blink(on_time=0.1, off_time=0.1)
                displayed_state = "saving"
            elif current_state == "error":
                led.on()
                displayed_state = "error"
        except Exception as e:
            print("Exception while updating LED state")
            print(e)

# Output logging
def log_output_from_usrp(out, file_out):
    global current_state
    for line in iter(out.readline, ''):
        if (current_state != "saving") and (line.startswith("Received chirp") or line.startswith("[START]")):
            current_state = "recording"
        file_out.write(f"[{time.time():0.3f}] \t{line}")
        print(f"UHD output: \t{line}", end="")
    out.close()
    file_out.close()

def start_recording():
    global current_state, uhd_process, uhd_output_reader_thread

    print("Starting UHD process")
    current_state = "starting"
    update_led_state()

    uhd_process = subprocess.Popen(["./radar", yaml_filename], stdout=subprocess.PIPE, bufsize=1, close_fds=True, text=True, cwd="sdr/build")
    uhd_output_reader_thread = threading.Thread(target=log_output_from_usrp, args=(uhd_process.stdout, open('uhd_stdout.log', 'w')))
    uhd_output_reader_thread.daemon = True # thread dies with the program
    uhd_output_reader_thread.start()

def stop_recording():
    global current_state, yaml_filename

    was_force_killed = False

    print("Attemping to stop UHD process")
    current_state = "saving"
    uhd_process.send_signal(signal.SIGINT)
    update_led_state()
    timeout = 10
    print(f"Waiting up to {timeout} seconds for the process to quit")
    try:
        uhd_process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as e:
        print(f"UHD process did not terminate within time limit. Killing...")
        uhd_process.kill()
        was_force_killed = True

    # Save output
    print("Copying data files...")
    save_data(yaml_filename, extra_files={"uhd_stdout.log": "uhd_stdout.log"})
    print("Finished copying data.")
    
    if was_force_killed:
        print("Copying completed, but the process had to be killed.")
        error_and_quit()
    else:
        current_state = "ready"

def error_and_quit():
    current_state = "error"
    update_led_state()
    time.sleep(5)
    exit(1)


# Get ready to run

update_led_state()

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
def run_and_fail_on_nonzero(cmd):
    retval = os.system(cmd)
    if retval != 0:
        print(f"Running '{cmd}' produced non-zero return value {retval}. Quitting...")
        error_and_quit()

os.chdir("sdr/build")
run_and_fail_on_nonzero("cmake ..")
run_and_fail_on_nonzero("make")
os.chdir("../..")

# If successful, move on to ready state
time.sleep(1) # TODO: Could remove - helps make it more obvious what's happening
current_state = "ready"
button.when_released = button_press # Toggle recording state on quick button press+release
button.when_held = button_hold # When held, ask system to shutdown
update_led_state()

# The rest is handled asynchronously
while True:
    # Check and updated LED state
    update_led_state()

    # Check if UHD process ended on it's own
    if (current_state == "recording") and uhd_process:
        retval = uhd_process.poll()
        if retval == 0:
            stop_recording()
        elif retval:
            print(f"UHD command returned non-zero output {retval}. Quitting...")
            stop_recording()
            error_and_quit()

    time.sleep(0.5)
