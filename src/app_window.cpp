#include "app_window.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

AppWindow::AppWindow(AdwApplication* app) : app_(app) {
    // Default memory presets M1 - M5
    memory_presets_ = {
        {3.300, 1.000}, // M1: 3.3V logic
        {5.000, 2.000}, // M2: 5V USB / logic
        {9.000, 1.500}, // M3: 9V battery / pedal
        {12.000, 3.000},// M4: 12V automotive / motors
        {24.000, 2.000} // M5: 24V industrial
    };

    graph_ = std::make_unique<GraphWidget>();
    sequencer_ = std::make_unique<Sequencer>(device_);
    logger_ = std::make_unique<DataLogger>();

    setup_ui();

    // Hook device callbacks
    device_.set_on_connection([this](bool connected, bool simulated, const std::string& desc) {
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer user_data) -> gboolean {
            auto* p = static_cast<std::tuple<AppWindow*, bool, bool, std::string>*>(user_data);
            auto [self, conn, sim, d] = *p;
            self->update_connection_status(conn, sim, d);
            delete p;
            return G_SOURCE_REMOVE;
        }, new std::tuple<AppWindow*, bool, bool, std::string>(this, connected, simulated, desc), nullptr);
    });

    device_.set_on_raw_log([this](bool is_tx, const std::string& data) {
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer user_data) -> gboolean {
            auto* p = static_cast<std::tuple<AppWindow*, bool, std::string>*>(user_data);
            auto [self, tx, d] = *p;
            self->append_terminal_log(tx, d);
            delete p;
            return G_SOURCE_REMOVE;
        }, new std::tuple<AppWindow*, bool, std::string>(this, is_tx, data), nullptr);
    });

    // 20 Hz periodic tick timer for smooth UI & graph updates
    timer_id_ = g_timeout_add(50, &AppWindow::on_tick_timer, this);

    // Auto-detect & auto-connect if Kiprim is available on /dev/ttyUSB0
    refresh_serial_ports();
    auto ports = SerialPort::enumerate_ports();
    for (const auto& p : ports) {
        if (p.is_likely_kiprim || p.path == "/dev/ttyUSB0") {
            connect_device(p.path);
            break;
        }
    }
}

AppWindow::~AppWindow() {
    if (timer_id_ > 0) {
        g_source_remove(timer_id_);
        timer_id_ = 0;
    }
}

void AppWindow::present() {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_));
    }
}

void AppWindow::setup_ui() {
    window_ = adw_application_window_new(GTK_APPLICATION(app_));
    gtk_window_set_title(GTK_WINDOW(window_), "Kiprim DC310S Power Studio");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1080, 720);

    g_signal_connect(window_, "destroy", G_CALLBACK(&AppWindow::on_window_destroy), this);

    // Load custom CSS
    GtkCssProvider* provider = gtk_css_provider_new();
    std::string css_path = "src/style.css";
    if (std::filesystem::exists(css_path)) {
        gtk_css_provider_load_from_path(provider, css_path.c_str());
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(provider);

    GtkWidget* toolbar_view = adw_toolbar_view_new();
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), toolbar_view);

    setup_views();
    setup_header();

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), view_stack_);
}

void AppWindow::setup_header() {
    GtkWidget* header = adw_header_bar_new();

    // View Switcher in Header Title
    GtkWidget* switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(view_stack_));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);

    // Connection Status Pill
    status_badge_ = gtk_label_new("○ Disconnected");
    gtk_widget_add_css_class(status_badge_, "badge-disconnected");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), status_badge_);

    // Attach to toolbar view
    GtkWidget* toolbar_view = adw_application_window_get_content(ADW_APPLICATION_WINDOW(window_));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);
}

void AppWindow::setup_views() {
    view_stack_ = adw_view_stack_new();

    GtkWidget* ctrl_view = build_control_view();
    GtkWidget* graph_view = build_graph_view();
    GtkWidget* seq_view = build_sequencer_view();
    GtkWidget* log_view = build_logger_view();
    GtkWidget* diag_view = build_diagnostics_view();

    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack_), ctrl_view, "control", "Control", "weather-clear-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack_), graph_view, "graph", "Curves", "emblem-shared-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack_), seq_view, "sequencer", "Sequence", "media-playback-start-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack_), log_view, "logger", "Data Logger", "x-office-spreadsheet-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack_), diag_view, "diagnostics", "Device", "preferences-system-symbolic");
}

