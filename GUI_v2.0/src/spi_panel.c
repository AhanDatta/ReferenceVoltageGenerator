#include "spi_panel.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "connection.h"
#include "protocol.h"

/* How many bytes the panel can stage and send in one frame. */
#define SPI_BYTE_COUNT 3

static GtkDropDown *speed_dropdown;
static GtkDropDown *bit_order_dropdown;
static GtkDropDown *mode_dropdown;

static GtkWidget *byte_entries[SPI_BYTE_COUNT];
static GtkWidget *byte_radios[SPI_BYTE_COUNT];

/* ---- Settings ---------------------------------------------------------
 *
 * The bus settings below are decoded but are NOT part of the wire format
 * yet: the firmware still uses its own compiled-in SPI configuration. They
 * are logged on every send so the intended settings stay visible while the
 * firmware side is being finished.
 */

/* SPI clock rates offered in the dropdown, in hertz. The labels are
 * generated from these, so adding a rate here is all that is needed. */
static const uint32_t SPI_SPEEDS_HZ[] = {
    1000000
};

static const char *selected_string(GtkDropDown *dropdown) {
    GtkStringObject *obj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(dropdown));
    return obj ? gtk_string_object_get_string(obj) : "";
}

/* Renders a clock rate in whichever unit keeps it short: "1 MHz" rather
 * than "1000000", "500 kHz" rather than "500000". %g drops the trailing
 * zeros, so exact rates stay free of a misleading decimal tail. */
static void spi_format_frequency(uint32_t hz, char *buf, size_t len) {
    double      value;
    const char *unit;

    if (hz >= 1000000u) {
        value = hz / 1000000.0;
        unit  = "MHz";
    } else if (hz >= 1000u) {
        value = hz / 1000.0;
        unit  = "kHz";
    } else {
        value = hz;
        unit  = "Hz";
    }

    snprintf(buf, len, "%g %s", value, unit);
}

/* The rate behind the selected label. The dropdown and SPI_SPEEDS_HZ are
 * built together, so the selected position indexes straight into it. */
static uint32_t selected_speed_hz(void) {
    guint i = gtk_drop_down_get_selected(speed_dropdown);
    if (i == GTK_INVALID_LIST_POSITION || i >= G_N_ELEMENTS(SPI_SPEEDS_HZ))
        return SPI_SPEEDS_HZ[0];
    return SPI_SPEEDS_HZ[i];
}

/* Mode byte values come from the Arduino SPI library:
 * https://github.com/arduino/ArduinoCore-avr/blob/master/libraries/SPI/src/SPI.h */
static uint8_t mode_byte_from_name(const char *mode) {
    if (strcmp(mode, "SPI_MODE0") == 0) return 0x00;
    if (strcmp(mode, "SPI_MODE1") == 0) return 0x04;
    if (strcmp(mode, "SPI_MODE2") == 0) return 0x08;
    return 0x0C; /* SPI_MODE3 */
}

static void log_spi_settings(void) {
    const char *order_str = selected_string(bit_order_dropdown);
    const char *mode_str  = selected_string(mode_dropdown);

    uint32_t speed = selected_speed_hz();
    char speed_label[32];
    spi_format_frequency(speed, speed_label, sizeof(speed_label));

    /* Arduino defines MSBFIRST as 1 and LSBFIRST as 0. */
    uint8_t bit_order = (strcmp(order_str, "LSBFIRST") == 0) ? 0 : 1;

    g_print("SPI settings (not yet transmitted): %s (%u Hz), %s (%u), %s (0x%02X)\n",
            speed_label, speed, order_str, bit_order,
            mode_str, mode_byte_from_name(mode_str));
}

/* ---- Callbacks ------------------------------------------------------- */

/* Number of staged bytes the selected radio button asks for. */
static int selected_byte_count(void) {
    for (int i = 0; i < SPI_BYTE_COUNT; i++) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(byte_radios[i])))
            return i + 1;
    }
    return 0;
}

static void on_send_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    int count = selected_byte_count();

    uint8_t bytes[SPI_BYTE_COUNT];
    for (int i = 0; i < count; i++) {
        const char *hex_val = gtk_editable_get_text(GTK_EDITABLE(byte_entries[i]));
        long val = strtol(hex_val, NULL, 16); /* parses "A5", "0xA5", "3F", ... */
        bytes[i] = (uint8_t)(val & 0xFF);
    }

    log_spi_settings();
    protocol_send_spi(connection_get_fd(), bytes, count);
}

static void on_reset_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;
    protocol_send_reset(connection_get_fd());
}

/* Keeps the byte entries to hex digits only. Editing keys and anything
 * that is not a text character pass straight through. */
