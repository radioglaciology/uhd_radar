#!/bin/sh
# start up script for using the X310 w/ 1GB ethernet

# for anna's mac
#sudo ifconfig en9 192.168.10.1 netmask 255.255.255.0 up
#sudo ifconfig en9 mtu 1500
# for rugged:
#sudo ip addr add 192.168.10.1/24 dev enx54b20384a025
#sudo ip addr add 192.168.10.1/24 dev enp0s31f6
uhd_find_devices --args "addr=192.168.10.2"
uhd_usrp_probe --args addr=192.168.10.2