// -------------------------------------------------------------
// VIEW 1: Control & Dashboard View
// -------------------------------------------------------------
GtkWidget* AppWindow::build_control_view() {
    GtkWidget* scrolled = gtk_scrolled_window_new();
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 16);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), main_box);

    // 1. Hero Multimeter Readout Section
    GtkWidget* meters_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(meters_box, TRUE);

    auto make_meter_card = [](const char* title, const char* unit, const char* css_val_class, GtkWidget** out_lbl) {
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(card, "hero-display");
        gtk_widget_set_hexpand(card, TRUE);

        GtkWidget* title_lbl = gtk_label_new(title);
        gtk_widget_add_css_class(title_lbl, "meter-title");
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(card), title_lbl);

        GtkWidget* val_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_halign(val_box, GTK_ALIGN_CENTER);

        GtkWidget* val_lbl = gtk_label_new("0.000");
        gtk_widget_add_css_class(val_lbl, css_val_class);
        gtk_box_append(GTK_BOX(val_box), val_lbl);

        GtkWidget* unit_lbl = gtk_label_new(unit);
        gtk_widget_add_css_class(unit_lbl, "meter-unit");
        gtk_widget_set_valign(unit_lbl, GTK_ALIGN_BASELINE_FILL);
        gtk_box_append(GTK_BOX(val_box), unit_lbl);

        gtk_box_append(GTK_BOX(card), val_box);
        *out_lbl = val_lbl;
        return card;
    };

    gtk_box_append(GTK_BOX(meters_box), make_meter_card("OUTPUT VOLTAGE", "V", "meter-value-v", &lbl_voltage_));
    gtk_box_append(GTK_BOX(meters_box), make_meter_card("OUTPUT CURRENT", "A", "meter-value-i", &lbl_current_));
    gtk_box_append(GTK_BOX(meters_box), make_meter_card("OUTPUT POWER", "W", "meter-value-p", &lbl_power_));
    gtk_box_append(GTK_BOX(main_box), meters_box);

    // 2. Sub-metrics bar: Mode Badge (CV/CC), Energy (Wh), Charge (Ah), Run Timer
    GtkWidget* sub_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(sub_bar, GTK_ALIGN_CENTER);

    badge_mode_ = gtk_label_new("CV MODE");
    gtk_widget_add_css_class(badge_mode_, "badge-cv");
    gtk_box_append(GTK_BOX(sub_bar), badge_mode_);

    lbl_energy_ = gtk_label_new("Energy: 0.000 Wh");
    gtk_widget_add_css_class(lbl_energy_, "stats-badge");
    gtk_box_append(GTK_BOX(sub_bar), lbl_energy_);

    lbl_charge_ = gtk_label_new("Charge: 0.000 Ah");
    gtk_widget_add_css_class(lbl_charge_, "stats-badge");
    gtk_box_append(GTK_BOX(sub_bar), lbl_charge_);

    lbl_timer_ = gtk_label_new("Time: 00:00:00");
    gtk_widget_add_css_class(lbl_timer_, "stats-badge");
    gtk_box_append(GTK_BOX(sub_bar), lbl_timer_);

    gtk_box_append(GTK_BOX(main_box), sub_bar);

    // 3. Prominent Output ON/OFF Switch Button
    btn_output_ = gtk_button_new_with_label("⏻  OUTPUT OFF");
    gtk_widget_add_css_class(btn_output_, "btn-output-off");
    gtk_widget_set_halign(btn_output_, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(btn_output_, 320, 54);
    g_signal_connect_swapped(btn_output_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->toggle_output();
    }), this);
    gtk_box_append(GTK_BOX(main_box), btn_output_);

    // 4. Setpoint Adjustments Card (Voltage & Current Controls)
    GtkWidget* setpoints_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(setpoints_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(setpoints_grid), 12);
    gtk_widget_set_hexpand(setpoints_grid, TRUE);

    // --- Voltage Set Card ---
    GtkWidget* card_v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card_v, "hero-display");
    gtk_widget_set_hexpand(card_v, TRUE);

    GtkWidget* hdr_v = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title_v = gtk_label_new("TARGET VOLTAGE SETPOINT");
    gtk_widget_add_css_class(title_v, "meter-title");
    gtk_box_append(GTK_BOX(hdr_v), title_v);
    gtk_box_append(GTK_BOX(card_v), hdr_v);

    GtkWidget* row_v = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    spin_voltage_ = gtk_spin_button_new_with_range(0.0, 31.0, 0.01);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin_voltage_), 3);
    gtk_widget_set_size_request(spin_voltage_, 150, -1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_voltage_), 5.0);
    gtk_box_append(GTK_BOX(row_v), spin_voltage_);

    scale_voltage_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 0.1);
    gtk_widget_set_hexpand(scale_voltage_, TRUE);
    gtk_range_set_value(GTK_RANGE(scale_voltage_), 5.0);
    gtk_box_append(GTK_BOX(row_v), scale_voltage_);
    gtk_box_append(GTK_BOX(card_v), row_v);

    // Stepper buttons: -1V, -0.1V, -0.01V | +0.01V, +0.1V, +1V
    GtkWidget* steps_v_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(steps_v_box, GTK_ALIGN_CENTER);

    struct StepContext { AppWindow* self; double d; };

    auto add_v_step = [this, steps_v_box](const char* label, double delta) {
        GtkWidget* btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "btn-step");
        auto* ctx = new StepContext{this, delta};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<StepContext*>(data);
            double cur = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->self->spin_voltage_));
            double next = std::clamp(cur + c->d, 0.0, 31.0);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_voltage_), next);
            c->self->device_.set_voltage(next);
        }), ctx, [](gpointer data, GClosure*) { delete static_cast<StepContext*>(data); }, (GConnectFlags)0);
        gtk_box_append(GTK_BOX(steps_v_box), btn);
    };

    add_v_step("-1V", -1.0);
    add_v_step("-0.1V", -0.1);
    add_v_step("-0.01V", -0.01);
    add_v_step("+0.01V", 0.01);
    add_v_step("+0.1V", 0.1);
    add_v_step("+1V", 1.0);
    gtk_box_append(GTK_BOX(card_v), steps_v_box);

    // Sync spin and scale
    g_signal_connect_swapped(spin_voltage_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        if (self->suppress_spin_callbacks_) return;
        double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->spin_voltage_));
        gtk_range_set_value(GTK_RANGE(self->scale_voltage_), val);
        self->device_.set_voltage(val);
    }), this);
    g_signal_connect_swapped(scale_voltage_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        if (self->suppress_spin_callbacks_) return;
        double val = gtk_range_get_value(GTK_RANGE(self->scale_voltage_));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->spin_voltage_), val);
        self->device_.set_voltage(val);
    }), this);

    gtk_grid_attach(GTK_GRID(setpoints_grid), card_v, 0, 0, 1, 1);

    // --- Current Set Card ---
    GtkWidget* card_i = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card_i, "hero-display");
    gtk_widget_set_hexpand(card_i, TRUE);

    GtkWidget* hdr_i = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title_i = gtk_label_new("CURRENT LIMIT SETPOINT");
    gtk_widget_add_css_class(title_i, "meter-title");
    gtk_box_append(GTK_BOX(hdr_i), title_i);
    gtk_box_append(GTK_BOX(card_i), hdr_i);

    GtkWidget* row_i = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    spin_current_ = gtk_spin_button_new_with_range(0.0, 10.0, 0.01);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin_current_), 3);
    gtk_widget_set_size_request(spin_current_, 150, -1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_current_), 1.0);
    gtk_box_append(GTK_BOX(row_i), spin_current_);

    scale_current_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 10.0, 0.05);
    gtk_widget_set_hexpand(scale_current_, TRUE);
    gtk_range_set_value(GTK_RANGE(scale_current_), 1.0);
    gtk_box_append(GTK_BOX(row_i), scale_current_);
    gtk_box_append(GTK_BOX(card_i), row_i);

    // Stepper buttons for current
    GtkWidget* steps_i_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(steps_i_box, GTK_ALIGN_CENTER);

    auto add_i_step = [this, steps_i_box](const char* label, double delta) {
        GtkWidget* btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "btn-step");
        auto* ctx = new StepContext{this, delta};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<StepContext*>(data);
            double cur = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->self->spin_current_));
            double next = std::clamp(cur + c->d, 0.0, 10.0);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_current_), next);
            c->self->device_.set_current(next);
        }), ctx, [](gpointer data, GClosure*) { delete static_cast<StepContext*>(data); }, (GConnectFlags)0);
        gtk_box_append(GTK_BOX(steps_i_box), btn);
    };

    add_i_step("-1A", -1.0);
    add_i_step("-0.1A", -0.1);
    add_i_step("-0.01A", -0.01);
    add_i_step("+0.01A", 0.01);
    add_i_step("+0.1A", 0.1);
    add_i_step("+1A", 1.0);
    gtk_box_append(GTK_BOX(card_i), steps_i_box);

    g_signal_connect_swapped(spin_current_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        if (self->suppress_spin_callbacks_) return;
        double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->spin_current_));
        gtk_range_set_value(GTK_RANGE(self->scale_current_), val);
        self->device_.set_current(val);
    }), this);
    g_signal_connect_swapped(scale_current_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        if (self->suppress_spin_callbacks_) return;
        double val = gtk_range_get_value(GTK_RANGE(self->scale_current_));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->spin_current_), val);
        self->device_.set_current(val);
    }), this);

    gtk_grid_attach(GTK_GRID(setpoints_grid), card_i, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(main_box), setpoints_grid);

    // 5. Presets & Memory Channels (M1 - M5)
    GtkWidget* presets_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(presets_card, "hero-display");

    GtkWidget* presets_hdr = gtk_label_new("QUICK PRESETS & MEMORY CHANNELS");
    gtk_widget_add_css_class(presets_hdr, "meter-title");
    gtk_widget_set_halign(presets_hdr, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(presets_card), presets_hdr);

    // Standard Lab Voltages row
    GtkWidget* lab_presets_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    struct PresetContext { AppWindow* self; double v; double i; };

    auto add_lab_preset = [this, lab_presets_box](const char* label, double v, double i) {
        GtkWidget* btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "btn-preset");
        auto* ctx = new PresetContext{this, v, i};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<PresetContext*>(data);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_voltage_), c->v);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_current_), c->i);
            c->self->device_.set_voltage(c->v);
            c->self->device_.set_current(c->i);
        }), ctx, [](gpointer data, GClosure*) { delete static_cast<PresetContext*>(data); }, (GConnectFlags)0);
        gtk_box_append(GTK_BOX(lab_presets_box), btn);
    };

    add_lab_preset("1.8V (Core)", 1.8, 1.0);
    add_lab_preset("3.3V (Logic)", 3.3, 1.0);
    add_lab_preset("5.0V (USB)", 5.0, 2.0);
    add_lab_preset("9.0V (Battery)", 9.0, 1.0);
    add_lab_preset("12.0V (Standard)", 12.0, 2.5);
    add_lab_preset("24.0V (Industrial)", 24.0, 2.0);
    gtk_box_append(GTK_BOX(presets_card), lab_presets_box);

    // Memory Channels M1 - M5 row
    GtkWidget* mem_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    struct MemContext { AppWindow* self; size_t slot; };

    for (size_t m = 0; m < 5; ++m) {
        std::string label = "M" + std::to_string(m + 1) + " (" +
                            std::to_string((int)memory_presets_[m].voltage) + "V)";
        GtkWidget* btn_m = gtk_button_new_with_label(label.c_str());
        gtk_widget_add_css_class(btn_m, "suggested-action");
        gtk_widget_set_hexpand(btn_m, TRUE);

        auto* ctx = new MemContext{this, m};
        g_signal_connect_data(btn_m, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<MemContext*>(data);
            double sv = c->self->memory_presets_[c->slot].voltage;
            double si = c->self->memory_presets_[c->slot].current;
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_voltage_), sv);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->self->spin_current_), si);
            c->self->device_.set_voltage(sv);
            c->self->device_.set_current(si);
        }), ctx, [](gpointer data, GClosure*) { delete static_cast<MemContext*>(data); }, (GConnectFlags)0);

        gtk_box_append(GTK_BOX(mem_box), btn_m);
    }
    gtk_box_append(GTK_BOX(presets_card), mem_box);
    gtk_box_append(GTK_BOX(main_box), presets_card);

    // 6. Protection Limits (OVP & OCP)
    GtkWidget* prot_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(prot_card, "hero-display");

    GtkWidget* ovp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* lbl_ovp_title = gtk_label_new("Over-Voltage Protection (OVP):");
    gtk_widget_add_css_class(lbl_ovp_title, "meter-title");
    spin_ovp_ = gtk_spin_button_new_with_range(1.0, 32.0, 0.5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_ovp_), 31.0);
    g_signal_connect_swapped(spin_ovp_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->spin_ovp_));
        self->device_.set_ovp(val);
    }), this);
    gtk_box_append(GTK_BOX(ovp_box), lbl_ovp_title);
    gtk_box_append(GTK_BOX(ovp_box), spin_ovp_);
    gtk_box_append(GTK_BOX(prot_card), ovp_box);

    GtkWidget* ocp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* lbl_ocp_title = gtk_label_new("Over-Current Protection (OCP):");
    gtk_widget_add_css_class(lbl_ocp_title, "meter-title");
    spin_ocp_ = gtk_spin_button_new_with_range(0.1, 10.5, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_ocp_), 10.0);
    g_signal_connect_swapped(spin_ocp_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->spin_ocp_));
        self->device_.set_ocp(val);
    }), this);
    gtk_box_append(GTK_BOX(ocp_box), lbl_ocp_title);
    gtk_box_append(GTK_BOX(ocp_box), spin_ocp_);
    gtk_box_append(GTK_BOX(prot_card), ocp_box);

    gtk_box_append(GTK_BOX(main_box), prot_card);

    return scrolled;
}

