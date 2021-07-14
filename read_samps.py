import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt

# Initialize Constants
sample_rate = 56e6;  # Hertz

## Part 1: Extract and Plot Received Samples

# Read received values from rx_samps.bin
rx_floats = np.fromfile('rx_samps.bin', dtype=np.float32, count=-1, sep='', offset=0)
#recov_floats = np.fromfile('chirp.bin', dtype=np.float32, count=-1, sep='', offset=0)

# Convert to complex values
n_rx_samps = int(np.shape(rx_floats)[0]/2) # averaged chirp 
#num_points = int(np.shape(recov_floats)[0]/20)   # first chirps (if averaging off)
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

## Part 2: Correlate with original chirp to determine time-offset

# Read Original Chirp from chirp.bin
tx_floats = np.fromfile('chirp.bin', dtype=np.float32, count=-1, sep='', offset=0)

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