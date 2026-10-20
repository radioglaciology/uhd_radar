#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <boost/asio.hpp>
#include <thread>

using namespace boost::asio;

// Send raw UBX message over Boost Asio serial port
void sendUBX(serial_port& serial, const std::vector<uint8_t>& msg) {
    write(serial, buffer(msg.data(), msg.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Configure GPS rate to X Hz (UBX-CFG-RATE)
void configureRate(serial_port& serial, int hz) {
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

// Enable only GGA messages, disable all others
void configureNMEAMessages(serial_port& serial, uint8_t ggaRate) {
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

int main() {
    printf("Starting GPS code...\n");

    io_context io;
    serial_port serial(io);

    try {
        printf("Opening serial port...\n");
        serial.open("/dev/ttyACM0");
        printf("Serial port opened.\n");
        serial.set_option(serial_port_base::baud_rate(115200));
        serial.set_option(serial_port_base::character_size(8));
        serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
        serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
        serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));

        // Send UBX commands to configure GPS
        configureRate(serial, 3);              // 3 Hz update rate
        configureNMEAMessages(serial, 1);      // Enable only GGA

        std::ofstream gps_output("gps_log.txt");

        std::string line;
        char c;

        std::cout << "Reading NMEA data...\n";

        while (true) {
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

    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return 0;
}
