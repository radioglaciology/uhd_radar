#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/convert.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <boost/asio/io_service.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/write.hpp>

#include "yaml-cpp/yaml.h"

#include "rf_settings.hpp"
#include "pseudorandom_phase.hpp"
#include "utils.hpp"

using namespace std;
using namespace uhd;

/*
 * PROTOTYPES
 */
void transmit_worker(tx_streamer::sptr& tx_stream, rx_streamer::sptr& rx_stream);

/*
 * SIG INT HANDLER
 */
static bool stop_signal_called = false;
void sig_int_handler(int) {
  stop_signal_called = true;
}

/*** CONFIGURATION PARAMETERS ***/

// DEVICE
string device_args;
string subdev;
string clk_ref;
double clk_rate;
string tx_channels; 
string rx_channels;
string cpu_format;
string otw_format;

// GPIO
int pwr_amp_pin;
string gpio_bank;
uint32_t AMP_GPIO_MASK;
uint32_t ATR_MASKS;
uint32_t ATR_CONTROL;
uint32_t GPIO_DDR;
int ref_out_int;

// RF
double rx_rate;
double tx_rate;
double freq;
double rx_gain;
double tx_gain;
double bw;
string tx_ant;
string rx_ant;
bool transmit;

// CHIRP
double time_offset;
double tx_duration;
double rx_duration;
double tr_on_lead;
double tr_off_trail;
double pulse_rep_int;
double tx_lead;
int num_pulses;
int num_presums;
int max_chirps_per_file;
bool phase_dither;

// FILENAMES
string chirp_loc;
string save_loc;
string gps_save_loc;

// Calculated Parameters
double tr_off_delay; // Time before turning off GPIO
size_t num_tx_samps; // Total samples to transmit per chirp
size_t num_rx_samps; // Total samples to receive per chirp

// Global state
long int pulses_scheduled = 0;
long int pulses_received = 0;
long int error_count = 0;

// Cout mutex
std::mutex cout_mutex;

/*
 * UHD_SAFE_MAIN
 */
