# Assumes that coherent summation is OFF in coherent.cpp 
# (This depends on #ifdef and can't be set via yaml. Edit coherent.cpp
# directly--or, if you're doing this a lot, change #ifdef to an if 
# statement so that you can set the value of average_before_save via yaml)
# Assumes that SDR configuration is loopback. 
import numpy as np
import math
import numpy.random as rand
import scipy.signal as sp
import matplotlib.pyplot as plt
import processing as pr
from ruamel.yaml import YAML as ym

# Initialize constants
yaml = ym(typ='safe')                 # Always use safe load if not dumping
with open("config/default.yaml") as stream:
    config = yaml.load(stream)
    noise_params = config["NOISE"]
    sample_rate = noise_params["sample_rate"]
    rx_samps = noise_params["rx_samps"]
    orig_chirp = noise_params["orig_chirp"]
    noise_std = noise_params["noise_std"]
    pulses = noise_params["num_pulses"]
    presums = noise_params["num_presums"]
    internal_start = noise_params["internal_start"]
    show_graphs = noise_params["show_graphs"]
    describe = noise_params["describe"]
    
coh_sums = math.ceil(pulses/presums)   # If I understand what main.cpp is doing
    
print("--- Loaded constants from config.yaml ---")
    
# Open received data
print("--- Opening data and determining internal path peak for first signal---")
rx_sig = pr.extractSig(rx_samps)
#rx_sig = np.divide(rx_sig, presums)
n_rx_samps = int(np.shape(rx_sig)[0])
if (show_graphs): pr.plotSigVsTime(rx_sig, 'Loopback Test', sample_rate) 

# Read original chirp
tx_sig = pr.extractSig(orig_chirp)
n_tx_samps = int(np.shape(tx_sig)[0])
if (show_graphs): pr.plotSigVsTime(tx_sig, 'Original Chirp', sample_rate)

# Read the first chirp in received data -- this is the "ideal" signal for SNR
samps_per = int(n_rx_samps / coh_sums)
rx_ideal = rx_sig[0:samps_per]
xcorr_ideal = np.abs(sp.correlate(rx_ideal, tx_sig, mode='valid', method='auto'))
internal_peak = pr.findInternalPath(xcorr_ideal, internal_start, describe)

if (show_graphs): pr.plotSigVsTime(rx_ideal, "'Ideal' Signal", sample_rate)

# Split received data into individual signals
print("--- Splitting rx signal into individual chirps ---")
signals = []   
for x in range(coh_sums):
   ind_start = x * (samps_per)
   this_sig = np.zeros(samps_per, dtype=np.csingle)
   for y in range(samps_per):
      this_sig[y] = rx_sig[ind_start + y]
   signals.append(this_sig)
signals = np.asarray(signals)

# For each signal:
count = 1
num_misalign = 0
total = np.zeros(samps_per, dtype=np.csingle)   
snrs = np.zeros(coh_sums)
for sig in signals:
    
    # Match filter signal & determine internal path peak to ensure alignment with ideal signal
    if (describe): print("\n--- Beginning Processing of Signal %d ---" % count)
    xcorr_sig = np.abs(sp.correlate(sig, tx_sig, mode='valid', method='auto'))
        
    aligned = (internal_peak == pr.findInternalPath(xcorr_sig, internal_start, describe))
    if (describe): print("\tIs Signal %d aligned with the first? %r" % (count, aligned))
    if (not aligned):
        if describe: print("--- Skipping signal %d due to poor alignment with first signal ---", count)
        num_misalign += 1
        continue

    # Add white noise
    if (describe): print("--- Adding White Noise ---")
    white_noise = np.zeros(samps_per, dtype=np.csingle)
    rx_noise = np.zeros(samps_per, dtype=np.csingle)
    for x in range(samps_per):
        noise_r = rand.normal(0, noise_std)
        noise_i = rand.normal(0, noise_std)
        white_noise[x] = np.csingle(complex(np.float32(noise_r), np.float32(noise_i)))
    rx_noise = np.add(white_noise, sig)         # Element-wise

    # Plot an example signal with white noise and match filter
    if (count == 1): 
        if (describe): print("--- Plotting an individual signal with added white noise ---")
        rx_time = np.zeros(samps_per)
        for x in range(samps_per):
            rx_time[x] = x/sample_rate
        rx_time *= 1e6
            
        pr.plotSigVsTime(rx_noise, "Individual Signal With Noise", sample_rate)
        
        xcorr_noise = np.abs(sp.correlate(rx_noise, tx_sig, mode='valid', method='auto'))
        xcorr_noise_samps = np.shape(xcorr_noise)[0]
        xcorr_noise_time = np.zeros(xcorr_noise_samps)
        for x in range(xcorr_noise_samps):
            xcorr_noise_time[x] = x * 1e6 / sample_rate
            
        plt.figure()
        plt.plot(xcorr_noise_time, 20* np.log10(xcorr_noise))
        plt.title("Match-filter of Individual Signal with Noise")
        plt.xlabel("Time (ms)")
        plt.ylabel("Power [dB]")
        plt.show()
    
    # Coherently sum and calculate SNR
    if (describe): print("--- Coherently sum result ---")
    for x in range(samps_per):
        total[x] += rx_noise[x]
    avg_total = np.divide(total, count)
    if (show_graphs): pr.plotSignal(avg_total, 'Received signal after %d coherent summations' % (count-num_misalign), sample_rate) 
    
    print("--- Calculating SNR after %d coherent summations ---" % (count - num_misalign))
    this_snr = pr.getSNR(rx_ideal, avg_total)
    print("\tSNR: %f" % this_snr)    
    snrs[count-1]=this_snr

    count += 1

print("\n--- Plotting total signal after %d coherent summations ---" % coh_sums)
pr.plotSigVsTime(avg_total, "Total After %d Averaged Summations" % coh_sums, sample_rate)

print("--- Plotting match filter of total signal after %d coherent summations ---" % coh_sums)
xcorr_total = np.abs(sp.correlate(avg_total, tx_sig, mode='valid', method='auto'))
plt.figure()
plt.plot(xcorr_noise_time, 20*np.log10(xcorr_total))        # xcorr_noise and xcorr_total are the same shape
plt.title("Match-filter of Total After %d Averaged Summations" % coh_sums)
plt.xlabel('Time (ms)')
plt.ylabel('Power [dB]')
plt.show()

print("--- Plotting SNR versus # Sums ---")
plt.figure()
plt.plot(np.add(1, range(coh_sums)), snrs)
plt.xlabel("Number of sums")
plt.ylabel("SNR")
plt.title("Signal to Noise Ratio vs. Number of Coherent Sums")
plt.show()


    