// -------------------------------------------------------------
// VIEW 2: Real-time Waveform Graphs
// -------------------------------------------------------------
GtkWidget* AppWindow::build_graph_view() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    // Toolbar
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    // Trace visibility checkboxes
    GtkWidget* chk_v = gtk_check_button_new_with_label("Voltage (Blue)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_v), TRUE);
    g_signal_connect_swapped(chk_v, "toggled", G_CALLBACK(+[](AppWindow* self, GtkCheckButton* b) {
        self->graph_->set_show_voltage(gtk_check_button_get_active(b));
    }), this);
    gtk_box_append(GTK_BOX(toolbar), chk_v);

    GtkWidget* chk_i = gtk_check_button_new_with_label("Current (Amber)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_i), TRUE);
    g_signal_connect_swapped(chk_i, "toggled", G_CALLBACK(+[](AppWindow* self, GtkCheckButton* b) {
        self->graph_->set_show_current(gtk_check_button_get_active(b));
    }), this);
    gtk_box_append(GTK_BOX(toolbar), chk_i);

    GtkWidget* chk_p = gtk_check_button_new_with_label("Power (Green)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_p), TRUE);
    g_signal_connect_swapped(chk_p, "toggled", G_CALLBACK(+[](AppWindow* self, GtkCheckButton* b) {
        self->graph_->set_show_power(gtk_check_button_get_active(b));
    }), this);
    gtk_box_append(GTK_BOX(toolbar), chk_p);

    // Time window selector
    GtkWidget* lbl_time = gtk_label_new("Window:");
    gtk_box_append(GTK_BOX(toolbar), lbl_time);

    const char* time_options[] = {"5s", "10s", "30s", "60s", "5m", "10m", nullptr};
    GtkWidget* combo_time = gtk_drop_down_new_from_strings(time_options);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_time), 2); // 30s default
    g_signal_connect_swapped(combo_time, "notify::selected", G_CALLBACK(+[](AppWindow* self, GParamSpec*, GtkDropDown* dd) {
        guint sel = gtk_drop_down_get_selected(dd);
        double seconds = 30.0;
        if (sel == 0) seconds = 5.0;
        else if (sel == 1) seconds = 10.0;
        else if (sel == 2) seconds = 30.0;
        else if (sel == 3) seconds = 60.0;
        else if (sel == 4) seconds = 300.0;
        else if (sel == 5) seconds = 600.0;
        self->graph_->set_time_window(seconds);
    }), this);
    gtk_box_append(GTK_BOX(toolbar), combo_time);

    // Auto-scale switch
    GtkWidget* lbl_autoscale = gtk_label_new("Auto-scale:");
    gtk_box_append(GTK_BOX(toolbar), lbl_autoscale);
    GtkWidget* sw_autoscale = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_autoscale), TRUE);
    g_signal_connect_swapped(sw_autoscale, "state-set", G_CALLBACK(+[](AppWindow* self, gboolean state) -> gboolean {
        self->graph_->set_auto_scale(state);
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(toolbar), sw_autoscale);

    // Spacer
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(toolbar), spacer);

    // Pause / Resume button
    btn_graph_pause_ = gtk_button_new_with_label("Pause");
    g_signal_connect_swapped(btn_graph_pause_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        bool now_paused = !self->graph_->is_paused();
        self->graph_->set_paused(now_paused);
        gtk_button_set_label(GTK_BUTTON(self->btn_graph_pause_), now_paused ? "Resume" : "Pause");
    }), this);
    gtk_box_append(GTK_BOX(toolbar), btn_graph_pause_);

    // Clear button
    GtkWidget* btn_clear = gtk_button_new_with_label("Clear");
    g_signal_connect_swapped(btn_clear, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->graph_->clear();
    }), this);
    gtk_box_append(GTK_BOX(toolbar), btn_clear);

    // Export PNG
    GtkWidget* btn_png = gtk_button_new_with_label("Save PNG");
    g_signal_connect_swapped(btn_png, "clicked", G_CALLBACK(+[](AppWindow* self) {
        std::string path = "kiprim_waveform_" + std::to_string(std::time(nullptr)) + ".png";
        self->graph_->export_png(path);
    }), this);
    gtk_box_append(GTK_BOX(toolbar), btn_png);

    gtk_box_append(GTK_BOX(box), toolbar);

    // The Canvas Drawing Area
    GtkWidget* canvas = graph_->get_widget();
    gtk_box_append(GTK_BOX(box), canvas);

    // Visible Window Statistics Bar
    GtkWidget* stats_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(stats_bar, GTK_ALIGN_CENTER);

    lbl_stats_v_ = gtk_label_new("V max: 0.00V  min: 0.00V  avg: 0.00V");
    gtk_widget_add_css_class(lbl_stats_v_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_bar), lbl_stats_v_);

    lbl_stats_i_ = gtk_label_new("I max: 0.00A  min: 0.00A  avg: 0.00A");
    gtk_widget_add_css_class(lbl_stats_i_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_bar), lbl_stats_i_);

    lbl_stats_p_ = gtk_label_new("P peak: 0.00W  avg: 0.00W");
    gtk_widget_add_css_class(lbl_stats_p_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_bar), lbl_stats_p_);

    gtk_box_append(GTK_BOX(box), stats_bar);
    return box;
}

