#include "channels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "connection.h"
#include "protocol.h"

/*
 * One row of the channel grid.
 *
 * Each channel carries its own ceiling. VOLTAGE_FULL_SCALE is the hardware
 * limit and never changes; `user_max` is a per-channel ceiling the operator
 * can lower to protect whatever is wired to that output. Every path that
 * can change the target voltage clamps against `user_max`, so the ceiling
 * holds whether the value was typed, stepped, or sent in bulk.
 */
typedef struct {
    int              ch;
    GtkWidget       *entry;     /* target voltage */
    GtkWidget       *max_entry; /* ceiling for this channel */
    GtkToggleButton *off_btn;
    double           user_max;
} Channel;

static Channel channels[TOTAL_DACS];

/*
 * While the maxima are locked, every route that could change a ceiling is
 * shut off: the per-channel entries stop accepting input, and the bulk
 * "set all" controls go insensitive. The target voltages stay editable --
 * the lock guards the limits, not the values being sent.
 */
static gboolean  maxima_locked = FALSE;
static GtkWidget *max_all_entry;
static GtkWidget *max_all_button;
static GtkWidget *lock_button;

/* ---- Value helpers --------------------------------------------------- */

static double snap(double value) {
    return round(value / VOLTAGE_STEP) * VOLTAGE_STEP;
}

/* Snaps to the entry resolution and clamps into [VOLTAGE_MIN, max_v]. */
static double snap_and_clamp(double value, double max_v) {
    value = snap(value);
    if (value < VOLTAGE_MIN) value = VOLTAGE_MIN;
    if (value > max_v)       value = max_v;
    return value;
}

static void write_value(GtkWidget *entry, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", value);
    gtk_editable_set_text(GTK_EDITABLE(entry), buf);
}

static double read_value(GtkWidget *entry) {
    return atof(gtk_editable_get_text(GTK_EDITABLE(entry)));
}

/* Normalises the channel target voltage against its ceiling, rewrites the
 * entry so the user sees exactly what will be sent, and returns it. */
static double apply_voltage(Channel *c, double value) {
    double v = snap_and_clamp(value, c->user_max);
    write_value(c->entry, v);
    return v;
}

static void refresh_max_tooltip(Channel *c) {
    char tip[128];
    snprintf(tip, sizeof(tip),
             "Channel %d target voltage (0 - %.4f V)", c->ch, c->user_max);
    gtk_widget_set_tooltip_text(c->entry, tip);
}

/* Accepts a new ceiling for one channel. A user maximum may only ever be
 * lower than the hardware full scale, so whatever was typed is clamped to
 * [VOLTAGE_MIN, VOLTAGE_FULL_SCALE] before it takes effect. */
static void apply_user_max(Channel *c, double value) {
    c->user_max = snap_and_clamp(value, VOLTAGE_FULL_SCALE);
    write_value(c->max_entry, c->user_max);
    refresh_max_tooltip(c);

    /* Lowering the ceiling under the current target pulls the target down
     * with it, so the displayed value is never one the channel refuses. */
    if (read_value(c->entry) > c->user_max)
        apply_voltage(c, c->user_max);
}

/* ---- Per-channel callbacks ------------------------------------------- */

static void on_voltage_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    Channel *c = (Channel *)user_data;
    apply_voltage(c, read_value(c->entry));
}

static void on_voltage_focus_leave(GtkEventController *controller, gpointer user_data) {
    (void)controller;
    Channel *c = (Channel *)user_data;
    apply_voltage(c, read_value(c->entry));
}

static void on_up_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    Channel *c = (Channel *)user_data;
    apply_voltage(c, read_value(c->entry) + VOLTAGE_ARROW_STEP);
}

static void on_down_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    Channel *c = (Channel *)user_data;
    apply_voltage(c, read_value(c->entry) - VOLTAGE_ARROW_STEP);
}

static void on_max_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    if (maxima_locked) return;
    Channel *c = (Channel *)user_data;
    apply_user_max(c, read_value(c->max_entry));
}

