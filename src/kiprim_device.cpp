#include "kiprim_device.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <cmath>

KiprimDevice::KiprimDevice() {
    state_.timestamp = std::chrono::system_clock::now();
}

KiprimDevice::~KiprimDevice() {
    disconnect();
}

bool KiprimDevice::connect(const std::string& port_path, int baud) {
    disconnect();

    if (!serial_.open_port(port_path, baud)) {
        if (on_connection_) {
            on_connection_(false, false, "Failed to open serial port: " + port_path);
        }
        return false;
    }

    connected_ = true;
    simulated_ = false;

    // Query hardware identity and initial settings
    query_device_hardware();

    // Start background polling thread
    running_ = true;
    worker_thread_ = std::thread(&KiprimDevice::worker_loop, this);

    if (on_connection_) {
        std::string desc = identity_.manufacturer + " " + identity_.model + " (" + identity_.serial_number + ")";
        on_connection_(true, false, desc);
    }
    return true;
}

void KiprimDevice::connect_simulation() {
    disconnect();

    connected_ = true;
    simulated_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        identity_.manufacturer = "Kiprim (Simulated)";
        identity_.model = "DC310S-PRO";
        identity_.serial_number = "SIM-2026-99";
        identity_.firmware = "FV:V5.2.0-SIM";

        state_.voltage_set = 5.0;
        state_.current_set = 2.0;
        state_.output_on = false;
        state_.ovp_limit = 31.0;
        state_.ocp_limit = 10.0;
        state_.measured_voltage = 0.0;
        state_.measured_current = 0.0;
        state_.measured_power = 0.0;
        state_.total_energy_wh = 0.0;
        state_.total_charge_ah = 0.0;
        state_.elapsed_active_sec = 0.0;
        sim_actual_v_ = 0.0;
    }

    running_ = true;
    worker_thread_ = std::thread(&KiprimDevice::worker_loop, this);

    if (on_connection_) {
        on_connection_(true, true, "Simulated Kiprim DC310S Ready");
    }
}

void KiprimDevice::disconnect() {
    if (running_) {
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    if (serial_.is_open()) {
        serial_.close_port();
    }

    connected_ = false;
    simulated_ = false;

    if (on_connection_) {
        on_connection_(false, false, "Disconnected");
    }
}

bool KiprimDevice::is_connected() const {
    return connected_;
}

bool KiprimDevice::is_simulated() const {
    return simulated_;
}

DeviceIdentity KiprimDevice::get_identity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return identity_;
}

PowerSupplyState KiprimDevice::get_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void KiprimDevice::set_voltage(double volts) {
    if (volts < 0.0) volts = 0.0;
    if (volts > 31.0) volts = 31.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << volts;
    std::string val_str = ss.str();

    std::lock_guard<std::mutex> lock(mutex_);
    state_.voltage_set = volts;
    command_queue_.push("VOLT " + val_str);
}

void KiprimDevice::set_current(double amps) {
    if (amps < 0.0) amps = 0.0;
    if (amps > 10.0) amps = 10.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << amps;
    std::string val_str = ss.str();

    std::lock_guard<std::mutex> lock(mutex_);
    state_.current_set = amps;
    command_queue_.push("CURR " + val_str);
}

void KiprimDevice::set_output(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.output_on = on;
    command_queue_.push(on ? "OUTP ON" : "OUTP OFF");
}

void KiprimDevice::set_ovp(double volts) {
    if (volts < 0.0) volts = 0.0;
    if (volts > 32.0) volts = 32.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << volts;

    std::lock_guard<std::mutex> lock(mutex_);
    state_.ovp_limit = volts;
    command_queue_.push("VOLT:LIM " + ss.str());
}

void KiprimDevice::set_ocp(double amps) {
    if (amps < 0.0) amps = 0.0;
    if (amps > 10.5) amps = 10.5;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << amps;

    std::lock_guard<std::mutex> lock(mutex_);
    state_.ocp_limit = amps;
    command_queue_.push("CURR:LIM " + ss.str());
}

void KiprimDevice::set_locked(bool locked) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.is_locked = locked;
    command_queue_.push(locked ? "SYST:LOCK ON" : "SYST:LOCK OFF");
}

void KiprimDevice::reset_energy_counters() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.total_energy_wh = 0.0;
    state_.total_charge_ah = 0.0;
    state_.elapsed_active_sec = 0.0;
}