// -------------------------------------------------------------
// VIEW 3: Automated Sequence / List Mode
// -------------------------------------------------------------
GtkWidget* AppWindow::build_sequencer_view() {
    GtkWidget* scrolled = gtk_scrolled_window_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);

    // Status Banner & Controls
    GtkWidget* ctrl_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(ctrl_card, "hero-display");

    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    seq_status_lbl_ = gtk_label_new("Sequence State: IDLE");
    gtk_widget_add_css_class(seq_status_lbl_, "meter-title");
    gtk_box_append(GTK_BOX(top_row), seq_status_lbl_);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);

    GtkWidget* lbl_cycles = gtk_label_new("Cycles (0=Loop):");
    spin_seq_cycles_ = gtk_spin_button_new_with_range(0, 1000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_seq_cycles_), 1);
    g_signal_connect_swapped(spin_seq_cycles_, "value-changed", G_CALLBACK(+[](AppWindow* self) {
        int c = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->spin_seq_cycles_));
        self->sequencer_->set_cycles(c);
    }), this);
    gtk_box_append(GTK_BOX(top_row), lbl_cycles);
    gtk_box_append(GTK_BOX(top_row), spin_seq_cycles_);
    gtk_box_append(GTK_BOX(ctrl_card), top_row);

    // Progress Bar
    seq_progress_bar_ = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(ctrl_card), seq_progress_bar_);

    // Buttons
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    btn_seq_start_ = gtk_button_new_with_label("▶ Start Sequence");
    gtk_widget_add_css_class(btn_seq_start_, "suggested-action");
    g_signal_connect_swapped(btn_seq_start_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->sequencer_->start();
    }), this);
    gtk_box_append(GTK_BOX(btn_row), btn_seq_start_);

    btn_seq_pause_ = gtk_button_new_with_label("⏸ Pause");
    g_signal_connect_swapped(btn_seq_pause_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        if (self->sequencer_->get_state() == SequencerState::RUNNING) {
            self->sequencer_->pause();
        } else if (self->sequencer_->get_state() == SequencerState::PAUSED) {
            self->sequencer_->resume();
        }
    }), this);
    gtk_box_append(GTK_BOX(btn_row), btn_seq_pause_);

    GtkWidget* btn_seq_stop = gtk_button_new_with_label("⏹ Stop");
    gtk_widget_add_css_class(btn_seq_stop, "destructive-action");
    g_signal_connect_swapped(btn_seq_stop, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->sequencer_->stop();
    }), this);
    gtk_box_append(GTK_BOX(btn_row), btn_seq_stop);

    // Generator Presets
    GtkWidget* btn_gen_ramp = gtk_button_new_with_label("⚡ Preset: Ramp (0-12V)");
    g_signal_connect_swapped(btn_gen_ramp, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->sequencer_->generate_ramp(0.0, 12.0, 1.0, 2.0, 1.5);
    }), this);
    gtk_box_append(GTK_BOX(btn_row), btn_gen_ramp);

    GtkWidget* btn_gen_pulse = gtk_button_new_with_label("⚡ Preset: Pulse Train");
    g_signal_connect_swapped(btn_gen_pulse, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->sequencer_->generate_pulse(1.0, 5.0, 1.5, 1.0, 2.0, 5);
    }), this);
    gtk_box_append(GTK_BOX(btn_row), btn_gen_pulse);

    gtk_box_append(GTK_BOX(ctrl_card), btn_row);
    gtk_box_append(GTK_BOX(box), ctrl_card);

    // Sequence Step Description & Instructions
    GtkWidget* info_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(info_card, "hero-display");
    GtkWidget* info_title = gtk_label_new("PROGRAMMED STEP LIST (DWELL MODE)");
    gtk_widget_add_css_class(info_title, "meter-title");
    gtk_box_append(GTK_BOX(info_card), info_title);

    GtkWidget* lbl_steps_summary = gtk_label_new(
        "Current Sequence Profile: 5 Steps\n"
        "• Step 1: 3.300 V  @ 1.000 A  for 3.0s\n"
        "• Step 2: 5.000 V  @ 1.500 A  for 3.0s\n"
        "• Step 3: 9.000 V  @ 1.000 A  for 3.0s\n"
        "• Step 4: 12.000 V @ 2.000 A  for 3.0s\n"
        "• Step 5: 0.000 V  @ 0.500 A  for 2.0s"
    );
    gtk_widget_set_halign(lbl_steps_summary, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(info_card), lbl_steps_summary);
    gtk_box_append(GTK_BOX(box), info_card);

    return scrolled;
}

