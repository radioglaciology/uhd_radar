import xarray as xr
import dask.array as da

import numpy as np
import matplotlib.pyplot as plt

def plot_radargram(pulse_compressed, figsize=None, vmin=-70, vmax=-40, ylims=(65,15), sig_speed=None):
    duration_s = pulse_compressed.slow_time[-1] - pulse_compressed.slow_time[0]

    if figsize is None:
        figsize = (duration_s/10, 5)
            
    fig, ax = plt.subplots(1,1, figsize=figsize)

    return_power = 20*np.log10(np.abs(pulse_compressed.compute()))

    if sig_speed:
        y_axis = pulse_compressed.fast_time * (sig_speed / 2)
        y_axis_label = 'Distance to reflector [m]'
    else:
        y_axis = pulse_compressed.fast_time
        y_axis_label = 'One-way travel time [s]'
    
    p = ax.pcolormesh(pulse_compressed.slow_time, y_axis, return_power, shading='auto', cmap='inferno', vmin=vmin, vmax=vmax)
    clb = fig.colorbar(p, ax=ax)
    clb.set_label('Power [dB]')
    ax.set_xlabel('Time [s]')
    ax.set_ylabel(y_axis_label)

    ax.set_ylim(ylims[0], ylims[1])

    if 'basename' in pulse_compressed.attrs:
        ax.text(0, 1.05, pulse_compressed.basename, horizontalalignment='left', verticalalignment='center', transform=ax.transAxes)

    for item in ([ax.title, ax.xaxis.label, ax.yaxis.label, clb.ax.yaxis.label] +
                ax.get_xticklabels() + ax.get_yticklabels() + clb.ax.get_yticklabels()):
        item.set_fontsize(18)
        item.set_fontfamily('sans-serif')
        
    fig.tight_layout()

    return fig, ax
