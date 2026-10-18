#pragma once

#include <gtk/gtk.h>
#include <deque>
#include <chrono>
#include <mutex>
#include <vector>
#include <string>

struct DataPoint {
    double timestamp_sec; // Relative seconds from start or absolute
    double voltage;
    double current;
    double power;
};

struct GraphStats {
    double v_min = 0.0, v_max = 0.0, v_avg = 0.0;
    double i_min = 0.0, i_max = 0.0, i_avg = 0.0;
    double p_min = 0.0, p_max = 0.0, p_avg = 0.0;
};

class GraphWidget {
public:
    GraphWidget();
    ~GraphWidget();

    GtkWidget* get_widget() const;

    void add_sample(double voltage, double current, double power);
    void clear();
    void set_paused(bool paused);
    bool is_paused() const;

    void set_time_window(double seconds);
    double get_time_window() const;

    void set_auto_scale(bool auto_scale);
    bool get_auto_scale() const;

    void set_show_voltage(bool show);
    void set_show_current(bool show);
    void set_show_power(bool show);

    bool get_show_voltage() const { return show_voltage_; }
    bool get_show_current() const { return show_current_; }
    bool get_show_power() const { return show_power_; }

    GraphStats calculate_visible_stats() const;

    bool export_csv(const std::string& filepath) const;
    bool export_png(const std::string& filepath) const;

private:
    static void draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
    static void on_motion(GtkEventControllerMotion* controller, double x, double y, gpointer user_data);
    static void on_leave(GtkEventControllerMotion* controller, gpointer user_data);

    void draw(cairo_t* cr, int width, int height);

    GtkWidget* drawing_area_ = nullptr;
    mutable std::mutex data_mutex_;

    std::deque<DataPoint> data_points_;
    std::chrono::steady_clock::time_point start_time_;

    bool paused_ = false;
    double time_window_sec_ = 30.0;
    bool auto_scale_ = true;

    bool show_voltage_ = true;
    bool show_current_ = true;
    bool show_power_ = true;

    // Hover cursor state
    bool hovering_ = false;
    double hover_x_ = 0.0;
    double hover_y_ = 0.0;

    static constexpr size_t MAX_POINTS = 50000;
};
