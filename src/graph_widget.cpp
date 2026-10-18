#include "graph_widget.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

GraphWidget::GraphWidget() {
    start_time_ = std::chrono::steady_clock::now();

    drawing_area_ = gtk_drawing_area_new();
    gtk_widget_set_hexpand(drawing_area_, TRUE);
    gtk_widget_set_vexpand(drawing_area_, TRUE);
    gtk_widget_set_size_request(drawing_area_, 400, 260);

    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(drawing_area_),
        &GraphWidget::draw_func,
        this,
        nullptr
    );

    // Mouse motion controller for interactive hover crosshair
    GtkEventController* motion_ctrl = gtk_event_controller_motion_new();
    g_signal_connect(motion_ctrl, "motion", G_CALLBACK(&GraphWidget::on_motion), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(&GraphWidget::on_leave), this);
    gtk_widget_add_controller(drawing_area_, motion_ctrl);
}

GraphWidget::~GraphWidget() {}

GtkWidget* GraphWidget::get_widget() const {
    return drawing_area_;
}

void GraphWidget::add_sample(double voltage, double current, double power) {
    if (paused_) return;

    auto now = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(now - start_time_).count();

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data_points_.push_back({t, voltage, current, power});

        if (data_points_.size() > MAX_POINTS) {
            data_points_.pop_front();
        }
    }

    if (drawing_area_ && gtk_widget_get_visible(drawing_area_)) {
        gtk_widget_queue_draw(drawing_area_);
    }
}

void GraphWidget::clear() {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data_points_.clear();
        start_time_ = std::chrono::steady_clock::now();
    }
    if (drawing_area_) {
        gtk_widget_queue_draw(drawing_area_);
    }
}

void GraphWidget::set_paused(bool paused) {
    paused_ = paused;
}

bool GraphWidget::is_paused() const {
    return paused_;
}

void GraphWidget::set_time_window(double seconds) {
    time_window_sec_ = std::max(2.0, seconds);
    if (drawing_area_) {
        gtk_widget_queue_draw(drawing_area_);
    }
}

double GraphWidget::get_time_window() const {
    return time_window_sec_;
}

void GraphWidget::set_auto_scale(bool auto_scale) {
    auto_scale_ = auto_scale;
    if (drawing_area_) {
        gtk_widget_queue_draw(drawing_area_);
    }
}

bool GraphWidget::get_auto_scale() const {
    return auto_scale_;
}

void GraphWidget::set_show_voltage(bool show) {
    show_voltage_ = show;
    if (drawing_area_) gtk_widget_queue_draw(drawing_area_);
}

void GraphWidget::set_show_current(bool show) {
    show_current_ = show;
    if (drawing_area_) gtk_widget_queue_draw(drawing_area_);
}

void GraphWidget::set_show_power(bool show) {
    show_power_ = show;
    if (drawing_area_) gtk_widget_queue_draw(drawing_area_);
}

GraphStats GraphWidget::calculate_visible_stats() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    GraphStats stats;
    if (data_points_.empty()) return stats;

    double latest_t = data_points_.back().timestamp_sec;
    double t_start = latest_t - time_window_sec_;

    bool first = true;
    size_t count = 0;
    double sum_v = 0.0, sum_i = 0.0, sum_p = 0.0;

    for (auto it = data_points_.rbegin(); it != data_points_.rend(); ++it) {
        if (it->timestamp_sec < t_start) break;

        if (first) {
            stats.v_min = stats.v_max = it->voltage;
            stats.i_min = stats.i_max = it->current;
            stats.p_min = stats.p_max = it->power;
            first = false;
        } else {
            stats.v_min = std::min(stats.v_min, it->voltage);
            stats.v_max = std::max(stats.v_max, it->voltage);
            stats.i_min = std::min(stats.i_min, it->current);
            stats.i_max = std::max(stats.i_max, it->current);
            stats.p_min = std::min(stats.p_min, it->power);
            stats.p_max = std::max(stats.p_max, it->power);
        }

        sum_v += it->voltage;
        sum_i += it->current;
        sum_p += it->power;
        count++;
    }

    if (count > 0) {
        stats.v_avg = sum_v / count;
        stats.i_avg = sum_i / count;
        stats.p_avg = sum_p / count;
    }

    return stats;
}

