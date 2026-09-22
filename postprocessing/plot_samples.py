from signal import pause
import sys
import argparse
import numpy as np
import scipy.signal as sp
import processing as pr
import matplotlib.pyplot as plt
from ruamel.yaml import YAML as ym
import os



# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')

args = parser.parse_args()

str_arg = str(args.yaml_file)
timestamp = str_arg[5:20]


# Initialize Constants
yaml = ym()                         # Always use safe load if not dumping
with open(args.yaml_file) as stream:
   config = yaml.load(stream)
   rx_params = config["PLOT"]
   sample_rate = rx_params["sample_rate"]    # Hertz
   
   orig_ch = rx_params["orig_chirp"]         # Chirp associated with the received data
   direct_start = rx_params["direct_start"]
   echo_start = rx_params["echo_start"]
   sig_speed = rx_params["sig_speed"]

   print("The timestamp is: ", timestamp)
   print("If the timestamp is incorrect, adjust plot_samples.py code")

   output_dir = config['FILES'].get('output_dir', 'data')
   rx_samps = output_dir + "/" + timestamp + "_rx_samps.bin" # Received data to analyze


   
print("--- Loaded constants from config.yaml ---")
# Read and plot RX/TX
rx_sig = pr.extractSig(rx_samps)
print("--- Plotting real samples read from %s ---" % rx_samps)
pr.plotChirpVsTime(rx_sig, 'Received Samples', sample_rate)

tx_sig = pr.extractSig(orig_ch)
print("--- Plotting transmited chirp, stored in %s ---" % orig_ch)
pr.plotChirpVsTime(tx_sig, 'Transmitted Chirp', sample_rate)

# Correlate the two chirps to determine time difference
print("--- Match filtering received chirp with transmitted chirp ---")
xcorr_sig = sp.correlate(rx_sig, tx_sig, mode='valid', method='auto')
# as finddirectpath is written right now, it must be called before taking log of the signal
# because if not, negative log values could have a greater absolute value than positive log values.
dir_peak = pr.findDirectPath(xcorr_sig, direct_start, True) 

#Test code, Only have one option uncommented--------
#Option 1:
xcorr_sig = 20 * np.log10(np.absolute(xcorr_sig))

#Option 2:
#xcorr_normalized = np.abs(xcorr_sig)
#xcorr_normalized /= np.max(xcorr_normalized)
#xcorr_sig = 20 * np.log10(xcorr_normalized + 0.00000000000000001)
#-------------------------------------------

print("--- Plotting result of match filter ---")
xcorr_samps = np.shape(xcorr_sig)[0]
print(echo_start)
xcorr_time = np.zeros(xcorr_samps)
for x in range (xcorr_samps):
    xcorr_time[x] = x * 1e6 /sample_rate

# plt.figure()
# plt.plot(xcorr_time, xcorr_sig)
# plt.title("Output of Match Filter: Signal")
# plt.xlabel('Time (us)')
# plt.ylabel('Power [dB]')
# plt.grid()

#COLIN TEST CODE----------
numSamps = 2000
xTime = np.arange(-10,numSamps) * 1e6 / sample_rate
    
plt.figure()
plt.plot(xTime, xcorr_sig[dir_peak-10:dir_peak+numSamps])
plt.title("Output of Match Filter: Peaks")
plt.xlabel('Time (us)')
plt.ylabel('Power [dB]')
plt.grid()
#-------------------

#Unedited version of above code
# plt.figure()
# #plt.plot(range(-10,10000), xcorr_sig[dir_peak-10:dir_peak+10000])
# plt.title("Output of Match Filter: Peaks")
# plt.xlabel('Sample')
# plt.ylabel('Power [dB]')
# plt.grid()

[echo_samp, echo_dist] = pr.findEcho(xcorr_sig, sample_rate, dir_peak, echo_start, sig_speed, True)

sys.stdout.flush()
#plt.savefig("match_filter.png", dpi=150)
plt.show()