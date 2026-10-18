#pragma once

#include <string>
#include <vector>
#include <chrono>

struct SerialDeviceInfo {
    std::string path;
    std::string description;
    bool is_likely_kiprim = false;
};

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool open_port(const std::string& path, int baud_rate = 115200);
    void close_port();
    bool is_open() const;

    bool write_data(const std::string& data);
    bool write_line(const std::string& line);

    std::string read_line(std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

    void flush_buffers();

    static std::vector<SerialDeviceInfo> enumerate_ports();

private:
    int fd_ = -1;
    std::string current_port_;
};
