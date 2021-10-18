import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt

# This function extracts the complex signal stored in a bin file.
# The format of the bin file is <1st real><1st imag><2nd real><2nd imag>
# The real and imaginary parts of the signal are of type np.float32
# -----
# filename - the name of the bin file to open
def extractSig (filename):
    sig_floats = np.fromfile(filename, dtype=np.float32, count=-1, sep='', offset=0)

    n_samps = int(np.shape(sig_floats)[0]/2) # averaged chirp 
    sig_comp = np.empty(n_samps, dtype=np.csingle);
    for x in range (n_samps):
        sig_comp[x] = np.csingle(complex(sig_floats[2*x], sig_floats[2*x+1]))
    return sig_comp

# This function plots a TX or RX complex chirp in an Voltage vs. Time (ms) graph
# -----
# signal      - an array of samples to plot (complex)
# title       - the title of the signal (eg. TX chirp)
# sample_rate - the sampling rate of the signal
def plotSigVsTime (signal, title, sample_rate):
    "Plot a transmitted or received signal versus time."
    n_rx_samps = np.shape(signal)[0]
    rx_time = np.zeros(n_rx_samps)
    for x in range(n_rx_samps):
        rx_time[x] = x/sample_rate  
    rx_time *= 1e6      # Convert from s to ms
        
    fig, axs = plt.subplots(2)
    fig.suptitle(title)
    axs[0].set(xlabel='Time (ms)', ylabel='Real Voltage [V]')
    axs[0].plot(rx_time, np.real(signal))
    axs[1].set(xlabel='Time (ms)', ylabel='Imaginary Voltage [V]')
    axs[1].plot(rx_time, np.imag(signal))
    return

# This function plots a TX or RX complex chirp in an Voltage vs. Sample graph.
# -----
# signal      - an array of samples to plot (complex)
# title       - the title of the signal (eg. TX chirp)
def plotSigVsSample (signal, title):
    "Plot a transmitted or received signal versus sample."        
    fig, axs = plt.subplots(2)
    fig.suptitle(title)
    axs[0].set(xlabel='Sample', ylabel='Real Voltage [V]')
    axs[0].plot(np.real(signal))
    axs[1].set(xlabel='Sample', ylabel='Imaginary Voltage [V]')
    axs[1].plot(np.imag(signal))
    return

# This function returns the sample number of the direct path peak 
# in a match-filtered rx signal. 
# -----
# correl_sig   - the match-filtered signal to examine (complex)
# direct_start - start search for direct path peak at this sample (sample)
# describe     - whether to include a text description as code executes
def findDirectPath (correl_sig, direct_start, describe=True):
    "Find the direct path peak of a cross-correlated signal."
    
    if (describe): print("--- Searching for direct path peak ---")
    if (describe): print("\tStarting search for direct path peak at sample %d." % direct_start)
    relevant_dir = correl_sig[direct_start:]
    dir_peak = np.argmax(np.absolute(relevant_dir)) + direct_start
    if (describe): print("\tThe direct path peak was found at sample %d with value %f." % (dir_peak, np.abs(correl_sig[dir_peak])))
    return dir_peak

# This function returns strongest echo peak in a match-filtered rx signal
# and the estimated distance to the reflector. Searching is conducted by 
# specifying the number of samples past the direct path peak to begin search.
# -----
# correl_sig  - the match-filtered signal to examine
# sample_rate - the sampling rate of the signal (s/s)
# dir_peak    - the location of the direct path peak (sample)
# echo_start  - start search for echo peak this number of samples past dir_peak
# sig_speed   - the speed of the signal through the medium (eg. 3e8) (m/s)
# describe    - whether to include a text description as code executes
def findEcho (correl_sig, sample_rate, dir_peak, echo_start, sig_speed, describe=True):
    "Find strongest echo in a correlated signal starting 'echo_start' samples past the direct path peak."
    if (describe): print("--- Searching for echo peak based on sample ---")
    if (describe): print("\tStarting search for echo peak at sample %d." % (dir_peak + echo_start))
    relevant_ech_s = correl_sig[(dir_peak + echo_start):]         
    echo_peak = np.argmax(relevant_ech_s) + dir_peak + echo_start
    if (describe): print("\tThe strongest echo peak was found at sample %d with value %f." % (echo_peak, correl_sig[echo_peak]))
    
    # Now calculate echo distance (assuming direct path occurs at time 0)
    # echo_dist = ((echo_peak - dir_peak) / sample_rate) * sig_speed
    #if (describe): print("\tThe signal echoed off a surface %f meters away. \n" % (echo_dist / 2))   
    #return [echo_peak, echo_dist]
    
    return echo_peak


# This function calculates the SNR of a signal knowing what the signal should
# ideally be.
# -----
# ideal  - the signal with no noise
# actual - the signal with noise
def getSNR (ideal, actual):
    # Verify signals have same shape
    if (np.shape(ideal)[0] != np.shape(actual)[0]):
        print("Cannot go forward: Ideal and Actual signals are not the same length.")
        print("Ideal: %d samples. Actual: %d samples." % (np.shape(ideal)[0], np.shape(actual)[0]))
        return
    
    # Find noise SNR and signal SNR
    noise = np.subtract(actual, ideal)
    n_samps = np.shape(ideal)[0]
    avg_noise_pwr = np.real(np.sum(np.multiply(noise, np.conj(noise)))) / n_samps
    avg_signal_pwr = np.real(np.sum(np.multiply(ideal, np.conj(ideal)))) / n_samps
    
    snr = avg_signal_pwr / avg_noise_pwr
    return snr

