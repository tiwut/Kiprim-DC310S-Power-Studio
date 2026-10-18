#pragma once

#include "kiprim_device.hpp"
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>

class DataLogger {
public:
    DataLogger();
    ~DataLogger();

    bool start_logging(const std::string& filepath, double interval_sec = 0.25);
    void stop_logging();

    bool is_logging() const;
    size_t get_sample_count() const;
    std::string get_filepath() const;
    double get_elapsed_sec() const;
    size_t get_file_bytes() const;

    void log_sample(const PowerSupplyState& state);

private:
    mutable std::mutex mutex_;
    std::ofstream file_;
    std::string current_path_;
    bool logging_ = false;

    double interval_sec_ = 0.25;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_log_time_;

    size_t samples_written_ = 0;
};
