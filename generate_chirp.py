import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt
from ruamel.yaml import YAML as ym

# Initialize constants
yaml = ym(typ='safe')
with open("config.yaml") as stream:
    config = yaml.load(stream)
    gen_params = config["GENERATE"]
    sample_rate = gen_params["sample_rate"]
    start_freq = gen_params["start_freq"]
    end_freq = gen_params["end_freq"]
    signal_time = gen_params["signal_time"]
    filename = gen_params["out_file"]
    n_samples = int(signal_time * sample_rate)

# Build chirp
samples = np.zeros(n_samples)
for x in range (n_samples):
    samples[x] = x/sample_rate   # time values
    
chirp = sp.chirp(samples, start_freq, signal_time, end_freq, 'linear')
chirp_complex = np.empty(shape=np.shape(chirp), dtype=np.csingle)
for x in range(np.shape(chirp)[0]):
    chirp_complex[x] = np.csingle(complex(chirp[x], 0))

plt.plot(samples, chirp_complex)
plt.xlabel('Time (s)')
plt.ylabel('Amplitude')
plt.title('Linear Up Chirp')
plt.show()

# Convert to file 

chirp_floats = np.empty(shape=(2* np.shape(chirp)[0],), dtype=np.float32)
for x in range(np.shape(chirp_complex)[0]):
    chirp_floats[2*x] = chirp_complex[x].real
    chirp_floats[2*x+1] = chirp_complex[x].imag

chirp_floats.tofile(filename, sep='', format='%f')

# Read file just to check success
# recov_floats = np.fromfile(filename, dtype=np.float32, count=-1, sep='', offset=0)



