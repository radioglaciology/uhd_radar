# Apply a windowing function to generated chirp and see how this affects 
# the width of the cross-correlation
import numpy as np
import numpy.random as rand
import scipy.signal as sp
import matplotlib.pyplot as plt
import processing as pr
from ruamel.yaml import YAML as ym

# Initialize constants
yaml = ym(typ='safe')                 # Always use safe load if not dumping
with open('../config/default.yaml') as stream:
    config = yaml.load(stream)
    wind_params = config["WINDOW"]
    sample_rate = wind_params["sample_rate"]
    window_type = wind_params["window_type"]
    orig_chirp = "../" + wind_params["orig_chirp"]
    amp_one = wind_params["amp_one"]
    amp_two = wind_params["amp_two"]
    delay = wind_params["delay"]
    noise_std = wind_params["noise_std"]
    loopback = wind_params["loopback"]
    show_graphs = wind_params["show_graphs"]   # Make all capital
    describe = wind_params["describe"]
    
if (describe): print("--- Loaded constants from config.yaml ---")


    
print("--- Opening data ---")
# Read original chirp
tx_sig = pr.extractSig(orig_chirp)
n_tx_samps = int(np.shape(tx_sig)[0])
if (show_graphs): pr.plotSigVsTime(tx_sig, 'Original Chirp', sample_rate)

# Window the chirp
# TODO: Allow specification of the window to use?
tx_sig_w = np.multiply(sp.windows.hann(n_tx_samps), tx_sig)
if (show_graphs): pr.plotSigVsTime(tx_sig_w, 'Original chirp with %s window applied' % window_type, sample_rate)

# --------------------------------------------------------------------------- #

print("--- Part 1: Cross correlate single chirp with itself, with/without window")
if (describe): print("\tFirst case: No Window Applied ---")
pr.testWindowSimple(tx_sig, tx_sig, sample_rate, False)

if (describe): print("\tSecond case: Window applied")
pr.testWindowSimple(tx_sig_w, tx_sig, sample_rate, True, window_type)

# --------------------------------------------------------------------------- #
print("--- Part 2: Simulating Closely Received Chirps ---")

# Extract test loopback
if (describe): print("--- Extract loopback signal & Find Direct Path Start---")
loopback = pr.extractSig(loopback)
xcorr_lb = sp.correlate(loopback, tx_sig, mode='valid', method='auto')
n_lb_samps = int(np.shape(loopback)[0])
if (show_graphs): pr.plotSigVsTime(loopback, 'Actual Loopback', sample_rate)

# Find direct path peak, for creating simulated signals
dir_peak = pr.findDirectPath(xcorr_lb, 10)  # Hard-code but 10 will always be fine...

# Create a simulated loopback signal
#sim_normal = pr.simulateRealistic(tx_sig, dir_peak, n_lb_samps, noise_std, sample_rate, amp_one, amp_two, delay, False)
#sim_window = pr.simulateRealistic(tx_sig_w, dir_peak, n_lb_samps, noise_std, sample_rate, amp_one, amp_two, delay, True, window_type)

sim_normal = pr.simulateCentered(tx_sig, noise_std, sample_rate, amp_one, amp_two, delay, False)
sim_window = pr.simulateCentered(tx_sig_w, noise_std, sample_rate, amp_one, amp_two, delay, True, window_type)

xcorr_normal = sp.correlate(sim_normal, tx_sig, mode='valid', method='auto')
xcorr_window = sp.correlate(sim_window, tx_sig_w, mode='valid', method='auto')

n_xcorr_samps = np.shape(xcorr_normal)[0]
xcorr_time = np.zeros(n_xcorr_samps)
for x in range (n_xcorr_samps):
    xcorr_time[x] = x/sample_rate

plt.figure()
plt.plot(xcorr_time * 1e6, 20* np.log10(np.absolute(xcorr_normal)))
plt.title("Match-filter of Simulated Signal (Normal Chirp)")
plt.xlabel("Time (ms)")
plt.ylabel("Power [dB]")
plt.show()

plt.figure()
plt.plot(xcorr_time * 1e6, 20* np.log10(np.absolute(xcorr_window)))
plt.title("Match-filter of Simulated Signal (Windowed Chirp)")
plt.xlabel("Time (ms)")
plt.ylabel("Power [dB]")
plt.show()






    
