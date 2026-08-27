#include "connection.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "rx_monitor.h"

/* The single port shared by every panel. Opened by Connect, closed by
 * Disconnect, and never handed out except through connection_get_fd(). */
static serial_fd_t active_fd = SERIAL_INVALID;

static GtkDropDown *com_dropdown;
static GtkDropDown *baud_dropdown;

serial_fd_t connection_get_fd(void) {
    return active_fd;
}

/* Reads the selected port and baud strings. Returns FALSE (leaving the
 * outputs untouched) if either dropdown has nothing selected. */
static gboolean get_port_and_baud(const char **port_out, const char **baud_out) {
    GtkStringObject *com_obj  = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(com_dropdown));
    GtkStringObject *baud_obj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(baud_dropdown));

    if (!com_obj || !baud_obj) return FALSE;

    *port_out = gtk_string_object_get_string(com_obj);
    *baud_out = gtk_string_object_get_string(baud_obj);
    return TRUE;
}

/* Every serial device this GUI is ever pointed at, in dropdown order:
 * COM1..COM<MAX_COM_PORT> followed by the usual Linux device nodes.
 * `*default_index` comes back as the position of DEFAULT_PORT_NAME, looked
 * up by name so that lengthening the list cannot silently move it. */
static GtkStringList *build_port_list(guint *default_index) {
    static const char *posix_ports[] = {
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/ttyACM0", "/dev/ttyACM1"
    };

    GtkStringList *list = gtk_string_list_new(NULL);
    guint index = 0;
    guint found = 0;

    for (int i = 1; i <= MAX_COM_PORT; i++) {
        char name[16];
        snprintf(name, sizeof(name), "COM%d", i);
        gtk_string_list_append(list, name);
        if (strcmp(name, DEFAULT_PORT_NAME) == 0) found = index;
        index++;
    }

    for (size_t i = 0; i < G_N_ELEMENTS(posix_ports); i++) {
        gtk_string_list_append(list, posix_ports[i]);
        if (strcmp(posix_ports[i], DEFAULT_PORT_NAME) == 0) found = index;
        index++;
    }

    *default_index = found;
    return list;
}

static GtkStringList *build_baud_list(guint *default_index) {
    static const char *bauds[] = {
        "1200", "2400", "4800", "9600",
        "19200", "38400", "57600", "115200",
        "230400", "460800", "921600"
    };

    GtkStringList *list = gtk_string_list_new(NULL);
    guint found = 0;

    for (size_t i = 0; i < G_N_ELEMENTS(bauds); i++) {
        gtk_string_list_append(list, bauds[i]);
        if (strcmp(bauds[i], DEFAULT_BAUD_NAME) == 0) found = (guint)i;
    }

    *default_index = found;
    return list;
}

static void on_connect_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    const char *port, *baud;
    if (!get_port_and_baud(&port, &baud)) {
        g_printerr("No COM port or baud rate selected.\n");
        return;
    }

    /* Drop any earlier connection first: the port is opened without
     * sharing, so a stale handle would block the new one. */
    if (!serial_is_invalid(active_fd)) {
        g_print("Closing existing connection...\n");
        serial_close(active_fd);
        active_fd = SERIAL_INVALID;
    }

    g_print("Connecting on %s at %s baud...\n", port, baud);

    active_fd = serial_open(port, baud);
    if (serial_is_invalid(active_fd)) {
        g_printerr("Warning: could not open %s - check the port.\n", port);
        return;
    }

    g_print("Port %s opened successfully and listening.\n", port);
    rx_monitor_start(active_fd);
}

static void on_disconnect_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    if (!serial_is_invalid(active_fd)) {
        g_print("Existing connection found. Closing...\n");
        serial_close(active_fd);
        active_fd = SERIAL_INVALID;
        g_print("Closed.\n");
        return;
    }
    g_print("No connection to close.\n");
}

void connection_build_row(GtkWidget *parent) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(parent), row);

    GtkWidget *com_label = gtk_label_new("COM Port:");
    gtk_widget_set_halign(com_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), com_label);

    guint com_default;
    GtkStringList *com_list = build_port_list(&com_default);
    GtkWidget *com_widget = gtk_drop_down_new(G_LIST_MODEL(com_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(com_widget), com_default);
    gtk_box_append(GTK_BOX(row), com_widget);
    com_dropdown = GTK_DROP_DOWN(com_widget);

    GtkWidget *baud_label = gtk_label_new("Baud Rate:");
    gtk_widget_set_halign(baud_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(baud_label, 8);
    gtk_box_append(GTK_BOX(row), baud_label);

    guint baud_default;
    GtkStringList *baud_list = build_baud_list(&baud_default);
    GtkWidget *baud_widget = gtk_drop_down_new(G_LIST_MODEL(baud_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(baud_widget), baud_default);
    gtk_box_append(GTK_BOX(row), baud_widget);
    baud_dropdown = GTK_DROP_DOWN(baud_widget);

    GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
    GtkWidget *disconnect_btn = gtk_button_new_with_label("Disconnect");
    gtk_widget_set_margin_start(connect_btn, 8);
    gtk_widget_set_margin_start(disconnect_btn, 8);

    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_clicked), NULL);
    g_signal_connect(disconnect_btn, "clicked", G_CALLBACK(on_disconnect_clicked), NULL);

    gtk_box_append(GTK_BOX(row), connect_btn);
    gtk_box_append(GTK_BOX(row), disconnect_btn);
}
