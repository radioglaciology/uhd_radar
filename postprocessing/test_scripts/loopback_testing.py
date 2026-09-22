import numpy as np
import sys
sys.path.append("postprocessing")
import processing
import matplotlib.pyplot as plt
import os
import scipy.signal as sp
sys.path.append("preprocessing")
from generate_chirp import generate_chirp


def load_data(prefix):
    """
    Load radar data from the specified file prefix.
    """
    return processing.load_radar_data(prefix)

def matched_filter(rx, tx):
    """
    Apply matched filtering to the received signal using the transmitted signal.

    Args:
        rx: Received signal.
        tx: Transmitted signal.
    """
    tx_mf = np.conjugate(tx[::-1]) # complex conjugate of time reversed tx signal for convolution
    return sp.fftconvolve(rx, tx_mf[:, None], mode='full', axes=0)

def plot_compress(rx, tx, sample_rate):
    tx_conj = np.conjugate(tx)
    compressed = np.fft.ifftshift(np.fft.ifft(np.sum(rx, axis=1) * tx_conj))
    # convert to distance
    c = 299792458.0  # speed of light in vacuum (m/s)
    v = 2/3 * c
    n = np.arange(len(compressed))
    plt.figure()
    plt.plot(n, 20 * np.log10(np.abs(compressed)))
    plt.title("Compressed Signal")
    plt.xlabel("Samples")
    plt.ylabel("Normalized Amplitude (dB)")
    plt.grid()
    plt.show()

def plot_chirp(tx, sample_rate):
    """
    Plot the transmitted chirp signal.
    """
    fig, ax = processing.plotChirpVsTime(tx, "chirp", sample_rate)
    plt.show()

def stack(rx, n):
    """
    Coherently stack received signals.

    Args:
        rx: Received signals (2D array).
        n: Number of signals to stack.
    """
    return processing.stack(rx, n)

def plot_raw_matched(matched, sample_rate, tx):
    # on x,y plot
    plt.plot(20*np.log10(np.abs(np.sum(matched, axis=1))))
    plt.title("Raw Matched Filter Output")
    plt.xlabel("Samples")
    plt.ylabel("Normalized Amplitude (dB)")
    plt.grid()
    plt.show()

def plot_matched(matched, sample_rate, tx=None, velocity_factor=2/3, loopback=True, coax_length=None, zero_sample_idx=159):
    """Plot matched filter output with physical distance scaling.

    Args:
        matched: 2D array (samples x pulses) after matched filtering.
        sample_rate: sampling rate in Hz.
        tx: optional transmit chirp (1D array). If provided, its length is
            used to compute the delay offset. If not, a fallback is used.
        velocity_factor: fraction of light speed inside the medium (coax),
            default 2/3 for typical coax.
        loopback: True if the signal traverses the medium one-way (tx->rx).
            For free-space reflections set False (two-way and divide by 2).
        coax_length: optional expected coax length in meters; if provided,
            a vertical line and annotation for expected return is drawn.
    """
    c0 = 299792458.0  # speed of light in vacuum (m/s)

    range_profile = np.sum(matched, axis=1) # coherent sum across matched axis
    magnitude = np.abs(range_profile) 

    # Use the provided chirp length if available, otherwise fall back to 1120
    chirp_len = len(tx) if tx is not None else 1120
    delay_offset = chirp_len - 1 + zero_sample_idx # correct for the delay caused by the matched filter, 159 comes from hardware latency and is proprotional to sampling rate (taken from Loopback test jupyter notebook)

    n = np.arange(len(magnitude))
    # time per sample converted from sample index difference
    time = (n - delay_offset) / float(sample_rate)

    v = velocity_factor * c0
    if loopback:
        ranges = time * v
    else:
        ranges = time * v / 2.0

    v = 2/3 * 299792458
    #Change this variable if your cable isn't 50 meters
    cable_length = 50
    expected_idx = int(cable_length / v * sample_rate + len(tx) - 1)
    print("Expected sample index for", cable_length, "m:", expected_idx)

    mask = ranges >= 0
    magnitude = magnitude[mask]
    ranges = ranges[mask]
    magnitude = 20 * np.log10(magnitude)

    plt.figure()
    plt.plot(ranges, magnitude)
    plt.title("Matched Filter")
    plt.xlabel("Distance (meters)")
    plt.ylabel("Normalized Amplitude")
    plt.grid()

    # If expected physical length is provided, mark it on the plot
    if coax_length is not None:
        expected_time = coax_length / v
        expected_sample = expected_time * sample_rate + delay_offset
        expected_range = coax_length
        plt.axvline(expected_range, color='r', linestyle='--', label=f"expected {coax_length} m")

        # Diagnostics: find the received peak and suggest a velocity_factor correction
        peak_idx = np.argmax(magnitude)
        measured_range = ranges[peak_idx]

        # Print diagnostics to console so you can see numeric mismatch
        print(f"Matched peak index: {peak_idx}")
        print(f"Measured range at peak: {measured_range:.3f} m")
        print(f"Expected coax length: {coax_length} m")

        # Annotate peak and suggested correction on the plot
        plt.plot([measured_range], [magnitude[peak_idx]], 'ko', label=f'measured peak: {measured_range:.1f} m')
        plt.legend()

    plt.show()

def compress(stacked, chirp, sample_rate):
    fast_time, x = processing.pulse_compress(stacked, chirp, sample_rate)
    return fast_time, x

def main(prefix):
    #prefix = "../../data/20260219_202834"
    #prefix = "../../data/20260218_233213"
    slowtime, sample_rate, rx = load_data(prefix)
    _, chirp = generate_chirp(processing.load_config(prefix))
    #plot_chirp(chirp, sample_rate)
    stacked = stack(rx, len(rx)) # initial stack to reduce size of rx 
    matched = matched_filter(stacked, chirp)

    #Change your coax_length to the length of your cable
    plot_matched(matched, sample_rate, tx=chirp, velocity_factor=2/3, loopback=True, coax_length=50)
    plot_raw_matched(matched, sample_rate, tx=chirp)
    plot_compress(rx, chirp, sample_rate)