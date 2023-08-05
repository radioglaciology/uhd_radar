import sys
import argparse
import numpy as np
import scipy.signal
import scipy.fft
import matplotlib.pyplot as plt
from ruamel.yaml import YAML

def generate_chirp(config):
    """
    Generate a chirp according to parameters in the config dictionary, typically
    loaded from a config YAML file.

    Returns a tuple (ts, chirp_complex), where ts is a numpy array of time
    samples, and chirp_complex is a numpy array of complex floating point values
    representing the chirp.

    If you're looking for a floating point valued chirp to use in convolution,
    this is probably the right function.

    This function does not convert the complex numpy array to the cpu format
    expected by the radar code. If you want to produce samples to feed the radar
    code, look at `generate_from_yaml_filename` (later in this file) instead.
    """
    # Load parameters
    gen_params = config["GENERATE"]
    chirp_type = gen_params["chirp_type"]
    sample_rate = gen_params["sample_rate"]
    chirp_bandwidth = gen_params["chirp_bandwidth"]
    offset = gen_params["lo_offset_sw"]
    window = gen_params["window"]
    chirp_length = gen_params["chirp_length"]
    pulse_length = gen_params.get("pulse_length", chirp_length) # default to chirp_length is no pulse_length is specified

    # Build chirp

    end_freq = chirp_bandwidth / 2 # Chirp goes from -BW/2 to BW/2
    start_freq = -1 * end_freq

    start_freq += offset
    end_freq += offset

    ts = np.arange(0, chirp_length-(1/(2*sample_rate)), 1/(sample_rate))
    ts_zp = np.arange(0, (pulse_length)-(1/(2*sample_rate)), 1/(sample_rate))

    if chirp_type == 'linear':
        ph = 2*np.pi*((start_freq)*ts + (end_freq - start_freq) * ts**2 / (2*chirp_length))
    elif chirp_type == 'hyperbolic':
        ph = 2*np.pi*(-1*start_freq*end_freq*chirp_length/(end_freq-start_freq))*np.log(1- (end_freq-start_freq)*ts/(end_freq*chirp_length))
    else:
        ph = 2*np.pi*(start_freq*ts + (end_freq - start_freq) * ts**2 / (2*chirp_length))
        printf("[ERROR] Unrecognized chirp type '{chirp_type}'")
        return None, None

    chirp_complex = np.exp(1j*ph)

    if window == "blackman":
        chirp_complex = chirp_complex * np.blackman(chirp_complex.size)
    elif window == "hamming":
        chirp_complex = chirp_complex * np.hamming(chirp_complex.size)
    elif window == "kaiser14":
        chirp_complex = chirp_complex * np.kaiser(chirp_complex.size, 14.0)
    elif window == "kaiser10":
        chirp_complex = chirp_complex * np.kaiser(chirp_complex.size, 10.0)
    elif window == "kaiser18": 
        chirp_complex = chirp_complex * np.kaiser(chirp_complex.size, 18.0)
    elif window != "rectangular":
        print("[ERROR] Unrecognized window function '{window}'")
        return None, None

    chirp_complex = np.pad(chirp_complex, (int(np.floor(ts_zp.size - ts.size)/2),), 'constant')

    chirp_complex = chirp_complex

    return ts_zp, chirp_complex


def generate_from_yaml_filename(yaml_filename):
    """
    Generate a chirp and save it to a binary file, according to parameters loaded
    from the supplied YAML filename.

    Typically, this function is called to produce a chirp file. The save location
    of this file is specified in the YAML file, under the GENERATE section.

    This function also returns the numpy array written to the file. Note that this
    is different from the chirp_complex array returned by generate_chirp(), as this
    array is in the format that is written to the file (interleaved I/Q samples
    with whatever datatype was specified in config['DEVICE']['cpu_format']).
    """

    # Load YAML file
    yaml = YAML(typ='safe')
    stream = open(yaml_filename)
    config = yaml.load(stream)

    # Load some additional paramters needed here
    filename = config['GENERATE']["out_file"]
    show_plot = config['GENERATE']['show_plot']
    sample_rate = config['GENERATE']['sample_rate']

    cpu_format = config['DEVICE'].get('cpu_format', 'fc32')

    # Create the chirp
    ts, chirp_complex = generate_chirp(config)

    if ts is None:
        print("Error occured when generating chirp.")
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

    if cpu_format == 'fc32':
        output_dtype = np.float32
        scale_factor = 1.0
    elif cpu_format == 'sc16':
        output_dtype = np.int16
        scale_factor = np.iinfo(output_dtype).max - 1
    elif cpu_format == 'sc8':
        output_dtype = np.int8
        scale_factor = np.iinfo(output_dtype).max - 1
    else:
        raise Exception(f"Unrecognized cpu_format '{cpu_format}'. Must be one of 'fc32', 'sc16', or 'sc8'.")

    chirp_floats = np.empty(shape=(2* np.shape(chirp_complex)[0],), dtype=output_dtype)
    for x in range(np.shape(chirp_complex)[0]):
        chirp_floats[2*x] = scale_factor * chirp_complex[x].real
        chirp_floats[2*x+1] = scale_factor * chirp_complex[x].imag

    chirp_floats.tofile(filename, sep='')

    # Read file just to check success
    recov_floats = np.fromfile(filename, dtype=output_dtype, count=-1, sep='', offset=0)
    if np.array_equiv(recov_floats, chirp_floats):
        print("\tChirp successfully stored in %s" % filename)
        return chirp_floats
    else:
        print("\t[ERROR] Chirp was not successfully stored in %s" % filename)
        raise Exception("Chirp was not successfully stored")


if __name__ == '__main__':
    # Check if a YAML file was provided as a command line argument
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
            help='Path to YAML configuration file')
    args = parser.parse_args()

    try:
        generate_from_yaml_filename(args.yaml_file)
    except Exception as e:
        print(e)
        sys.exit(1)
    