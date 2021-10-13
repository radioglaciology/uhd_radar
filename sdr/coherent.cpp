#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>

#include "yaml-cpp/yaml.h"

//#define USE_GPIO
#define AVERAGE_BEFORE_SAVE

using namespace std;
using namespace uhd;

/*
 * PROTOTYPES
 */
void transmit_worker(usrp::multi_usrp::sptr usrp);
  
void transmit_samples(tx_streamer::sptr& tx_stream,
  vector<complex<float>>& tx_buff, time_spec_t tx_time,
  size_t num_tx_samps);
  
void recieve_samples(rx_streamer::sptr& rx_stream, size_t num_rx_samps,
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

/*** CONFIGURATION PARAMETERS ***/

// DEVICE
string device_args;
string subdev;
string tx_ant;
string rx_ant;
string clk_ref;

// GPIO
string gpio;
int num_bits;
uint32_t gpio_mask = (1 << num_bits) - 1;

// RF
double rx_rate;
double tx_rate;
double freq;
double rx_gain;
double tx_gain;
double bw;
double clk_rate;

// CHIRP
double time_offset;
double tx_duration;
double tr_on_lead;
double tr_off_trail;
double pulse_rep_int;
double tx_lead;

//SEQUENCE
int coherent_sums;

// FILENAMES
string chirp_loc;
string save_loc;


// Calculated Parameters
double tr_off_delay; // Time before turning off GPIO
size_t num_tx_samps; // Total samples to transmit per chirp
size_t num_rx_samps; // Total samples to recieve per chirp

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
  tx_ant = dev_params["tx_ant"].as<string>();
  rx_ant = dev_params["rx_ant"].as<string>();
  clk_ref = dev_params["clk_ref"].as<string>();
  device_args = dev_params["device_args"].as<string>();

  YAML::Node gpio_params = config["GPIO"];
  gpio = gpio_params["gpio"].as<string>();
  num_bits = gpio_params["num_bits"].as<int>();
  gpio_mask = (1 << num_bits) - 1;

  YAML::Node rf = config["RF"];
  rx_rate = rf["rx_rate"].as<double>();
  tx_rate = rf["tx_rate"].as<double>();
  freq = rf["freq"].as<double>();
  rx_gain = rf["rx_gain"].as<double>();
  tx_gain = rf["tx_gain"].as<double>();
  bw = rf["bw"].as<double>();
  clk_rate = rf["clk_rate"].as<double>();

  YAML::Node chirp = config["CHIRP"];
  time_offset = chirp["time_offset"].as<double>();
  tx_duration = chirp["tx_duration"].as<double>();
  tr_on_lead = chirp["tr_on_lead"].as<double>();
  tr_off_trail = chirp["tr_off_trail"].as<double>();
  pulse_rep_int = chirp["pulse_rep_int"].as<double>();
  tx_lead = chirp["tx_lead"].as<double>();

  YAML::Node sequence = config["SEQUENCE"];
  coherent_sums = sequence["coherent_sums"].as<int>();

  YAML::Node files = config["FILES"];
  chirp_loc = files["chirp_loc"].as<string>();
  save_loc = files["save_loc"].as<string>();

  // Calculated parameters
  tr_off_delay = tx_duration + tr_off_trail; // Time before turning off GPIO
  num_tx_samps = tx_rate*tx_duration; // Total samples to transmit per chirp
  num_rx_samps = rx_rate*tx_duration*3; // Total samples to recieve per chirp


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
  if (bw < config["GENERATE"]["chirp_bandwidth"].as<double>()){
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

  // Lock mboard clocks
  cout << boost::format("Lock mboard clocks: %f") % clk_ref << endl;
  usrp->set_clock_source(clk_ref);

  // always select the subdevice first, the channel mapping affects the
  // other settings
  cout << boost::format("subdev set to: %f") % subdev << endl;
  usrp->set_rx_subdev_spec(subdev);
  cout << boost::format("Using Device: %s") % usrp->get_pp_string()
    << endl;

  // set master clock rate
  cout << boost::format("Setting master clock rate: %f MHz...")
    % (clk_rate / 1e6) << endl;
  usrp->set_master_clock_rate(clk_rate);
  cout << boost::format("Actual master clock rate: %f MHz...")
    % (usrp->get_master_clock_rate() / 1e6) << endl;

  // set sample rate
  cout << boost::format("Setting RX Rate: %f Msps...") % (rx_rate / 1e6) 
    << endl;
  usrp->set_rx_rate(rx_rate);

  cout << boost::format("Setting TX Rate: %f Msps...") % (tx_rate / 1e6) 
    << endl;
  usrp->set_tx_rate(tx_rate);
  cout << boost::format("Actual RX Rate: %f Msps...") 
    % (usrp->get_rx_rate() / 1e6) << endl;
  cout << boost::format("Actual TX Rate: %f Msps...")
    % (usrp->get_tx_rate() / 1e6) << endl << endl;

  // set freq
  cout << boost::format("Setting TX+RX Freq: %f MHz...") 
    % (freq / 1e6) << endl;
  uhd::tune_request_t tune_request(freq);
  usrp->set_rx_freq(tune_request);
  usrp->set_tx_freq(tune_request);
  cout << boost::format("Actual RX Freq: %f MHz...") 
    % (usrp->get_rx_freq() / 1e6) << endl;
  cout << boost::format("Actual TX Freq: %f MHz...") 
    % (usrp->get_tx_freq() / 1e6) << endl << endl;

  // set the rf gain
  cout << boost::format("Setting RX Gain: %f dB...") % rx_gain << endl;
  usrp->set_rx_gain(rx_gain);
  cout << boost::format("Actual RX Gain: %f dB...") 
    % usrp->get_rx_gain() << endl << endl;
  cout << boost::format("Setting TX Gain: %f dB...") % tx_gain << endl;
  usrp->set_tx_gain(tx_gain);
  cout << boost::format("Actual TX Gain: %f dB...") 
    % usrp->get_tx_gain() << endl << endl;

  // set the IF filter bandwidth
  cout << boost::format("Setting RX Bandwidth: %f MHz...") 
    % (bw / 1e6) << endl;
  usrp->set_rx_bandwidth(bw);
  cout << boost::format("Actual RX Bandwidth: %f MHz...") 
    % (usrp->get_rx_bandwidth() / 1e6) << endl << endl;

  // set the antenna
  cout << boost::format("Setting RX Antenna: %s") % rx_ant << endl;
  usrp->set_rx_antenna(rx_ant);
  cout << boost::format("Actual RX Antenna: %s") 
    % usrp->get_rx_antenna() << endl;
  cout << boost::format("Setting TX Antenna: %s") % tx_ant << endl;
  usrp->set_tx_antenna(tx_ant);
  cout << boost::format("Actual TX Antenna: %s") 
    % usrp->get_tx_antenna() << endl << endl;
    
  cout << "INFO: Number of TX samples: " << num_tx_samps << endl;
  cout << "INFO: Number of RX samples: " << num_rx_samps << endl;

  // set device timestamp
  cout << boost::format("Setting device timestamp to 0...") << endl;
  usrp->set_time_now(time_spec_t(0.0));

  // stream arguments for both tx and rx
  stream_args_t stream_args("fc32", "sc16");

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
  rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

  // open file for writing rx samples
  ofstream outfile;
  outfile.open("../../" + save_loc, ofstream::binary);
  
  vector<complex<float>> sample_sum(num_rx_samps, 0);


  int error_count = 0;
 
  /*** RX LOOP AND SUM ***/

  for(int i=0;i<coherent_sums;i++){ 
  
    sent_bar.wait();
  
    double rx_time = time_offset + (pulse_rep_int * i);
    double tx_time = rx_time - tx_lead;
    double time_ms;

#ifdef USE_GPIO
    // Schedule GPIO off
    double tr_off_time = tx_time + tr_off_delay;
    
    time_ms = (uhd::time_spec_t(tr_off_time).get_real_secs())*1000.0;
    cout << boost::format("Scheduling chirp %d GPIO OFF for %0.3f ms\n") % i % time_ms;
    
    usrp->set_command_time(time_spec_t(tr_off_time));
    usrp->set_gpio_attr(gpio, "OUT", 0x00, gpio_mask);
    usrp->clear_command_time();
#endif

    stream_cmd_t stream_cmd(stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = num_rx_samps;
    stream_cmd.stream_now = false;
    stream_cmd.time_spec = uhd::time_spec_t(rx_time);
    
    time_ms = (uhd::time_spec_t(rx_time).get_real_secs())*1000.0;
    cout << boost::format("Scheduling chirp %d RX for %0.3f ms\n") % i % time_ms;
    
    rx_stream->issue_stream_cmd(stream_cmd);

    recieve_samples(rx_stream, num_rx_samps, sample_sum);

    if (error_state) {
      cout << "Error occured. Trying to reset." << endl;
      error_count++;

      //time_offset = time_offset + 2*pulse_rep_int;
      error_state = false;
    }

    cout << "Recieved chirp " << i << " [samples: " << num_rx_samps << "]" << endl;
  
#ifndef AVERAGE_BEFORE_SAVE
    if (outfile.is_open())
        outfile.write((const char*)&sample_sum.front(),
            num_rx_samps*sizeof(complex<float>));
#endif

    recv_bar.wait();
  }

#ifdef AVERAGE_BEFORE_SAVE
  cout << "Calculing mean and saving to file..." << endl;
  // Average
  for(int i=0;i<num_rx_samps;i++){
    sample_sum[i] = sample_sum[i] / ((float) coherent_sums);
  }

  if (outfile.is_open())
    outfile.write((const char*)&sample_sum.front(),
        num_rx_samps*sizeof(complex<float>));
#endif

  /*** WRAP UP ***/

  cout << "Error count: " << error_count << endl;
  
  cout << "Done." << endl << endl;

  return EXIT_SUCCESS;
  
}

/*
 * TRANSMIT_WORKER
 */
void transmit_worker(usrp::multi_usrp::sptr usrp){

  /*** TX SETUP ***/

  // stream arguments for both tx and rx
  stream_args_t stream_args("fc32", "sc16");

  // tx streamer
  tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);
  
  cout << "INFO: get_max_num_samps: " << tx_stream->get_max_num_samps() << endl;

  // open file to stream from
  ifstream infile("../../" + chirp_loc, ifstream::binary);
  
  if (! infile.is_open() ) {
    cout << endl << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
    cout << "ERROR! Faild to open chirp.bin input file" << endl;
    cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl << endl;
    exit(1);
  }
  
  vector<complex<float>> tx_buff(num_tx_samps);
  infile.read((char*)&tx_buff.front(), num_tx_samps*sizeof(complex<float>));

  for(int i=0;i<coherent_sums;i++){ 
    if (stop_signal_called) break;
  
    double rx_time = time_offset + (pulse_rep_int * i);
    double tx_time = rx_time - tx_lead;
    double time_ms;
    
    

#ifdef USE_GPIO
    // GPIO Schedule
    double tr_on_time = tx_time - tr_on_lead;
    
    time_ms = (uhd::time_spec_t(tr_on_time).get_real_secs())*1000.0;
    cout << boost::format("Scheduling chirp %d GPIO ON for %0.3f ms\n") % i % time_ms;
    
    usrp->set_command_time(time_spec_t(tr_on_time));
    usrp->set_gpio_attr(gpio, "OUT", 0x01, gpio_mask);
    usrp->clear_command_time();
#endif

    time_ms = (uhd::time_spec_t(tx_time).get_real_secs())*1000.0;
    cout << boost::format("Scheduling chirp %d TX for %0.3f ms\n") % i % time_ms;

    transmit_samples(tx_stream, tx_buff, uhd::time_spec_t(tx_time),
        num_tx_samps);

    // Wait for the chirp to be recieved
    sent_bar.wait();
    recv_bar.wait();
  }
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
 * RECIEVE_SAMPLES
 */
void recieve_samples(rx_streamer::sptr& rx_stream, size_t num_rx_samps,
    vector<complex<float>>& res){

  // meta data holder
  rx_metadata_t md;

  // recieve buffer
  vector<complex<float>> buff(rx_stream->get_max_num_samps());
  vector<void *> buffs;
  for(size_t ch=0;ch<rx_stream->get_num_channels();ch++)
    buffs.push_back(&buff.front());

  double timeout = 5; //pulse_rep_int*2; // delay before recieve + padding

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
      // add samples
      for(int i=0;i<n_samps;i++){
#ifdef AVERAGE_BEFORE_SAVE
        res[num_acc_samps + i] += buff[i];
#else
        res[num_acc_samps + i] = buff[i];
#endif
      } // TODO

      num_acc_samps += n_samps;
    }
    
    
  }

  if (num_acc_samps < num_rx_samps) cerr << "Receive timeout before all "
    "samples received..." << endl;
}

  
  
  