// -------------------------------------------------------------
// VIEW 4: Data Logger
// -------------------------------------------------------------
GtkWidget* AppWindow::build_logger_view() {
    GtkWidget* scrolled = gtk_scrolled_window_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(card, "hero-display");

    GtkWidget* title = gtk_label_new("CONTINUOUS CSV MEASUREMENT DATA LOGGER");
    gtk_widget_add_css_class(title, "meter-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), title);

    // Destination file entry
    GtkWidget* row_file = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* lbl_file = gtk_label_new("Output File:");
    entry_log_path_ = gtk_entry_new();
    gtk_widget_set_hexpand(entry_log_path_, TRUE);

    std::string default_path = (std::filesystem::current_path() / "kiprim_log.csv").string();
    gtk_editable_set_text(GTK_EDITABLE(entry_log_path_), default_path.c_str());

    gtk_box_append(GTK_BOX(row_file), lbl_file);
    gtk_box_append(GTK_BOX(row_file), entry_log_path_);
    gtk_box_append(GTK_BOX(card), row_file);

    // Interval selector
    GtkWidget* row_opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* lbl_interval = gtk_label_new("Sample Interval:");
    const char* intervals[] = {"100 ms", "250 ms", "500 ms", "1.0 s", "2.0 s", nullptr};
    combo_log_interval_ = gtk_drop_down_new_from_strings(intervals);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_log_interval_), 1); // 250ms default
    gtk_box_append(GTK_BOX(row_opts), lbl_interval);
    gtk_box_append(GTK_BOX(row_opts), combo_log_interval_);

    btn_log_toggle_ = gtk_button_new_with_label("⏺ Start Recording");
    gtk_widget_add_css_class(btn_log_toggle_, "suggested-action");
    g_signal_connect_swapped(btn_log_toggle_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        if (!self->logger_->is_logging()) {
            const char* path = gtk_editable_get_text(GTK_EDITABLE(self->entry_log_path_));
            guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(self->combo_log_interval_));
            double iv = 0.25;
            if (sel == 0) iv = 0.1;
            else if (sel == 1) iv = 0.25;
            else if (sel == 2) iv = 0.5;
            else if (sel == 3) iv = 1.0;
            else if (sel == 4) iv = 2.0;

            if (self->logger_->start_logging(path, iv)) {
                gtk_button_set_label(GTK_BUTTON(self->btn_log_toggle_), "⏹ Stop Recording");
                gtk_widget_remove_css_class(self->btn_log_toggle_, "suggested-action");
                gtk_widget_add_css_class(self->btn_log_toggle_, "destructive-action");
            }
        } else {
            self->logger_->stop_logging();
            gtk_button_set_label(GTK_BUTTON(self->btn_log_toggle_), "⏺ Start Recording");
            gtk_widget_remove_css_class(self->btn_log_toggle_, "destructive-action");
            gtk_widget_add_css_class(self->btn_log_toggle_, "suggested-action");
        }
    }), this);
    gtk_box_append(GTK_BOX(row_opts), btn_log_toggle_);
    gtk_box_append(GTK_BOX(card), row_opts);

    // Live Stats
    GtkWidget* stats_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    lbl_log_samples_ = gtk_label_new("Samples Written: 0");
    gtk_widget_add_css_class(lbl_log_samples_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_box), lbl_log_samples_);

    lbl_log_elapsed_ = gtk_label_new("Elapsed: 00:00:00");
    gtk_widget_add_css_class(lbl_log_elapsed_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_box), lbl_log_elapsed_);

    lbl_log_size_ = gtk_label_new("File Size: 0 KB");
    gtk_widget_add_css_class(lbl_log_size_, "stats-badge");
    gtk_box_append(GTK_BOX(stats_box), lbl_log_size_);

    gtk_box_append(GTK_BOX(card), stats_box);
    gtk_box_append(GTK_BOX(box), card);

    return scrolled;
}

