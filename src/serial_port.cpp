#include "serial_port.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

SerialPort::SerialPort() : fd_(-1) {}

SerialPort::~SerialPort() {
    close_port();
}

static speed_t get_baud_constant(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return B115200;
    }
}

bool SerialPort::open_port(const std::string& path, int baud_rate) {
    close_port();

    fd_ = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd_, &tty) != 0) {
        close_port();
        return false;
    }

    speed_t speed = get_baud_constant(baud_rate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;        // No parity
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // No hardware flow control
    tty.c_cflag |= (CLOCAL | CREAD); // Enable receiver, local line

    // Raw input
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // Raw output
    tty.c_oflag &= ~OPOST;

    // Software flow control disabled
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR | IGNCR);

    // Timeout settings: non-blocking or short wait
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; // 0.1s timeout

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        close_port();
        return false;
    }

    // Flush any pending data
    tcflush(fd_, TCIOFLUSH);

    current_port_ = path;
    return true;
}

void SerialPort::close_port() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        current_port_.clear();
    }
}

bool SerialPort::is_open() const {
    return fd_ >= 0;
}

bool SerialPort::write_data(const std::string& data) {
    if (fd_ < 0) return false;
    ssize_t written = 0;
    size_t total = data.size();
    while (written < (ssize_t)total) {
        ssize_t res = write(fd_, data.data() + written, total - written);
        if (res < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(2000);
                continue;
            }
            return false;
        }
        written += res;
    }
    return true;
}

bool SerialPort::write_line(const std::string& line) {
    std::string to_send = line;
    if (to_send.empty() || to_send.back() != '\n') {
        to_send += "\n";
    }
    return write_data(to_send);
}

std::string SerialPort::read_line(std::chrono::milliseconds timeout) {
    if (fd_ < 0) return "";

    std::string result;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        );
        if (elapsed >= timeout) {
            break;
        }

        int remaining_ms = (int)(timeout.count() - elapsed.count());
        if (remaining_ms <= 0) remaining_ms = 1;

        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_ret = poll(&pfd, 1, remaining_ms);
        if (poll_ret <= 0) {
            break; // timeout or error
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        if (pfd.revents & POLLIN) {
            char buf[64];
            ssize_t n = read(fd_, buf, sizeof(buf));
            if (n > 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    char c = buf[i];
                    if (c == '\n') {
                        // Strip trailing '\r' if present
                        if (!result.empty() && result.back() == '\r') {
                            result.pop_back();
                        }
                        return result;
                    }
                    result.push_back(c);
                }
            } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;
            }
        }
    }

    if (!result.empty() && result.back() == '\r') {
        result.pop_back();
    }
    return result;
}

void SerialPort::flush_buffers() {
    if (fd_ >= 0) {
        tcflush(fd_, TCIOFLUSH);
    }
}

std::vector<SerialDeviceInfo> SerialPort::enumerate_ports() {
    std::vector<SerialDeviceInfo> devices;

    // Check /dev/serial/by-id first for descriptive names
    if (fs::exists("/dev/serial/by-id")) {
        for (const auto& entry : fs::directory_iterator("/dev/serial/by-id")) {
            std::string symlink_name = entry.path().filename().string();
            std::string resolved_path = "";
            try {
                resolved_path = fs::canonical(entry.path()).string();
            } catch (...) {
                continue;
            }

            SerialDeviceInfo info;
            info.path = resolved_path;
            info.description = symlink_name;
            if (symlink_name.find("1a86") != std::string::npos || 
                symlink_name.find("CH340") != std::string::npos ||
                symlink_name.find("USB_Serial") != std::string::npos) {
                info.is_likely_kiprim = true;
                info.description += " (Likely Kiprim DC310S)";
            }
            devices.push_back(info);
        }
    }

    // Also check standard /dev/ttyUSB* and /dev/ttyACM*
    for (int i = 0; i < 16; ++i) {
        std::string p1 = "/dev/ttyUSB" + std::to_string(i);
        if (fs::exists(p1)) {
            bool already_added = false;
            for (const auto& d : devices) {
                if (d.path == p1) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                SerialDeviceInfo info;
                info.path = p1;
                info.description = "USB Serial Port (" + p1 + ")";
                devices.push_back(info);
            }
        }

        std::string p2 = "/dev/ttyACM" + std::to_string(i);
        if (fs::exists(p2)) {
            bool already_added = false;
            for (const auto& d : devices) {
                if (d.path == p2) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                SerialDeviceInfo info;
                info.path = p2;
                info.description = "ACM Serial Port (" + p2 + ")";
                devices.push_back(info);
            }
        }
    }

    return devices;
}