std::string KiprimDevice::execute_raw_command(const std::string& cmd) {
    if (simulated_) {
        if (cmd == "*IDN?") return "KIPRIM,DC310S,SIM-2026-99,FV:V5.2.0-SIM";
        if (cmd == "VOLT?") return std::to_string(state_.voltage_set);
        if (cmd == "CURR?") return std::to_string(state_.current_set);
        if (cmd == "OUTP?") return state_.output_on ? "ON" : "OFF";
        if (cmd == "MEAS:VOLT?" || cmd == "MEAS?") return std::to_string(state_.measured_voltage);
        if (cmd == "MEAS:CURR?") return std::to_string(state_.measured_current);
        if (cmd == "MEAS:POW?") return std::to_string(state_.measured_power);
        if (cmd == "MEAS:ALL?") {
            return std::to_string(state_.measured_voltage) + "," + std::to_string(state_.measured_current);
        }
        return "OK (Simulated)";
    }

    if (!serial_.is_open()) return "ERROR: Port not open";

    std::lock_guard<std::mutex> lock(mutex_);
    if (on_raw_log_) on_raw_log_(true, cmd);
    serial_.write_line(cmd);

    // If query command, read response
    if (cmd.find('?') != std::string::npos) {
        std::string resp = serial_.read_line(std::chrono::milliseconds(300));
        if (on_raw_log_) on_raw_log_(false, resp);
        return resp;
    }
    return "SENT";
}

void KiprimDevice::set_on_update(UpdateCallback cb) {
    on_update_ = cb;
}

void KiprimDevice::set_on_connection(ConnectionCallback cb) {
    on_connection_ = cb;
}

void KiprimDevice::set_on_raw_log(RawLogCallback cb) {
    on_raw_log_ = cb;
}

void KiprimDevice::query_device_hardware() {
    serial_.flush_buffers();

    // Query *IDN?
    serial_.write_line("*IDN?");
    std::string idn = serial_.read_line(std::chrono::milliseconds(500));
    if (!idn.empty()) {
        std::stringstream ss(idn);
        std::string part;
        if (std::getline(ss, part, ',')) identity_.manufacturer = part;
        if (std::getline(ss, part, ',')) identity_.model = part;
        if (std::getline(ss, part, ',')) identity_.serial_number = part;
        if (std::getline(ss, part, ',')) identity_.firmware = part;
    }

    // Query initial setpoints
    serial_.write_line("VOLT?");
    std::string volt_str = serial_.read_line(std::chrono::milliseconds(200));
    try { if (!volt_str.empty()) state_.voltage_set = std::stod(volt_str); } catch (...) {}

    serial_.write_line("CURR?");
    std::string curr_str = serial_.read_line(std::chrono::milliseconds(200));
    try { if (!curr_str.empty()) state_.current_set = std::stod(curr_str); } catch (...) {}

    serial_.write_line("OUTP?");
    std::string outp_str = serial_.read_line(std::chrono::milliseconds(200));
    state_.output_on = (outp_str.find("ON") != std::string::npos || outp_str.find("1") != std::string::npos);

    serial_.write_line("VOLT:LIM?");
    std::string vlim_str = serial_.read_line(std::chrono::milliseconds(200));
    try { if (!vlim_str.empty()) state_.ovp_limit = std::stod(vlim_str); } catch (...) {}

    serial_.write_line("CURR:LIM?");
    std::string clim_str = serial_.read_line(std::chrono::milliseconds(200));
    try { if (!clim_str.empty()) state_.ocp_limit = std::stod(clim_str); } catch (...) {}
}

