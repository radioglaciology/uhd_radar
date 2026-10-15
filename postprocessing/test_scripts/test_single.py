import numpy as np
import matplotlib.pyplot as plt
import loopback_testing as lt
import argparse
import re
import sys

sys.path.append('postprocessing')
import processing as pr

from ruamel.yaml import YAML as ym

# Check if a YAML file was provided as a command line argument
parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',
        help='Path to YAML configuration file')
args = parser.parse_args()

str_arg = str(args.yaml_file)
print("original string is:", str_arg)
#This code takes the timestamp from the "data/yyyymmdd_hhmmss_config.yaml" string
#If your output_dir isn't data/isn't 4 letters long, then edit the two indices below to line up with
#the timestamp
timestamp_search = re.search(r'\d+_\d+_f\d+e6', str_arg)
if not(timestamp_search):
    timestamp_search = re.search(r'\d+_\d+', str_arg)
timestamp = timestamp_search.group(0)

print("Prefix is:", timestamp)

# Initialize Constants
yaml = ym()                         # Always use safe load if not dumping
with open(args.yaml_file) as stream:
   config = yaml.load(stream)
   rx_params = config["PLOT"]
   sample_rate = rx_params["sample_rate"]    # Hertz
   
   orig_ch = rx_params["orig_chirp"]         # Chirp associated with the received data
   direct_start = rx_params["direct_start"]
   echo_start = rx_params["echo_start"]
   sig_speed = rx_params["sig_speed"]

   output_dir = config['FILES'].get('output_dir', 'data') 
   rx_samps = output_dir + "/" + timestamp + "_rx_samps.bin" # Received data to analyze
   prefix = output_dir + "/" + timestamp # Format neccessary for loading data

   rx_sig = pr.extractSig(rx_samps)
   tx_sig = pr.extractSig(orig_ch)
   print("Loaded data")

   print("rx size is first, tx size is second")
   print(rx_sig.shape)
   print(tx_sig.shape)


slowtime, sample_rate, rx = lt.load_data(prefix)
_, chirp = lt.generate_chirp(pr.load_config(prefix))

xaxis = np.linspace(0,1400,1400)
xaxis2 = np.linspace(0,1120,1120)
squish = np.sum(rx, axis=1)
tx_conj = np.conjugate(chirp)

difference = len(squish) - len(tx_conj)
zeros_to_add = int(difference/2)
zero_array = np.zeros(zeros_to_add)
print("sjsaeo", zero_array.size)
newtx = np.concatenate((zero_array, tx_conj, zero_array))




# make a loop for the mask and slowly increase it from lik e0.01 to however many until
# squish equals the length of tx-conj not just 1120 specifically


print("size of rx", squish.shape)
print("size of chirp,", newtx.shape)


plt.plot(xaxis, squish, 'bo')
plt.title('rx vs samples')
plt.show()

plt.plot(xaxis, newtx, 'bo')
plt.title("tx chirp vs samples")
plt.show()
# line = "data/20261014_165819_config.yaml"
# searched = re.search(r'\d+_\d+_f\d+e6', line)
# if not(searched):
#     searched = re.search(r'\d+_\d+', line)
# print(searched)
# print(searched.group(0))

# parser = argparse.ArgumentParser()
# parser.add_argument("yaml_file", nargs='?',
#     help='First file') #searching for yaml file that is in one line but none default to default
# parser.add_argument("second_file", nargs='?',
#     help="second file?")
# parser.add_argument("third_file", nargs='?',
#     help="third file?")
# args = parser.parse_args() 
# yaml_filename = args.yaml_file
# second_filename = args.second_file
# third_filename = args.third_file

# print("first file is", yaml_filename)
# print("second file is", second_filename)
# print("third file is", third_filename)



# x = np.array([0.0, 1.0, 2.0, 3.0,  4.0,  5.0])
# newx = []
# for i in range (0,6,1):
#     newx.append(x[i])
# print("length of newx", len(newx))

# ranges =  [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18]
# magnitude = [10,9,8,7,6, 6.5, 6.7, 7, 7.2,7.5, 7.9, 8, 8.1, 7.6,7.1, 6.7, 6.5, 6.7, 6.6]
# parabola_ranges = []
# parabola_magnitude = []

# plt.plot(ranges, magnitude, 'bo--')

# starting_index = 12
# j = starting_index
# counter_up = 1
# counter_down = 1

# while(magnitude[j] > magnitude[j-1]):
#     print("j is", j)
#     print("magnitude", magnitude[j])
#     print("magnitude of previous", magnitude[j-1])
#     parabola_ranges.append(ranges[j])
#     parabola_magnitude.append(magnitude[j])
#     j += 1
#     counter_up += 1

# while(counter_up < 3):
#     print("adding additional earlier values")
#     # is the value before the starting index lower
#     if (magnitude[starting_index-1] < magnitude[starting_index]):
#         parabola_ranges.insert(0,ranges[starting_index-1])
#         parabola_magnitude.insert(0,magnitude[starting_index-1])
#         counter_up += 1
#     else: break

# while((magnitude[j] < magnitude[j-1]) & (counter_down < 3)):
#     print("j is", j)
#     print("magnitude", magnitude[j])
#     print("magnitude of previous", magnitude[j-1])
#     parabola_ranges.append(ranges[j])
#     parabola_magnitude.append(magnitude[j])
#     j += 1
#     counter_down += 1

# plt.plot(parabola_ranges, parabola_magnitude, 'ro-')

# # Creating the parabola and printing it on the plot
# p = np.polyfit(parabola_ranges, parabola_magnitude, 2)
# print("print p", p)
# if(p[0] >= 0):
#     print("Second parabola peak did not calculate properly")
# z = np.poly1d(p)
# xaxis = np.linspace(parabola_ranges[0], ranges[-1],50)
# plt.axvline(xaxis[0], color='g', linestyle='--', label=f"left border {xaxis[0]}")
# plt.axvline(xaxis[49], color='g', linestyle='--', label=f"right border{xaxis[49]}")
# plt.plot(xaxis, z(xaxis), "g--")

# # Get derivative of parabola, returns another polynomial coeffecients
# p_derivative = np.polyder(p)
# print("p derive", p_derivative)
# z_derivative = np.poly1d(p_derivative)
# plt.plot(xaxis, z_derivative(xaxis), 'r-')
# p_root = np.roots(p_derivative) #returns the roots, there should only be 1
# print("Roots", p_root) 
# plt.plot(p_root, z(p_root), 'ko', label=f'2nd parabola peak: {p_root[0]:.1f} m')
# print(f"Second parabola peak: {p_root[0]:.3f} m")

# plt.show()

    


# y = np.array([0.0, 0.8, 0.9, 0.1, -0.8, -1.0])
# z = np.polyfit(x, y, 3) #gives the coefficients but you can't do anything with them

# p = np.poly1d(z) #turns it into a nice function

# print(p(2))
# xaxis = np.linspace(0,5,100) 
# # for i in range(0, len(xaxis),1):
# #     print(xaxis[i])

# print(np.shape(xaxis))
# plt.plot(x,y, 'bo--', xaxis, p(xaxis), "g-")

# plt.show()