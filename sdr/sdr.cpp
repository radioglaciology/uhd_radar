#include "sdr.hpp"

/**
* @brief Constructs a new Sdr object
*
* Constructs a new Sdr object and calls the relevant functions
* that initialize each member variable from the specified YAML file.
*
* @param kYamlFile Path to the YAML configuration file (config/)
*/
Sdr::Sdr(const string& kYamlFile) {
  loadConfigFromYaml(kYamlFile);
}

/**
* @brief Load configuration from YAML file into the SDR class
*
* Reads in SDR and hardware-related configuration settings from the
* provided YAML file, and stores them in the corresponding member
* variables of the SDR class.
*
* @param kYamlFile Path to the YAML configuration file (config/)
*/
void Sdr::loadConfigFromYaml(const string& kYamlFile) {
  YAML::Node config = YAML::LoadFile(kYamlFile);

  // Device
  YAML::Node dev_params = config["DEVICE"];
  subdev = dev_params["subdev"].as<string>();
  clk_ref = dev_params["clk_ref"].as<string>();
  device_args = dev_params["device_args"].as<string>();
  clk_rate = dev_params["clk_rate"].as<double>();
  tx_channels = dev_params["tx_channels"].as<string>();
  rx_channels = dev_params["rx_channels"].as<string>();
  cpu_format = dev_params["cpu_format"].as<string>("fc32");
  otw_format = dev_params["otw_format"].as<string>();

  // GPIO
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

  // RF
  rf0 = config["RF0"];
  rf1 = config["RF1"];
  rx_rate = rf1["rx_rate"].as<double>();
  tx_rate = rf1["tx_rate"].as<double>();
  freq = rf1["freq"].as<double>();
  rx_gain = rf1["rx_gain"].as<double>();
  tx_gain = rf1["tx_gain"].as<double>();
  bw = rf1["bw"].as<double>();
  tx_ant = rf1["tx_ant"].as<string>();
  rx_ant = rf1["rx_ant"].as<string>();

  transmit = rf0["transmit"].as<bool>(true); // True if transmission enabled

/**
* Sanity checks for configuration parameters
*
* Ensures that the configuration parameters loaded from the YAML file
* are consistent and valid.
*
*/

  if (tx_rate != rx_rate){
    cout << "WARNING: TX sample rate does not match RX sample rate.\n";
  }
  if (config["GENERATE"]["sample_rate"].as<double>() != tx_rate){
    cout << "WARNING: TX sample rate does not match sample rate of generated chirp.\n";
  }
  if (bw < config["GENERATE"]["chirp_bandwidth"].as<double>() && bw != 0){
    cout << "WARNING: RX bandwidth is narrower than the chirp bandwidth.\n";
  }
}

/**
* @brief Initializes the USRP device setting time and clock source
*
* Creates the USRP device using the specified device arguments
* and sets the clock source to the specified reference clock.
* It also locks the mboard clocks and sets the time source.
* It ensures that the USRP device is ready for operation
*
*/
void Sdr::createUsrp(){
  cout << endl;
  cout << boost::format("Creating the usrp device with: %s...")
    % device_args << endl; 
  usrp = uhd::usrp::multi_usrp::make(device_args);
  cout << boost::format("TX/RX Device: %s") % usrp->get_pp_string() << endl;

  // Lock mboard clocks
  usrp->set_clock_source(clk_ref);
  usrp->set_time_source(clk_ref);
}