int UHD_SAFE_MAIN(int argc, char *argv[]) {

  /** Load YAML file **/

  string yaml_filename;
  if (argc >= 2) {
    yaml_filename = "../../" + string(argv[1]);
  } else {
    yaml_filename = "../../config/default.yaml";
  }
  cout << "Reading from config file: " << yaml_filename << endl;

  YAML::Node config = YAML::LoadFile(yaml_filename);

  YAML::Node dev_params = config["DEVICE"];
  subdev = dev_params["subdev"].as<string>();
  clk_ref = dev_params["clk_ref"].as<string>();
  device_args = dev_params["device_args"].as<string>();
  clk_rate = dev_params["clk_rate"].as<double>();
  tx_channels = dev_params["tx_channels"].as<string>();
  rx_channels = dev_params["rx_channels"].as<string>();
  cpu_format = dev_params["cpu_format"].as<string>("fc32");
  otw_format = dev_params["otw_format"].as<string>();

  YAML::Node gpio_params = config["GPIO"];
  gpio_bank = gpio_params["gpio_bank"].as<string>();
  pwr_amp_pin = gpio_params["pwr_amp_pin"].as<int>();
  pwr_amp_pin -= 2; // map the specified DB15 pin to the GPIO pin numbering
  if (pwr_amp_pin != -1) {
    AMP_GPIO_MASK = (1 << pwr_amp_pin);
    ATR_MASKS = (AMP_GPIO_MASK);
    ATR_CONTROL = (AMP_GPIO_MASK);
    GPIO_DDR = (AMP_GPIO_MASK);
  }
  
  ref_out_int = gpio_params["ref_out"].as<int>();

  YAML::Node rf0 = config["RF0"];
  YAML::Node rf1 = config["RF1"];
  rx_rate = rf1["rx_rate"].as<double>();
  tx_rate = rf1["tx_rate"].as<double>();
  freq = rf1["freq"].as<double>();
  rx_gain = rf1["rx_gain"].as<double>();
  tx_gain = rf1["tx_gain"].as<double>();
  bw = rf1["bw"].as<double>();
  tx_ant = rf1["tx_ant"].as<string>();
  rx_ant = rf1["rx_ant"].as<string>();

  transmit = rf0["transmit"].as<bool>(true); // True if transmission enabled

  YAML::Node chirp = config["CHIRP"];
  time_offset = chirp["time_offset"].as<double>();
  tx_duration = chirp["tx_duration"].as<double>();
  rx_duration = chirp["rx_duration"].as<double>();
  tr_on_lead = chirp["tr_on_lead"].as<double>();
  tr_off_trail = chirp["tr_off_trail"].as<double>();
  pulse_rep_int = chirp["pulse_rep_int"].as<double>();
  tx_lead = chirp["tx_lead"].as<double>();
  num_pulses = chirp["num_pulses"].as<int>();
  num_presums = chirp["num_presums"].as<int>(1); // Default of 1 is equivalent to no pre-summing
  phase_dither = chirp["phase_dithering"].as<bool>(false);

  YAML::Node files = config["FILES"];
  chirp_loc = files["chirp_loc"].as<string>();
  save_loc = files["save_loc"].as<string>();
  gps_save_loc = files["gps_loc"].as<string>();
  max_chirps_per_file = files["max_chirps_per_file"].as<int>();

  // Calculated parameters
  tr_off_delay = tx_duration + tr_off_trail; // Time before turning off GPIO
  num_tx_samps = tx_rate * tx_duration; // Total samples to transmit per chirp // TODO: Should use ["GENERATE"]["sample_rate"] instead!
  num_rx_samps = rx_rate * rx_duration; // Total samples to receive per chirp // TODO: Should use ["GENERATE"]["sample_rate"] instead!


  /** Thread, interrupt setup **/

  set_thread_priority_safe(1.0, true);
  
  signal(SIGINT, &sig_int_handler);

  /*** VERSION INFO ***/

  // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
  cout << "[VERSION] 0.0.1" << endl; // Version numbers: First number:  Increment for major new versions
                                     //                  Second number: Increment for any changes that you expect to matter to post-processing
                                     //                  Third number:  Increment for any change
  // Human-readable notes -- explain notable behavior for humans
  cout << "Note: Phase inversion is performed in this code." << endl;
  cout << "Note: Pre-summing is supported. If used, each sample written will have num_presums error-free samples averaged in." << endl;
  cout << "Note: Nothing is written to the file for error pulses." << endl;
  cout << "Note: A full num_pulses of error-free chirp data will be collected. ";
  cout << "(Total number of TX chirps will be num_pulses + # errors)" << endl;

  /*** SANITY CHECKS ***/
  
  if (tx_rate != rx_rate){
    cout << "WARNING: TX sample rate does not match RX sample rate.\n";
  }
  if (config["GENERATE"]["sample_rate"].as<double>() != tx_rate){
    cout << "WARNING: TX sample rate does not match sample rate of generated chirp.\n";
  }
  if (bw < config["GENERATE"]["chirp_bandwidth"].as<double>() && bw != 0){
    cout << "WARNING: RX bandwidth is narrower than the chirp bandwidth.\n";
  }
  if (config["GENERATE"]["chirp_length"].as<double>() > tx_duration){
    cout << "WARNING: TX duration is shorter than chirp duration.\n";
  }
  if (config["CHIRP"]["rx_duration"].as<double>() < tx_duration) {
    cout << "WARNING: RX duration is shorter than TX duration.\n";
  }
  
  /*** SETUP USRP ***/
  
  // create a usrp device
  cout << endl;
  cout << boost::format("Creating the usrp device with: %s...")
    % device_args << endl;
  usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
  cout << boost::format("TX/RX Device: %s") % usrp->get_pp_string() << endl;
  
  // Lock mboard clocks
  usrp->set_clock_source(clk_ref);
  usrp->set_time_source(clk_ref);

  if (clk_ref == "gpsdo") {
    // Check for 10 MHz lock
    vector<string> sensor_names = usrp->get_mboard_sensor_names(0);
    if (find(sensor_names.begin(), sensor_names.end(), "ref_locked")
        != sensor_names.end()) {
        cout << "Waiting for reference lock..." << flush;
        bool ref_locked = false;
        for (int i = 0; i < 30 and not ref_locked; i++) {
            ref_locked = usrp->get_mboard_sensor("ref_locked", 0).to_bool();
            if (not ref_locked) {
                cout << "." << flush;
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
        if (ref_locked) {
            cout << "LOCKED" << endl;
        } else {
            cout << "FAILED" << endl;
            cout << "Failed to lock to GPSDO 10 MHz Reference. Exiting."
                      << endl;
            exit(EXIT_FAILURE);
        }
    } else {
        cout << boost::format(
            "ref_locked sensor not present on this board.\n");
    }

    // Wait for GPS lock
    bool gps_locked = usrp->get_mboard_sensor("gps_locked", 0).to_bool();
    size_t num_gps_locked = 0;
    for (int i = 0; i < 30 and not gps_locked; i++) {
      gps_locked = usrp->get_mboard_sensor("gps_locked", 0).to_bool();
      if (not gps_locked) {
          cout << "." << flush;
          this_thread::sleep_for(chrono::seconds(1));
      }
    }
    if (gps_locked) {
        num_gps_locked++;
        cout << boost::format("GPS Locked\n");
    } else {
        cerr
            << "WARNING:  GPS not locked - time will not be accurate until locked"
            << endl;
    }

    // Set to GPS time
    time_spec_t gps_time = time_spec_t(
        int64_t(usrp->get_mboard_sensor("gps_time", 0).to_int()));
    usrp->set_time_next_pps(gps_time + 1.0, 0);

    // Wait for it to apply
    // The wait is 2 seconds because N-Series has a known issue where
    // the time at the last PPS does not properly update at the PPS edge
    // when the time is actually set.
    this_thread::sleep_for(chrono::seconds(2));

    // Check times
    gps_time = time_spec_t(
        int64_t(usrp->get_mboard_sensor("gps_time", 0).to_int()));
    time_spec_t time_last_pps = usrp->get_time_last_pps(0);
    cout << "USRP time: "
              << (boost::format("%0.9f") % time_last_pps.get_real_secs())
              << endl;
    cout << "GPSDO time: "
              << (boost::format("%0.9f") % gps_time.get_real_secs()) << std::endl;
    if (gps_time.get_real_secs() == time_last_pps.get_real_secs())
        cout << endl
                  << "SUCCESS: USRP time synchronized to GPS time" << endl
                  << endl;
    else
        std::cerr << endl
                  << "ERROR: Failed to synchronize USRP time to GPS time"
                  << endl
                  << endl;
  } else {
    // set the USRP time, let chill for a little bit to lock
    usrp->set_time_next_pps(time_spec_t(0.0));
    this_thread::sleep_for((chrono::milliseconds(1000)));
  }

  // always select the subdevice first, the channel mapping affects the
  // other settings
  if (transmit) {
    usrp->set_tx_subdev_spec(subdev);
  }
  usrp->set_rx_subdev_spec(subdev);

  // set master clock rate
  usrp->set_master_clock_rate(clk_rate);

  // detect which channels to use
  vector<string> tx_channel_strings;
  vector<size_t> tx_channel_nums;
  boost::split(tx_channel_strings, tx_channels, boost::is_any_of("\"',"));
  for (size_t ch = 0; ch < tx_channel_strings.size(); ch++) {
    size_t chan = stoi(tx_channel_strings[ch]);
    if (chan >= usrp->get_tx_num_channels()) {
      throw std::runtime_error("Invalid TX channel(s) specified.");
    } else
      tx_channel_nums.push_back(stoi(tx_channel_strings[ch]));
  }
  vector<string> rx_channel_strings;
  vector<size_t> rx_channel_nums;
  boost::split(rx_channel_strings, rx_channels, boost::is_any_of("\"',"));
  for (size_t ch = 0; ch < rx_channel_strings.size(); ch++) {
    size_t chan = stoi(rx_channel_strings[ch]);
    if (chan >= usrp->get_rx_num_channels()) {
      throw std::runtime_error("Invalid RX channel(s) specified.");
    } else
      rx_channel_nums.push_back(stoi(rx_channel_strings[ch]));
  }

  // set the RF parameters based on 1 or 2 channel operation
  if (tx_channel_nums.size() == 1) {
    set_rf_params_single(usrp, rf0, rx_channel_nums, tx_channel_nums);
  } else if (tx_channel_nums.size() == 2) {
    if (!transmit) {
      throw std::runtime_error("Non-transmit mode not supported by set_rf_params_multi");
    }
    set_rf_params_multi(usrp, rf0, rf1, rx_channel_nums, tx_channel_nums);
  } else {
    throw std::runtime_error("Number of channels requested not supported");
  }

  // allow for some setup time
  this_thread::sleep_for(chrono::seconds(1));
    
  cout << "INFO: Number of TX samples: " << num_tx_samps << endl;
  cout << "INFO: Number of RX samples: " << num_rx_samps << endl << endl;

  // Check Ref and LO Lock detect
  vector<std::string> tx_sensor_names, rx_sensor_names;
  if (transmit) {
    for (size_t ch = 0; ch < tx_channel_nums.size(); ch++) {
      // Check LO locked
      tx_sensor_names = usrp->get_tx_sensor_names(ch);
      if (find(tx_sensor_names.begin(), tx_sensor_names.end(), "lo_locked") != tx_sensor_names.end())
      {
        sensor_value_t lo_locked = usrp->get_tx_sensor("lo_locked", ch);
        cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string()
            << endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
      }
    }
  }

  for (size_t ch = 0; ch < rx_channel_nums.size(); ch++) {
    // Check LO locked
    rx_sensor_names = usrp->get_rx_sensor_names(ch);
    if (find(rx_sensor_names.begin(), rx_sensor_names.end(), "lo_locked") != rx_sensor_names.end())
    {
      sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked", ch);
      cout << boost::format("Checking RX: %s ...") % lo_locked.to_pp_string()
           << endl;
      UHD_ASSERT_THROW(lo_locked.to_bool());
    }
  }

  /*** SETUP GPIO ***/
  cout << "Available GPIO banks: " << std::endl;
  auto banks = usrp->get_gpio_banks(0);
  for (auto& bank : banks) {
      cout << "* " << bank << std::endl;
  }

  // basic ATR setup
  if (pwr_amp_pin != -1) {
    usrp->set_gpio_attr(gpio_bank, "CTRL", ATR_CONTROL, ATR_MASKS);
    usrp->set_gpio_attr(gpio_bank, "DDR", GPIO_DDR, ATR_MASKS);

    // set amp output pin as desired (on only when TX)
    usrp->set_gpio_attr(gpio_bank, "ATR_0X", 0, AMP_GPIO_MASK);
    usrp->set_gpio_attr(gpio_bank, "ATR_RX", 0, AMP_GPIO_MASK);
    usrp->set_gpio_attr(gpio_bank, "ATR_TX", 0, AMP_GPIO_MASK);
    usrp->set_gpio_attr(gpio_bank, "ATR_XX", AMP_GPIO_MASK, AMP_GPIO_MASK);
  }

  //cout << "AMP_GPIO_MASK: " << bitset<32>(AMP_GPIO_MASK) << endl;

  // turns external ref out port on or off
   if (ref_out_int == 1) {
    usrp->set_clock_source_out(true);
  } else if (ref_out_int == 0) {
    usrp->set_clock_source_out(false);
  } // else do nothing (SDR likely doesn't support this parameter)
  
  // update the offset time for start of streaming to be offset from the current usrp time
  time_offset = time_offset + time_spec_t(usrp->get_time_now()).get_real_secs();

  /*** TX SETUP ***/

  // Stream formats
  stream_args_t tx_stream_args(cpu_format, otw_format);
  tx_stream_args.channels = tx_channel_nums;

  // tx streamer
  tx_streamer::sptr tx_stream;
  if (transmit) {
    tx_stream = usrp->get_tx_stream(tx_stream_args);
    cout << "INFO: tx_stream get_max_num_samps: " << tx_stream->get_max_num_samps() << endl;
  }

  /*** RX SETUP ***/

  stream_args_t rx_stream_args(cpu_format, otw_format);

  // rx streamer
  rx_stream_args.channels = rx_channel_nums;
  rx_streamer::sptr rx_stream = usrp->get_rx_stream(rx_stream_args);

  cout << "INFO: rx_stream get_max_num_samps: " << rx_stream->get_max_num_samps() << endl;

  /*** SPAWN THE TX THREAD ***/
  boost::thread_group transmit_thread;
  transmit_thread.create_thread(boost::bind(&transmit_worker, tx_stream, rx_stream));
  
  if (!transmit) {
    cout << "WARNING: Transmit disabled by configuration file!" << endl;
  }

  //////////////////////////////////////////////////////////////////////////////////////////

  /*** FILE WRITE SETUP ***/
  boost::asio::io_service ioservice;

  if (save_loc[0] != '/') {
    save_loc = "../../" + save_loc;
  }
  if (gps_save_loc[0] != '/') {
    gps_save_loc = "../../" + gps_save_loc;
  }

  int gps_file = open(gps_save_loc.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
  if (gps_file == -1) {
      throw std::runtime_error("Failed to open GPS file: " + gps_save_loc);
  }

  boost::asio::posix::stream_descriptor gps_stream{ioservice, gps_file};
  auto gps_asio_handler = [](const boost::system::error_code& ec, std::size_t) {
    if (ec.value() != 0) {
      cout << "GPS write error: " << ec.message() << endl;
    }
  };

  ioservice.run();

  

  // open file for writing rx samples
  ofstream outfile;
  int save_file_index = 0;
  string current_filename = save_loc;
  if (max_chirps_per_file > 0) {
    // Breaking into multiple files is enabled
    current_filename = current_filename + "." + to_string(save_file_index);
  }

  // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
  cout << "[OPEN FILE] " << current_filename << endl;
  outfile.open(current_filename, ofstream::binary);

  /*** RX LOOP AND SUM ***/
  if (num_pulses < 0) {
    cout << "num_pulses is < 0. Will continue to send chirps until stopped with Ctrl-C." << endl;
  }

  string gps_data;

  if (cpu_format != "fc32") {
    cout << "Only cpu_format 'fc32' is supported for now." << endl;
    // This is because we actually need buff and sample_sum to have the correct
    // data type to facilitate phase modulation and summing. In the future, this could be
    // fixed up so that it can work with any supported cpu_format, but it
    // seems unnecessary right now.
    exit(1);
  }

  // receive buffer
  size_t bytes_per_sample = convert::get_bytes_per_item(cpu_format);
  vector<complex<float>> sample_sum(num_rx_samps, 0); // Sum error-free RX pulses into this vector
  //
  vector<complex<float>> intermediate_sum(num_rx_samps, 0); // Sum intermediate RX pulses
  vector<complex<float>> overflow_sum;
  //
  int intermediate_position = 0;
  //track poistion
  

  vector<complex<float>> buff(num_rx_samps); // Buffer sized for one pulse at a time
  vector<void *> buffs;
  for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++) {
    buffs.push_back(&buff.front()); // TODO: I don't think this actually works for num_channels > 1
  }
  size_t n_samps_in_rx_buff;
  rx_metadata_t rx_md; // Captures metadata from rx_stream->recv() -- specifically primarily timeouts and other errors

  float inversion_phase; // Store phase to use for phase inversion of this chirp
  int last_pulse_num_written = -1; // Index number (pulses_received - error_count) of last sample written to outfile

  // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
  cout << "[START] Beginning main loop" << endl;
  int count = 0;

  while ((num_pulses < 0) || (pulses_received < num_pulses)) {
    //add loop 
      //check error
      //check how many samps, write samps into intermediate storage
      //if enough samples, process and add to sample sum
      if(!overflow_sum.empty()) {
        std::copy(overflow_sum.begin(), overflow_sum.end(), intermediate_sum.begin());
        intermediate_position += overflow_sum.size();
        overflow_sum.clear();
      }
      //cout<< "Pulses: " << pulses_received << endl;

      while(intermediate_position < num_rx_samps) {
        
        count++;
        cout << count << endl;
        n_samps_in_rx_buff = rx_stream->recv(buffs, num_rx_samps, rx_md, 60.0, false);
        cout << "num samps in rx: " << n_samps_in_rx_buff << endl;
        cout << "space " << (num_rx_samps - intermediate_position - n_samps_in_rx_buff) << endl;
        cout << "num_rx_samps: " << num_rx_samps << endl;
        cout << "intermediate: " << intermediate_position << endl;
        //cout << "intermediate position: " << intermediate_position << endl;
        //cout << "num_rx_samps: " << num_rx_samps << endl;

        if (phase_dither) {
          inversion_phase = -1.0 * get_next_phase(false); // Get next phase from the generator each time to keep in sequence with TX
        }
        cout << count << endl;

        if (rx_md.error_code != rx_metadata_t::ERROR_CODE_NONE){
          // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
          cout_mutex.lock();
          cout << "[ERROR] (Chirp " << pulses_received << ") Receiver error: " << rx_md.strerror() << "\n";
          cout_mutex.unlock();
      
          pulses_received++;
          error_count++;
        } else if (n_samps_in_rx_buff != num_rx_samps) {
            // Unexpected number of samples received in buffer!
          // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
          cout_mutex.lock();
          cout << "[ERROR] (Chirp " << pulses_received << ") Unexpected number of samples in the RX buffer.";
          cout << " Got: " << n_samps_in_rx_buff << " Expected: " << num_rx_samps << endl;
          cout << "Note: Adding to Intermediate Sums" << endl;
          cout_mutex.unlock();
          // If you encounter this error, one possible reason is that the buffer sizes set in your transport parameters are too small.
          // For libUSB-based transport, recv_frame_size should be at least the size of num_rx_samps.
        }
        if(num_rx_samps < n_samps_in_rx_buff + intermediate_position) {
           intermediate_position = num_rx_samps;
          cout << "overflow " << endl;
          cout << intermediate_position << endl;
          // ADD ERROR IF TO MUCH DATA FOR OVERFLOW
          transform(intermediate_sum.begin() + intermediate_position, intermediate_sum.end() + 1, buff.begin(), intermediate_sum.begin() + intermediate_position, plus<complex<float>>());
          // Fill rest of space in intermediate buffer
          transform(overflow_sum.begin(), overflow_sum.begin() + n_samps_in_rx_buff - (num_rx_samps - intermediate_position), buff.begin() + num_rx_samps - intermediate_position , overflow_sum.begin(), plus<complex<float>>());
          overflow_position = n_samps_in_rx_buff -(num_rx_samps - intermediate_position) ;
          // Add the rest of samples to an overflow buffer which will be added to the intermediate buffer in the next iteration

        } else {
          cout << "before " << buff[200] << endl;
          //cout << "intermediate position: " << intermediate_position << endl;
        //std::copy(buff.begin(), buff.end(), intermediate_sum.begin() + intermediate_position);
        transform(intermediate_sum.begin() + intermediate_position, intermediate_sum.end(), buff.begin(), intermediate_sum.begin() + intermediate_position, plus<complex<float>>());
        cout << "before add: " << intermediate_position << endl;
        intermediate_position += n_samps_in_rx_buff;
        cout << "no overflow " << endl;
        cout << "after add: " << intermediate_position << endl;
        cout << "last" << intermediate_sum[59132] << endl;
        cout << "buff" << buff[500] << endl;
        cout << "next " << intermediate_sum[59133] << endl;
        buff.clear();
          
        }
      }
      cout << "exited inner" << endl;
      if (phase_dither) {
        // Undo phase modulation and divide by num_presums in one go
        transform(intermediate_sum.begin(), intermediate_sum.end(), intermediate_sum.begin(), std::bind(std::multiplies<complex<float>>(), std::placeholders::_1, polar((float) 1.0/num_presums, inversion_phase)));
      } else if (num_presums != 1) {
        // Only divide by num_presums
        transform(intermediate_sum.begin(), intermediate_sum.end(), intermediate_sum.begin(), std::bind(std::multiplies<complex<float>>(), std::placeholders::_1, 1.0/num_presums));
      }
      // Add to sample_sum
      //cout << "bye "<< endl;
      //cout << "before " << sample_sum[200] << endl;
      //cout << "to be added " << intermediate_sum[200] << endl;
      transform(sample_sum.begin(), sample_sum.end() + 1, intermediate_sum.begin(), sample_sum.begin(), plus<complex<float>>());
      //cout << "after " << sample_sum[200] << endl;
      fill(intermediate_sum.begin(), intermediate_sum.end() + 1, complex<float>(0,0)); // Zero out sum for next time
      //cout << "cleared " << intermediate_sum[200] << endl;
      intermediate_position = 0;
      pulses_received++;
      cout << "errors: " << error_count << endl;


    // Check if we have a full sample_sum ready to write to file
    if (((pulses_received - error_count) > last_pulse_num_written) && ((pulses_received - error_count) % num_presums == 0)) {
      // As each sample is added, it has phase inversion applied and is divided by # presums, so no additional work to do here.
      // write RX data to file
      if (outfile.is_open()) {
        outfile.write((const char*)&sample_sum.front(), 
          num_rx_samps * sizeof(complex<float>));
          cout << "writing to file" << endl;
      } else {
        cout_mutex.lock();
        cout << "Cannot write to outfile!" << endl;
        cout_mutex.unlock();
        exit(1);
      }
      fill(sample_sum.begin(), sample_sum.end(), complex<float>(0,0)); // Zero out sum for next time
      last_pulse_num_written = pulses_received - error_count;
    }

    // get gps data
    /*if (clk_ref == "gpsdo" && ((pulses_received % 100000) == 0)) {
      gps_data = usrp->get_mboard_sensor("gps_gprmc").to_pp_string();
      //cout << gps_data << endl;
    }*/

    // check if someone wants to stop
    if (stop_signal_called) {
      cout_mutex.lock();
      cout << "[RX] Reached stop signal handling for outer RX loop -> break" << endl;
      cout_mutex.unlock();
      break;
    }

    // write gps string to file
    /*if (clk_ref == "gpsdo") {
      boost::asio::async_write(gps_stream, boost::asio::buffer(gps_data + "\n"), gps_asio_handler);
    }*/

    if ( (max_chirps_per_file > 0) && (int(pulses_received / max_chirps_per_file) > save_file_index)) {
      outfile.close();
      // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
      cout_mutex.lock();
      cout << "[CLOSE FILE] " << current_filename << endl;
      cout_mutex.unlock();

      save_file_index++;
      current_filename = save_loc + "." + to_string(save_file_index);

      // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
      cout_mutex.lock();
      cout << "[OPEN FILE] " << current_filename << endl;
      cout_mutex.unlock();
      outfile.open(current_filename, ofstream::binary);
    }
    
    // // clear the matrices holding the sums
    // fill(sample_sum.begin(), sample_sum.end(), complex<int16_t>(0,0));
  }

  /*** WRAP UP ***/

  cout << "[RX] Closing output file." << endl;
  outfile.close();
  cout << "[CLOSE FILE] " << current_filename << endl;

  gps_stream.close();

  cout << "[RX] Error count: " << error_count << endl;
  
  cout << "[RX] Done. Calling join_all() on transmit thread group." << endl;

  transmit_thread.join_all();

  cout << "[RX] transmit_thread.join_all() complete." << endl << endl;

  return EXIT_SUCCESS;
  
}

/*
 * TRANSMIT_WORKER
 */
void transmit_worker(tx_streamer::sptr& tx_stream, rx_streamer::sptr& rx_stream)
{
  set_thread_priority_safe(1.0, true);

  // open file to stream from
  ifstream infile("../../" + chirp_loc, ifstream::binary);

  if (!infile.is_open())
  {
    cout << endl
         << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
    cout << "ERROR! Failed to open chirp.bin input file" << endl;
    cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl
         << endl;
    exit(1);
  }

  // Transmit buffers

  if (cpu_format != "fc32") {
    cout << "Only cpu_format 'fc32' is supported for now." << endl;
    // This is because we actually need chirp_unmodulated to have the correct
    // data type to facilitate phase modulation. In the future, this could be
    // fixed up so that it can work with any supported cpu_format, but it
    // seems unnecessary right now.
    exit(1);
  }

  vector<std::complex<float>> tx_buff(num_tx_samps); // Ready-to-transmit samples
  vector<std::complex<float>> chirp_unmodulated(num_tx_samps); // Chirp samples before any phase modulation

  infile.read((char *)&chirp_unmodulated.front(), num_tx_samps * convert::get_bytes_per_item(cpu_format));
  tx_buff = chirp_unmodulated;

  // Transmit metadata structure
  tx_metadata_t tx_md;
  tx_md.start_of_burst = true;
  tx_md.end_of_burst = true;
  tx_md.has_time_spec = true;

  // Receive command structure
  stream_cmd_t stream_cmd(stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
  stream_cmd.num_samps = num_rx_samps;
  stream_cmd.stream_now = false;

  int chirps_sent = 0;
  double rx_time;
  size_t n_samp_tx;

  long int last_error_count = 0;
  double error_delay = 0;

  while ((num_pulses < 0) || (pulses_scheduled < num_pulses))
  {
    // Setup next chirp for modulation
    if (phase_dither) {
      transform(chirp_unmodulated.begin(), chirp_unmodulated.end(), tx_buff.begin(), std::bind(std::multiplies<complex<float>>(), std::placeholders::_1, polar((float) 1.0, get_next_phase(true))));
    }

    /*
    The idea here is scheduler a handful of chirps ahead to let
    the transport layer (i.e. libUSB or whatever it is for ethernet)
    buffering actually do its job.
    
    In practice, letting this schedule 10s of pulses ahead seems to
    perform well. According to the documentation, however, the maximum
    queue depth is 8 for both the B20x-mini and X310. (And each pulse
    is two commands -- TX and RX.) So if we're following that, then
    we should only schedule 6 pulses ahead.
    */
    while ((pulses_scheduled - 6) > pulses_received) { // TODO: hardcoded
      if (stop_signal_called) {
        cout << "[TX] stop signal called while scheduler thread waiting -> break" << endl;
        break;
      }
      boost::this_thread::sleep_for(boost::chrono::nanoseconds(10));
    }

    if (error_count > last_error_count) {
      error_delay = (error_count - last_error_count) * 2 * pulse_rep_int;
      time_offset += error_delay;
      cout_mutex.lock();
      cout << "[TX] (Chirp " << pulses_scheduled << ") time_offset increased by " << error_delay << endl;
      cout_mutex.unlock();
      last_error_count = error_count;
    }
    // TX
    rx_time = time_offset + (pulse_rep_int * pulses_scheduled); // TODO: How do we track timing
    tx_md.time_spec = time_spec_t(rx_time - tx_lead);
    
    if (transmit) {
      n_samp_tx = tx_stream->send(&tx_buff.front(), num_tx_samps, tx_md, 60); // TODO: Think about timeout
    }

    // RX
    stream_cmd.time_spec = time_spec_t(rx_time);
    rx_stream->issue_stream_cmd(stream_cmd);

    //cout << "[TX] Scheduled pulse " << pulses_scheduled << " at " << rx_time << " (n_samp_tx = " << n_samp_tx << ")" << endl;

    pulses_scheduled++;

    if (stop_signal_called) {
      cout << "[TX] stop signal called -> break" << endl;
      break;
    }
  }

  cout << "[TX] Closing file" << endl;
  infile.close();
  cout << "[TX] Done." << endl;

}
