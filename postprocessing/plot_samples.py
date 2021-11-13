import sys
import argparse
import numpy as np
import scipy.signal as sp
import processing as pr
import matplotlib.pyplot as plt
from ruamel.yaml import YAML as ym

# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')
args = parser.parse_args()

# Initialize Constants
yaml = ym()                         # Always use safe load if not dumping
with open(args.yaml_file) as stream:
   config = yaml.load(stream)
   rx_params = config["PLOT"]
   sample_rate = rx_params["sample_rate"]    # Hertz
   rx_samps = rx_params["rx_samps"]          # Received data to analyze
   orig_ch = rx_params["orig_chirp"]         # Chirp associated with the received data
   internal_start = rx_params["internal_start"]
   echo_start = rx_params["echo_start"]
   sig_speed = rx_params["sig_speed"]
   
print("--- Loaded constants from config.yaml ---")

# Read and plot RX/TX
rx_sig = pr.extractSig(rx_samps)
print("--- Plotting real samples read from %s ---" % rx_samps)
pr.plotSigVsTime(rx_sig, 'Received Samples', sample_rate)

tx_sig = pr.extractSig(orig_ch)
print("--- Plotting transmited chirp, stored in %s ---" % orig_ch)
pr.plotSigVsTime(tx_sig, 'Transmitted Chirp', sample_rate)

# Correlate the two chirps to determine time difference
print("--- Match filtering received chirp with transmitted chirp ---")
xcorr_sig = sp.correlate(rx_sig, tx_sig, mode='valid', method='auto')
# as finddirectpath is written right now, it must be called before taking log of the signal
# because if not, negative log values could have a greater absolute value than positive log values.
internal_peak = pr.findInternalPath(xcorr_sig, internal_start, True) 
xcorr_sig = 20 * np.log10(np.absolute(xcorr_sig))

print("--- Plotting result of match filter ---")
xcorr_samps = np.shape(xcorr_sig)[0]
xcorr_time = np.zeros(xcorr_samps)
for x in range (xcorr_samps):
    xcorr_time[x] = x * 1e6 /sample_rate

plt.figure()
plt.plot(xcorr_time, xcorr_sig)
plt.title("Output of Match Filter: Signal")
plt.xlabel('Time (us)')
plt.ylabel('Power [dB]')
plt.grid()

plt.figure()
plt.plot(range(-10,60), xcorr_sig[internal_peak-10:internal_peak+60])
plt.title("Output of Match Filter: Peaks")
plt.xlabel('Sample')
plt.ylabel('Power [dB]')
plt.grid()

echo_samp = pr.findEcho(xcorr_sig, sample_rate, internal_peak, echo_start, sig_speed, True)

sys.stdout.flush()
plt.show()
