import os
import re

import xarray as xr
import dask.array as da
import dask

import numpy as np
import scipy.signal

import processing as old_processing

# Write docstring


def load_errors_from_log(log_file):
    """
    Load timestamp and ERROR_CODE_LATE_COMMAND information from a UHD stdout log file.
    Returns the starting timestamp and a dictionary of errors, where the keys are
    chirp indices and the values are error codes.
    """
    errors = None
    start_timestamp = None

    if os.path.exists(log_file):
        errors = {}

        log_f = open(log_file, 'r')
        log = log_f.readlines()

        for idx, line in enumerate(log):
            if "Receiver error:" in line:
                error_code = re.search(
                    "(?:Receiver error: )([\w_]+)", line).groups()[0]
                old_style_regex_search = re.search(
                    "(?:Scheduling chirp )([\d]+)", log[idx-1])
                if old_style_regex_search is not None:
                    chirp_idx = int(
                        re.search("(?:Scheduling chirp )([\d]+)", log[idx-1]).groups()[0])
                else:
                    chirp_idx = int(
                        re.search("(?:Chirp )([\d]+)", line).groups()[0])
                errors[chirp_idx] = error_code
                if error_code != "ERROR_CODE_LATE_COMMAND":
                    print(
                        f"WARNING: Uncommon error in the log: {error_code} (on chirp {chirp_idx})")
                    print(f"Full message: {line}")
            if ("[START]" in line) or ("Scheduling chirp 0 RX" in line):
                start_timestamp = float(
                    re.search("(?:\[)([\d]+\.[\d]+)", line).groups()[0])

        log_f.close()
        return start_timestamp, errors
    else:
        raise FileNotFoundError(f"Log file not found: {log_file}")


def save_radar_data_to_zarr(prefix, skip_if_cached=True, zarr_base_location=None, expected_base_name_regex='\d{8}_\d{6}', log_required=True, dryrun=False):

    #
    # Validation and file name generation
    #
    basename = os.path.basename(prefix)
    if expected_base_name_regex:  # Optional sanity check that basename makes sense -- set expected_base_name_regex to None to skip
        if not re.match(expected_base_name_regex, basename):
            raise ValueError(
                f"Prefix basename {basename} does not match expected regex {expected_base_name_regex}")

    # Generate expected zarr output location
    if zarr_base_location is None:
        zarr_path = prefix + ".zarr"
    else:
        zarr_path = os.path.join(zarr_base_location, basename + ".zarr")

    # Check if zarr file already exists, if so just return the path
    if skip_if_cached and os.path.exists(zarr_path):
        return zarr_path

    # Build filenames from prefix
    rx_samps_file = prefix + "_rx_samps.bin"
    log_file = prefix + "_uhd_stdout.log"

    #
    # Data loading
    #
    # Load configuration YAML
    config = old_processing.load_config(prefix)

    # Load raw RX samples
    rx_len_samples = int(config['CHIRP']['rx_duration']
                         * config['GENERATE']['sample_rate'])
    rx_sig = da.from_array(
        np.memmap(rx_samps_file, dtype=np.float32, mode='r', order='C'), chunks=rx_len_samples*2*100)
    rx_sig = (rx_sig[::2] + (1j * rx_sig[1::2]))
    n_rxs = rx_sig.size // rx_len_samples
    radar_data = da.transpose(da.reshape(
        rx_sig, (n_rxs, rx_len_samples), merge_chunks=True))

    # Create time axes
    slow_time = np.linspace(0, config['CHIRP']['pulse_rep_int']
                            * config['CHIRP']['num_presums']*n_rxs, radar_data.shape[1])
    fast_time = np.linspace(
        0, config['CHIRP']['rx_duration'], radar_data.shape[0])

    # Load raw data from log
    log = None
    if os.path.exists(log_file):
        with open(log_file, 'r') as log_f:
            log = log_f.read()
    else:
        if log_required:
            raise FileNotFoundError(
                f"Log file not found: {log_file}. If a log file is not required, set log_required=False")

    # Save radar_data, slow_time, and fs to an xarray datarray
    data = xr.DataArray(
        data=radar_data,
        name="radar_data",
        coords={
            "fast_time": (["fast_time"], fast_time, {"description": "relative time in seconds"}),
            "slow_time": (["slow_time"], slow_time, {"description": "time in seconds"}),
        },
        attrs={
            "config": config,
            "stdout_log": log,
            "prefix": prefix,
            "basename": basename
        }
    )

    if not dryrun:
        with dask.config.set(scheduler='single-threaded'):
            data.to_zarr(zarr_path, mode="w")
    else:
        print("This is a dry run: not saving data to disk")
        print(data)

    return zarr_path


# Leftover error handling stuff TODO TODO TODO
# # Load errors from log file
#     timestamp, errors = load_errors_from_log(log_file)
#     error_idxs = np.array(list(errors.keys()))
#     if error_fill_value:
#         radar_data[:, error_idxs] = error_fill_value

#     if remove_errors:
#         all_idxs = np.arange(radar_data.shape[1])
#         keep_idxs = [x for x in all_idxs if x not in error_idxs]

#         radar_data = radar_data[:, keep_idxs]
#         n_rxs = len(keep_idxs)

#         slow_time = slow_time[keep_idxs]

#     # TODO: Fix slow time in case of errors

def stack(data, n_stack):
    """
    Stack (average) data along the slow time axis in chunks of n_stack chirps
    """
    return data.coarsen(slow_time=n_stack, boundary='trim').mean()


def pulse_compress(data, chirp, fs, zero_sample_idx=0):
    if len(data.shape) == 1:  # If 1 dimensional, assume it's a single trace
        data = da.expand_dims(data, axis=1)

    # We use the `apply_along_axis` function directly from Dask Array
    # Providing the dtype and shape is necessary because the auto-inference
    # gets the size wrong.
    # Documentation is a bit confusion, but `shape` needs to be the shape of
    # each individual output of the lambda function, not the shape of the
    # full result.

    pc = da.apply_along_axis(
        lambda x: scipy.signal.correlate(x, chirp, mode='valid') / np.sum(np.abs(chirp)**2),
        axis=0, arr=data.data,
        dtype=data.dtype, shape=(data.shape[0]-len(chirp)+1,)
    )

    fast_time = np.linspace(0, pc.shape[0]/fs, pc.shape[0])
    fast_time = fast_time - fast_time[zero_sample_idx]

    result = xr.DataArray(
        data=pc,
        name="pulse_compressed",
        coords={
            "fast_time": (["fast_time"], fast_time, {"description": "one-way travel time in seconds"}),
            "slow_time": data["slow_time"]
        },
        attrs=data.attrs
    )

    result.attrs["pule_compress"] = {"fs": fs, "chirp": chirp, "zero_sample_idx": zero_sample_idx}

    return result
