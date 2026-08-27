#include "rx_monitor.h"

#include <stdio.h>
#include <string.h>

/* Labels showing the most recent bytes; NULL until the section is built. */
static GtkWidget *rx_labels[RX_BYTE_COUNT];

/* Which label the next parsed byte lands in; wraps around. */
static int rx_next_index = 0;

typedef struct {
    int          index;
    unsigned int value;
} RxUpdate;

/* Runs on the main loop, so it is safe to call from the Windows reader
 * thread by way of g_idle_add(). */
static gboolean rx_apply_update(gpointer user_data) {
    RxUpdate *u = (RxUpdate *)user_data;

    if (rx_labels[u->index]) {
        char markup[64];
        snprintf(markup, sizeof(markup), "<b>0x%02X</b>", u->value);
        gtk_label_set_markup(GTK_LABEL(rx_labels[u->index]), markup);
    }

    g_free(u);
    return G_SOURCE_REMOVE;
}

/* Splits a chunk of received text into lines and mirrors every
 * "RX: <hex>" line into the next label. `text` is modified in place.
 *
 * Both platforms funnel through here, and both hand the result to the main
 * loop, so the Windows reader thread never touches a widget directly. */
static void rx_parse_lines(char *text) {
    char *line = text;

    while (*line) {
        /* Take one line without strtok(): it is not reliably reentrant
         * across the toolchains this builds under, and MinGW does not
         * ship strtok_r at all. */
        char *end = strchr(line, '\n');
        if (end) *end = '\0';

        unsigned int hex_val;
        /* Accepts both "RX: AA" and "RX: 0xAA". */
        if (sscanf(line, "RX: %x", &hex_val) == 1 ||
            sscanf(line, "RX: 0x%x", &hex_val) == 1) {

            RxUpdate *u = g_new(RxUpdate, 1);
            u->index = rx_next_index;
            u->value = hex_val;
            g_idle_add(rx_apply_update, u);

            rx_next_index = (rx_next_index + 1) % RX_BYTE_COUNT;
        }

        if (!end) break;   /* trailing partial line */
        line = end + 1;
    }
}

#ifdef _WIN32

/* Windows has no g_io_add_watch() for serial handles, so a dedicated
 * thread does the blocking reads. */
static gpointer rx_thread_func(gpointer user_data) {
    HANDLE hPort = (HANDLE)user_data;
    char   buf[512];
    DWORD  bytes_read;

    for (;;) {
        if (!ReadFile(hPort, buf, sizeof(buf) - 1, &bytes_read, NULL)) {
            /* The handle was closed by Disconnect, or the device went
             * away. Either way this thread is done. */
            break;
        }
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            rx_parse_lines(buf);
        }
        g_usleep(10000);
    }
    return NULL;
}

void rx_monitor_start(serial_fd_t fd) {
    rx_next_index = 0;
    g_thread_unref(g_thread_new("serial_reader", rx_thread_func, (gpointer)fd));
}

#else /* POSIX */

#include <unistd.h>

static gboolean rx_data_available(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)user_data;

    if (condition & (G_IO_IN | G_IO_PRI)) {
        int  fd = g_io_channel_unix_get_fd(source);
        char buf[512];
        ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);

        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            rx_parse_lines(buf);
        }
    }

    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        g_printerr("Serial connection closed or lost.\n");
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void rx_monitor_start(serial_fd_t fd) {
    rx_next_index = 0;

    GIOChannel *channel = g_io_channel_unix_new(fd);
    g_io_add_watch(channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                   rx_data_available, NULL);
    g_io_channel_unref(channel); /* the main loop holds its own reference */
}

#endif /* _WIN32 */

void rx_monitor_build_section(GtkWidget *parent) {
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Bytes Received</b>");
    gtk_widget_set_margin_top(title, 12);
    gtk_box_append(GTK_BOX(parent), title);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(parent), grid);

    for (int i = 0; i < RX_BYTE_COUNT; i++) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Byte %d", i);

        GtkWidget *heading = gtk_label_new(label_text);
        gtk_widget_set_halign(heading, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(grid), heading, i, 0, 1, 1);

        GtkWidget *value = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(value), "<i>Nothing Received</i>");
        gtk_widget_set_hexpand(value, TRUE);
        gtk_grid_attach(GTK_GRID(grid), value, i, 1, 1, 1);

        rx_labels[i] = value;
    }
}