static gboolean on_byte_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                    guint keycode, GdkModifierType state,
                                    gpointer user_data) {
    (void)keycode; (void)state; (void)user_data;

    GtkWidget *entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));

    if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete ||
        keyval == GDK_KEY_Left      || keyval == GDK_KEY_Right  ||
        keyval == GDK_KEY_Tab       || keyval == GDK_KEY_Return ||
        keyval == GDK_KEY_KP_Enter) {
        return GDK_EVENT_PROPAGATE; /* let GTK handle standard text actions */
    }

    /* 0 means a non-text key: a bare Ctrl, Alt, Shift or function key. */
    guint32 key_char = gdk_keyval_to_unicode(keyval);
    if (key_char == 0) return GDK_EVENT_PROPAGATE;

    if (!isxdigit((unsigned char)key_char)) {
        gtk_widget_error_bell(entry);
        return GDK_EVENT_STOP; /* block the invalid character */
    }

    return GDK_EVENT_PROPAGATE;
}

/* ---- Construction ---------------------------------------------------- */

static GtkWidget *make_dropdown(const char *const *items, GtkDropDown **out) {
    GtkStringList *list = gtk_string_list_new(items);
    GtkWidget *widget = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
    *out = GTK_DROP_DOWN(widget);
    return widget;
}

/* Labels come from SPI_SPEEDS_HZ, so the dropdown reads "1 MHz" while the
 * code keeps working in hertz. */
static GtkWidget *build_speed_dropdown(void) {
    GtkStringList *list = gtk_string_list_new(NULL);

    for (size_t i = 0; i < G_N_ELEMENTS(SPI_SPEEDS_HZ); i++) {
        char label[32];
        spi_format_frequency(SPI_SPEEDS_HZ[i], label, sizeof(label));
        gtk_string_list_append(list, label);
    }

    GtkWidget *widget = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
    speed_dropdown = GTK_DROP_DOWN(widget);
    return widget;
}

static void build_settings_bar(GtkWidget *parent) {
    static const char *bit_orders[] = { "MSBFIRST", "LSBFIRST", NULL };
    static const char *modes[]      = { "SPI_MODE0", "SPI_MODE1",
                                        "SPI_MODE2", "SPI_MODE3", NULL };

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_widget_set_halign(bar, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(bar), gtk_label_new("Speed"));
    gtk_box_append(GTK_BOX(bar), build_speed_dropdown());

    gtk_box_append(GTK_BOX(bar), gtk_label_new("MSB or LSB"));
    gtk_box_append(GTK_BOX(bar), make_dropdown(bit_orders, &bit_order_dropdown));

    gtk_box_append(GTK_BOX(bar), gtk_label_new("Mode"));
    gtk_box_append(GTK_BOX(bar), make_dropdown(modes, &mode_dropdown));

    /* Writes the Reset pin, which is a plain GPIO on the microcontroller. */
    GtkWidget *reset_btn = gtk_button_new_with_label("RESET");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);
    gtk_box_append(GTK_BOX(bar), reset_btn);

    gtk_box_append(GTK_BOX(parent), bar);
}

static void build_byte_grid(GtkWidget *parent) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(parent), grid);

    for (int i = 0; i < SPI_BYTE_COUNT; i++) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Byte %d", i);
        GtkWidget *label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(grid), label, i, 0, 1, 1);

        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(entry), 2); /* two hex digits */
        gtk_widget_set_tooltip_text(entry, "Two hex digits, e.g. A5");
        gtk_widget_set_hexpand(entry, TRUE);

        GtkEventController *keys = gtk_event_controller_key_new();
        g_signal_connect(keys, "key-pressed", G_CALLBACK(on_byte_key_pressed), NULL);
        gtk_widget_add_controller(entry, keys);

        gtk_grid_attach(GTK_GRID(grid), entry, i, 1, 1, 1);
        byte_entries[i] = entry;
    }
}

static void build_send_row(GtkWidget *parent) {
    static const char *radio_labels[SPI_BYTE_COUNT] = {
        "Byte 0", "Bytes 0-1", "Bytes 0-2"
    };

    GtkWidget *prompt = gtk_label_new("Select which bytes to send.");
    gtk_widget_set_margin_top(prompt, 12);
    gtk_box_append(GTK_BOX(parent), prompt);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(parent), row);

    /* Check buttons behave as radio buttons once grouped. */
    for (int i = 0; i < SPI_BYTE_COUNT; i++) {
        byte_radios[i] = gtk_check_button_new_with_label(radio_labels[i]);
        if (i > 0)
            gtk_check_button_set_group(GTK_CHECK_BUTTON(byte_radios[i]),
                                       GTK_CHECK_BUTTON(byte_radios[0]));
        gtk_box_append(GTK_BOX(row), byte_radios[i]);
    }
    /* Start on a real selection so SEND cannot ship an empty frame. */
    gtk_check_button_set_active(GTK_CHECK_BUTTON(byte_radios[0]), TRUE);

    GtkWidget *send_btn = gtk_button_new_with_label("SEND");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_send_clicked), NULL);
    gtk_box_append(GTK_BOX(row), send_btn);
}

void spi_panel_build(GtkWidget *parent) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 8);
    gtk_widget_set_margin_bottom(sep, 8);
    gtk_box_append(GTK_BOX(parent), sep);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>SPI Communication</b>");
    gtk_box_append(GTK_BOX(parent), title);

    build_settings_bar(parent);
    build_byte_grid(parent);
    build_send_row(parent);
}
