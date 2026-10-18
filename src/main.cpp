#include <adwaita.h>
#include "app_window.hpp"
#include <memory>

static std::unique_ptr<AppWindow> g_app_window;

static void on_activate(GtkApplication* app, gpointer) {
    if (!g_app_window) {
        g_app_window = std::make_unique<AppWindow>(ADW_APPLICATION(app));
    }
    g_app_window->present();
}

int main(int argc, char* argv[]) {
    g_autoptr(AdwApplication) app = adw_application_new(
        "org.kiprim.dc310s",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_app_window.reset();
    return status;
}
