import gpiozero
import time
import argparse
import os
import sys

sys.path.append("preprocessing")
from generate_chirp import generate_from_yaml_filename

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

# Setup button and button LED
button = gpiozero.Button(4, pull_up=False)
led = gpiozero.PWMLED(18)

def button_press():
    global current_state

    print("Button pressed")

    if current_state == "ready":
        start_recording()
    elif current_state == "recording":
        stop_recording()

def start_recording():
    global current_state

    print("recording")
    current_state = "recording"
    # TODO

def stop_recording():
    global current_state

    print("stopping")
    current_state = "saving"
    # TODO
    current_state = "ready"
    # TODO

def update_led_state():
    global current_state, displayed_state
    if current_state != displayed_state:
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

button.when_pressed = button_press

# Manager setup finished

update_led_state()

# Check for correct working directory
if os.getcwd() != expected_cwd:
    print(f"This script should ONLY be run from {expected_cwd}. Detected CWD {os.getcwd()}")

# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')
args = parser.parse_args()

# Chirp generation
try:
    generate_from_yaml_filename(args.yaml_file)
except Exception as e:
    current_state = "error"
    update_led_state()
    exit(1)

# If successful, move on to ready state
current_state = "ready"
time.sleep(1) # TODO: Could remove - helps make it more obvious what's happening
update_led_state()

# The rest is handled asynchronously
while True:
    # Check and updated LED state
    update_led_state()
    
    time.sleep(0.5)