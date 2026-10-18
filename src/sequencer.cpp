#include "sequencer.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

Sequencer::Sequencer(KiprimDevice& device) : device_(device) {
    // Default sample sequence
    steps_ = {
        {3.3, 1.0, 3.0},
        {5.0, 1.5, 3.0},
        {9.0, 1.0, 3.0},
        {12.0, 2.0, 3.0},
        {0.0, 0.5, 2.0}
    };
}

Sequencer::~Sequencer() {
    stop();
}

void Sequencer::set_steps(const std::vector<SequenceStep>& steps) {
    if (state_ == SequencerState::RUNNING || state_ == SequencerState::PAUSED) {
        stop();
    }
    steps_ = steps;
}

const std::vector<SequenceStep>& Sequencer::get_steps() const {
    return steps_;
}

void Sequencer::set_cycles(int cycles) {
    cycles_ = std::max(0, cycles);
}

int Sequencer::get_cycles() const {
    return cycles_;
}

void Sequencer::set_turn_off_on_finish(bool turn_off) {
    turn_off_on_finish_ = turn_off;
}

bool Sequencer::get_turn_off_on_finish() const {
    return turn_off_on_finish_;
}

void Sequencer::start() {
    if (steps_.empty()) return;

    current_step_idx_ = 0;
    current_cycle_ = 1;
    step_elapsed_sec_ = 0.0;
    state_ = SequencerState::RUNNING;

    // Ensure output is enabled
    device_.set_output(true);
    apply_current_step();

    if (on_progress_) {
        on_progress_(get_progress());
    }
}

void Sequencer::pause() {
    if (state_ == SequencerState::RUNNING) {
        state_ = SequencerState::PAUSED;
        if (on_progress_) on_progress_(get_progress());
    }
}

void Sequencer::resume() {
    if (state_ == SequencerState::PAUSED) {
        state_ = SequencerState::RUNNING;
        apply_current_step();
        if (on_progress_) on_progress_(get_progress());
    }
}

void Sequencer::stop() {
    if (state_ == SequencerState::IDLE) return;

    state_ = SequencerState::IDLE;
    current_step_idx_ = 0;
    step_elapsed_sec_ = 0.0;

    if (turn_off_on_finish_) {
        device_.set_output(false);
    }

    if (on_progress_) {
        on_progress_(get_progress());
    }
}

bool Sequencer::is_running() const {
    return state_ == SequencerState::RUNNING;
}

SequencerState Sequencer::get_state() const {
    return state_;
}

SequencerProgress Sequencer::get_progress() const {
    SequencerProgress p;
    p.state = state_;
    p.current_step_index = current_step_idx_;
    p.total_steps = steps_.size();
    p.current_cycle = current_cycle_;
    p.total_cycles = cycles_;

    if (!steps_.empty() && current_step_idx_ < steps_.size()) {
        p.step_duration = steps_[current_step_idx_].duration_sec;
        p.step_time_elapsed = step_elapsed_sec_;
        if (p.step_duration > 0.0) {
            p.progress_fraction = std::clamp(step_elapsed_sec_ / p.step_duration, 0.0, 1.0);
        }
    }
    return p;
}

void Sequencer::tick(double dt_sec) {
    if (state_ != SequencerState::RUNNING) return;
    if (steps_.empty() || current_step_idx_ >= steps_.size()) {
        stop();
        return;
    }

    step_elapsed_sec_ += dt_sec;

    double step_target_dur = steps_[current_step_idx_].duration_sec;
    if (step_elapsed_sec_ >= step_target_dur) {
        step_elapsed_sec_ = 0.0;
        current_step_idx_++;

        if (current_step_idx_ >= steps_.size()) {
            // Completed one cycle
            if (cycles_ == 0 || current_cycle_ < cycles_) {
                current_cycle_++;
                current_step_idx_ = 0;
                apply_current_step();
            } else {
                // Completed all requested cycles
                state_ = SequencerState::COMPLETED;
                if (turn_off_on_finish_) {
                    device_.set_output(false);
                }
                if (on_progress_) on_progress_(get_progress());
                return;
            }
        } else {
            apply_current_step();
        }
    }

    if (on_progress_) {
        on_progress_(get_progress());
    }
}