void KiprimDevice::simulation_step(double dt_sec) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Process commands in simulation
    while (!command_queue_.empty()) {
        std::string cmd = command_queue_.front();
        command_queue_.pop();
        if (cmd.rfind("VOLT ", 0) == 0) {
            try { state_.voltage_set = std::stod(cmd.substr(5)); } catch (...) {}
        } else if (cmd.rfind("CURR ", 0) == 0) {
            try { state_.current_set = std::stod(cmd.substr(5)); } catch (...) {}
        } else if (cmd == "OUTP ON") {
            state_.output_on = true;
        } else if (cmd == "OUTP OFF") {
            state_.output_on = false;
        } else if (cmd.rfind("VOLT:LIM ", 0) == 0) {
            try { state_.ovp_limit = std::stod(cmd.substr(9)); } catch (...) {}
        } else if (cmd.rfind("CURR:LIM ", 0) == 0) {
            try { state_.ocp_limit = std::stod(cmd.substr(9)); } catch (...) {}
        }
    }

    static std::mt19937 rng(42);
    std::normal_distribution<double> noise_dist(0.0, 0.003); // ±3mV ripple noise

    if (state_.output_on) {
        state_.elapsed_active_sec += dt_sec;

        // Smooth voltage transition to setpoint
        double alpha = std::min(1.0, dt_sec * 12.0); // fast rise time
        sim_actual_v_ += (state_.voltage_set - sim_actual_v_) * alpha;

        // Current through simulated 10 ohm load + random perturbation
        double ideal_i = sim_actual_v_ / sim_load_resistance_;
        if (ideal_i > state_.current_set) {
            // Constant Current (CC) clamping
            state_.is_cc_mode = true;
            state_.measured_current = state_.current_set + noise_dist(rng) * 0.2;
            state_.measured_voltage = (state_.current_set * sim_load_resistance_) + noise_dist(rng);
        } else {
            // Constant Voltage (CV) mode
            state_.is_cc_mode = false;
            state_.measured_voltage = sim_actual_v_ + noise_dist(rng);
            state_.measured_current = ideal_i + noise_dist(rng) * 0.1;
        }

        if (state_.measured_voltage < 0.0) state_.measured_voltage = 0.0;
        if (state_.measured_current < 0.0) state_.measured_current = 0.0;

        state_.measured_power = state_.measured_voltage * state_.measured_current;

        // Wh and Ah integration
        double hours = dt_sec / 3600.0;
        state_.total_energy_wh += state_.measured_power * hours;
        state_.total_charge_ah += state_.measured_current * hours;
    } else {
        // Output OFF: discharge to 0
        sim_actual_v_ *= 0.7;
        if (sim_actual_v_ < 0.01) sim_actual_v_ = 0.0;
        state_.measured_voltage = sim_actual_v_;
        state_.measured_current = 0.0;
        state_.measured_power = 0.0;
        state_.is_cc_mode = false;
    }

    state_.timestamp = std::chrono::system_clock::now();
}

void KiprimDevice::worker_loop() {
    auto last_time = std::chrono::steady_clock::now();
    int poll_counter = 0;
    int consecutive_errors = 0;

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        double dt_sec = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        if (simulated_) {
            simulation_step(dt_sec);
            if (on_update_) {
                on_update_(get_state());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Real hardware loop
        // 1. Process queued commands
        std::vector<std::string> pending_commands;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!command_queue_.empty()) {
                pending_commands.push_back(command_queue_.front());
                command_queue_.pop();
            }
        }

        for (const auto& cmd : pending_commands) {
            serial_.write_line(cmd);
            if (on_raw_log_) on_raw_log_(true, cmd);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }

        // 2. Query measurements: MEAS:ALL? returns "voltage,current"
        serial_.write_line("MEAS:ALL?");
        std::string meas_resp = serial_.read_line(std::chrono::milliseconds(200));

        if (!meas_resp.empty() && meas_resp != "ERR") {
            consecutive_errors = 0;
            double v = 0.0, i = 0.0;
            size_t comma = meas_resp.find(',');
            if (comma != std::string::npos) {
                try {
                    v = std::stod(meas_resp.substr(0, comma));
                    i = std::stod(meas_resp.substr(comma + 1));
                } catch (...) {}
            } else {
                try { v = std::stod(meas_resp); } catch (...) {}
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.measured_voltage = v;
                state_.measured_current = i;
                state_.measured_power = v * i;

                if (state_.output_on) {
                    state_.elapsed_active_sec += dt_sec;
                    double hours = dt_sec / 3600.0;
                    state_.total_energy_wh += state_.measured_power * hours;
                    state_.total_charge_ah += state_.measured_current * hours;
                }

                // Check regulation mode
                if (state_.output_on && state_.current_set > 0.001) {
                    if (state_.measured_current >= state_.current_set * 0.96 &&
                        state_.measured_voltage < state_.voltage_set * 0.96) {
                        state_.is_cc_mode = true;
                    } else {
                        state_.is_cc_mode = false;
                    }
                } else {
                    state_.is_cc_mode = false;
                }

                state_.timestamp = std::chrono::system_clock::now();
            }

            if (on_update_) {
                on_update_(get_state());
            }
        } else {
            consecutive_errors++;
            if (consecutive_errors > 15) {
                std::cerr << "Too many consecutive serial read timeouts. Disconnecting." << std::endl;
                connected_ = false;
                if (on_connection_) {
                    on_connection_(false, false, "Connection lost / Device unresponsive");
                }
                break;
            }
        }

        // 3. Periodically query OUTP?
        poll_counter++;
        if (poll_counter % 8 == 0) {
            serial_.write_line("OUTP?");
            std::string outp_resp = serial_.read_line(std::chrono::milliseconds(100));
            if (!outp_resp.empty()) {
                bool is_on = (outp_resp.find("ON") != std::string::npos || outp_resp.find("1") != std::string::npos);
                std::lock_guard<std::mutex> lock(mutex_);
                state_.output_on = is_on;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
}
