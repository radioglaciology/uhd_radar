// Created 10/22/2021

#include <uhd/usrp/multi_usrp.hpp>
#include <thread>
#include "yaml-cpp/yaml.h"
#include "rf_settings.hpp"

using namespace std;
using namespace uhd;

/**
 * Set USRP RF parameters for a single channel of operation (one
 * set of tx/rx ports, single daughterboard). 
 * 
 * Inputs: usrp - sptr to a USRP device
 *         rf0 - YAML node describing the RF paramters to be set up,
 *               specific parameters are set in the config file
 * Outputs: returns true if all RF parameters were successfully set, 
 * otherwise returns false
 */
bool set_rf_params_single(usrp::multi_usrp::sptr usrp, YAML::Node rf0)
{
    // get first block of rf parameters (for channel 0)
    double rx_rate = rf0["rx_rate"].as<double>();
    double tx_rate = rf0["tx_rate"].as<double>();
    double fc = rf0["freq"].as<double>();
    double rx_gain = rf0["rx_gain"].as<double>();
    double tx_gain = rf0["tx_gain"].as<double>();
    double bw = rf0["bw"].as<double>();
    string tx_ant = rf0["tx_ant"].as<string>();
    string rx_ant = rf0["rx_ant"].as<string>();

    size_t channel = 0; // this is the default anyways

    // set the sample rates
    usrp->set_tx_rate(tx_rate, channel);
    usrp->set_rx_rate(rx_rate, channel);

    // set center frequency at same time for both daughterboards
    usrp->clear_command_time();
    usrp->set_command_time(usrp->get_time_now() + time_spec_t(0.1));

    tune_request_t tune_request(fc);
    usrp->set_rx_freq(tune_request, channel);
    usrp->set_tx_freq(tune_request, channel);

    // sleep 100ms (~10ms after retune occurs) to allow LO to lock
    this_thread::sleep_for(chrono::milliseconds(110)); 
    usrp->clear_command_time();

    // set the rf gain
    usrp->set_rx_gain(rx_gain, channel);
    usrp->set_tx_gain(tx_gain, channel);

    // set the IF filter bandwidth
    if (bw != 0)
    {
        usrp->set_rx_bandwidth(bw, channel);
    }

    // set the antenna
    usrp->set_rx_antenna(rx_ant, channel);
    usrp->set_tx_antenna(tx_ant, channel);

    // sanity check actual values against requested values
    bool mismatch = rf_error_check(usrp, rf0, channel);

    return !mismatch;
}

/**
 * Set USRP RF parameters for multi channel operation. Right now this only works
 * for a two channel set up (will not work with multiple USRP devices configured
 * in one multi-usrp set up). See the following links for more information:
 * https://files.ettus.com/manual/page_configuration.html
 * https://files.ettus.com/manual/page_multiple.html
 * 
 * Inputs: usrp - sptr to a USRP device
 *         rf0 - YAML node describing the RF parameters to be set up for channel 0,
 *               specific parameters are set in the config file
 *         rf1 - YAML node describing the RF parameters to be set up for channel 1,
 *               specific parameters are set in the config file
 * Outputs: returns true if all RF parameters were successfully set, 
 * otherwise returns false
 */
bool set_rf_params_multi(usrp::multi_usrp::sptr usrp, YAML::Node rf0, YAML::Node rf1, 
                         vector<size_t> rx_channels, vector<size_t> tx_channels)
{
    // get first block of rf parameters (for channel 0)
    double rx_rate0 = rf0["rx_rate"].as<double>();
    double tx_rate0 = rf0["tx_rate"].as<double>();
    double fc0 = rf0["freq"].as<double>();
    double rx_gain0 = rf0["rx_gain"].as<double>();
    double tx_gain0 = rf0["tx_gain"].as<double>();
    double bw0 = rf0["bw"].as<double>();
    string tx_ant0 = rf0["tx_ant"].as<string>();
    string rx_ant0 = rf0["rx_ant"].as<string>();

    // get first block of rf parameters (for channel 1)
    double rx_rate1 = rf1["rx_rate"].as<double>();
    double tx_rate1 = rf1["tx_rate"].as<double>();
    double fc1 = rf1["freq"].as<double>();
    double rx_gain1 = rf1["rx_gain"].as<double>();
    double tx_gain1 = rf1["tx_gain"].as<double>();
    double bw1 = rf1["bw"].as<double>();
    string tx_ant1 = rf1["tx_ant"].as<string>();
    string rx_ant1 = rf1["rx_ant"].as<string>();

    // check if tx and rx channel lists are the same
    if (!(rx_channels == tx_channels)) {
        throw std::runtime_error("Different TX and RX channel lists are not currently supported.");
    } 
    vector<size_t> channels = tx_channels;

    // set the sample rates
    usrp->set_tx_rate(tx_rate0, channels[0]);
    usrp->set_rx_rate(rx_rate0, channels[0]);
    usrp->set_tx_rate(tx_rate1, channels[1]);
    usrp->set_rx_rate(rx_rate1, channels[1]);

    // set center frequency at same time for both daughterboards
    usrp->clear_command_time();
    usrp->set_command_time(usrp->get_time_now() + time_spec_t(0.1));

    tune_request_t tune_request0(fc0);
    tune_request_t tune_request1(fc1);
    usrp->set_tx_freq(tune_request0, channels[0]);
    usrp->set_rx_freq(tune_request0, channels[0]);
    usrp->set_tx_freq(tune_request1, channels[1]);
    usrp->set_rx_freq(tune_request1, channels[1]);

    // sleep 100ms (~10ms after retune occurs) to allow LO to lock
    this_thread::sleep_for(chrono::milliseconds(110)); 
    usrp->clear_command_time();

    // set the rf gain
    usrp->set_rx_gain(rx_gain0, channels[0]);
    usrp->set_tx_gain(tx_gain0, channels[0]);
    usrp->set_rx_gain(rx_gain1, channels[1]);
    usrp->set_tx_gain(tx_gain1, channels[1]);

    // set the IF filter bandwidth
    if (bw0 != 0) {
        usrp->set_rx_bandwidth(bw0, channels[0]);
    }
    if (bw1 != 0) {
        usrp->set_rx_bandwidth(bw1, channels[1]);
    }

    // set the antenna
    usrp->set_rx_antenna(rx_ant0, channels[0]);
    usrp->set_tx_antenna(tx_ant0, channels[0]);
    usrp->set_rx_antenna(rx_ant1, channels[1]);
    usrp->set_tx_antenna(tx_ant1, channels[1]);

    // sanity check actual values against requested values
    bool mismatch = rf_error_check(usrp, rf0, channels[0]);
    mismatch = mismatch || rf_error_check(usrp, rf1, channels[1]); 

    return !mismatch;
}

