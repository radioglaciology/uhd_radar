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
  string device_args("serial=30EF570"); // device addr  "serial=30EF570"
  string subdev("A:A");
  string tx_ant("TX/RX");
  string rx_ant("RX2");
  string clk_ref("internal");
  
  // GPIO
  string gpio = "FP0";
  int num_bits = 8;
  uint32_t gpio_mask = (1 << num_bits) - 1;

  // RF
  double rx_rate(14e6);  // RX Sample Rate [sps]
  double tx_rate(14e6);  // TX Sample Rate [sps]
  double freq(435e6);    // 435 MHz Center Frequency
  double rx_gain(55);    // RX Gain [dB]
  double tx_gain(60.8);    // TX Gain [dB] - 60.8 is -10 dBm output
  double bw(14e6);       // TX/RX Bandwidth [Hz]
  double clk_rate(56e6); // Clock rate [Hz]

  // Chirp Parametres
  double time_offset = 1;    // Time before first receieve [s]
  double tx_duration = 30e-6; //(10e-6);  // Transmission duration [s]
  double tr_on_lead = 1e-6;    // Time from GPIO output toggle on to TX [s]
  double tr_off_trail = 10e-6; // Time from TX off to GPIO output off [s]
  double pulse_rep_int = 10e-3;//1000e-3;//20e-3;    // Chirp period [s]
  double tx_lead = 0e-6;       // Time between start of TX and RX [s]
  
  // Chirp Sequence Parameters
  int coherent_sums = 1; // Number of chirps
  
  // Calculated Parameters
  double tr_off_delay = tx_duration + tr_off_trail; // Time before turning off GPIO
  size_t num_tx_samps = tx_rate*tx_duration; // Total samples to transmit per chirp
  size_t num_rx_samps = rx_rate*tx_duration*3; // Total samples to recieve per chirp


/*
 * UHD_SAFE_MAIN
 */
int UHD_SAFE_MAIN(int argc, char *argv[]) {
  set_thread_priority_safe(1.0, true);
  
  signal(SIGINT, &sig_int_handler);

  
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
  outfile.open("../rx_samps.bin", ofstream::binary);
  
  vector<complex<float>> sample_sum(num_rx_samps, 0);
 
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

    cout << "Recieved chirp " << i << " [samples: " << num_rx_samps << "]" << endl;

    
    recv_bar.wait();
  }

  // Average
  for(int i=0;i<num_rx_samps;i++){
    sample_sum[i] = sample_sum[i] / ((float) coherent_sums);
  }

  if (outfile.is_open())
    outfile.write((const char*)&sample_sum.front(),
        num_rx_samps*sizeof(complex<float>));


  /*** WRAP UP ***/
  
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
  ifstream infile("../chirp.bin", ifstream::binary);
  
  if (! infile.is_open() ) {
    cout << endl << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
    cout << "ERROR! Faild to open chirp.bin input file" << endl;
    cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl << endl;
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

  double timeout = 5; // delay before recieve + padding

  size_t num_acc_samps = 0;
  while(num_acc_samps < num_rx_samps){
    if (stop_signal_called) break;

    size_t n_samps = rx_stream->recv(
        buffs, buff.size(), md, timeout, true);

    // errors
    if (md.error_code != rx_metadata_t::ERROR_CODE_NONE){
      cout << num_acc_samps << endl;
      throw std::runtime_error(str(boost::format(
        "Receiver error %s") % md.strerror()));
    }
    
    // add samples
    for(int i=0;i<n_samps;i++){
      res[num_acc_samps + i] += buff[i];
    } // TODO

    num_acc_samps += n_samps;
  }

  if (num_acc_samps < num_rx_samps) cerr << "Receive timeout before all "
    "samples received..." << endl;
}

  
  
  
