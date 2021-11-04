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

#include "yaml-cpp/yaml.h"

#include "rf_settings.hpp"
#include "utils.hpp"

//#define USE_GPIO

using namespace std;
using namespace uhd;

/*
 * PROTOTYPES
 */
void transmit_worker(usrp::multi_usrp::sptr usrp);
  
void transmit_samples(tx_streamer::sptr& tx_stream,
  vector<complex<float>>& tx_buff, time_spec_t tx_time,
  size_t num_tx_samps);
  
void receive_samples(rx_streamer::sptr& rx_stream, size_t num_rx_samps,
    vector<complex<float>>& res);

/*
 * SIG INT HANDLER
 */
static bool stop_signal_called = false;
void sig_int_handler(int){stop_signal_called = true;}

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
string gpio;
int num_bits;
uint32_t gpio_mask = (1 << num_bits) - 1;

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
  gpio = gpio_params["gpio"].as<string>();
  num_bits = gpio_params["num_bits"].as<int>();
  gpio_mask = (1 << num_bits) - 1;

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
  num_rx_samps = rx_rate * tx_duration * 3; // Total samples to receive per chirp


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

  // set the USRP time, let chill for a little bit to lock
  usrp->set_time_next_pps(uhd::time_spec_t(0.0));
  this_thread::sleep_for((chrono::milliseconds(1000)));

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

  // update the offset time for start of streaming to be offset from the current usrp time
  time_offset = time_offset + time_spec_t(usrp->get_time_now()).get_real_secs();

  /*** SETUP GPIO ***/
#ifdef USE_GPIO
  //set data direction register (DDR)
  usrp->set_gpio_attr(gpio, "DDR", 0xff, gpio_mask);

  //set control register
  usrp->set_gpio_attr(gpio, "CTRL", 0x00, gpio_mask);
  
  // initialize off
  usrp->set_gpio_attr(gpio, "OUT", 0x00, gpio_mask);
#endif
  
  /*** SPAWN THE TX THREAD ***/
  boost::thread_group transmit_thread;
  transmit_thread.create_thread(boost::bind(&transmit_worker, usrp));

  /*** RX SETUP ***/

  // (TX setup happens in the TX thread)

  // rx streamer
  stream_args.channels = rx_channel_nums;
  rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

  // open file for writing rx samples
  ofstream outfile;
  outfile.open("../../" + save_loc, ofstream::binary);

  // set up buffers for rx
  uhd::rx_metadata_t md;
  
  vector<complex<float>> sample_sum(num_rx_samps, 0);
  vector<complex<float>> rx_sample(num_rx_samps, 0);


  int error_count = 0;

  /*** RX LOOP AND SUM ***/

  for (int i = 0; i < num_pulses; i += num_presums) {
    for (int m = 0; m < num_presums; m++) {

      sent_bar.wait();

      double rx_time = time_offset + (pulse_rep_int * (i + m));
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
      cout << boost::format("Scheduling chirp %d RX for %0.3f ms\n") % (i + m) % time_ms;

      rx_stream->issue_stream_cmd(stream_cmd);

      receive_samples(rx_stream, num_rx_samps, rx_sample);

      if (error_state) {
        cout << "Error occured. Trying to reset." << endl;
        error_count++;

        //time_offset = time_offset + 2*pulse_rep_int;
        error_state = false;
      }

      cout << "Received chirp " << i << " [samples: " << num_rx_samps << "]" << endl;

      for (int n = 0; n < num_rx_samps; n++) {
        sample_sum[n] += rx_sample[n];
      }

      recv_bar.wait();
    } // then take average of coherently summed samples
    /*for (int n = 0; n < num_rx_samps; n++) {
      sample_sum[n] = sample_sum[n] / ((float)num_presums);
    }*/

    // write summed data to file
    if (outfile.is_open()) {
      outfile.write((const char*)&sample_sum.front(), 
        num_rx_samps * sizeof(complex<float>));
    }

    // clear the matrices holding the sums
    fill(sample_sum.begin(), sample_sum.end(), complex<float>(0,0));
  }


  /*** WRAP UP ***/

  outfile.close();

  cout << "Error count: " << error_count << endl;
  
  cout << "Done." << endl << endl;

  return EXIT_SUCCESS;
  
}

/*
 * TRANSMIT_WORKER
 */
void transmit_worker(usrp::multi_usrp::sptr usrp)
{

  /*** TX SETUP ***/

  // stream arguments for both tx and rx
  stream_args_t stream_args("fc32", "sc16");

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

  for (int i = 0; i < num_pulses; i++)
  {
    if (stop_signal_called)
      break;

    double rx_time = time_offset + (pulse_rep_int * i);
    double tx_time = rx_time - tx_lead;
    double time_ms;

#ifdef USE_GPIO
    // GPIO Schedule
    double tr_on_time = tx_time - tr_on_lead;

    time_ms = (time_spec_t(tr_on_time).get_real_secs()) * 1000.0;
    cout << boost::format("Scheduling chirp %d GPIO ON for %0.3f ms\n") % i % time_ms;

    usrp->set_command_time(time_spec_t(tr_on_time));
    usrp->set_gpio_attr(gpio, "OUT", 0x01, gpio_mask);
    usrp->clear_command_time();
#endif

    time_ms = (time_spec_t(tx_time).get_real_secs()) * 1000.0;
    cout << boost::format("Scheduling chirp %d TX for %0.3f ms\n") % i % time_ms;

    transmit_samples(tx_stream, tx_buff, time_spec_t(tx_time),
        num_tx_samps);

    // Wait for the chirp to be received
    sent_bar.wait();
    recv_bar.wait();
  }

  infile.close();
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
    if (stop_signal_called) break;

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