static void on_max_focus_leave(GtkEventController *controller, gpointer user_data) {
    (void)controller;
    if (maxima_locked) return;
    Channel *c = (Channel *)user_data;
    apply_user_max(c, read_value(c->max_entry));
}

/* Keeps an off/on toggle label and CSS name in step with its state. */
static void set_off_button_visual(GtkWidget *btn, gboolean is_on) {
    gtk_button_set_label(GTK_BUTTON(btn), is_on ? "ON" : "OFF");
    gtk_widget_set_name(btn, is_on ? "btn-on" : "btn-off");
}

static void on_off_clicked(GtkToggleButton *button, gpointer user_data) {
    Channel *c = (Channel *)user_data;

    gboolean is_on = gtk_toggle_button_get_active(button);
    set_off_button_visual(GTK_WIDGET(button), is_on);

    protocol_send_voltage(connection_get_fd(), REG_CONFIG, c->ch,
                          is_on ? DAC_POWER_ON : DAC_POWER_OFF);
}

static void on_update_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    Channel *c = (Channel *)user_data;

    /* Re-apply before sending: the value may have been typed without the
     * entry ever losing focus, so this is the last point at which the
     * channel ceiling can still be enforced. */
    double v = apply_voltage(c, read_value(c->entry));
    protocol_send_voltage(connection_get_fd(), REG_DAC, c->ch, voltage_to_code(v));
}

/* ---- Locking and bulk maxima ----------------------------------------- */

/* Pushes `maxima_locked` out to every widget it governs. Called once at
 * construction so the unlocked state is set up the same way it is later. */
static void apply_lock_state(void) {
    for (int i = 0; i < TOTAL_DACS; i++) {
        /* set_editable rather than set_sensitive: a locked ceiling still
         * has to be easy to read, and greyed-out text is not. */
        gtk_editable_set_editable(GTK_EDITABLE(channels[i].max_entry), !maxima_locked);
    }

    gtk_widget_set_sensitive(max_all_entry,  !maxima_locked);
    gtk_widget_set_sensitive(max_all_button, !maxima_locked);

    /* The label names the action the button performs, not the state. */
    gtk_button_set_label(GTK_BUTTON(lock_button),
                         maxima_locked ? "Unlock Max" : "Lock Max");
    gtk_widget_set_name(lock_button,
                        maxima_locked ? "btn-locked" : "btn-unlocked");
    gtk_widget_set_tooltip_text(lock_button,
        maxima_locked ? "Let the per-channel maximums be edited again"
                      : "Freeze every per-channel maximum so it cannot be edited");
}

static void on_lock_toggled(GtkToggleButton *button, gpointer user_data) {
    (void)user_data;
    maxima_locked = gtk_toggle_button_get_active(button);
    apply_lock_state();
}

/* Applies one ceiling to all twelve channels. Each channel clamps it and
 * pulls its own target down if the new ceiling sits below it. */
static void set_all_max(double value) {
    for (int i = 0; i < TOTAL_DACS; i++)
        apply_user_max(&channels[i], value);
}

static void on_set_all_max(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;
    if (maxima_locked) return;

    set_all_max(read_value(max_all_entry));

    /* Echo back what the channels actually took, so a value that was out
     * of range does not sit in the box looking like it was accepted. */
    write_value(max_all_entry, channels[0].user_max);
}

static void on_set_all_max_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    on_set_all_max(NULL, user_data);
}

/* ---- Bulk callbacks -------------------------------------------------- */

/* Flips a toggle without running on_off_clicked, so All ON / All OFF stay
 * display-only and do not fire twelve config writes at the hardware. */
static void set_toggle_silently(Channel *c, gboolean is_on) {
    g_signal_handlers_block_by_func(c->off_btn, G_CALLBACK(on_off_clicked), c);
    gtk_toggle_button_set_active(c->off_btn, is_on);
    set_off_button_visual(GTK_WIDGET(c->off_btn), is_on);
    g_signal_handlers_unblock_by_func(c->off_btn, G_CALLBACK(on_off_clicked), c);
}

