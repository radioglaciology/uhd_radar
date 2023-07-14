#!/bin/sh
# start up script for using the X310 w/ 1GB ethernet

#sudo ifconfig en9 192.168.10.1 netmask 255.255.255.0 up
#sudo ifconfig en9 mtu 1500
sudo ip addr add 192.168.10.1/24 dev enx3c18a09632e5
sudo sysctl -w net.core.wmem_max=2426666
sudo sysctl -w net.core.rmem_max=2426666
uhd_find_devices --args "addr=192.168.10.2"
uhd_usrp_probe --args addr=192.168.10.2