bool GraphWidget::export_csv(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "Time_sec,Voltage_V,Current_A,Power_W\n";
    for (const auto& pt : data_points_) {
        file << std::fixed << std::setprecision(4)
             << pt.timestamp_sec << ","
             << pt.voltage << ","
             << pt.current << ","
             << pt.power << "\n";
    }
    return true;
}

bool GraphWidget::export_png(const std::string& filepath) const {
    int w = gtk_widget_get_width(drawing_area_);
    int h = gtk_widget_get_height(drawing_area_);
    if (w <= 0 || h <= 0) {
        w = 800; h = 500;
    }

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);

    const_cast<GraphWidget*>(this)->draw(cr, w, h);

    cairo_status_t status = cairo_surface_write_to_png(surface, filepath.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return status == CAIRO_STATUS_SUCCESS;
}

void GraphWidget::draw_func(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer user_data) {
    auto* self = static_cast<GraphWidget*>(user_data);
    self->draw(cr, width, height);
}

void GraphWidget::on_motion(GtkEventControllerMotion*, double x, double y, gpointer user_data) {
    auto* self = static_cast<GraphWidget*>(user_data);
    self->hovering_ = true;
    self->hover_x_ = x;
    self->hover_y_ = y;
    gtk_widget_queue_draw(self->drawing_area_);
}

void GraphWidget::on_leave(GtkEventControllerMotion*, gpointer user_data) {
    auto* self = static_cast<GraphWidget*>(user_data);
    self->hovering_ = false;
    gtk_widget_queue_draw(self->drawing_area_);
}

