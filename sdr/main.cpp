#include "main.hpp"

/*
 * SIG INT HANDLER
 */
static bool stop_signal_called = false;
void sig_int_handler(int) {
  stop_signal_called = true;
}


/*** CONFIGURATION PARAMETERS ***/

// FILENAMES
string chirp_loc;
string output_dir;
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
long int last_pulse_num_written = -1; // Index number (pulses_received - error_count) of last sample written to outfile

// Cout mutex
std::mutex cout_mutex;


/**
 * @brief Checks for errors in the RX buffer and adds the errors to a counter, before using transform() on the incoming pulse
 * 
 * checks for assorted unknown errors related to RX, checks for unexpected number of samples in the RX buffer, and then uses the transform() function if no errors are found
 * @param n_samps_in_rx_buff Number of samples in the RX buffer
 * @param rx_md Metadata from the RX stream
 * @param chirp Chirp object containing parameters for the chirp
 * @param buff Buffer for individual RX samples
 * @param sample_sum Sum error-free RX pulses
 * @param inversion_phase Phase to use for phase inversion of this chirp
 */
void handleRxBuffer(size_t n_samps_in_rx_buff, rx_metadata_t& rx_md, Chirp& chirp, vector<complex<float>>& buff, vector<complex<float>>& sample_sum, float& inversion_phase) {
  if (chirp.getPhaseDither()) {
    inversion_phase = -1.0 * get_next_phase(false); // Get next phase from the generator each time to keep in sequence with TX
  }

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
    cout << "Note: rx_stream->recv can return less than the expected number of samples in some situations, ";
    cout << "but it's not currently supported by this code." << endl;
    cout_mutex.unlock();
    // If you encounter this error, one possible reason is that the buffer sizes set in your transport parameters are too small.
    // For libUSB-based transport, recv_frame_size should be at least the size of num_rx_samps.

    pulses_received++;
    error_count++;
  } else {
    pulses_received++;

    if (chirp.getPhaseDither()) {
      // Undo phase modulation and divide by num_presums in one go
      transform(buff.begin(), buff.end(), buff.begin(), std::bind1st(std::multiplies<complex<float>>(), polar((float) 1.0/chirp.getNumPresums(), inversion_phase)));
    } else if (chirp.getNumPresums() != 1) {
      // Only divide by num_presums
      transform(buff.begin(), buff.end(), buff.begin(), std::bind1st(std::multiplies<complex<float>>(), 1.0/chirp.getNumPresums()));
    }

    // Add to sample_sum
    transform(sample_sum.begin(), sample_sum.end(), buff.begin(), sample_sum.begin(), plus<complex<float>>());
  }
}

/**
 * @brief Writes received RX data to file if enough pulses have been received
 * 
 * Checks if the number of pulses received is enough to write a full sample_sum to the file, only if enough error-free pulses have been received.
 * @param pulses_received Total number of pulses received
 * @param error_count Total number of errors encountered
 * @param last_pulse_num_written Last pulse number written to the file
 * @param chirp Chirp object containing parameters for the chirp
 * @param sample_sum Vector containing the sum of error-free RX pulses
 * @param outfile Output file stream to write the RX data
 * @return Returns true if the data was successfully written to the file, false otherwise signaling error
 */
bool checkForFullSampleSum(Chirp& chirp, vector<complex<float>>& sample_sum, ofstream& outfile) {
  if (((pulses_received - error_count) > last_pulse_num_written) && ((pulses_received - error_count) % chirp.getNumPresums() == 0)) {
    // As each sample is added, it has phase inversion applied and is divided by # presums, so no additional work to do here.
    // write RX data to file
    if (outfile.is_open()) {
      outfile.write((const char*)&sample_sum.front(), 
        num_rx_samps * sizeof(complex<float>));
    } else {
      cout_mutex.lock();
      cout << "Cannot write to outfile!" << endl;
      cout_mutex.unlock();
      return false; // Error writing to file
    }
    fill(sample_sum.begin(), sample_sum.end(), complex<float>(0,0)); // Zero out sum for next time
    last_pulse_num_written = pulses_received - error_count;
  }
  return true;
}