static void on_all_off_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;
    for (int i = 0; i < TOTAL_DACS; i++) set_toggle_silently(&channels[i], FALSE);
}

static void on_all_on_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;
    for (int i = 0; i < TOTAL_DACS; i++) set_toggle_silently(&channels[i], TRUE);
}

static void on_all_update_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    /* Sends over the connection the Connect button opened, exactly like
     * the per-channel UPDATE buttons. */
    serial_fd_t fd = connection_get_fd();

    for (int i = 0; i < TOTAL_DACS; i++) {
        Channel *c = &channels[i];
        double v = apply_voltage(c, read_value(c->entry));
        protocol_send_voltage(fd, REG_DAC, c->ch, voltage_to_code(v));
    }
}

/* ---- Construction ---------------------------------------------------- */

static GtkWidget *make_heading(const char *text) {
    GtkWidget *label = gtk_label_new(NULL);
    char markup[64];
    snprintf(markup, sizeof(markup), "<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    return label;
}

/* Grid columns, shared by the header row and every channel row. */
enum {
    COL_LABEL = 0,
    COL_ENTRY,
    COL_UP,
    COL_DOWN,
    COL_OFF,
    COL_UPDATE,
    COL_MAX
};

static void build_channel_row(GtkWidget *grid, int i, int row) {
    Channel *c = &channels[i];
    c->ch = i;

    char label_text[32];
    snprintf(label_text, sizeof(label_text), "Channel %d:", i);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, COL_LABEL, row, 1, 1);

    /* Target voltage */
    c->entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(c->entry), GTK_INPUT_PURPOSE_NUMBER);
    gtk_widget_set_hexpand(c->entry, TRUE);
    g_signal_connect(c->entry, "activate", G_CALLBACK(on_voltage_activate), c);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_voltage_focus_leave), c);
    gtk_widget_add_controller(c->entry, focus);
    gtk_grid_attach(GTK_GRID(grid), c->entry, COL_ENTRY, row, 1, 1);

    /* Step buttons */
    char step_tip[64];
    snprintf(step_tip, sizeof(step_tip), "Increase value (+%.1f V)", VOLTAGE_ARROW_STEP);
    GtkWidget *up_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_set_tooltip_text(up_btn, step_tip);
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_up_clicked), c);
    gtk_grid_attach(GTK_GRID(grid), up_btn, COL_UP, row, 1, 1);

    snprintf(step_tip, sizeof(step_tip), "Decrease value (-%.1f V)", VOLTAGE_ARROW_STEP);
    GtkWidget *down_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_set_tooltip_text(down_btn, step_tip);
    g_signal_connect(down_btn, "clicked", G_CALLBACK(on_down_clicked), c);
    gtk_grid_attach(GTK_GRID(grid), down_btn, COL_DOWN, row, 1, 1);

    /* Power toggle */
    c->off_btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("OFF"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(c->off_btn), "Power this DAC on or off");
    gtk_widget_set_name(GTK_WIDGET(c->off_btn), "btn-off");
    g_signal_connect(c->off_btn, "clicked", G_CALLBACK(on_off_clicked), c);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(c->off_btn), COL_OFF, row, 1, 1);

    /* Send this channel */
    GtkWidget *update_btn = gtk_button_new_with_label("UPDATE");
    g_signal_connect(update_btn, "clicked", G_CALLBACK(on_update_clicked), c);
    gtk_grid_attach(GTK_GRID(grid), update_btn, COL_UPDATE, row, 1, 1);

    /* Per-channel ceiling */
    c->max_entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(c->max_entry), GTK_INPUT_PURPOSE_NUMBER);
    gtk_editable_set_width_chars(GTK_EDITABLE(c->max_entry), 7);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(c->max_entry), 7);

    g_signal_connect(c->max_entry, "activate", G_CALLBACK(on_max_activate), c);
    GtkEventController *max_focus = gtk_event_controller_focus_new();
    g_signal_connect(max_focus, "leave", G_CALLBACK(on_max_focus_leave), c);
    gtk_widget_add_controller(c->max_entry, max_focus);
    gtk_grid_attach(GTK_GRID(grid), c->max_entry, COL_MAX, row, 1, 1);

    char max_tip[128];
    snprintf(max_tip, sizeof(max_tip),
             "Highest voltage channel %d may be set to (0 - %.1f V)",
             i, VOLTAGE_FULL_SCALE);
    gtk_widget_set_tooltip_text(c->max_entry, max_tip);

    /* Seed the ceiling first: the target is clamped against it. */
    c->user_max = VOLTAGE_USER_MAX_DEFAULT;
    write_value(c->max_entry, c->user_max);
    refresh_max_tooltip(c);
    apply_voltage(c, VOLTAGE_MIN);
}

