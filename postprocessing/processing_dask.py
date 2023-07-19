import os
import re

import xarray as xr
import dask.array as da
import dask

import numpy as np
import scipy.signal

import processing as old_processing

def process_stdout_log(log):
    """
    Load timestamp and ERROR_CODE_LATE_COMMAND information from UHD radar code stdout.
    Returns the starting timestamp and a dictionary of errors, where the keys are
    chirp indices and the values are error codes.
    """
    errors = {}
    start_timestamp = None

    for idx, line in enumerate(log.splitlines()):
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

    return start_timestamp, errors


def save_radar_data_to_zarr(prefix, skip_if_cached=True, zarr_base_location=None, expected_base_name_regex='\d{8}_\d{6}', log_required=True, dryrun=False):
    """
    Load raw radar data from a given prefix, and save it to a zarr file.
    
    `prefix` is the path to the raw data, without the _rx_samps.bin/_config.yaml/_uhd_stdout.log suffixes.
    (`log_required` can be set to False if no log file is available)

    As a safety precaution, this function will check that the prefix basename matches the `expected_base_name_regex` expression.
    If using the python code to run the radar system, the prefixes will be of the form YYYYMMDD_HHMMSS,
    which is what the default regex is looking for. `expected_base_name_regex` can be set to None to skip this check.
    
    The location for the zarr file is the same directory containing the prefix, unless you provide
    an alternate `zarr_base_location` argument.

    By default, this will first look for an existing zarr file that matches the expected filename.
    If you want to force reprocessing, set `skip_if_cached` to False.

    Setting `dryrun` to True will cause this function to return the path to the zarr file
    that it would have created without actually writing anything to disk.
    
    Returns the path to the zarr file only. You are responsible for re-loading the data from the zarr file.
    """

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

    cpu_format = config['DEVICE'].get('cpu_format', 'fc32')
    if cpu_format == 'fc32':
        output_dtype = np.float32
        scale_factor = 1.0
    elif cpu_format == 'sc16':
        output_dtype = np.int16
        scale_factor = np.iinfo(output_dtype).max
    elif cpu_format == 'sc8':
        output_dtype = np.int8
        scale_factor = np.iinfo(output_dtype).max
    else:
        raise Exception(f"Unrecognized cpu_format '{cpu_format}'. Must be one of 'fc32', 'sc16', or 'sc8'.")

    # Load raw RX samples
    rx_len_samples = int(config['CHIRP']['rx_duration']
                         * config['GENERATE']['sample_rate'])
    rx_sig = da.from_array(
        np.memmap(rx_samps_file, dtype=output_dtype, mode='r', order='C'), chunks=rx_len_samples*2*100)
    rx_sig = (rx_sig[::2] + (1j * rx_sig[1::2])).astype(np.complex64) / scale_factor
    n_rxs = rx_sig.size // rx_len_samples
    radar_data = da.transpose(da.reshape(
        rx_sig, (n_rxs, rx_len_samples), merge_chunks=True))

    # Create time axes
    slow_time = np.linspace(0, config['CHIRP']['pulse_rep_int']
                            * config['CHIRP'].get('num_presums', 1)*n_rxs, radar_data.shape[1])
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
    data = xr.Dataset(
        data_vars={
            "radar_data": (["sample_idx", "pulse_idx"], radar_data, {"description": "complex radar data"}),
        },
        coords={
            "sample_idx": ("sample_idx", np.arange(radar_data.shape[0]), {"description": "Index of this sample in the chirp"}),
            "fast_time": ("sample_idx", fast_time, {"description": "time relative to start of this recording interval in seconds"}),
            "pulse_idx": ("pulse_idx", np.arange(radar_data.shape[1]), {"description": "Index of this chirp in the sequence"}),
            "slow_time": ("pulse_idx", slow_time, {"description": "time in seconds"}),
        },
        attrs={
            "config": config,
            "stdout_log": log,
            "prefix": prefix,
            "basename": basename
        }
    )

    # TODO: Due to the currently hard-coded increase in pulse repetition interval after an error,
    # the slow time may not be correct.

    if not dryrun:
        with dask.config.set(scheduler='single-threaded'):
            data.to_zarr(zarr_path, mode="w")
    else:
        print("This is a dry run: not saving data to disk")
        print(data)

    return zarr_path


