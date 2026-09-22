import sys
import os
import signal
import postprocessing.processing as pr
import argparse
import numpy as np
import scipy.signal as sp
import matplotlib.pyplot as plt
from ruamel.yaml import YAML as ym
import math as math
from run import *

c = 3e8
pi = math.pi

#This file is incomplete and has some todo taks. The purpose of this is to automatically optimize the rx gain 
#so the data that is recieved is optimized. Currently it is not functional, but has the base equations. This 
#was not able to be full implented due to time constraints and limitations in knowledge about python

parser = argparse.ArgumentParser()
parser.add_argument("yaml_file", nargs='?', default='config/default.yaml',help='Path to YAML configuration file')
parser.add_argument("rx_samps_file", nargs = '?', help = 'Path to file with rx data')
args = parser.parse_args()

#initializing variables from yaml
yaml = ym()                         # Always use safe load if not dumping
with open(args.yaml_file) as stream:
   config = yaml.load(stream)

   params = config["RF0"]

   tx_gain = float(params["tx_gain"])
   rx_gain = float(params["rx_gain"])
   freq = float(params["freq"])

   factors = config["ENVIRONMENTAL_FACTORS"]
   
   #TODO: use gps data to calculate radius(altitude) for geometric spreading
   radius = 100 # should be changed later, this is control data
   wavelength = (c/freq)
   geometric_spreading = ((wavelength)/(4*pi*radius))**2

   scattering_max = float(factors["scattering_max"])
   scattering_current = float(factors["scattering_current"])
   specular_reflection_coeff_max = float(factors["specular_reflection_coeff_max"])
   specular_reflection_coeff_current = float(factors["specular_reflection_coeff_current"])
   antenna_gain = float(factors["antenna_gain"])
   tilt_of_plane = float(factors["tilt_of_plane"])
   max_rx_gain = float(factors["max_rx_gain"])
   reference_power = float(factors["reference_power"])
   power_loss = float(factors["power_loss"])

   transmit_power = reference_power + tx_gain - power_loss #measurements in .yaml file

   
runner = RadarProcessRunner(args.yaml_file) #allows the code to be run from this file
signal.signal(signal.SIGINT, sigint_handler)
def sigint_handler(signum, frame):
   runner.stop() # On Ctrl-C, attempt to stop radar process

runner.setup()

print("Config factors and parameters being uploaded...")
print(os.getcwd()) #prints the file path the code is being executed on
rx_sig = pr.loadSamplesFromFile(args.rx_samps_file, config)
if rx_sig is None or len(rx_sig) == 0:
   raise ValueError("No recieved signals") # throws an error if rx_sigs is empty

running = True
while(running):

   max_rx_volts = np.max(rx_sig) #this takes binary data and I'm not sure if it outputs the correct number. Should be 1.1-1.2 volts if gain is maximized
   print(max_rx_volts)
   max_rx_samp_power = ((max_rx_volts)**2)/50 #R is 50 ohms

   current_rx_compute_power = transmit_power*antenna_gain*geometric_spreading*scattering_current*specular_reflection_coeff_current #using radar power equation with current varibales from config
   max_rx_compute_power = transmit_power*antenna_gain*geometric_spreading*scattering_max*specular_reflection_coeff_max #using radar power equation with max varibales from config
   
   dB_gain = max_rx_compute_power/transmit_power #calculate gain based on above samples

   if(dB_gain > max_rx_gain): #76 dB is the max availabe rx gain for b205 mini
      raise ValueError("Calculated rx gain is too high for b205 mini")
   #catch error?

   #TODO: generate and output graphs using new gain based on scales power

   print("New gain is", dB_gain, "dB. Are you satisfied with this new gain? 'yes' or 'no'")
   user_ans = input().lower()
   if(user_ans == 'yes'):
      running = False
   else:
      print("Running again. Please enter new samples into commad line when finished")
      running = True
      runner.run()
      runner.wait()
      runner.stop() #generates the chirp that the data is taken from
      #TODO: currently reads in user input files, but should be able to take data from this run and re-calculate gain based on that.