def testWindowSimple(test_sig, tx_sig, sample_rate, window, window_type = "No"):
    n_tx_samps = np.shape(tx_sig)[0]
    island = np.concatenate((np.zeros(n_tx_samps - 2), tx_sig, np.zeros(n_tx_samps - 2)))
    
    wind_message = "%s Window Applied" % window_type
    	
    plotSigVsTime(island, 'Zero-padded Original Chirp, %s' % wind_message, sample_rate)

	# Cross correlation with no window applied
    xcorr_raw = sp.correlate(island, tx_sig, mode='valid', method='auto') 
    xcorr_sig = 20 * np.log10(np.absolute(xcorr_raw))

    xcorr_sig_samps = np.shape(xcorr_sig)[0]
    xcorr_sig_time = np.zeros(xcorr_sig_samps)
    for x in range (xcorr_sig_samps):
	    xcorr_sig_time[x] = x * 1e6 /sample_rate

    plt.figure()
    plt.plot(xcorr_sig_time, xcorr_sig)
    plt.title("Cross Correlation of Generated Chirp with Itself, %s" % wind_message)
    plt.xlabel('Time (ms)')
    plt.ylabel('Power [dB]')
    plt.show()


def simulateRealistic(tx_sig, dir_peak, n_lb_samps, noise_std, sample_rate, amp_one, amp_two, delay, window, window_type = "No",                          ):
    
    wind_message = "%s Window Applied" % window_type
    
    print("--- Generating two scaled, shifted loopback signals ---")
    print("\tSimulating loopback signal without noise")
    n_tx_samps = np.shape(tx_sig)[0]
    if (window):
        loop_part = np.concatenate((np.zeros(dir_peak + 1), np.multiply(np.real(tx_sig), sp.windows.hann(n_tx_samps)), np.zeros(n_lb_samps - dir_peak - 1 - n_tx_samps)))
    else:
        loop_part = np.concatenate((np.zeros(dir_peak + 1), np.real(tx_sig), np.zeros(n_lb_samps - dir_peak - 1 - n_tx_samps)))

    plotSigVsTime(loop_part, "Real / Imag. Part of Simulated Loopback", sample_rate)

    loop_sim = np.zeros(n_lb_samps, dtype=np.csingle)
    for x in range(n_lb_samps):
        loop_sim[x] = np.csingle(complex(loop_part[x], loop_part[x]))
    
    white_noise = np.zeros(n_lb_samps, dtype=np.csingle)
    for x in range(n_lb_samps):
        noise_r = np.random.normal(0, noise_std)
        noise_i = np.random.normal(0, noise_std)
        white_noise[x] = np.csingle(complex(np.float32(noise_r), np.float32(noise_i)))
    
    loop_one = np.add((loop_sim * amp_one), white_noise)
    
    for x in range(n_lb_samps):
        noise_r = np.random.normal(0, noise_std)
        noise_i = np.random.normal(0, noise_std)
        white_noise[x] = np.csingle(complex(np.float32(noise_r), np.float32(noise_i)))
    
    samp_del = int((delay * 1e-6) * sample_rate) 
    print("\tSecond loopback signal shifted by %d samples compared to first." % samp_del)
    loop_two = np.roll(np.add((loop_sim * amp_two), white_noise), samp_del)
    simulated = loop_one + loop_two
    
    plotSigVsTime(loop_one, "Simulated Direct Path", sample_rate)
    plotSigVsTime(loop_two, "Simulated Echo Path", sample_rate)
    plotSigVsTime(simulated, "Simulated Receive Signal, %s" % wind_message, sample_rate)
    
    return simulated
    
def simulateCentered(tx_sig, noise_std, sample_rate, amp_one, amp_two, delay, window, window_type = "No"):
    
    wind_message = "%s Window Applied" % window_type
    
    print("--- Generating two scaled, shifted loopback signals ---")
    print("\tSimulating loopback signal without noise")
    n_tx_samps = np.shape(tx_sig)[0]
    if (window):
        loop_sig = np.multiply(np.real(tx_sig), sp.windows.hann(n_tx_samps))
    else:
        loop_sig = np.real(tx_sig)

    plotSigVsTime(loop_sig, "Real / Imag. Part of Simulated Loopback", sample_rate)
    
    n_loop_samps = 4*n_tx_samps 
    loop_part = np.zeros(n_loop_samps)
    half_samp_del = int(((delay * 1e-6) * sample_rate)/2) 
    
    for x in range(n_tx_samps):
        loop_part[int(n_loop_samps/2) - half_samp_del - int(n_tx_samps/2) + x] += amp_one * loop_sig[x]
        loop_part[int(n_loop_samps/2) + half_samp_del - int(n_tx_samps/2) + x] += amp_two * loop_sig[x]
            
    white_noise = np.zeros(n_loop_samps, dtype=np.csingle)
    for x in range(n_loop_samps):
        noise_r = np.random.normal(0, noise_std)
        noise_i = np.random.normal(0, noise_std)
        white_noise[x] = np.csingle(complex(np.float32(noise_r), np.float32(noise_i)))
    
    simulated = np.zeros(n_loop_samps, dtype=np.csingle)
    for x in range(n_loop_samps):
        simulated[x] = np.csingle(complex(loop_part[x], loop_part[x])) + white_noise[x]
    
    plotSigVsTime(simulated, "Simulated Receive Signal, %s" % wind_message, sample_rate)
    
    return simulated
