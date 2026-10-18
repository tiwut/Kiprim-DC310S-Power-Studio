#include "data_logger.hpp"

#include <iomanip>
#include <ctime>
#include <filesystem>

DataLogger::DataLogger() {}

DataLogger::~DataLogger() {
    stop_logging();
}

bool DataLogger::start_logging(const std::string& filepath, double interval_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_logging();

    file_.open(filepath, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        return false;
    }

    current_path_ = filepath;
    interval_sec_ = std::max(0.01, interval_sec);
    start_time_ = std::chrono::steady_clock::now();
    last_log_time_ = start_time_ - std::chrono::milliseconds(10000);
    samples_written_ = 0;
    logging_ = true;

    // Write header if newly created
    if (std::filesystem::file_size(filepath) == 0) {
        file_ << "Timestamp_ISO8601,Elapsed_sec,V_Set,I_Set,V_Meas,I_Meas,P_Meas,Energy_Wh,Output_State,Mode\n";
        file_.flush();
    }

    return true;
}

void DataLogger::stop_logging() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    logging_ = false;
}

bool DataLogger::is_logging() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logging_;
}

size_t DataLogger::get_sample_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_written_;
}

std::string DataLogger::get_filepath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_path_;
}

double DataLogger::get_elapsed_sec() const {
    if (!logging_) return 0.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

size_t DataLogger::get_file_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_path_.empty() && std::filesystem::exists(current_path_)) {
        return std::filesystem::file_size(current_path_);
    }
    return 0;
}

void DataLogger::log_sample(const PowerSupplyState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!logging_ || !file_.is_open()) return;

    auto now = std::chrono::steady_clock::now();
    double since_last = std::chrono::duration<double>(now - last_log_time_).count();
    if (since_last < interval_sec_) {
        return; // throttle by interval
    }
    last_log_time_ = now;

    double elapsed = std::chrono::duration<double>(now - start_time_).count();

    // Get current ISO8601 timestamp string
    auto sys_now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(sys_now);
    std::tm tm_buf;
    localtime_r(&tt, &tm_buf);

    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    file_ << time_str << ","
          << std::fixed << std::setprecision(3) << elapsed << ","
          << state.voltage_set << ","
          << state.current_set << ","
          << state.measured_voltage << ","
          << state.measured_current << ","
          << state.measured_power << ","
          << std::setprecision(5) << state.total_energy_wh << ","
          << (state.output_on ? "ON" : "OFF") << ","
          << (state.is_cc_mode ? "CC" : "CV") << "\n";

    samples_written_++;

    // Flush every 10 samples to ensure durability
    if (samples_written_ % 10 == 0) {
        file_.flush();
    }
}