void GraphWidget::draw(cairo_t* cr, int width, int height) {
    // Background: Dark GNOME palette #1e1e24
    cairo_set_source_rgb(cr, 0.118, 0.118, 0.141);
    cairo_paint(cr);

    const double pad_left = 55.0;
    const double pad_right = 50.0;
    const double pad_top = 34.0;
    const double pad_bottom = 32.0;

    const double plot_x = pad_left;
    const double plot_y = pad_top;
    const double plot_w = std::max(20.0, (double)width - pad_left - pad_right);
    const double plot_h = std::max(20.0, (double)height - pad_top - pad_bottom);

    // Plot area background
    cairo_set_source_rgb(cr, 0.086, 0.086, 0.106);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_fill(cr);

    std::vector<DataPoint> visible_points;
    double t_now = 0.0;
    double max_v_seen = 1.0;
    double max_i_seen = 0.5;
    double max_p_seen = 2.0;

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (!data_points_.empty()) {
            t_now = data_points_.back().timestamp_sec;
            double t_min = t_now - time_window_sec_;

            for (auto it = data_points_.rbegin(); it != data_points_.rend(); ++it) {
                visible_points.push_back(*it);
                max_v_seen = std::max(max_v_seen, it->voltage);
                max_i_seen = std::max(max_i_seen, it->current);
                max_p_seen = std::max(max_p_seen, it->power);
                if (it->timestamp_sec < t_min) break;
            }
            std::reverse(visible_points.begin(), visible_points.end());
        }
    }

    // Determine scale limits
    double v_limit = 32.0;
    double i_limit = 10.0;
    if (auto_scale_) {
        v_limit = std::max(2.0, std::ceil(max_v_seen * 1.25));
        i_limit = std::max(0.5, std::ceil(max_i_seen * 10.0 * 1.25) / 10.0);
    }

    // Draw Grid Lines (Horizontal & Vertical)
    cairo_set_line_width(cr, 1.0);
    int num_h_divisions = 5;
    for (int d = 0; d <= num_h_divisions; ++d) {
        double y = plot_y + plot_h - (d * plot_h / num_h_divisions);

        // Grid line
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, (d == 0 || d == num_h_divisions) ? 0.18 : 0.07);
        cairo_move_to(cr, plot_x, y);
        cairo_line_to(cr, plot_x + plot_w, y);
        cairo_stroke(cr);

        // Left axis label: Voltage
        double v_val = (v_limit / num_h_divisions) * d;
        char v_label[32];
        snprintf(v_label, sizeof(v_label), "%.1fV", v_val);
        cairo_set_source_rgba(cr, 0.40, 0.65, 0.95, 0.85); // Blue
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, 8.0, y + 4.0);
        cairo_show_text(cr, v_label);

        // Right axis label: Current
        double i_val = (i_limit / num_h_divisions) * d;
        char i_label[32];
        snprintf(i_label, sizeof(i_label), "%.2fA", i_val);
        cairo_set_source_rgba(cr, 0.95, 0.80, 0.25, 0.85); // Amber
        cairo_move_to(cr, plot_x + plot_w + 6.0, y + 4.0);
        cairo_show_text(cr, i_label);
    }

    // Vertical time grid lines
    int num_t_divisions = 6;
    for (int d = 0; d <= num_t_divisions; ++d) {
        double x = plot_x + (d * plot_w / num_t_divisions);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
        cairo_move_to(cr, x, plot_y);
        cairo_line_to(cr, x, plot_y + plot_h);
        cairo_stroke(cr);

        // Time label
        double t_offset = -time_window_sec_ + (d * time_window_sec_ / num_t_divisions);
        char t_label[32];
        if (std::abs(t_offset) < 0.1) {
            snprintf(t_label, sizeof(t_label), "Now");
        } else {
            snprintf(t_label, sizeof(t_label), "%.0fs", t_offset);
        }
        cairo_set_source_rgba(cr, 0.80, 0.80, 0.85, 0.65);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, x - 12.0, plot_y + plot_h + 18.0);
        cairo_show_text(cr, t_label);
    }

    // Helper lambdas to map data points to screen coordinates
    auto map_x = [&](double t) -> double {
        double dt = t_now - t;
        double norm = 1.0 - (dt / time_window_sec_);
        return plot_x + norm * plot_w;
    };
    auto map_y_v = [&](double v) -> double {
        double norm = std::clamp(v / v_limit, 0.0, 1.1);
        return plot_y + plot_h - (norm * plot_h);
    };
    auto map_y_i = [&](double i) -> double {
        double norm = std::clamp(i / i_limit, 0.0, 1.1);
        return plot_y + plot_h - (norm * plot_h);
    };
    auto map_y_p = [&](double p) -> double {
        // Power mapped to left axis (scaled to v_limit * i_limit or appropriate max)
        double p_max = v_limit * 1.5;
        double norm = std::clamp(p / p_max, 0.0, 1.1);
        return plot_y + plot_h - (norm * plot_h);
    };

    // Clip to plot area for waveform drawing
    cairo_save(cr);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_clip(cr);

    // Function to render a filled line trace
    auto draw_trace = [&](auto map_y, double r, double g, double b, double stroke_w, bool with_fill) {
        if (visible_points.size() < 2) return;

        // Path for the curve
        cairo_new_path(cr);
        bool first = true;
        for (const auto& pt : visible_points) {
            double px = map_x(pt.timestamp_sec);
            double py = map_y(pt);
            if (first) {
                cairo_move_to(cr, px, py);
                first = false;
            } else {
                cairo_line_to(cr, px, py);
            }
        }

        if (with_fill) {
            // Fill polygon down to baseline
            cairo_save(cr);
            double last_x = map_x(visible_points.back().timestamp_sec);
            double first_x = map_x(visible_points.front().timestamp_sec);
            cairo_line_to(cr, last_x, plot_y + plot_h);
            cairo_line_to(cr, first_x, plot_y + plot_h);
            cairo_close_path(cr);

            cairo_pattern_t* pat = cairo_pattern_create_linear(0, plot_y, 0, plot_y + plot_h);
            cairo_pattern_add_color_stop_rgba(pat, 0.0, r, g, b, 0.22);
            cairo_pattern_add_color_stop_rgba(pat, 1.0, r, g, b, 0.02);
            cairo_set_source(cr, pat);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);
            cairo_restore(cr);
        }

        // Stroke line
        cairo_set_source_rgba(cr, r, g, b, 0.95);
        cairo_set_line_width(cr, stroke_w);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        first = true;
        for (const auto& pt : visible_points) {
            double px = map_x(pt.timestamp_sec);
            double py = map_y(pt);
            if (first) {
                cairo_move_to(cr, px, py);
                first = false;
            } else {
                cairo_line_to(cr, px, py);
            }
        }
        cairo_stroke(cr);
    };

    // 1. Draw Power Trace (Neon Green) if enabled
    if (show_power_) {
        draw_trace([&](const DataPoint& d) { return map_y_p(d.power); },
                   0.20, 0.82, 0.48, 1.8, true);
    }

    // 2. Draw Voltage Trace (Cyan Blue) if enabled
    if (show_voltage_) {
        draw_trace([&](const DataPoint& d) { return map_y_v(d.voltage); },
                   0.21, 0.52, 0.90, 2.4, true);
    }

    // 3. Draw Current Trace (Vibrant Amber) if enabled
    if (show_current_) {
        draw_trace([&](const DataPoint& d) { return map_y_i(d.current); },
                   0.97, 0.83, 0.18, 2.4, true);
    }

    // Interactive Hover Crosshair
    const DataPoint* hovered_pt = nullptr;
    if (hovering_ && hover_x_ >= plot_x && hover_x_ <= plot_x + plot_w &&
        hover_y_ >= plot_y && hover_y_ <= plot_y + plot_h && !visible_points.empty()) {

        double hover_t = t_now - (1.0 - (hover_x_ - plot_x) / plot_w) * time_window_sec_;

        // Find closest point
        double min_dist = 1e9;
        for (const auto& pt : visible_points) {
            double d = std::abs(pt.timestamp_sec - hover_t);
            if (d < min_dist) {
                min_dist = d;
                hovered_pt = &pt;
            }
        }

        if (hovered_pt) {
            double hx = map_x(hovered_pt->timestamp_sec);

            // Vertical crosshair dashed line
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.65);
            cairo_set_line_width(cr, 1.0);
            double dashes[] = {4.0, 4.0};
            cairo_set_dash(cr, dashes, 2, 0.0);
            cairo_move_to(cr, hx, plot_y);
            cairo_line_to(cr, hx, plot_y + plot_h);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0); // reset dash

            // Highlight dots on each active curve
            auto draw_dot = [&](double y, double r, double g, double b) {
                cairo_set_source_rgba(cr, r, g, b, 1.0);
                cairo_arc(cr, hx, y, 4.5, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                cairo_arc(cr, hx, y, 2.0, 0, 2 * M_PI);
                cairo_fill(cr);
            };

            if (show_voltage_) draw_dot(map_y_v(hovered_pt->voltage), 0.21, 0.52, 0.90);
            if (show_current_) draw_dot(map_y_i(hovered_pt->current), 0.97, 0.83, 0.18);
            if (show_power_) draw_dot(map_y_p(hovered_pt->power), 0.20, 0.82, 0.48);
        }
    }

    cairo_restore(cr); // restore clip

    // Border around plot area
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_stroke(cr);

    // Top Legend & Status Header
    double leg_x = plot_x + 8.0;
    double leg_y = 22.0;

    auto draw_legend_item = [&](const char* label, double r, double g, double b, bool active) {
        cairo_set_source_rgba(cr, r, g, b, active ? 1.0 : 0.35);
        cairo_rectangle(cr, leg_x, leg_y - 8.0, 10.0, 10.0);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.9, 0.9, 0.95, active ? 0.95 : 0.4);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, leg_x + 14.0, leg_y);
        cairo_show_text(cr, label);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        leg_x += ext.x_advance + 28.0;
    };

    draw_legend_item("Voltage (V)", 0.21, 0.52, 0.90, show_voltage_);
    draw_legend_item("Current (A)", 0.97, 0.83, 0.18, show_current_);
    draw_legend_item("Power (W)", 0.20, 0.82, 0.48, show_power_);

    // Hover Tooltip / Floating HUD Badge
    if (hovered_pt) {
        char hud_text[128];
        double rel_t = hovered_pt->timestamp_sec - t_now;
        snprintf(hud_text, sizeof(hud_text), "t: %.1fs  |  %.3f V  |  %.3f A  |  %.2f W",
                 rel_t, hovered_pt->voltage, hovered_pt->current, hovered_pt->power);

        cairo_text_extents_t ext;
        cairo_set_font_size(cr, 12.0);
        cairo_text_extents(cr, hud_text, &ext);

        double badge_w = ext.width + 20.0;
        double badge_h = 24.0;
        double badge_x = std::clamp(hover_x_ - badge_w / 2.0, plot_x + 5.0, plot_x + plot_w - badge_w - 5.0);
        double badge_y = plot_y + 10.0;

        // Badge pill background
        cairo_set_source_rgba(cr, 0.15, 0.15, 0.20, 0.92);
        cairo_rectangle(cr, badge_x, badge_y, badge_w, badge_h);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.4, 0.6, 0.9, 0.7);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, badge_x, badge_y, badge_w, badge_h);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, badge_x + 10.0, badge_y + 16.0);
        cairo_show_text(cr, hud_text);
    }
}
