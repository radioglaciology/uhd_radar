#!/bin/bash
source $HOME/miniconda/etc/profile.d/conda.sh
conda activate uhd
cd /home/ubuntu/uhd_radar
python -u manager/uav_payload_manager.py