// Split output files based on number of chirps

/**
 * @brief Determines if the amount of pulses received is enough to add another file for storage
 * 
 * Creates more files if the number of pulses is higher than the maximum number of chirps per file, but only if file splitting is enabled.
 * @param chirp Chirp object containing parameters for the chirp
 * @param outfile Output file stream to write the RX data
 * @param current_filename Current filename for the output file
 * @param save_file_index Index of the current file being saved
 * @param last_pulse_num_written Last pulse number written to the file
 */
void splitOutputFiles(Chirp& chirp, ofstream& outfile, string& current_filename, int& save_file_index) {
  if ( (chirp.getMaxChirpsPerFile() > 0) && (int(last_pulse_num_written / chirp.getMaxChirpsPerFile()) > save_file_index)) {
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
}

//Wrapping up main function after the RX loop is done

/**
 * @brief Finalizes the main function once the main while loop is done
 * 
 * Various tasks are finished and significant information is printed to the console, such as the number of errors encountered, total pulses written, and total pulses attempted.
 * @param gps_stream GPS stream descriptor for closing the GPS file
 * @param outfile Output file stream to close
 * @param current_filename Current filename for the output file
 * @param error_count Total number of errors encountered during the RX process
 * @param last_pulse_num_written Last pulse number written to the file
 * @param pulses_received Total number of pulses received during the RX process
 * @param transmit_thread Thread group for the transmit worker
 */
void wrapUp(boost::asio::posix::stream_descriptor& gps_stream, ofstream& outfile, string& current_filename, boost::thread_group& transmit_thread) {
  cout << "[RX] Closing output file." << endl;
  outfile.close();
  cout << "[CLOSE FILE] " << current_filename << endl;

  gps_stream.close();

  cout << "[RX] Error count: " << error_count << endl;
  cout << "[RX] Total pulses written: " << last_pulse_num_written << endl;
  cout << "[RX] Total pulses attempted: " << pulses_received << endl;
  
  cout << "[RX] Done. Calling join_all() on transmit thread group." << endl;

  transmit_thread.join_all();

  cout << "[RX] transmit_thread.join_all() complete." << endl << endl;
}

// TODO: This needs to be commented out otherwise the chirp to the spec analyzer won't transmit 
// because it can't find the data/rx_samps.bin
 // Send raw UBX message over Boost Asio serial port
/**
 * @brief Sends message to GPS module over serial port
 * 
 * Sends a UBX message to the GPS module over the specified serial port.
 * @param serial Serial port to send UBX commands to configure GPS
 * @param msg Message to send
 */
void sendUBX(boost::asio::serial_port& serial, const std::vector<uint8_t>& msg) {
    write(serial, boost::asio::buffer(msg.data(), msg.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
} 

// Configure GPS rate to X Hz (UBX-CFG-RATE)
/**
 * @brief Changes how often the GPS module sends data
 * 
 * Configures the measurement rate for the GPS module, using hz
 * @param serial Serial port to send UBX commands to configure GPS
 * @param hz Desired measurement rate in Hz
 */
void configureRate(boost::asio::serial_port& serial, int hz) {
    uint16_t measRate = 1000 / hz;
    std::vector<uint8_t> msg = {
        0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
        static_cast<uint8_t>(measRate & 0xFF),
        static_cast<uint8_t>((measRate >> 8) & 0xFF),
        0x01, 0x00, 0x01, 0x00, // navRate = 1, timeRef = UTC
        0x00, 0x00 // Checksum placeholders
    };

    // Calculate checksum
    uint8_t ckA = 0, ckB = 0;
    for (size_t i = 2; i < 12; ++i) {
        ckA += msg[i];
        ckB += ckA;
    }
    msg[12] = ckA;
    msg[13] = ckB;

    sendUBX(serial, msg);
}

/**
 * @brief Configures what messages are sent by the GPS module
 * 
 * Determines which messages are sent by the GPS module on startup, such as only sending the GGA message format.
 * @param serial Serial port to send UBX commands to configure GPS
 * @param ggaRate Rate for GGA messages
 */
void configureNMEAMessages(boost::asio::serial_port& serial, uint8_t ggaRate) {
    std::vector<std::pair<uint8_t, uint8_t>> nmeaMsgs = {
        {0xF0, 0x00}, // GGA
        {0xF0, 0x01}, {0xF0, 0x02}, {0xF0, 0x03}, {0xF0, 0x04},
        {0xF0, 0x05}, {0xF0, 0x06}, {0xF0, 0x07}, {0xF0, 0x08},
        {0xF0, 0x09}, {0xF0, 0x0D}, {0xF0, 0x0F}
    };

    for (auto [clsId, msgId] : nmeaMsgs) {
        uint8_t rate = (msgId == 0x00) ? ggaRate : 0;
        std::vector<uint8_t> msg = {
            0xB5, 0x62, 0x06, 0x01, 0x08, 0x00,
            clsId, msgId,
            0x00, // I2C
            rate, // UART1
            0x00, // UART2
            0x00, // USB
            0x00, // SPI
            0x00, 0x00 // Checksum placeholders
        };

        // Calculate checksum
        uint8_t ckA = 0, ckB = 0;
        for (size_t i = 2; i < 14; ++i) {
            ckA += msg[i];
            ckB += ckA;
        }
        msg[14] = ckA;
        msg[15] = ckB;

        sendUBX(serial, msg);
    }
} 


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

  Sdr sdr(yaml_filename);
  Chirp chirp(yaml_filename);
  YAML::Node config = YAML::LoadFile(yaml_filename);
  sdr.createUsrp();
  sdr.setupUsrp();

  //YAML::Node rf0 = config["RF0"];
 // YAML::Node rf1 = config["RF1"];

  YAML::Node files = config["FILES"];
  chirp_loc = files["chirp_loc"].as<string>();
  output_dir = files["output_dir"].as<string>();
  save_loc = files["save_loc"].as<string>();
  gps_save_loc = files["gps_loc"].as<string>();
  chirp.setMaxChirpsPerFile(files["max_chirps_per_file"].as<int>());

  //Merge save_loc and gps_save_loc with output_dir
  save_loc = std::filesystem::path(output_dir).string() + "/" + save_loc;
  gps_save_loc = std::filesystem::path(output_dir).string() + "/" + gps_save_loc;

  // Calculated parameters

  tr_off_delay = chirp.getTxDuration() + chirp.getTrOffTrail(); // Time before turning off GPIO
  num_tx_samps = sdr.getTxRate() * chirp.getTxDuration(); // Total samples to transmit per chirp // TODO: Should use ["GENERATE"]["sample_rate"] instead!
  num_rx_samps = sdr.getRxRate() * chirp.getRxDuration(); // Total samples to receive per chirp // TODO: Should use ["GENERATE"]["sample_rate"] instead!


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
  
  cout << "INFO: Number of TX samples: " << num_tx_samps << endl;  //needs to be after chirp and sdr object are both made
  cout << "INFO: Number of RX samples: " << num_rx_samps << endl << endl;  //needs to be after chirp and sdr object are both made

 
  // update the offset time for start of streaming to be offset from the current usrp time
  chirp.setTimeOffset(chirp.getTimeOffset() + time_spec_t(sdr.getUsrp()->get_time_now()).get_real_secs());  //needs to be after chirp and sdr object are both made

  /*** SPAWN THE TX THREAD ***/
  boost::thread_group transmit_thread;
  transmit_thread.create_thread(boost::bind(&transmit_worker, sdr.getTxStream(), sdr.getRxStream(), boost::ref(chirp), boost::ref(sdr)));
  
  if (!sdr.getTransmit()) {
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
  if (chirp.getMaxChirpsPerFile() > 0) {
    // Breaking into multiple files is enabled
    current_filename = current_filename + "." + to_string(save_file_index);
  }

  // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
  cout << "[OPEN FILE] " << current_filename << endl;
  outfile.open(current_filename, ofstream::binary);

  /*** RX LOOP AND SUM ***/
  if (chirp.getNumPulses() < 0) {
    cout << "num_pulses is < 0. Will continue to send chirps until stopped with Ctrl-C." << endl;
  }

  string gps_data;

  if (sdr.getCpuFormat() != "fc32") {
    cout << "Only cpu_format 'fc32' is supported for now." << endl;
    // This is because we actually need buff and sample_sum to have the correct
    // data type to facilitate phase modulation and summing. In the future, this could be
    // fixed up so that it can work with any supported cpu_format, but it
    // seems unnecessary right now.
    exit(1);
  }

  // receive buffer
  size_t bytes_per_sample = convert::get_bytes_per_item(sdr.getCpuFormat());
  vector<complex<float>> sample_sum(num_rx_samps, 0); // Sum error-free RX pulses into this vector

  vector<complex<float>> buff(num_rx_samps); // Buffer sized for one pulse at a time
  vector<void *> buffs;
  for (size_t ch = 0; ch < sdr.getRxStream()->get_num_channels(); ch++) {
    buffs.push_back(&buff.front()); // TODO: I don't think this actually works for num_channels > 1
  }
  size_t n_samps_in_rx_buff;
  rx_metadata_t rx_md; // Captures metadata from rx_stream->recv() -- specifically primarily timeouts and other errors

  float inversion_phase; // Store phase to use for phase inversion of this chirp
  
  //Creating GPS log & vars
  printf("Starting GPS code...\n");
  using namespace boost::asio;

  io_service io;
  serial_port serial(io);
  bool using_gps = true;

  // TODO: Consider using a library for working with GPS;
  try {
    printf("Opening serial port...\n");
    serial.open("/dev/ttyACM0");  // Adjust if needed for your system
  } catch (boost::system::system_error err) {
    printf("Failed to open GPS.\n");
    using_gps = false;
  }

  if (using_gps) {
    printf("Serial port opened.\n");
    serial.set_option(serial_port_base::baud_rate(115200));
    serial.set_option(serial_port_base::character_size(8));
    serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
    serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
    serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
    
    // Send UBX commands to configure GPS
    configureRate(serial, 3);              // 3 Hz update rate
    configureNMEAMessages(serial, 1);      // Enable only GGA
  }

  ofstream gps_output("gps_log.txt");

  std::string line;
  char c; 

  // Note: This print statement is used by automated post-processing code. Please be careful about changing the format.
  cout << "[START] Beginning main loop" << endl;

  while ((chirp.getNumPulses() < 0) || (last_pulse_num_written < chirp.getNumPulses())) {

    n_samps_in_rx_buff = sdr.getRxStream()->recv(buffs, num_rx_samps, rx_md, 60.0, false); // TODO: Think about timeout

    // Check for errors in the RX buffer
    handleRxBuffer(n_samps_in_rx_buff, rx_md, chirp, buff, sample_sum, inversion_phase);
    // Check if we have a full sample_sum ready to write to file
    if (!checkForFullSampleSum(chirp, sample_sum, outfile)) {exit(1);};


    // Our GPS method (below commented GPS from old version)
    if (using_gps && ((pulses_received % 2000) == 0) && (sdr.getClkRef() == "gpsdo")) {
      read(serial, buffer(&c, 1));
      if (c == '\n') {
          if (line.find("$GNGGA") == 0) {
              auto now = std::chrono::system_clock::now();
              auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                now.time_since_epoch()).count();

              std::stringstream ss(line);
              std::string field;
              std::vector<std::string> fields;

              while (std::getline(ss, field, ',')) {
                  fields.push_back(field);
              }

              if (fields.size() >= 10) {
                  auto convertToDecimal = [](const std::string& nmeaCoord, const std::string& dir) {
                      if (nmeaCoord.empty()) return 0.0;
                      double raw = std::stod(nmeaCoord);
                      int degrees = static_cast<int>(raw / 100);
                      double minutes = raw - (degrees * 100);
                      double decimal = degrees + minutes / 60.0;
                      if (dir == "S" || dir == "W") decimal = -decimal;
                      return decimal;
                  };

                  double latitude = convertToDecimal(fields[2], fields[3]);
                  double longitude = convertToDecimal(fields[4], fields[5]);
                  double altitude = std::stod(fields[9]);

                  if (gps_output.is_open()) {
                      gps_output << std::fixed << std::setprecision(9)
                              << now_us << "," << latitude << "," << longitude << "," << altitude << std::endl;
                  }
                  
                  // FOR READABILITY ONLY!!! Remove for actual use
                  std::cout << std::fixed << std::setprecision(9)
                          << "t=" << now_us << " s, "
                          << "Lat: " << latitude << ", Lon: " << longitude
                          << ", Alt: " << altitude << " m\n";
              }

              line.clear();

          }
          line.clear();
      } else if (c != '\r') {
          line += c;
      }
    }


    // get gps data
    /*if (sdr.getClkRef() == "gpsdo" && ((pulses_received % 100000) == 0)) {
      gps_data = sdr.getUsrp()->get_mboard_sensor("gps_gprmc").to_pp_string();
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
    /*if (sdr.getClkRef() == "gpsdo") {
      boost::asio::async_write(gps_stream, boost::asio::buffer(gps_data + "\n"), gps_asio_handler);
    }*/

    // split output files based on number of chirps
    splitOutputFiles(chirp, outfile, current_filename, save_file_index);
    
    // // clear the matrices holding the sums
    // fill(sample_sum.begin(), sample_sum.end(), complex<int16_t>(0,0));
  }

  /*** WRAP UP ***/
  wrapUp(gps_stream, outfile, current_filename, transmit_thread);

  return EXIT_SUCCESS;
  
}

/*
 * TRANSMIT_WORKER
 */

void transmit_worker(tx_streamer::sptr& tx_stream, rx_streamer::sptr& rx_stream, Chirp& chirp, Sdr& sdr){
  set_thread_priority_safe(1.0, true);

  // open file to stream from
  ifstream infile("../../" + output_dir + "/" + chirp_loc, ifstream::binary);

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

  if (sdr.getCpuFormat() != "fc32") {
    cout << "Only cpu_format 'fc32' is supported for now." << endl;
    // This is because we actually need chirp_unmodulated to have the correct
    // data type to facilitate phase modulation. In the future, this could be
    // fixed up so that it can work with any supported cpu_format, but it
    // seems unnecessary right now.
    exit(1);
  }

  vector<std::complex<float>> tx_buff(num_tx_samps); // Ready-to-transmit samples
  vector<std::complex<float>> chirp_unmodulated(num_tx_samps); // Chirp samples before any phase modulation

  infile.read((char *)&chirp_unmodulated.front(), num_tx_samps * convert::get_bytes_per_item(sdr.getCpuFormat()));
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

  while ((chirp.getNumPulses() < 0) || ((pulses_scheduled - error_count) < chirp.getNumPulses()))
  {
    // Setup next chirp for modulation
    if (chirp.getPhaseDither()) {
      transform(chirp_unmodulated.begin(), chirp_unmodulated.end(), tx_buff.begin(), std::bind1st(std::multiplies<complex<float>>(), polar((float) 1.0, get_next_phase(true))));
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
      error_delay = (error_count - last_error_count) * 2 * chirp.getPulseRepInt();
      chirp.setTimeOffset(chirp.getTimeOffset() + error_delay);
      cout_mutex.lock();
      cout << "[TX] (Chirp " << pulses_scheduled << ") time_offset increased by " << error_delay << endl;
      cout_mutex.unlock();
      last_error_count = error_count;
    }
    // TX
    rx_time = chirp.getTimeOffset() + (chirp.getPulseRepInt() * pulses_scheduled); // TODO: How do we track timing
    tx_md.time_spec = time_spec_t(rx_time - chirp.getTxLead());
    
    if (sdr.getTransmit()) {
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