// -------------------------------------------------------------
// VIEW 5: Device & SCPI Diagnostics
// -------------------------------------------------------------
GtkWidget* AppWindow::build_diagnostics_view() {
    GtkWidget* scrolled = gtk_scrolled_window_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);

    // Connection Card
    GtkWidget* conn_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(conn_card, "hero-display");

    GtkWidget* title_conn = gtk_label_new("SERIAL CONNECTION & PORT SETTINGS");
    gtk_widget_add_css_class(title_conn, "meter-title");
    gtk_widget_set_halign(title_conn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(conn_card), title_conn);

    GtkWidget* row_conn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* lbl_port = gtk_label_new("Serial Port:");
    gtk_box_append(GTK_BOX(row_conn), lbl_port);

    combo_ports_ = gtk_drop_down_new(nullptr, nullptr);
    gtk_widget_set_hexpand(combo_ports_, TRUE);
    gtk_box_append(GTK_BOX(row_conn), combo_ports_);

    GtkWidget* btn_rescan = gtk_button_new_with_label("↻ Rescan");
    g_signal_connect_swapped(btn_rescan, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->refresh_serial_ports();
    }), this);
    gtk_box_append(GTK_BOX(row_conn), btn_rescan);

    btn_connect_ = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(btn_connect_, "suggested-action");
    g_signal_connect_swapped(btn_connect_, "clicked", G_CALLBACK(+[](AppWindow* self) {
        if (!self->device_.is_connected()) {
            const char* str = gtk_string_object_get_string(
                GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(self->combo_ports_)))
            );
            if (str) {
                std::string s(str);
                size_t space = s.find(' ');
                std::string dev_path = (space != std::string::npos) ? s.substr(0, space) : s;
                self->connect_device(dev_path);
            }
        } else {
            self->disconnect_device();
        }
    }), this);
    gtk_box_append(GTK_BOX(row_conn), btn_connect_);

    // Simulation Mode Switch
    GtkWidget* lbl_sim = gtk_label_new("Simulation Mode:");
    switch_sim_ = gtk_switch_new();
    g_signal_connect_swapped(switch_sim_, "state-set", G_CALLBACK(+[](AppWindow* self, gboolean state) -> gboolean {
        if (state) {
            self->device_.connect_simulation();
        } else {
            self->disconnect_device();
        }
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(row_conn), lbl_sim);
    gtk_box_append(GTK_BOX(row_conn), switch_sim_);

    gtk_box_append(GTK_BOX(conn_card), row_conn);

    // Device Hardware Info
    lbl_dev_info_ = gtk_label_new("Hardware: Disconnected");
    gtk_widget_add_css_class(lbl_dev_info_, "stats-badge");
    gtk_widget_set_halign(lbl_dev_info_, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(conn_card), lbl_dev_info_);

    gtk_box_append(GTK_BOX(box), conn_card);

    // Front Panel Lock & Reset Energy
    GtkWidget* safety_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(safety_card, "hero-display");

    GtkWidget* lbl_lock = gtk_label_new("Front-Panel Keypad Lock (SYST:LOCK):");
    gtk_widget_add_css_class(lbl_lock, "meter-title");
    switch_lock_ = gtk_switch_new();
    g_signal_connect_swapped(switch_lock_, "state-set", G_CALLBACK(+[](AppWindow* self, gboolean state) -> gboolean {
        self->device_.set_locked(state);
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(safety_card), lbl_lock);
    gtk_box_append(GTK_BOX(safety_card), switch_lock_);

    GtkWidget* btn_reset_energy = gtk_button_new_with_label("Reset Energy/Charge Counters");
    g_signal_connect_swapped(btn_reset_energy, "clicked", G_CALLBACK(+[](AppWindow* self) {
        self->device_.reset_energy_counters();
    }), this);
    gtk_box_append(GTK_BOX(safety_card), btn_reset_energy);
    gtk_box_append(GTK_BOX(box), safety_card);

    // SCPI Terminal Section
    GtkWidget* term_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(term_card, "hero-display");

    GtkWidget* term_hdr = gtk_label_new("INTERACTIVE SCPI TERMINAL & CONSOLE");
    gtk_widget_add_css_class(term_hdr, "meter-title");
    gtk_widget_set_halign(term_hdr, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(term_card), term_hdr);

    // Scrolled TextView for Terminal Log
    GtkWidget* sc_term = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sc_term, -1, 160);
    tv_terminal_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv_terminal_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv_terminal_), FALSE);
    gtk_widget_add_css_class(tv_terminal_, "scpi-terminal");
    tb_terminal_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv_terminal_));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc_term), tv_terminal_);
    gtk_box_append(GTK_BOX(term_card), sc_term);

    // Command Entry & Send Button
    GtkWidget* cmd_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    entry_scpi_ = gtk_entry_new();
    gtk_widget_set_hexpand(entry_scpi_, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_scpi_), "Enter SCPI command (e.g. *IDN?, MEAS:ALL?, VOLT 12.0)");

    GtkWidget* btn_send = gtk_button_new_with_label("Send");
    gtk_widget_add_css_class(btn_send, "suggested-action");

    g_signal_connect_swapped(entry_scpi_, "activate", G_CALLBACK(+[](AppWindow* self) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(self->entry_scpi_));
        if (text && strlen(text) > 0) {
            self->device_.execute_raw_command(text);
            gtk_editable_set_text(GTK_EDITABLE(self->entry_scpi_), "");
        }
    }), this);
    g_signal_connect_swapped(btn_send, "clicked", G_CALLBACK(+[](AppWindow* self) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(self->entry_scpi_));
        if (text && strlen(text) > 0) {
            self->device_.execute_raw_command(text);
            gtk_editable_set_text(GTK_EDITABLE(self->entry_scpi_), "");
        }
    }), this);

    gtk_box_append(GTK_BOX(cmd_box), entry_scpi_);
    gtk_box_append(GTK_BOX(cmd_box), btn_send);
    gtk_box_append(GTK_BOX(term_card), cmd_box);

    // Quick Command Buttons
    GtkWidget* quick_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    struct CmdContext { AppWindow* self; std::string c; };

    auto add_quick_cmd = [this, quick_box](const char* cmd) {
        GtkWidget* b = gtk_button_new_with_label(cmd);
        gtk_widget_add_css_class(b, "btn-step");
        auto* ctx = new CmdContext{this, cmd};
        g_signal_connect_data(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<CmdContext*>(data);
            c->self->device_.execute_raw_command(c->c);
        }), ctx, [](gpointer data, GClosure*) { delete static_cast<CmdContext*>(data); }, (GConnectFlags)0);
        gtk_box_append(GTK_BOX(quick_box), b);
    };

    add_quick_cmd("*IDN?");
    add_quick_cmd("MEAS:ALL?");
    add_quick_cmd("VOLT?");
    add_quick_cmd("CURR?");
    add_quick_cmd("OUTP?");
    add_quick_cmd("VOLT:LIM?");
    add_quick_cmd("CURR:LIM?");
    gtk_box_append(GTK_BOX(term_card), quick_box);

    gtk_box_append(GTK_BOX(box), term_card);
    return scrolled;
}

