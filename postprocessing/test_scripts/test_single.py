import numpy as np
import matplotlib.pyplot as plt
import warnings
from ruamel.yaml import YAML
import argparse



parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?',
    help='First file') #searching for yaml file that is in one line but none default to default
parser.add_argument("second_file", nargs='?',
    help="second file?")
parser.add_argument("third_file", nargs='?',
    help="third file?")
args = parser.parse_args() 
yaml_filename = args.yaml_file
second_filename = args.second_file
third_filename = args.third_file

print("first file is", yaml_filename)
print("second file is", second_filename)
print("third file is", third_filename)



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