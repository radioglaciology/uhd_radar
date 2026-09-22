"""
Outdoor (free-space) range detection test.

Companion to loopback_testing.py. Where the loopback test measures a known coax
delay (one-way, velocity factor ~2/3), this script measures the two-way delay
to a target in air and reports its range relative to the direct TX->RX antenna
coupling peak, so no hardware-latency calibration constant is needed.

Processing chain:
    1. Load the recording and coherently stack all pulses into one trace
    2. Matched filter against a windowed copy of the transmitted chirp
    3. Locate the direct-path peak and use it as the zero-delay reference
    4. (Optional) coherently subtract a background recording with no target
    5. Search for the target echo beyond a blanking distance

Usage (from the repository root):
    python postprocessing/test_scripts/outdoor_range_testing.py data/20260301_120000
    python postprocessing/test_scripts/outdoor_range_testing.py data/20260301_120000 \\
        --background data/20260301_115500 --expected-range 150 --antenna-separation 2
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as sp

# Resolve imports relative to this file so the script works from any directory
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.append(os.path.join(REPO_ROOT, "postprocessing"))
sys.path.append(os.path.join(REPO_ROOT, "preprocessing"))
import processing
from generate_chirp import generate_chirp

C0 = 299792458.0  # speed of light in vacuum (m/s)
AIR_VELOCITY_FACTOR = 0.9997  # refractive index of air at sea level ~1.0003

WINDOWS = {
    "rectangular": "boxcar",
    "hann": "hann",
    "hamming": "hamming",
    "blackman": "blackman",
    "taylor": ("taylor", 4, 35),  # 4 nbar, -35 dB sidelobes
    "tukey": ("tukey", 0.25),
}


def normalize_prefix(path):
    """Accept either a data prefix or its _config.yaml path and return the prefix."""
    suffix = "_config.yaml"
    return path[:-len(suffix)] if path.endswith(suffix) else path


def load_stacked_trace(prefix):
    """
    Load a recording and coherently average every pulse into one trace.

    Returns:
        trace: 1D complex array (fast-time samples)
        sample_rate: sampling rate in Hz
        config: the recording's config dictionary
    """
    _, sample_rate, rx = processing.load_radar_data(prefix)
    n_pulses = rx.shape[1]  # rx is (samples x pulses)
    if n_pulses == 0:
        raise ValueError(f"No pulses found in {prefix}_rx_samps.bin")
    trace = processing.stack(rx, n_pulses)[:, 0]
    print(f"Loaded {prefix}: {rx.shape[0]} samples/pulse, stacked {n_pulses} pulses")
    return trace, sample_rate, processing.load_config(prefix)


def windowed_reference(chirp, window="hann"):
    """
    Apply an amplitude window to the non-zero part of the chirp for use as the
    matched filter reference. This trades a small loss of range resolution for
    much lower range sidelobes, which keeps the direct path from masking weak
    nearby targets.
    """
    if window not in WINDOWS:
        raise ValueError(f"Unknown window '{window}', choose from {list(WINDOWS)}")
    ref = np.array(chirp, dtype=np.complex64)
    nonzero = np.flatnonzero(np.abs(ref) > 0)
    start, stop = nonzero[0], nonzero[-1] + 1
    ref[start:stop] *= sp.get_window(WINDOWS[window], stop - start)
    return ref


def matched_filter(trace, ref):
    """
    Cross-correlate the received trace with the reference chirp.

    Returns:
        compressed: complex matched filter output, normalized so a perfect copy
            of the reference produces a peak of 1
        lags: sample lag for each output sample (0 = reference aligned with
            the start of the receive window)
    """
    compressed = sp.correlate(trace, ref, mode="full") / np.sum(np.abs(ref) ** 2)
    lags = sp.correlation_lags(len(trace), len(ref), mode="full")
    return compressed, lags


def parabolic_peak(mag, idx):
    """Sub-sample peak location by fitting a parabola to the peak and its neighbors."""
    if idx <= 0 or idx >= len(mag) - 1:
        return float(idx)
    a, b, c = mag[idx - 1], mag[idx], mag[idx + 1]
    denom = a - 2 * b + c
    return idx + (0.5 * (a - c) / denom if denom != 0 else 0.0)


def find_direct_path(compressed, lags, min_lag=0):
    """
    Find the direct TX->RX antenna coupling peak. It is assumed to be the
    strongest return at or after min_lag samples.

    Returns the (fractional) index of the peak into compressed.
    """
    mag = np.abs(compressed)
    search = np.flatnonzero(lags >= min_lag)
    idx = search[np.argmax(mag[search])]
    return parabolic_peak(mag, idx)


def lags_to_range(n_samples, direct_idx, sample_rate, velocity, antenna_separation=0.0):
    """
    Convert matched filter sample indices to target range.

    The direct path covers the antenna separation d, and the echo covers
    roughly 2R (for R >> d). Delay measured from the direct peak is therefore
    (2R - d) / v, giving R = (delay * v + d) / 2.
    """
    delay = (np.arange(n_samples) - direct_idx) / sample_rate
    return (delay * velocity + antenna_separation) / 2.0


def find_echo(profile_db, ranges, blank_range, expected_range=None,
              search_halfwidth=None, prominence_db=6.0):
    """
    Find the target echo beyond the blanking range.

    If expected_range is given, returns the strongest peak within
    +/- search_halfwidth of it. Otherwise returns the strongest peak past
    the blanking range.

    Returns the index of the detected peak into profile_db, or None.
    """
    peaks, _ = sp.find_peaks(profile_db, prominence=prominence_db)
    peaks = peaks[ranges[peaks] > blank_range]
    if expected_range is not None and search_halfwidth is not None:
        peaks = peaks[np.abs(ranges[peaks] - expected_range) <= search_halfwidth]
    if len(peaks) == 0:
        return None
    return peaks[np.argmax(profile_db[peaks])]


def print_capability(config, velocity, window):
    """Print range resolution and how far the receive window can see."""
    bandwidth = config["GENERATE"]["chirp_bandwidth"]
    chirp_len = config["GENERATE"]["chirp_length"]
    rx_duration = config["CHIRP"]["rx_duration"]

    resolution = velocity / (2 * bandwidth)
    print(f"Range resolution (rectangular): {resolution:.2f} m "
          f"(expect ~1.3-1.8x wider with the '{window}' window)")

    # An echo is fully captured only if its whole chirp lands in the RX window
    full_range = (rx_duration - chirp_len) * velocity / 2
    any_range = rx_duration * velocity / 2
    if full_range <= 0:
        print(f"WARNING: rx_duration ({rx_duration*1e6:.1f} us) <= chirp_length "
              f"({chirp_len*1e6:.1f} us). Every echo is truncated, costing SNR "
              f"that grows with range. Echoes beyond ~{any_range:.0f} m are not "
              f"recorded at all. Increase rx_duration for outdoor tests.")
    else:
        print(f"Echoes fully captured out to {full_range:.0f} m "
              f"(partially out to {any_range:.0f} m)")


def plot_range_profile(ranges, profile_db, direct_range, blank_range, echo_idx=None,
                       expected_range=None, background_db=None, title="Range Profile"):
    plt.figure()
    if background_db is not None:
        plt.plot(ranges, background_db, color="0.6", label="background (no target)")
    plt.plot(ranges, profile_db, label="target" if background_db is None else "target - background")

    plt.axvline(direct_range, color="k", linestyle=":", label="direct path")
    plt.axvspan(ranges[0], blank_range, color="0.9", zorder=0, label="blanked")
    if expected_range is not None:
        plt.axvline(expected_range, color="r", linestyle="--", label=f"expected {expected_range:g} m")
    if echo_idx is not None:
        plt.plot(ranges[echo_idx], profile_db[echo_idx], "ko", label="detected echo")

    plt.title(title)
    plt.xlabel("Range (m)")
    plt.ylabel("Power relative to direct path (dB)")
    plt.grid()
    plt.legend()


def main():
    parser = argparse.ArgumentParser(description="Outdoor free-space range detection test")
    parser.add_argument("prefix", help="Data prefix of the target recording, e.g. data/20260301_120000")
    parser.add_argument("--background", help="Data prefix of a recording with no target, for clutter subtraction")
    parser.add_argument("--expected-range", type=float, help="Surveyed target range in meters")
    parser.add_argument("--search-halfwidth", type=float, default=20.0,
                        help="Only accept peaks within this many meters of --expected-range (default 20)")
    parser.add_argument("--blank-range", type=float, default=10.0,
                        help="Ignore returns closer than this many meters (direct path + near clutter, default 10)")
    parser.add_argument("--antenna-separation", type=float, default=0.0,
                        help="Distance between TX and RX antennas in meters (default 0)")
    parser.add_argument("--velocity-factor", type=float, default=AIR_VELOCITY_FACTOR,
                        help=f"Propagation speed as a fraction of c (default {AIR_VELOCITY_FACTOR} for air)")
    parser.add_argument("--window", default="hann", choices=list(WINDOWS),
                        help="Window applied to the matched filter reference (default hann)")
    parser.add_argument("--prominence", type=float, default=6.0,
                        help="Minimum peak prominence in dB for a detection (default 6)")
    parser.add_argument("--max-range", type=float, help="Limit the plot to this range in meters")
    args = parser.parse_args()

    prefix = normalize_prefix(args.prefix)
    velocity = args.velocity_factor * C0

    trace, sample_rate, config = load_stacked_trace(prefix)
    _, chirp = generate_chirp(config)
    ref = windowed_reference(chirp, args.window)
    print_capability(config, velocity, args.window)

    compressed, lags = matched_filter(trace, ref)
    direct_min_lag = int(config.get("PLOT", {}).get("direct_start", 0))
    direct_idx = find_direct_path(compressed, lags, min_lag=direct_min_lag)
    direct_peak = np.abs(compressed[int(round(direct_idx))])
    ranges = lags_to_range(len(compressed), direct_idx, sample_rate, velocity, args.antenna_separation)
    direct_range = args.antenna_separation / 2.0
    print(f"Direct path at sample {direct_idx:.2f} (lag {lags[0] + direct_idx:.2f})")

    background_db = None
    if args.background:
        # Coherent subtraction assumes TX and RX share a clock so the direct path
        # and static clutter have the same phase in both recordings
        bg_trace, bg_rate, _ = load_stacked_trace(normalize_prefix(args.background))
        if bg_rate != sample_rate or len(bg_trace) != len(trace):
            raise ValueError("Background recording must use the same sample rate and rx_duration")
        bg_compressed, _ = matched_filter(bg_trace, ref)
        background_db = 20 * np.log10(np.abs(bg_compressed) / direct_peak + 1e-12)
        compressed = compressed - bg_compressed

    profile_db = 20 * np.log10(np.abs(compressed) / direct_peak + 1e-12)

    blank_range = max(args.blank_range, direct_range)
    echo_idx = find_echo(profile_db, ranges, blank_range, args.expected_range,
                         args.search_halfwidth if args.expected_range is not None else None,
                         args.prominence)

    if echo_idx is None:
        print("No echo detected beyond the blanking range")
    else:
        # Interpolate between range bins using the linear magnitude
        echo_frac = parabolic_peak(np.abs(compressed), echo_idx)
        echo_range = np.interp(echo_frac, np.arange(len(ranges)), ranges)
        print(f"Detected echo at {echo_range:.2f} m, {profile_db[echo_idx]:.1f} dB relative to direct path")
        if args.expected_range is not None:
            print(f"Error vs expected {args.expected_range:g} m: {echo_range - args.expected_range:+.2f} m")

    # Plot from just before the direct path out to the requested/available range
    view = ranges >= direct_range - 5
    if args.max_range is not None:
        view &= ranges <= args.max_range
    offset = np.flatnonzero(view)[0]
    plot_range_profile(ranges[view], profile_db[view], direct_range, blank_range,
                       None if echo_idx is None or not view[echo_idx] else echo_idx - offset,
                       args.expected_range,
                       None if background_db is None else background_db[view],
                       title=f"Outdoor Range Profile ({os.path.basename(prefix)})")
    plt.show()


if __name__ == "__main__":
    main()
