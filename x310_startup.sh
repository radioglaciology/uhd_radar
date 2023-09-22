#!/bin/sh
# start up script for using the X310 w/ 1GB ethernet

# for anna's mac
#sudo ifconfig en9 192.168.10.1 netmask 255.255.255.0 up
#sudo ifconfig en9 mtu 1500
# for rugged:
#sudo ip addr add 192.168.10.1/24 dev enx54b20384a025
#sudo ifconfig enx54b20384a025 mtu 1500
sudo ip addr add 192.168.10.1/24 dev enp0s31f6
sudo ifconfig enp0s31f6 mtu 1500
sudo sysctl -w net.core.wmem_max=33554432
sudo sysctl -w net.core.rmem_max=33554432
sudo sysctl -w net.core.wmem_default=3354432
sudo sysctl -w net.core.rmem_default=33554432
uhd_find_devices --args "addr=192.168.10.2"
uhd_usrp_probe --args addr=192.168.10.2
