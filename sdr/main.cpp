#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <thread>
#include <cstdlib>
#include <boost/asio/io_service.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/write.hpp>

#include "yaml-cpp/yaml.h"

#include "rf_settings.hpp"
#include "utils.hpp"

//#define USE_GPIO

using namespace std;
using namespace uhd;

/*
 * PROTOTYPES
 */
void transmit_worker(usrp::multi_usrp::sptr usrp, vector<size_t> tx_channel_nums);
  
void transmit_samples(tx_streamer::sptr& tx_stream,
  vector<complex<float>>& tx_buff, time_spec_t tx_time,
  size_t num_tx_samps);
  
void receive_samples(rx_streamer::sptr& rx_stream, size_t num_rx_samps,
    vector<complex<float>>& res);

/*
 * SIG INT HANDLER
 */
static bool stop_signal_called = false;
void sig_int_handler(int) {
  stop_signal_called = true;
}

/*
 * Thread barriers
 */
boost::barrier sent_bar(2);
boost::barrier recv_bar(2);
boost::barrier end_bar(2);

/*** CONFIGURATION PARAMETERS ***/

// DEVICE
string device_args;
string subdev;
string clk_ref;
double clk_rate;
string tx_channels; 
string rx_channels;

// GPIO
int pwr_amp_pin;
string gpio_bank;
uint32_t AMP_GPIO_MASK;
uint32_t ATR_MASKS;
uint32_t ATR_CONTROL;
uint32_t GPIO_DDR;
//uint32_t gpio_mask = (1 << num_bits) - 1;
bool ref_out;

// RF1
double rx_rate;
double tx_rate;
double freq;
double rx_gain;
double tx_gain;
double bw;
string tx_ant;
string rx_ant;

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

// FILENAMES
string chirp_loc;
string save_loc;


// Calculated Parameters
double tr_off_delay; // Time before turning off GPIO
size_t num_tx_samps; // Total samples to transmit per chirp
size_t num_rx_samps; // Total samples to receive per chirp

// Error case [TODO]
bool error_state = false;

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
  ref_out = gpio_params["ref_out"].as<bool>();

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

  YAML::Node chirp = config["CHIRP"];
  time_offset = chirp["time_offset"].as<double>();
  tx_duration = chirp["tx_duration"].as<double>();
  rx_duration = chirp["rx_duration"].as<double>();
  tr_on_lead = chirp["tr_on_lead"].as<double>();
  tr_off_trail = chirp["tr_off_trail"].as<double>();
  pulse_rep_int = chirp["pulse_rep_int"].as<double>();
  tx_lead = chirp["tx_lead"].as<double>();
  num_pulses = chirp["num_pulses"].as<int>();
  num_presums = chirp["num_presums"].as<int>();

  YAML::Node files = config["FILES"];
  chirp_loc = files["chirp_loc"].as<string>();
  save_loc = files["save_loc"].as<string>();

  // Calculated parameters
  tr_off_delay = tx_duration + tr_off_trail; // Time before turning off GPIO
  num_tx_samps = tx_rate * tx_duration; // Total samples to transmit per chirp
  num_rx_samps = rx_rate * rx_duration; // Total samples to receive per chirp


  /** Thread, interrupt setup **/

  set_thread_priority_safe(1.0, true);
  
  signal(SIGINT, &sig_int_handler);

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
  usrp->set_tx_subdev_spec(subdev);
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
    set_rf_params_multi(usrp, rf0, rf1, rx_channel_nums, tx_channel_nums);
  } else {
    throw std::runtime_error("Number of channels requested not supported");
  }

  // allow for some setup time
  this_thread::sleep_for(chrono::seconds(1));
    
  cout << "INFO: Number of TX samples: " << num_tx_samps << endl;
  cout << "INFO: Number of RX samples: " << num_rx_samps << endl << endl;

  // stream arguments for both tx and rx
  stream_args_t stream_args("fc32", "sc16");

  // Check Ref and LO Lock detect
  vector<std::string> tx_sensor_names, rx_sensor_names;
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

