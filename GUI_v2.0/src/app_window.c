#include "app_window.h"

#include "channels.h"
#include "connection.h"
#include "rx_monitor.h"
#include "spi_panel.h"

/* The channel toggles set their widget name to btn-on / btn-off, and the
 * maxima lock sets btn-locked; this is what gives those names a visible
 * effect. */
static const char *APP_CSS =
    "#btn-on     { background-image: none; background-color: #2e7d32; color: #ffffff; }\n"
    "#btn-off    { background-image: none; background-color: #b71c1c; color: #ffffff; }\n"
    "#btn-locked { background-image: none; background-color: #f9a825; color: #000000; }\n";

static void install_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();

#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, APP_CSS);
#else
    gtk_css_provider_load_from_data(provider, APP_CSS, -1);
#endif

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

void app_window_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    install_css();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Reference Voltage Generator");
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_window_set_child(GTK_WINDOW(window), scroll);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), vbox);

    /* Order here is the order the sections appear down the window. */
    connection_build_row(vbox);
    channels_build(vbox);
    spi_panel_build(vbox);
    rx_monitor_build_section(vbox);

    gtk_window_set_default_size(GTK_WINDOW(window), 820, 950);
    gtk_window_present(GTK_WINDOW(window));
}