// -------------------------------------------------------------
// Serial & State Logic
// -------------------------------------------------------------
void AppWindow::refresh_serial_ports() {
    auto ports = SerialPort::enumerate_ports();
    GtkStringList* str_list = gtk_string_list_new(nullptr);

    for (const auto& p : ports) {
        std::string item = p.path + " - " + p.description;
        gtk_string_list_append(str_list, item.c_str());
    }

    if (combo_ports_) {
        gtk_drop_down_set_model(GTK_DROP_DOWN(combo_ports_), G_LIST_MODEL(str_list));
    }
    g_object_unref(str_list);
}

void AppWindow::connect_device(const std::string& port) {
    if (device_.connect(port, 115200)) {
        // Initial sync of setpoints from hardware
        PowerSupplyState st = device_.get_state();
        suppress_spin_callbacks_ = true;
        if (spin_voltage_) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_voltage_), st.voltage_set);
        if (scale_voltage_) gtk_range_set_value(GTK_RANGE(scale_voltage_), st.voltage_set);
        if (spin_current_) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_current_), st.current_set);
        if (scale_current_) gtk_range_set_value(GTK_RANGE(scale_current_), st.current_set);
        if (spin_ovp_) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_ovp_), st.ovp_limit);
        if (spin_ocp_) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_ocp_), st.ocp_limit);
        suppress_spin_callbacks_ = false;
    }
}

void AppWindow::disconnect_device() {
    device_.disconnect();
}

void AppWindow::toggle_output() {
    PowerSupplyState st = device_.get_state();
    device_.set_output(!st.output_on);
}

void AppWindow::update_connection_status(bool connected, bool simulated, const std::string& desc) {
    (void)desc;
    if (status_badge_) {
        gtk_widget_remove_css_class(status_badge_, "badge-connected");
        gtk_widget_remove_css_class(status_badge_, "badge-simulated");
        gtk_widget_remove_css_class(status_badge_, "badge-disconnected");

        if (connected) {
            if (simulated) {
                gtk_label_set_text(GTK_LABEL(status_badge_), "● Simulated DC310S");
                gtk_widget_add_css_class(status_badge_, "badge-simulated");
            } else {
                gtk_label_set_text(GTK_LABEL(status_badge_), "● Kiprim DC310S Connected");
                gtk_widget_add_css_class(status_badge_, "badge-connected");
            }
            if (btn_connect_) {
                gtk_button_set_label(GTK_BUTTON(btn_connect_), "Disconnect");
                gtk_widget_remove_css_class(btn_connect_, "suggested-action");
                gtk_widget_add_css_class(btn_connect_, "destructive-action");
            }
        } else {
            gtk_label_set_text(GTK_LABEL(status_badge_), "○ Disconnected");
            gtk_widget_add_css_class(status_badge_, "badge-disconnected");
            if (btn_connect_) {
                gtk_button_set_label(GTK_BUTTON(btn_connect_), "Connect");
                gtk_widget_remove_css_class(btn_connect_, "destructive-action");
                gtk_widget_add_css_class(btn_connect_, "suggested-action");
            }
        }
    }

    if (lbl_dev_info_) {
        auto id = device_.get_identity();
        std::string info = "Device: " + id.manufacturer + " " + id.model +
                           " | Serial: " + id.serial_number +
                           " | Firmware: " + id.firmware;
        gtk_label_set_text(GTK_LABEL(lbl_dev_info_), info.c_str());
    }
}

void AppWindow::append_terminal_log(bool is_tx, const std::string& text) {
    if (!tb_terminal_) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(tb_terminal_, &end);

    std::string line = (is_tx ? ">> " : "<< ") + text + "\n";
    gtk_text_buffer_insert(tb_terminal_, &end, line.c_str(), -1);

    // Scroll to bottom
    if (tv_terminal_) {
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tv_terminal_), &end, 0.0, FALSE, 0.0, 0.0);
    }
}