#ifdef USE_GPIO
  //set data direction register (DDR)
  usrp->set_gpio_attr(gpio, "DDR", 0xff, gpio_mask);

  //set control register
  usrp->set_gpio_attr(gpio, "CTRL", 0x00, gpio_mask);
  
  // initialize off
  usrp->set_gpio_attr(gpio, "OUT", 0x00, gpio_mask);
#endif

  // turns external ref out port on or off
  usrp->set_clock_source_out(ref_out);
  
  // update the offset time for start of streaming to be offset from the current usrp time
  time_offset = time_offset + time_spec_t(usrp->get_time_now()).get_real_secs();

  /*** SPAWN THE TX THREAD ***/
  boost::thread_group transmit_thread;
  transmit_thread.create_thread(boost::bind(&transmit_worker, usrp, tx_channel_nums));

  /*** FILE WRITE SETUP ***/
  boost::asio::io_service ioservice;

  string gps_path = "../../data/" + save_loc + "_gps.txt"; 
  int gps_file = open(gps_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
  if (gps_file == -1) {
      throw std::runtime_error("Failed to open GPS file: " + gps_path);
  }

  string rf_path = "../../data/" + save_loc + "_rx_samps.dat";
  int rf_file = open(rf_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
  if (rf_file == -1) {
    throw std::runtime_error("Failed to open RF file: " + rf_path);
  }

  //boost::asio::posix::stream_descriptor stream{ioservice, STDOUT_FILENO};
  boost::asio::posix::stream_descriptor gps_stream{ioservice, gps_file};
  auto gps_asio_handler = [](const boost::system::error_code& ec, std::size_t) {
    if (ec.value() != 0) {
      cout << "GPS write error: " << ec.message() << endl;
    }
  };

  boost::asio::posix::stream_descriptor rf_stream{ioservice, rf_file};
  auto rf_asio_handler = [](const boost::system::error_code& ec, size_t) {
    if (ec.value() != 0) {
      cout << "RF write error: " << ec.message() << endl;
    }
  };

  ioservice.run();

  /*** RX SETUP ***/

  // (TX setup happens in the TX thread)

  // rx streamer
  stream_args.channels = rx_channel_nums;
  rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

  // open file for writing rx samples
  /*ofstream outfile;
  outfile.open("../../data/" + save_loc + ".dat", ofstream::binary);
  ofstream gpsfile;
  if (clk_ref == "gpsdo") {  
    gpsfile.open("../../data/gps_" + save_loc + ".txt", ofstream::binary);
    cout << "[HERE] gps file opened" << endl;
  }*/

  // set up buffers for rx
  uhd::rx_metadata_t md;
  
  vector<complex<float>> sample_sum(num_rx_samps, 0);
  vector<complex<float>> rx_sample(num_rx_samps, 0);


  int error_count = 0;

  /*** RX LOOP AND SUM ***/
  if (num_pulses < 0) {
    cout << "num_pulses is < 0. Will continue to send chirps until stopped with Ctrl-C." << endl;
  }

  //for (int i = 0; i < num_pulses; i += num_presums) {
  int chirps_sent = 0;
  string gps_data;
  while ((num_pulses < 0) || (chirps_sent < num_pulses)) {

    for (int m = 0; m < num_presums; m++) {
      sent_bar.wait();

      double rx_time = time_offset + (pulse_rep_int * chirps_sent);
      double tx_time = rx_time - tx_lead;
      double time_ms;

#ifdef USE_GPIO
      // Schedule GPIO off
      double tr_off_time = tx_time + tr_off_delay;

      time_ms = (uhd::time_spec_t(tr_off_time).get_real_secs()) * 1000.0;
      cout << boost::format("Scheduling chirp %d GPIO OFF for %0.3f ms\n") % i % time_ms;

      usrp->set_command_time(time_spec_t(tr_off_time));
      usrp->set_gpio_attr(gpio, "OUT", 0x00, gpio_mask);
      usrp->clear_command_time();
#endif

      stream_cmd_t stream_cmd(stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
      stream_cmd.num_samps = num_rx_samps;
      stream_cmd.stream_now = false;
      stream_cmd.time_spec = time_spec_t(rx_time);

      time_ms = time_spec_t(rx_time).get_real_secs() * 1000.0;
      //cout << boost::format("Scheduling chirp %d RX for %0.3f ms\n") % (chirps_sent) % time_ms;

      rx_stream->issue_stream_cmd(stream_cmd);

      receive_samples(rx_stream, num_rx_samps, rx_sample);

      // get gps data
      if (clk_ref == "gpsdo" && ((chirps_sent % 500) == 0)) {
        gps_data = usrp->get_mboard_sensor("gps_gprmc").to_pp_string();
        //cout << gps_data << endl;
      }

      if (error_state) {
        cout << "Error occured. Trying to reset." << endl;
        error_count++;

        if (clk_ref == "gpsdo") {
          boost::asio::async_write(gps_stream, boost::asio::buffer("RF ERROR\n"), gps_asio_handler);
        }

        //time_offset = time_offset + 2*pulse_rep_int;
        error_state = false;
      }

      //cout << "Received chirp " << chirps_sent << " [samples: " << num_rx_samps << "]" << endl;


      for (int n = 0; n < num_rx_samps; n++) {
        sample_sum[n] += rx_sample[n];
      }

      chirps_sent++;

      recv_bar.wait();

      // check if someone wants to stop
      if (stop_signal_called) {
        cout << "[RX] Stop signal set during inner loop. Breaking from inner loop." << endl;
        break;
      }
    } // then take average of coherently summed samples
    /*for (int n = 0; n < num_rx_samps; n++) {
      sample_sum[n] = sample_sum[n] / ((float)num_presums);
    }*/

    // check if someone wants to stop
    if (stop_signal_called) {
      cout << "[RX] Reached stop signal handling for outer RX loop -> break" << endl;
      break;
    }

    // write summed data to file
    /*if (outfile.is_open()) {
      outfile.write((const char*)&sample_sum.front(), 
        num_rx_samps * sizeof(complex<float>));
    }*/
    boost::asio::async_write(rf_stream, boost::asio::buffer(sample_sum), rf_asio_handler);

    // write gps string to file
    if (clk_ref == "gpsdo") {
      /*gpsfile.write(gps_data.c_str(), sizeof(char) * gps_data.size());
      gpsfile.write("\n", sizeof(char));
      cout << "[HERE] writing gps string" << endl;*/

      //cout << "gps data size: " << sizeof(gps_data) << endl;
      boost::asio::async_write(gps_stream, boost::asio::buffer(gps_data + "\n"), gps_asio_handler);

      //gps_buffer = uv_buf_init((char*)gps_data.c_str(), sizeof(gps_data));
    }

    // clear the matrices holding the sums
    fill(sample_sum.begin(), sample_sum.end(), complex<float>(0,0));
  }

  /*** WRAP UP ***/

  cout << "[RX] Closing output file." << endl;
  /*outfile.close();
  if (gpsfile.is_open()) {
    gpsfile.close();
  }*/

  gps_stream.close();
  rf_stream.close();

  cout << "[RX] Error count: " << error_count << endl;
  
  cout << "[RX] Done. Calling join_all() on transmit thread group." << endl;

  transmit_thread.join_all();

  cout << "[RX] transmit_thread.join_all() complete." << endl << endl;

  return EXIT_SUCCESS;
  
}

/*
 * TRANSMIT_WORKER
 */
void transmit_worker(usrp::multi_usrp::sptr usrp, vector<size_t> tx_channel_nums)
{

  /*** TX SETUP ***/

  // stream arguments for both tx and rx
  stream_args_t stream_args("fc32", "sc16");
  stream_args.channels = tx_channel_nums;

  // tx streamer
  tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

  cout << "INFO: get_max_num_samps: " << tx_stream->get_max_num_samps() << endl;

  // open file to stream from
  ifstream infile("../../" + chirp_loc, ifstream::binary);

  if (!infile.is_open())
  {
    cout << endl
         << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
    cout << "ERROR! Faild to open chirp.bin input file" << endl;
    cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl
         << endl;
    exit(1);
  }

  vector<complex<float>> tx_buff(num_tx_samps);
  infile.read((char *)&tx_buff.front(), num_tx_samps * sizeof(complex<float>));

  int chirps_sent = 0;
  while ((num_pulses < 0) || (chirps_sent < num_pulses))
  {
    // if (stop_signal_called) { 
    //   // someone wants to stop, let's try to clean up first
    //   infile.close();
    //   sent_bar.wait();
    //   cout << "[TX] sent_bar cleared" << endl;
    //   recv_bar.wait();
    //   cout << "[TX] recv_bar cleared" << endl;
    //   break;
    // }

    double rx_time = time_offset + (pulse_rep_int * chirps_sent);
    double tx_time = rx_time - tx_lead;
    double time_ms;

#ifdef USE_GPIO
    // GPIO Schedule
    double tr_on_time = tx_time - tr_on_lead;

    time_ms = (time_spec_t(tr_on_time).get_real_secs()) * 1000.0;
    cout << boost::format("Scheduling chirp %d GPIO ON for %0.3f ms\n") % chirps_sent % time_ms;

    usrp->set_command_time(time_spec_t(tr_on_time));
    usrp->set_gpio_attr(gpio, "OUT", 0x01, gpio_mask);
    usrp->clear_command_time();
#endif

    time_ms = (time_spec_t(tx_time).get_real_secs()) * 1000.0;
    //cout << boost::format("Scheduling chirp %d TX for %0.3f ms\n") % chirps_sent % time_ms;

    transmit_samples(tx_stream, tx_buff, time_spec_t(tx_time),
        num_tx_samps);

    chirps_sent++;

    if (stop_signal_called) {
      cout << "[TX] Stop signal set, but waiting for RX thread before quitting..." << endl;
    }

    // Wait for the chirp to be received
    sent_bar.wait();
    recv_bar.wait();

    if (stop_signal_called) {
      cout << "[TX] stop signal called -> break" << endl;
      break;
    }
  }

  cout << "[TX] Closing file" << endl;
  infile.close();
  cout << "[TX] Done." << endl;

}

/*
 * TRANSMIT_SAMPLES
 */
void transmit_samples(tx_streamer::sptr& tx_stream,
    vector<complex<float>>& tx_buff, time_spec_t tx_time,
    size_t num_tx_samps) {
  
  // meta data holder
  tx_metadata_t md;
  md.start_of_burst = true;
  md.end_of_burst = true;
  md.has_time_spec = true;
  md.time_spec = tx_time;
  
  // TODO: The timeout shouldn't be hard-coded, but it doesn't really matter
  size_t n_samp = tx_stream->send(&tx_buff.front(), num_tx_samps, md, 60);
}

/*
 * RECEIVE_SAMPLES
 */
void receive_samples(rx_streamer::sptr& rx_stream, size_t num_rx_samps,
    vector<complex<float>>& res){

  // meta data holder
  rx_metadata_t md;

  // receive buffer
  vector<complex<float>> buff(rx_stream->get_max_num_samps());
  vector<void *> buffs;
  for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++) {
    buffs.push_back(&buff.front());
  }

  double timeout = 5; //pulse_rep_int*2; // delay before receive + padding

  size_t num_acc_samps = 0;
  while(num_acc_samps < num_rx_samps){
    if (stop_signal_called) {

      break;
    }

    size_t n_samps = rx_stream->recv(
        buffs, buff.size(), md, timeout, true);

    // errors
    if (md.error_code != rx_metadata_t::ERROR_CODE_NONE){
      cout << "WARNING: Receiver error: " << md.strerror() << endl;
      // throw std::runtime_error(str(boost::format(
      //   "Receiver error %s") % md.strerror()));
      error_state = true;
      return;
    } else {
      for (int i = 0; i < n_samps; i++) { // TODO: this feels inefficient 
        res[num_acc_samps + i] = buff[i]; 
      }
      num_acc_samps += n_samps;
    }
  }

  if (num_acc_samps < num_rx_samps) cerr << "Receive timeout before all "
    "samples received..." << endl;
}
