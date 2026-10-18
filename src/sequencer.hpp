#pragma once

#include "kiprim_device.hpp"
#include <vector>
#include <string>
#include <functional>
#include <chrono>

struct SequenceStep {
    double voltage = 5.0;     // V
    double current = 1.0;     // A
    double duration_sec = 2.0;// seconds
};

enum class SequencerState {
    IDLE,
    RUNNING,
    PAUSED,
    COMPLETED
};

struct SequencerProgress {
    SequencerState state = SequencerState::IDLE;
    size_t current_step_index = 0;
    size_t total_steps = 0;
    int current_cycle = 0;
    int total_cycles = 1;      // 0 = Infinite
    double step_time_elapsed = 0.0;
    double step_duration = 0.0;
    double progress_fraction = 0.0; // 0.0 - 1.0
};

class Sequencer {
public:
    using ProgressCallback = std::function<void(const SequencerProgress&)>;

    Sequencer(KiprimDevice& device);
    ~Sequencer();

    void set_steps(const std::vector<SequenceStep>& steps);
    const std::vector<SequenceStep>& get_steps() const;

    void set_cycles(int cycles); // 0 = Infinite, >=1 = fixed
    int get_cycles() const;

    void set_turn_off_on_finish(bool turn_off);
    bool get_turn_off_on_finish() const;

    void start();
    void pause();
    void resume();
    void stop();

    bool is_running() const;
    SequencerState get_state() const;
    SequencerProgress get_progress() const;

    void tick(double dt_sec); // Called periodically by GTK timer

    void set_on_progress(ProgressCallback cb);

    // Waveform generator presets
    void generate_ramp(double start_v, double end_v, double step_v, double current, double step_duration);
    void generate_pulse(double v_low, double v_high, double current, double t_low, double t_high, int pulses);
    void generate_staircase(double v_min, double v_max, int steps, double current, double step_duration);

    bool save_to_file(const std::string& filepath) const;
    bool load_from_file(const std::string& filepath);

private:
    void apply_current_step();

    KiprimDevice& device_;
    std::vector<SequenceStep> steps_;
    int cycles_ = 1;
    bool turn_off_on_finish_ = true;

    SequencerState state_ = SequencerState::IDLE;
    size_t current_step_idx_ = 0;
    int current_cycle_ = 0;
    double step_elapsed_sec_ = 0.0;

    ProgressCallback on_progress_;
};
