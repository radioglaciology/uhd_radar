import sys
import argparse
import numpy as np
import scipy.signal
import scipy.fft
import matplotlib.pyplot as plt
from ruamel.yaml import YAML

# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')
args = parser.parse_args()

# Initialize constants
yaml = YAML(typ='safe')
with open(args.yaml_file) as stream:
    config = yaml.load(stream)
    gen_params = config["GENERATE"]
    chirp_type = gen_params["chirp_type"]
    sample_rate = gen_params["sample_rate"]
    chirp_bandwidth = gen_params["chirp_bandwidth"]
    window = gen_params["window"]
    chirp_length = gen_params["chirp_length"]
    filename = gen_params["out_file"]
    show_plot = gen_params["show_plot"]
    n_samples = int(chirp_length * sample_rate)
    
print("--- Loaded constants from config.yaml ---")

# Build chirp
print("--- Building Chirp ---")

end_freq = chirp_bandwidth / 2 # Chirp goes from -BW/2 to BW/2
start_freq = -1 * end_freq

ts = np.arange(0, chirp_length-(1/(2*sample_rate)), 1/(sample_rate))

if chirp_type == 'linear':
    ph = 2*np.pi*(start_freq*ts + (end_freq - start_freq) * ts**2 / (2*chirp_length))
elif chirp_type == 'hyperbolic':
    ph = 2*np.pi*(-1*start_freq*end_freq*chirp_length/(end_freq-start_freq))*np.log(1- (end_freq-start_freq)*ts/(end_freq*chirp_length))
else:
    ph = 2*np.pi*(start_freq*ts + (end_freq - start_freq) * ts**2 / (2*chirp_length))
    print(f"[ERROR] Unrecognized chirp type '{chirp_type}'")
    sys.exit(1)

chirp_complex = np.exp(1j*ph)


if window == "blackman":
    chirp_complex = chirp_complex * np.blackman(chirp_complex.size)
elif window == "hamming":
    chirp_complex = chirp_complex * np.hamming(chirp_complex.size)
elif window != "rectangular":
    print(f"[ERROR] Unrecognized window function '{window}'")
    sys.exit(1)


if show_plot:

    fig, axs = plt.subplots(2,1)

    # Time domain plot
    axs[0].plot(ts*1e6, np.real(chirp_complex), label='I')
    axs[0].plot(ts*1e6, np.imag(chirp_complex), label='Q')
    axs[0].set_xlabel('Time [us]')
    axs[0].set_ylabel('Samples')
    axs[0].set_title('Time Domain')
    axs[0].legend()

    # Frequency domain plot
    freqs = scipy.fft.fftshift(scipy.fft.fftfreq(chirp_complex.size, d=1/sample_rate))
    ms = 20*np.log10(scipy.fft.fftshift(np.abs(scipy.fft.fft(chirp_complex))))
    axs[1].plot(freqs/1e6, ms)
    axs[1].set_xlabel('Frequency [MHz]')
    axs[1].set_ylabel('Amplitude [dB]')
    axs[1].set_title('Frequency Domain')
    axs[1].grid()

    fig.tight_layout()

    plt.show()

# Convert to file 
print("--- Converting Chirp to File ---")
chirp_floats = np.empty(shape=(2* np.shape(chirp_complex)[0],), dtype=np.float32)
for x in range(np.shape(chirp_complex)[0]):
    chirp_floats[2*x] = chirp_complex[x].real
    chirp_floats[2*x+1] = chirp_complex[x].imag

chirp_floats.tofile(filename, sep='', format='%f')

# Read file just to check success
recov_floats = np.fromfile(filename, dtype=np.float32, count=-1, sep='', offset=0)
if np.array_equiv(recov_floats, chirp_floats):
    print("\tChirp successfully stored in %s" % filename)
else:
    print("\t[ERROR] Chirp was not successfully stored in %s" % filename)
    sys.exit(1)
