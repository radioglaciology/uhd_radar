#pragma once

#include "common.hpp"

#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/convert.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/barrier.hpp>
#include <fstream>
#include <csignal>
#include <complex>
#include <mutex>
#include <cstdlib>
#include <filesystem>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/write.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <boost/asio.hpp>
#include <thread>


#include "yaml-cpp/yaml.h"
#include "rf_settings.hpp"
#include "pseudorandom_phase.hpp"
#include "utils.hpp"
#include "sdr.hpp"
#include "chirp.hpp"



void transmit_worker(tx_streamer::sptr& tx_stream, rx_streamer::sptr& rx_stream, Chirp& chirp, Sdr& sdr);
void handleRxBuffer(size_t n_samps_in_rx_buff, rx_metadata_t& rx_md, Chirp& chirp, vector<complex<float>>& buff, vector<complex<float>>& sample_sum, float& inversion_phase);
bool checkForFullSampleSum(Chirp& chirp, vector<complex<float>>& sample_sum, ofstream& outfile);
void splitOutputFiles(Chirp& chirp, ofstream& outfile, string& current_filename, int& save_file_index);
void wrapUp(boost::asio::posix::stream_descriptor& gps_stream, ofstream& outfile, string& current_filename, boost::thread_group& transmit_thread);
void sendUBX(boost::asio::serial_port& serial, const std::vector<uint8_t>& msg);
void configureRate(boost::asio::serial_port& serial, int hz);
void configureNMEAMessages(boost::asio::serial_port& serial, uint8_t ggaRate);