void Sequencer::apply_current_step() {
    if (current_step_idx_ < steps_.size()) {
        const auto& step = steps_[current_step_idx_];
        device_.set_voltage(step.voltage);
        device_.set_current(step.current);
    }
}

void Sequencer::set_on_progress(ProgressCallback cb) {
    on_progress_ = cb;
}

void Sequencer::generate_ramp(double start_v, double end_v, double step_v, double current, double step_duration) {
    std::vector<SequenceStep> new_steps;
    if (step_v <= 0.01) step_v = 0.5;
    if (step_duration <= 0.05) step_duration = 0.5;

    if (start_v <= end_v) {
        for (double v = start_v; v <= end_v + 0.001; v += step_v) {
            new_steps.push_back({v, current, step_duration});
        }
    } else {
        for (double v = start_v; v >= end_v - 0.001; v -= step_v) {
            new_steps.push_back({v, current, step_duration});
        }
    }
    set_steps(new_steps);
}

void Sequencer::generate_pulse(double v_low, double v_high, double current, double t_low, double t_high, int pulses) {
    std::vector<SequenceStep> new_steps;
    pulses = std::max(1, pulses);
    for (int i = 0; i < pulses; ++i) {
        new_steps.push_back({v_high, current, t_high});
        new_steps.push_back({v_low, current, t_low});
    }
    set_steps(new_steps);
}

void Sequencer::generate_staircase(double v_min, double v_max, int steps, double current, double step_duration) {
    std::vector<SequenceStep> new_steps;
    steps = std::max(2, steps);
    double step_size = (v_max - v_min) / (steps - 1);

    // Ramp up
    for (int i = 0; i < steps; ++i) {
        new_steps.push_back({v_min + i * step_size, current, step_duration});
    }
    // Ramp down
    for (int i = steps - 2; i > 0; --i) {
        new_steps.push_back({v_min + i * step_size, current, step_duration});
    }
    set_steps(new_steps);
}

bool Sequencer::save_to_file(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "# Kiprim DC310S Sequence Profile\n";
    out << "# Cycles," << cycles_ << "\n";
    out << "# TurnOffOnFinish," << (turn_off_on_finish_ ? "1" : "0") << "\n";
    out << "Step,Voltage_V,Current_A,Duration_sec\n";

    for (size_t i = 0; i < steps_.size(); ++i) {
        out << (i + 1) << ","
            << steps_[i].voltage << ","
            << steps_[i].current << ","
            << steps_[i].duration_sec << "\n";
    }
    return true;
}

bool Sequencer::load_from_file(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    std::vector<SequenceStep> new_steps;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            if (line.find("# Cycles,") == 0) {
                try { cycles_ = std::stoi(line.substr(9)); } catch (...) {}
            } else if (line.find("# TurnOffOnFinish,") == 0) {
                turn_off_on_finish_ = (line.substr(18) == "1");
            }
            continue;
        }
        if (line.find("Step,") == 0) continue; // header

        std::stringstream ss(line);
        std::string part;
        double v = 0.0, i = 0.0, d = 0.0;

        if (std::getline(ss, part, ',')) {
            // step num
            if (std::getline(ss, part, ',')) v = std::stod(part);
            if (std::getline(ss, part, ',')) i = std::stod(part);
            if (std::getline(ss, part, ',')) d = std::stod(part);
            if (d > 0.0) {
                new_steps.push_back({v, i, d});
            }
        }
    }

    if (!new_steps.empty()) {
        set_steps(new_steps);
        return true;
    }
    return false;
}
