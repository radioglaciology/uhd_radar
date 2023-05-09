import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt
import os
import re

# This function extracts the complex signal stored in a bin file.
# The format of the bin file is <1st real><1st imag><2nd real><2nd imag>
# The real and imaginary parts of the signal are of type np.float32
# -----
# filename - the name of the bin file to open
def extractSig (filename, count=-1, offset=0):
    sig_floats = np.fromfile(filename, dtype=np.float32, count=count, sep='', offset=offset)
    return (sig_floats[::2] + (1j * sig_floats[1::2])).astype(np.csingle)

# Load samples from a file safely
# Maximum file size and chunk-by-chunk loading used to manage memory
def loadSamplesFromFile(filename, config, reshape=True, max_chunk_size=int(5e8), max_seconds_to_load=60*20, load_start_seconds=0):

    rx_len_samples = int(config['CHIRP']['rx_duration'] * config['GENERATE']['sample_rate'])
    max_file_size_bytes = rx_len_samples*int(1/config['CHIRP']['pulse_rep_int'])*8*max_seconds_to_load
    load_start_bytes = rx_len_samples*int(1/config['CHIRP']['pulse_rep_int'])*8*load_start_seconds

    file_size_bytes = os.path.getsize(filename) - load_start_bytes
    if file_size_bytes > max_file_size_bytes:
        print(f"WARNING: File is {file_size_bytes/(2**30):.2f} GB ({file_size_bytes / (rx_len_samples*int(1/config['CHIRP']['pulse_rep_int'])*2):.2f} seconds). Only loading the first {max_seconds_to_load} seconds.")
        file_size_bytes = max_file_size_bytes

    rx_sig = np.zeros((file_size_bytes//8,), dtype=np.csingle)
    for start_offset in np.arange(0, file_size_bytes, max_chunk_size, dtype=np.int64):
        if start_offset + max_chunk_size > file_size_bytes:
            #print(f"{start_offset} (last) count: {file_size_bytes-start_offset}, offset: {load_start_bytes+start_offset}")
            rx_sig[(start_offset//8):] = extractSig(filename, count=file_size_bytes-start_offset, offset=load_start_bytes+start_offset)
        else:
            #print(f"{max_chunk_size}, {start_offset}")
            rx_sig[(start_offset//8):((start_offset//8)+(max_chunk_size//2))] = extractSig(filename, count=max_chunk_size, offset=load_start_bytes+start_offset)

    # Reshape
    if reshape:
        n_rxs = len(rx_sig) // rx_len_samples
        rx_sig = np.transpose(np.reshape(rx_sig, (n_rxs, rx_len_samples), order='C'))


    return rx_sig

# This function plots a TX or RX complex chirp in an Voltage vs. Time (ms) graph
# -----
# signal      - an array of samples to plot (complex)
# title       - the title of the signal (eg. TX chirp)
# sample_rate - the sampling rate of the signal
def plotChirpVsTime (signal, title, sample_rate):
    "Plot a transmitted or received signal versus time."
    n_rx_samps = np.shape(signal)[0]
    rx_time = np.zeros(n_rx_samps)
    for x in range(n_rx_samps):
        rx_time[x] = x/sample_rate  
    rx_time *= 1e6      # Convert from s to us
        
    fig, axs = plt.subplots(2)
    fig.suptitle(title)
    axs[0].set(xlabel='Time (us)', ylabel='Real')
    axs[0].plot(rx_time, np.real(signal))
    axs[1].set(xlabel='Time (us)', ylabel='Imaginary')
    axs[1].plot(rx_time, np.imag(signal))
    return fig, axs

# This function plots a TX or RX complex chirp in an Voltage vs. Sample graph.
# -----
# signal      - an array of samples to plot (complex)
# title       - the title of the signal (eg. TX chirp)
def plotChirpVsSample (signal, title):
    "Plot a transmitted or received signal versus sample."        
    fig, axs = plt.subplots(2)
    fig.suptitle(title)
    axs[0].set(xlabel='Sample', ylabel='Real Voltage')
    axs[0].plot(np.real(signal))
    axs[1].set(xlabel='Sample', ylabel='Imaginary Voltage')
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
    echo_dist = ((echo_peak - dir_peak) / sample_rate) * sig_speed
    if (describe): print("\tThe signal echoed off a surface %f meters away. \n" % (echo_dist / 2))   
    return [echo_peak, echo_dist]


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

def extractErrorsFromLog(log_file, raise_exception=False):
    errors = None

    if os.path.exists(log_file):
        errors = {}
        
        log_f = open(log_file, 'r')
        log = log_f.readlines()
        
        for idx, line in enumerate(log):
            if "Receiver error:" in line:
                error_code = re.search("(?:Receiver error: )([\w_]+)", line).groups()[0]
                chirp_idx = int(re.search("(?:Scheduling chirp )([\d]+)", log[idx-1]).groups()[0])
                errors[chirp_idx] = error_code
                if error_code != "ERROR_CODE_LATE_COMMAND":
                    print(f"WARNING: Uncommon error in the log: {error_code} (on chirp {chirp_idx})")
                    print(f"Full message: {line}")
                    if raise_exception:
                        raise Exception("Unexpected error found in log")
    else:
        print(f"WARNING: No log file found. This is fine, but checks for error codes will be disabled.")
        print(f"(Looking for a log file in: {log_file})")
        if raise_exception:
            raise Exception("Log file not found")

    return errors