void channels_build(GtkWidget *parent) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(parent), grid);

    /* Header row: with seven columns the layout no longer reads at a
     * glance, and "Max (V)" has to be told apart from the target. */
    gtk_grid_attach(GTK_GRID(grid), make_heading("Voltage (V)"), COL_ENTRY, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_heading("Max (V)"),     COL_MAX,   0, 1, 1);

    for (int i = 0; i < TOTAL_DACS; i++)
        build_channel_row(grid, i, i + 1);

    /* Bulk bar: maxima controls on the left, channel actions on the right. */
    GtkWidget *all_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(all_box, TRUE);
    gtk_box_append(GTK_BOX(parent), all_box);

    gtk_box_append(GTK_BOX(all_box), gtk_label_new("Max (V) for all:"));

    max_all_entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(max_all_entry), GTK_INPUT_PURPOSE_NUMBER);
    gtk_editable_set_width_chars(GTK_EDITABLE(max_all_entry), 7);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(max_all_entry), 7);
    gtk_widget_set_tooltip_text(max_all_entry,
                                "Ceiling to apply to every channel at once");
    write_value(max_all_entry, VOLTAGE_USER_MAX_DEFAULT);
    g_signal_connect(max_all_entry, "activate",
                     G_CALLBACK(on_set_all_max_activate), NULL);
    gtk_box_append(GTK_BOX(all_box), max_all_entry);

    max_all_button = gtk_button_new_with_label("SET ALL");
    gtk_widget_set_tooltip_text(max_all_button,
                                "Give every channel the maximum shown to the left");
    g_signal_connect(max_all_button, "clicked", G_CALLBACK(on_set_all_max), NULL);
    gtk_box_append(GTK_BOX(all_box), max_all_button);

    lock_button = gtk_toggle_button_new_with_label("Lock Max");
    g_signal_connect(lock_button, "toggled", G_CALLBACK(on_lock_toggled), NULL);
    gtk_box_append(GTK_BOX(all_box), lock_button);

    /* Empty expanding filler pushes the channel actions to the far edge. */
    GtkWidget *spacer = gtk_label_new(NULL);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(all_box), spacer);

    /* All OFF / All ON / All UPDATE */
    GtkWidget *all_off_btn    = gtk_button_new_with_label("All OFF");
    GtkWidget *all_on_btn     = gtk_button_new_with_label("All ON");
    GtkWidget *all_update_btn = gtk_button_new_with_label("All UPDATE");

    g_signal_connect(all_off_btn,    "clicked", G_CALLBACK(on_all_off_clicked),    NULL);
    g_signal_connect(all_on_btn,     "clicked", G_CALLBACK(on_all_on_clicked),     NULL);
    g_signal_connect(all_update_btn, "clicked", G_CALLBACK(on_all_update_clicked), NULL);

    gtk_box_append(GTK_BOX(all_box), all_off_btn);
    gtk_box_append(GTK_BOX(all_box), all_on_btn);
    gtk_box_append(GTK_BOX(all_box), all_update_btn);

    /* Everything the lock governs now exists, so set the initial state. */
    apply_lock_state();
}
