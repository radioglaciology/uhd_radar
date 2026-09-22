#ifndef SDR_HPP
#define SDR_HPP

#include "yaml-cpp/yaml.h"
#include "rf_settings.hpp"
#include "common.hpp"

class Sdr {
  public:
    Sdr(const string& kYamlFile);

    void createUsrp();
    void setupUsrp();

    // DEVICE
    string getDeviceArgs() const;
    string getSubdev() const;
    string getClkRef() const;
    double getClkRate() const;
    string getTxChannels() const;
    string getRxChannels() const;
    string getCpuFormat() const;
    string getOtwFormat() const;

    // GPIO
    int getPwrAmpPin() const;
    string getGpioBank() const;
    uint32_t getAmpGpioMask() const;
    uint32_t getAtrMasks() const;
    uint32_t getAtrControl() const;
    uint32_t getGpioDdr() const;
    int getRefOutInt() const;

    // RF
    YAML::Node getRf0() const;
    YAML::Node getRf1() const;
    double getRxRate() const;
    double getTxRate() const;
    double getFreq() const;
    double getRxGain() const;
    double getTxGain() const;
    double getBw() const;
    string getRxAnt() const;
    string getTxAnt() const;
    bool getTransmit() const;

    // USRP
    usrp::multi_usrp::sptr getUsrp() const;
    tx_streamer::sptr getTxStream() const;
    rx_streamer::sptr getRxStream() const;
    vector<string>& getTxChannelStrings();
    vector<size_t>& getTxChannelNums();
    vector<string>& getRxChannelStrings();
    vector<size_t>& getRxChannelNums();

  private:
    friend class SdrHwTest;
    void loadConfigFromYaml(const string& kYamlFile);
    void check10MhzLock();
    void gpsLock();
    void checkAndSetTime();
    void detectChannels();
    void setRFParams();
    void refLoLockDetect();
    void setupGpio();
    void setupTx();
    void setupRx();

    // DEVICE
    string device_args;
    string subdev;      // Active SDR submodules. See https://files.ettus.com/manual/page_configuration.html
    string clk_ref;     // Clock reference source. See https://files.ettus.com/manual/page_sync.html
    double clk_rate;    // [Hz] SDR main clock frequency
    string tx_channels; // List of TX channels to use (command separated)
    string rx_channels; // List of RX channels to use (command separated) (must be the same length as tx_channels)
    string cpu_format;  // CPU-side sample format. See https://files.ettus.com/manual/structuhd_1_1stream__args__t.html#a602a64b4937a85dba84e7f724387e252
                        // Supported options: "fc32", "sc16", "sc8"
    string otw_format;  // On the wire format. See https://files.ettus.com/manual/structuhd_1_1stream__args__t.html#a0ba0e946d2f83f7ac085f4f4e2ce9578
                        // (Any format supported.)

    // GPIO
    int pwr_amp_pin;        // Which GPIO pin to use for external power amplifier control (set to -1 if not using)
    string gpio_bank;       // Which GPIO bank to use (FP0 is front panel and default)
    uint32_t AMP_GPIO_MASK;
    uint32_t ATR_MASKS;
    uint32_t ATR_CONTROL;
    uint32_t GPIO_DDR;
    int ref_out_int;        // Turns the 10 MHz reference out signal on (1) or off (0)
                            // set to (-1) if SDR does not support

    // RF
    YAML::Node rf0; // RF FRONTEND 0
    YAML::Node rf1; // RF FRONTEND 1 (not supported on b205mini)
    double rx_rate; // [Hz] RX Sample Rate
    double tx_rate; // [Hz] TX Sample Rate
    double freq;    // [Hz] Center Frequency (mixer frequency)
    double rx_gain; // [dB] RX Gain
    double tx_gain; // [dB] TX Gain - 60.8 is about -10 dBm output on the b205mini
    double bw;      // [Hz] Configurable filter bandwidth
    string rx_ant;  // Port to be used for RX
    string tx_ant;  // Port to be used for TX
    bool transmit;  // "true" (or not set) for normal operation, set to "false" to completely disable transmit

    // USRP
    usrp::multi_usrp::sptr usrp;
    tx_streamer::sptr tx_stream;
    rx_streamer::sptr rx_stream;
    vector<string> tx_channel_strings;
    vector<size_t> tx_channel_nums;
    vector<string> rx_channel_strings;
    vector<size_t> rx_channel_nums;

};

#endif