/** 
 * Check whether requested RF parameters are equal to the reported values. 
 * 
 * Inputs: usrp - sptr to a USRP device
 *         rf - YAML node describing the requested RF parameters,
 *               specific parameters are set in the config file
 *         channel - which channel these parameters correspond to
 * Outputs: returns true if there is a mismatch between requested and 
 * actual RF parameters. Returns false if everything was successfully set. 
 */
bool rf_error_check(usrp::multi_usrp::sptr usrp, YAML::Node rf, size_t channel) {
    bool mismatch = false;

    // get first block of requested rf parameters (for channel 0)
    double rx_rate = rf["rx_rate"].as<double>();
    double tx_rate = rf["tx_rate"].as<double>();
    double fc = rf["freq"].as<double>();
    double rx_gain = rf["rx_gain"].as<double>();
    double tx_gain = rf["tx_gain"].as<double>();
    double bw = rf["bw"].as<double>();
    string tx_ant = rf["tx_ant"].as<string>();
    string rx_ant = rf["rx_ant"].as<string>();

    if (usrp->get_rx_rate(channel) != rx_rate) {
        cout << boost::format("[WARNING]: Requested RX rate (CH%d): %f MHz. Actual RX rate: %f MHz.")
            % channel % (rx_rate / (1e6)) % (usrp->get_rx_rate(channel) / (1e6)) << endl;
        mismatch = true;
    }

    if (usrp->get_tx_rate(channel) != tx_rate) {
        cout << boost::format("[WARNING]: Requested TX rate (CH%d): %f MHz. Actual TX rate: %f MHz.")
            % channel % (tx_rate / (1e6)) % (usrp->get_tx_rate(channel) / (1e6)) << endl;
        mismatch = true;
    }

    if (usrp->get_rx_freq(channel) != fc) {
        cout << boost::format("[WARNING]: Requested RX center freq (CH%d): %f MHz. Actual RX center freq: %f MHz.")
            % channel % (fc / (1e6)) % (usrp->get_rx_freq(channel) / (1e6)) << endl;
        mismatch = true;
    }

    if (usrp->get_tx_freq(channel) != fc) {
        cout << boost::format("[WARNING]: Requested TX center freq (CH%d): %f MHz. Actual TX center freq: %f MHz.")
            % channel % (fc / (1e6)) % (usrp->get_tx_freq(channel) / (1e6)) << endl;
        mismatch = true;
    }

    if (usrp->get_rx_gain(channel) != rx_gain) {
        cout << boost::format("[WARNING]: Requested RX gain (CH%d): %f dB. Actual RX gain: %f dB.")
            % channel % rx_gain % usrp->get_rx_gain(channel) << endl;
        mismatch = true;
    }

    if (usrp->get_tx_gain(channel) != tx_gain) {
        cout << boost::format("[WARNING]: Requested TX gain (CH%d): %f dB. Actual TX gain: %f dB.")
            % channel % tx_gain % usrp->get_tx_gain(channel) << endl;
        mismatch = true;
    }

    if ((usrp->get_rx_bandwidth(channel) != bw) && (bw != 0)) {
        cout << boost::format("[WARNING]: Requested analog bandwidth (CH%d): %f MHz. Actual analog bandwidth: %f MHz.")
            % channel % bw % (usrp->get_rx_bandwidth(channel) / (1e6)) << endl;
        mismatch = true;
    }

    if (usrp->get_tx_antenna(channel) != tx_ant) {
        cout << boost::format("[WARNING]: Requested TX ant (CH%d): %s. Actual TX ant: %s.")
            % channel % tx_ant % usrp->get_tx_antenna(channel) << endl;
        mismatch = true;
    }

    if (usrp->get_rx_antenna(channel) != rx_ant) {
        cout << boost::format("[WARNING]: Requested RX ant (CH%d): %s. Actual RX ant: %s.")
            % channel % rx_ant % usrp->get_rx_antenna(channel) << endl;
        mismatch = true;
    }

    return mismatch;
}