void AppWindow::update_ui_from_state(const PowerSupplyState& state) {
    // Voltage
    char buf_v[32];
    snprintf(buf_v, sizeof(buf_v), "%06.3f", state.measured_voltage);
    if (lbl_voltage_) gtk_label_set_text(GTK_LABEL(lbl_voltage_), buf_v);

    // Current
    char buf_i[32];
    snprintf(buf_i, sizeof(buf_i), "%06.3f", state.measured_current);
    if (lbl_current_) gtk_label_set_text(GTK_LABEL(lbl_current_), buf_i);

    // Power
    char buf_p[32];
    snprintf(buf_p, sizeof(buf_p), "%06.2f", state.measured_power);
    if (lbl_power_) gtk_label_set_text(GTK_LABEL(lbl_power_), buf_p);

    // Mode
    if (badge_mode_) {
        if (state.is_cc_mode) {
            gtk_label_set_text(GTK_LABEL(badge_mode_), "CC MODE (CONSTANT CURRENT)");
            gtk_widget_remove_css_class(badge_mode_, "badge-cv");
            gtk_widget_add_css_class(badge_mode_, "badge-cc");
        } else {
            gtk_label_set_text(GTK_LABEL(badge_mode_), "CV MODE (CONSTANT VOLTAGE)");
            gtk_widget_remove_css_class(badge_mode_, "badge-cc");
            gtk_widget_add_css_class(badge_mode_, "badge-cv");
        }
    }

    // Energy & Charge
    if (lbl_energy_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Energy: %.4f Wh", state.total_energy_wh);
        gtk_label_set_text(GTK_LABEL(lbl_energy_), buf);
    }
    if (lbl_charge_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Charge: %.4f Ah", state.total_charge_ah);
        gtk_label_set_text(GTK_LABEL(lbl_charge_), buf);
    }

    // Run Timer
    if (lbl_timer_) {
        int total_s = (int)state.elapsed_active_sec;
        int h = total_s / 3600;
        int m = (total_s % 3600) / 60;
        int s = total_s % 60;
        char buf[64];
        snprintf(buf, sizeof(buf), "Time: %02d:%02d:%02d", h, m, s);
        gtk_label_set_text(GTK_LABEL(lbl_timer_), buf);
    }

    // Output Button state
    if (btn_output_) {
        if (state.output_on) {
            char b_text[64];
            snprintf(b_text, sizeof(b_text), "⚡  OUTPUT ON  [ %.3fV / %.3fA ]", state.voltage_set, state.current_set);
            gtk_button_set_label(GTK_BUTTON(btn_output_), b_text);
            gtk_widget_remove_css_class(btn_output_, "btn-output-off");
            gtk_widget_add_css_class(btn_output_, "btn-output-on");
        } else {
            gtk_button_set_label(GTK_BUTTON(btn_output_), "⏻  OUTPUT OFF (STANDBY)");
            gtk_widget_remove_css_class(btn_output_, "btn-output-on");
            gtk_widget_add_css_class(btn_output_, "btn-output-off");
        }
    }

    // Feed real-time graph
    graph_->add_sample(state.measured_voltage, state.measured_current, state.measured_power);

    // Feed Data Logger
    logger_->log_sample(state);
}

gboolean AppWindow::on_tick_timer(gpointer user_data) {
    auto* self = static_cast<AppWindow*>(user_data);
    if (!self->window_ || !GTK_IS_WIDGET(self->window_)) {
        return G_SOURCE_REMOVE;
    }

    // 1. Update from device state
    PowerSupplyState st = self->device_.get_state();
    self->update_ui_from_state(st);

    // 2. Advance sequencer
    self->sequencer_->tick(0.05); // 50ms tick
    SequencerProgress sp = self->sequencer_->get_progress();
    if (self->seq_progress_bar_) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->seq_progress_bar_), sp.progress_fraction);
    }
    if (self->seq_status_lbl_) {
        std::string s_text = "Sequence: ";
        if (sp.state == SequencerState::RUNNING) {
            s_text += "RUNNING Step " + std::to_string(sp.current_step_index + 1) + "/" +
                      std::to_string(sp.total_steps) + " (Cycle " + std::to_string(sp.current_cycle) + ")";
        } else if (sp.state == SequencerState::PAUSED) {
            s_text += "PAUSED";
        } else if (sp.state == SequencerState::COMPLETED) {
            s_text += "COMPLETED";
        } else {
            s_text += "IDLE";
        }
        gtk_label_set_text(GTK_LABEL(self->seq_status_lbl_), s_text.c_str());
    }

    // 3. Update Graph Stats
    GraphStats gs = self->graph_->calculate_visible_stats();
    if (self->lbl_stats_v_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "V max: %.2fV  min: %.2fV  avg: %.2fV", gs.v_max, gs.v_min, gs.v_avg);
        gtk_label_set_text(GTK_LABEL(self->lbl_stats_v_), buf);
    }
    if (self->lbl_stats_i_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "I max: %.3fA  min: %.3fA  avg: %.3fA", gs.i_max, gs.i_min, gs.i_avg);
        gtk_label_set_text(GTK_LABEL(self->lbl_stats_i_), buf);
    }
    if (self->lbl_stats_p_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "P peak: %.2fW  avg: %.2fW", gs.p_max, gs.p_avg);
        gtk_label_set_text(GTK_LABEL(self->lbl_stats_p_), buf);
    }

    // 4. Update Logger UI Stats
    if (self->lbl_log_samples_) {
        std::string samp_txt = "Samples Written: " + std::to_string(self->logger_->get_sample_count());
        gtk_label_set_text(GTK_LABEL(self->lbl_log_samples_), samp_txt.c_str());
    }
    if (self->lbl_log_elapsed_) {
        int el = (int)self->logger_->get_elapsed_sec();
        char buf[64];
        snprintf(buf, sizeof(buf), "Elapsed: %02d:%02d:%02d", el / 3600, (el % 3600) / 60, el % 60);
        gtk_label_set_text(GTK_LABEL(self->lbl_log_elapsed_), buf);
    }
    if (self->lbl_log_size_) {
        size_t bytes = self->logger_->get_file_bytes();
        char buf[64];
        if (bytes < 1024 * 1024) {
            snprintf(buf, sizeof(buf), "File Size: %.1f KB", bytes / 1024.0);
        } else {
            snprintf(buf, sizeof(buf), "File Size: %.2f MB", bytes / (1024.0 * 1024.0));
        }
        gtk_label_set_text(GTK_LABEL(self->lbl_log_size_), buf);
    }

    return G_SOURCE_CONTINUE;
}

void AppWindow::on_window_destroy(GtkWidget*, gpointer user_data) {
    auto* self = static_cast<AppWindow*>(user_data);
    if (self->timer_id_ > 0) {
        g_source_remove(self->timer_id_);
        self->timer_id_ = 0;
    }
    self->window_ = nullptr;
    self->device_.set_output(false);
    self->device_.disconnect();
}
