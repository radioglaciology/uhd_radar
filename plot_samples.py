import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt
from ruamel.yaml import YAML as ym

# Initialize Constants
yaml = ym(typ='safe')
with open('config.yaml') as stream:
   config = yaml.load(stream)
   rx_params = config["PROCESS"]
   sample_rate = rx_params["sample_rate"]    # Hertz
   by_sample = rx_params["by_sample"]        # Search for echo by sample
   direct_start = rx_params["direct_start"]  # Samples past 0 to search for direct path peak
   echo_start = rx_params["echo_start"]      # Samples past direct path peak to search for echo
   by_distance = rx_params["by_distance"]    # Search for echo by distance
   sig_speed = rx_params["sig_speed"]        # Speed of signal through medium [ms/]
   echo_dist = rx_params["echo_dist"]        # Est. distance of echo [m]
   samp_buff = rx_params["samp_buff"]        # Start this # samps from distance-estimated peak
   
   rx_samps = rx_params["rx_samps"]          # Received data to analyze
   orig_ch = rx_params["orig_chirp"]         # Chirp associated with the received data
   
### Extract and Plot Received Samples ###

# Read received values from rx_samps.bin
rx_floats = np.fromfile(rx_samps, dtype=np.float32, count=-1, sep='', offset=0)

# Convert to complex values
n_rx_samps = int(np.shape(rx_floats)[0]/2) # averaged chirp 
rx_comp = np.empty(n_rx_samps, dtype=np.csingle);
for x in range (n_rx_samps):
    rx_comp[x] = np.csingle(complex(rx_floats[2*x], rx_floats[2*x+1]))
    
rx_samp_time = np.zeros(n_rx_samps)
for x in range(n_rx_samps):
    rx_samp_time[x] = x/sample_rate;  
    
plt.plot(rx_samp_time, rx_comp)
plt.xlabel('Time (s)')
plt.ylabel('Amplitude')
plt.title('Received Samples')
plt.show()

### Correlate with original chirp to determine time-offset ####

# Read Original Chirp from chirp.bin
tx_floats = np.fromfile(orig_ch, dtype=np.float32, count=-1, sep='', offset=0)

n_tx_samps = int(np.shape(tx_floats)[0]/2)

tx_comp = np.empty(n_tx_samps, dtype=np.csingle);
for x in range (n_tx_samps):
    tx_comp[x] = np.csingle(complex(tx_floats[2*x], rx_floats[2*x+1]))
    
tx_samp_time = np.zeros(n_tx_samps)
for x in range(n_tx_samps):
    tx_samp_time[x] = x/sample_rate;  
    
plt.plot(tx_samp_time, tx_comp)
plt.xlabel('Time (s)')
plt.ylabel('Amplitude')
plt.title('Original Chirp')
plt.show()

# Correlate the two chirps to determine time difference

correl_sig = sp.correlate(rx_comp, tx_comp, mode='valid', method='auto')
correl_times = np.empty(np.shape(correl_sig)[0])
for x in range (np.shape(correl_sig)[0]):
    correl_times[x] = x/sample_rate  # time units

plt.plot(correl_times, correl_sig)
plt.xlabel('Time Units (s)')
plt.ylabel('Amplitude')
plt.title('Correlated Chirps')
plt.show()

### Determine location of direct path and echo ###

print("---Searching for direct path peak---")
print("Starting search for direct path peak at sample %d." % direct_start)
relevant_dir = correl_sig[direct_start:]
dir_peak = np.argmax(relevant_dir) + direct_start
print("The direct path peak was found at sample %d.\n" % dir_peak)

if (by_sample):
   print("--- Searching for echo peak based on sample ---")
   print("Starting search for echo peak at sample %d." % (dir_peak + echo_start))
   relevant_ech_s = correl_sig[(dir_peak + echo_start):]         
   ech_peak_s = np.argmax(relevant_ech_s) + dir_peak + echo_start
   print("The strongest echo peak was found at sample %d." % ech_peak_s)
   
   # Now calculate echo distance (assuming direct path occurs at time 0)
   ech_dist = ((ech_peak_s - dir_peak) / sample_rate) * sig_speed
   print("The signal echoed off a surface %f meters away. \n" % (ech_dist / 2))
   
if (by_distance):
   print("--- Searching for echo peak based on distance estimation ---")
   ech_start = int((2 * echo_dist / sig_speed) * sample_rate + dir_peak)  # Est. echo peak sample
   print("The echo peak is estimated to be at %d samples after the direct peak." % (ech_start - dir_peak))
   print("Starting search for echo peak at sample %d." % (ech_start - samp_buff))
   relevant_ech_d = correl_sig[(ech_start - samp_buff):]
   ech_peak_d = np.argmax(relevant_ech_d) + ech_start - samp_buff
   print("Via distance estimation, the strongest echo peak was found at sample %d." % ech_peak_d)
   
   # Now calculate echo distance (assuming direct path occurs at time 0)
   echo_dist_real = ((ech_peak_d - dir_peak) / sample_rate) * sig_speed / 2
   print("The signal echoed off a surface %f meters away." % echo_dist_real)
