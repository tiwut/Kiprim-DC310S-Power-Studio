#pragma once

#include "serial_port.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <chrono>

struct PowerSupplyState {
    double voltage_set = 5.0;      // Setpoint (V)
    double current_set = 1.0;      // Current limit (A)
    bool output_on = false;        // Output active state

    double measured_voltage = 0.0; // Actual output (V)
    double measured_current = 0.0; // Actual output (A)
    double measured_power = 0.0;   // Actual power (W)

    double ovp_limit = 31.0;       // Over-voltage protection (V)
    double ocp_limit = 10.0;       // Over-current protection (A)

    bool is_cc_mode = false;       // True = Constant Current, False = Constant Voltage
    bool is_locked = false;        // Front-panel keypad lock

    double total_energy_wh = 0.0;  // Cumulative energy (Wh)
    double total_charge_ah = 0.0;  // Cumulative charge (Ah)
    double elapsed_active_sec = 0.0;

    std::chrono::system_clock::time_point timestamp;
};

struct DeviceIdentity {
    std::string manufacturer = "Kiprim";
    std::string model = "DC310S";
    std::string serial_number = "Unknown";
    std::string firmware = "Unknown";
};

class KiprimDevice {
public:
    using UpdateCallback = std::function<void(const PowerSupplyState&)>;
    using ConnectionCallback = std::function<void(bool connected, bool is_simulated, const std::string& message)>;
    using RawLogCallback = std::function<void(bool is_tx, const std::string& data)>;

    KiprimDevice();
    ~KiprimDevice();

    bool connect(const std::string& port_path, int baud = 115200);
    void connect_simulation();
    void disconnect();

    bool is_connected() const;
    bool is_simulated() const;
    DeviceIdentity get_identity() const;

    // Controls
    void set_voltage(double volts);
    void set_current(double amps);
    void set_output(bool on);
    void set_ovp(double volts);
    void set_ocp(double amps);
    void set_locked(bool locked);
    void reset_energy_counters();

    // Raw SCPI interface
    std::string execute_raw_command(const std::string& cmd);

    // Get snapshot of current state
    PowerSupplyState get_state() const;

    // Callbacks
    void set_on_update(UpdateCallback cb);
    void set_on_connection(ConnectionCallback cb);
    void set_on_raw_log(RawLogCallback cb);

private:
    void worker_loop();
    void simulation_step(double dt_sec);
    void query_device_hardware();

    mutable std::mutex mutex_;
    SerialPort serial_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> simulated_{false};
    std::atomic<bool> running_{false};

    std::thread worker_thread_;

    PowerSupplyState state_;
    DeviceIdentity identity_;

    std::queue<std::string> command_queue_;

    UpdateCallback on_update_;
    ConnectionCallback on_connection_;
    RawLogCallback on_raw_log_;

    // Simulation internal state
    double sim_load_resistance_ = 10.0; // 10 Ohm nominal load
    double sim_actual_v_ = 0.0;
};
