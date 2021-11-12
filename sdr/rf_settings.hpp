// Created 10/22/2021

#ifndef RF_SETTINGS_HPP
#define RF_SETTINGS_HPP

#include <uhd/usrp/multi_usrp.hpp>
#include "yaml-cpp/yaml.h"

using namespace uhd;
using namespace std;

// Set USRP RF parameters for a single channel of operation
bool set_rf_params_single(usrp::multi_usrp::sptr usrp, YAML::Node rf0, 
                          vector<size_t> rx_channels, vector<size_t> tx_channels);

// Set USRP RF parameters for multi channel operation
bool set_rf_params_multi(usrp::multi_usrp::sptr usrp, YAML::Node rf0, YAML::Node rf1, 
                         vector<size_t> rx_channels, vector<size_t> tx_channels);

// Check whether requested RF parameters are equal to the reported values
bool rf_error_check(usrp::multi_usrp::sptr usrp, YAML::Node rf, size_t tx_channel,
                    size_t rx_channel);

#endif // RF_SETTINGS_HPP