def fill_errors(data, error_fill_value=np.nan):
    """
    Replace all values from chirps with a reported error with the specified error_fill_value
    """
    _, errors = process_stdout_log(data.attrs["stdout_log"])
    result = data.copy()
    error_idxs = np.array(list(errors.keys()))

    # only fill errors if they exist, otherwise avoid throwing an index error
    if error_idxs.size != 0:
        result["radar_data"][{"pulse_idx": error_idxs}] = error_fill_value
        
    return result

def remove_errors(data, skip_if_already_complete=True):
    """
    Remove received data associated with chrips with a reported error
    """

    if "errors_removed" in data.attrs:
        if skip_if_already_complete:
            print("Errors have already been removed from this data, skipping")
            return data
        else:
            raise ValueError("Errors have already been removed from this data")

    _, errors = process_stdout_log(data.attrs["stdout_log"])
    result = data.copy()
    all_idxs = np.arange(data["radar_data"].shape[1])
    keep_idxs = [x for x in all_idxs if x not in list(errors.keys())]

    result = data[{'pulse_idx': keep_idxs}]
    result.attrs["errors_removed"] = True
    return result


def stack(data: xr.Dataset, n_stack: int):
    """
    Stack (average) data along the slow time axis in chunks of n_stack chirps
    All relevant coordinates (i.e. slow_time, pulse_idx) are taken as their
    minimum value in the stack.
    """
    return data.coarsen({'pulse_idx': n_stack},
                 boundary='trim',
                 coord_func='min').mean()


def pulse_compress(data: xr.Dataset, chirp, fs: float, zero_sample_idx: int=0, signal_speed: float=None):
    """
    Apply pulse compression using samples from `chirp` to each pulse from `data`.
    Zero travel time is assumed to be at `zero_sample_idx` in the chirp.
    If a `signal_speed` is provided, this is used to create a secondary coordinate
    `reflection_distance` which is the distance from the radar to the reflection point
    assuming a constant signal speed.
    """

    output_len = data["radar_data"].shape[0]-len(chirp)+1
    travel_time = np.linspace(0, output_len/fs, output_len)
    travel_time = travel_time - travel_time[zero_sample_idx]

    coords = {"travel_time": travel_time}
    if signal_speed is not None:
        coords['reflection_distance'] = ("travel_time", travel_time * (signal_speed/2))

    # This code is kind of a nightmare, but it should be a fairly efficient way
    # to do this.
    # This function call applies the lambda function (first argument) to each
    # pulse in the data.
    # If you want to understand all the other parameters, I recommend starting
    # with these pages:
    # https://docs.xarray.dev/en/stable/examples/apply_ufunc_vectorize_1d.html
    # https://docs.xarray.dev/en/stable/generated/xarray.apply_ufunc.html

    compressed = xr.apply_ufunc(
        lambda x: scipy.signal.correlate(
                x, chirp, mode='valid') / np.sum(np.abs(chirp)**2),
        data,
        input_core_dims=[['sample_idx']], # The dimension operated over -- aka "don't vectorize over this"
        output_core_dims=[["travel_time"]], # The output dimensions of the lambda function itself
        exclude_dims=set(("sample_idx",)), # Dimensions to not vectorize over
        vectorize=True, # Vectorize other dimensions using a call to np.vectorize
        dask="parallelized", # Allow dask to chunk and parallelize the computation
        output_dtypes=[data["radar_data"].dtype], # Needed for dask: explicitly provide the output dtype
        dask_gufunc_kwargs={"output_sizes": {'travel_time': output_len}} # Also needed for dask:
        # explicitly provide the output size of the lambda function. See
        # https://docs.dask.org/en/stable/generated/dask.array.gufunc.apply_gufunc.html
    ).assign_coords(coords) # And finally add coordinate(s) corresponding to the new "travel_time" dimension

    # Save the input parameters for future reference
    compressed.attrs["pulse_compress"]={
            "fs": fs, "chirp": chirp, "zero_sample_idx": zero_sample_idx, "signal_speed": signal_speed}
    
    return compressed
