#pragma once

#include <adwaita.h>
#include "kiprim_device.hpp"
#include "graph_widget.hpp"
#include "sequencer.hpp"
#include "data_logger.hpp"
#include <memory>
#include <vector>

struct PresetMemory {
    double voltage = 5.0;
    double current = 1.0;
};

class AppWindow {
public:
    AppWindow(AdwApplication* app);
    ~AppWindow();

    void present();
    GtkWidget* get_widget() const { return window_; }

private:
    void setup_ui();
    void setup_header();
    void setup_views();
    GtkWidget* build_control_view();
    GtkWidget* build_graph_view();
    GtkWidget* build_sequencer_view();
    GtkWidget* build_logger_view();
    GtkWidget* build_diagnostics_view();

    void refresh_serial_ports();
    void connect_device(const std::string& port);
    void disconnect_device();
    void toggle_output();

    void update_ui_from_state(const PowerSupplyState& state);
    void update_connection_status(bool connected, bool simulated, const std::string& desc);
    void append_terminal_log(bool is_tx, const std::string& text);

    static gboolean on_tick_timer(gpointer user_data);
    static void on_window_destroy(GtkWidget* widget, gpointer user_data);

    AdwApplication* app_ = nullptr;
    GtkWidget* window_ = nullptr;

    // Core engines
    KiprimDevice device_;
    std::unique_ptr<GraphWidget> graph_;
    std::unique_ptr<Sequencer> sequencer_;
    std::unique_ptr<DataLogger> logger_;

    // Presets M1-M5
    std::vector<PresetMemory> memory_presets_;

    // View stack
    GtkWidget* view_stack_ = nullptr;
    GtkWidget* status_badge_ = nullptr;

    // Hero meter widgets
    GtkWidget* lbl_voltage_ = nullptr;
    GtkWidget* lbl_current_ = nullptr;
    GtkWidget* lbl_power_ = nullptr;
    GtkWidget* lbl_energy_ = nullptr;
    GtkWidget* lbl_charge_ = nullptr;
    GtkWidget* lbl_timer_ = nullptr;
    GtkWidget* badge_mode_ = nullptr;
    GtkWidget* btn_output_ = nullptr;

    // Controls
    GtkWidget* spin_voltage_ = nullptr;
    GtkWidget* scale_voltage_ = nullptr;
    GtkWidget* spin_current_ = nullptr;
    GtkWidget* scale_current_ = nullptr;
    GtkWidget* spin_ovp_ = nullptr;
    GtkWidget* spin_ocp_ = nullptr;
    GtkWidget* switch_lock_ = nullptr;

    // Graph UI widgets
    GtkWidget* lbl_stats_v_ = nullptr;
    GtkWidget* lbl_stats_i_ = nullptr;
    GtkWidget* lbl_stats_p_ = nullptr;
    GtkWidget* btn_graph_pause_ = nullptr;

    // Sequencer UI widgets
    GtkWidget* seq_list_box_ = nullptr;
    GtkWidget* seq_progress_bar_ = nullptr;
    GtkWidget* seq_status_lbl_ = nullptr;
    GtkWidget* spin_seq_cycles_ = nullptr;
    GtkWidget* btn_seq_start_ = nullptr;
    GtkWidget* btn_seq_pause_ = nullptr;

    // Logger UI widgets
    GtkWidget* entry_log_path_ = nullptr;
    GtkWidget* combo_log_interval_ = nullptr;
    GtkWidget* btn_log_toggle_ = nullptr;
    GtkWidget* lbl_log_samples_ = nullptr;
    GtkWidget* lbl_log_elapsed_ = nullptr;
    GtkWidget* lbl_log_size_ = nullptr;

    // Diagnostics / Device UI
    GtkWidget* combo_ports_ = nullptr;
    GtkWidget* btn_connect_ = nullptr;
    GtkWidget* switch_sim_ = nullptr;
    GtkWidget* lbl_dev_info_ = nullptr;
    GtkWidget* tv_terminal_ = nullptr;
    GtkTextBuffer* tb_terminal_ = nullptr;
    GtkWidget* entry_scpi_ = nullptr;

    guint timer_id_ = 0;
    bool suppress_spin_callbacks_ = false;
};