/**
* @brief sets up the USRP device
*
* Initializes the USRP device with the specified parameters, sets the subdevice
* specifications, master clock rate, and configures the RF parameters. checks
* reference and LO locks, and initializes GPIO, transmit, and receive settings.
*
*/
void Sdr::setupUsrp(){
  if (clk_ref == "gpsdo") {
    check10MhzLock();
    gpsLock();
    checkAndSetTime();
  }else{
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
  detectChannels();
  setRFParams();
  refLoLockDetect();
  setupGpio();
  setupTx();
  setupRx();
}

/*** @brief Checks for 10 MHz reference lock
*
* This function checks if the USRP device has locked to the 10 MHz reference
* signal. Retrieves the list of sensor names from the USRP device and checks
* for the presence of the "ref_locked" sensor. If it exists, waits for
* the reference lock to be established, printing a dot every second until
* the lock is achieved or a timeout occurs. If the lock is successful, it
* prints "LOCKED"; otherwise, it prints "FAILED" and exits the program.
*
*/
void Sdr::check10MhzLock(){
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
}

/*** @brief Locks GPS and sets the USRP time
 * 
 * Waits for the GPS to lock, retrieves the GPS time, and sets the
 * USRP time to the GPS time. If the GPS is locked, sets the USRP time,
 * checks if the USRP time matches the GPS time and prints results. If
 * GPS not locked, prints a warning message indicating that the time
 * isn't accurate.
 *
*/
void Sdr::gpsLock(){
  //wait for GPS lock
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
}
/*** @brief Checks the USRP time against GPS time
 *
 * Retrieves the GPS time from the USRP device, compares it with last PPS time
 * from USRP device, and prints results. If the GPS time matches the last PPS time,
 * synchronization is indicated. If it doesn't match, an error message is printed.
 */
void Sdr::checkAndSetTime(){
  //set GPS time
    time_spec_t gps_time = time_spec_t(int64_t(usrp->get_mboard_sensor("gps_time", 0).to_int()));
    usrp->set_time_next_pps(gps_time + 1.0, 0);

    // Wait for it to apply
    // The wait is 2 seconds because N-Series has a known issue where
    // the time at the last PPS does not properly update at the PPS edge
    // when the time is actually set.
    this_thread::sleep_for(chrono::seconds(2));
 gps_time = time_spec_t(int64_t(usrp->get_mboard_sensor("gps_time", 0).to_int()));
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
}

/*** @brief Detects and validates TX and RX channels
 * 
 * Splits the specified TX and RX channel strings into individual channel numbers,
 * checks if they are valid, and stores them in the respective vectors.
 * If any channel number is invalid, throws a runtime error.
 */
void Sdr::detectChannels(){
  boost::split(tx_channel_strings, tx_channels, boost::is_any_of("\"',"));
  for (size_t ch = 0; ch < tx_channel_strings.size(); ch++) {
    size_t chan = stoi(tx_channel_strings[ch]);
    if (chan >= usrp->get_tx_num_channels()) {
      throw std::runtime_error("Invalid TX channel(s) specified.");
    } else
      tx_channel_nums.push_back(stoi(tx_channel_strings[ch]));
  }
  boost::split(rx_channel_strings, rx_channels, boost::is_any_of("\"',"));
  for (size_t ch = 0; ch < rx_channel_strings.size(); ch++) {
    size_t chan = stoi(rx_channel_strings[ch]);
    if (chan >= usrp->get_rx_num_channels()) {
      throw std::runtime_error("Invalid RX channel(s) specified.");
    } else
      rx_channel_nums.push_back(stoi(rx_channel_strings[ch]));
  }
}
/*** @brief Sets the RF parameters for the USRP device
 * 
 * Configures the RF parameters for the USRP device based on the number of
 * TX channels specified. If only one TX channel is specified, calls
 * `set_rf_params_single` function. If two TX channels specified, calls
 * `set_rf_params_multi`. If number of channels is not supported, throws a runtime
 * error. After setting RF parameters, allows for a setup time
 * to ensure the parameters are applied correctly.
*/

void Sdr::setRFParams(){
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
}

/*** @brief Checks the reference and local oscillator (LO) lock status
 * 
 * Checks the lock status of the reference and local oscillator
 * for both transmit and receive channels. It retrieves the sensor names for
 * each channel and checks if the "lo_locked" sensor is present. If it is,
 * it retrieves the lock status and asserts that it is true. If the lock is not
 * established, it throws an assertion error.
*/

void Sdr::refLoLockDetect(){
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
}
/*** @brief Sets up GPIO pins for the USRP device
 * 
 * Configures the GPIO pins for the USRP device based on YAML configuration file. 
 * It sets the GPIO bank, control, data direction, and ATR masks. If power amplifier pin 
 * is specified, configures the ATR pins accordingly. Also sets the external reference
 * output port based on the specified integer value. 1 enables the reference output, 0
 * disables the reference output. If the value is not specified, it does nothing.
*/

void Sdr::setupGpio(){
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

//  cout << "AMP_GPIO_MASK: " << bitset<32>(AMP_GPIO_MASK) << endl;

  // turns external ref out port on or off
   if (ref_out_int == 1) {
    usrp->set_clock_source_out(true);
  } else if (ref_out_int == 0) {
    usrp->set_clock_source_out(false);
  } // else do nothing (SDR likely doesn't support this parameter)
  
  // update the offset time for start of streaming to be offset from the current usrp time
}

/*** @brief Sets up the transmit stream for the USRP device
 * 
 * Initializes the transmit stream with the specified CPU and OTW formats,
 * and sets the number of channels for transmission. If transmission is enabled,
 * retrieves the transmit stream from the USRP device and prints maximum
 * number of samples that can be sent in a single call.
*/
void Sdr::setupTx(){
  // Stream formats
  stream_args_t tx_stream_args(cpu_format, otw_format);
  tx_stream_args.channels = tx_channel_nums;

  // tx streamer
  if (transmit) {
    tx_stream = usrp->get_tx_stream(tx_stream_args);
    cout << "INFO: tx_stream get_max_num_samps: " << tx_stream->get_max_num_samps() << endl;
  }
}

/*** @brief Sets up the receive stream for the USRP device
 * 
 * Initializes the receive stream with the specified CPU and OTW formats,
 * and sets the number of channels for reception. Retrieves the receive stream
 * from the USRP device and prints maximum number of samples that can be
 * received in a single call.
 */
void Sdr::setupRx(){
   stream_args_t rx_stream_args(cpu_format, otw_format);

  // rx streamer
  rx_stream_args.channels = rx_channel_nums;
  rx_stream = usrp->get_rx_stream(rx_stream_args);

  cout << "INFO: rx_stream get_max_num_samps: " << rx_stream->get_max_num_samps() << endl;
}

// DEVICE
string Sdr::getDeviceArgs() const {return device_args;}
string Sdr::getSubdev() const {return subdev;}
string Sdr::getClkRef() const {return clk_ref;}
double Sdr::getClkRate() const {return clk_rate;}
string Sdr::getTxChannels() const {return tx_channels;}
string Sdr::getRxChannels() const {return rx_channels;}
string Sdr::getCpuFormat() const {return cpu_format;}
string Sdr::getOtwFormat() const {return otw_format;}

// GPIO
int Sdr::getPwrAmpPin() const {return pwr_amp_pin;}
string Sdr::getGpioBank() const {return gpio_bank;}
uint32_t Sdr::getAmpGpioMask() const {return AMP_GPIO_MASK;}
uint32_t Sdr::getAtrMasks() const {return ATR_MASKS;}
uint32_t Sdr::getAtrControl() const {return ATR_CONTROL;}
uint32_t Sdr::getGpioDdr() const {return GPIO_DDR;}
int Sdr::getRefOutInt() const {return ref_out_int;}

// RF
YAML::Node Sdr::getRf0() const {return rf0;}
YAML::Node Sdr::getRf1() const {return rf1;}
double Sdr::getRxRate() const {return rx_rate;}
double Sdr::getTxRate() const {return tx_rate;}
double Sdr::getFreq() const {return freq;}
double Sdr::getRxGain() const {return rx_gain;}
double Sdr::getTxGain() const {return tx_gain;}
double Sdr::getBw() const {return bw;}
string Sdr::getRxAnt() const {return rx_ant;}
string Sdr::getTxAnt() const {return tx_ant;}
bool Sdr::getTransmit() const {return transmit;}

// USRP
usrp::multi_usrp::sptr Sdr::getUsrp() const {return usrp;}
tx_streamer::sptr Sdr::getTxStream() const {return tx_stream;}
rx_streamer::sptr Sdr::getRxStream() const {return rx_stream;}
vector<string>& Sdr::getTxChannelStrings() {return tx_channel_strings;}
vector<size_t>& Sdr::getTxChannelNums() {return tx_channel_nums;}
vector<string>& Sdr::getRxChannelStrings() {return rx_channel_strings;}
vector<size_t>& Sdr::getRxChannelNums() {return rx_channel_nums;}
