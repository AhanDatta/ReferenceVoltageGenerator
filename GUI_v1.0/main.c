/*
To compile and run:
gcc $(pkg-config --cflags gtk4) -o main main.c $(pkg-config --libs gtk4) -lm
./main
*/

#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#  include <windows.h>
   typedef HANDLE serial_fd_t;
   typedef DWORD speed_t;
#  define SERIAL_INVALID INVALID_HANDLE_VALUE
#else
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
   typedef int serial_fd_t;
#  define SERIAL_INVALID (-1)
#endif

#define VOLTAGE_MIN   0
#define VOLTAGE_MAX   5.0
#define VOLTAGE_STEP  0.0001   /* 100 µV */


// Keep a global or static descriptor variable for the ongoing session
static serial_fd_t active_serial_fd = SERIAL_INVALID;

// serial connection controls (shared, heap-allocated in activate())
typedef struct {
    GtkEntry    *port_entry;
    GtkDropDown *baud_dropdown;
    char         port_name[64];
} ConnectData;

// per-channel Update button data
typedef struct {
    int         ch;
    GtkWidget  *entry;
    ConnectData *conn; // pointer to the shared ConnectData
} BtnCallbackData;

// data for the "All OFF / All ON / All UPDATE" buttons
typedef struct {
    GtkWidget       *off_buttons[12];    // the 12 per-channel toggle buttons
    BtnCallbackData *off_data[12];       // the 12 BtnCallbackData pointers for the off buttons
                                         // (needed to match the handler when blocking/unblocking signals)
    GtkWidget       *entry_boxes[12];    // the 12 per-channel voltage entries
    ConnectData     *conn;               // shared serial connection controls
    int              channel_values[12]; // channel index for each row
} AllBtnData;

// Radio button state to choose which bytes to send
typedef struct {
	GtkWidget *radio_btn0;
	GtkWidget *radio_btn1;
	GtkWidget *radio_btn2;
    GtkWidget *byte_entries[3];
    GtkWidget *error_label;
    ConnectData *conn; // pointer to the shared ConnectData
} RadioBtnSelect;

#define SERIAL_RX_BUFFER_SIZE 1024

typedef struct {
    char data[SERIAL_RX_BUFFER_SIZE];
    size_t length;
} SerialRxBuffer;

// voltage helper functions
static double snap_and_clamp(double value) {
    value = round(value / VOLTAGE_STEP) * VOLTAGE_STEP;
    if (value < VOLTAGE_MIN) value = VOLTAGE_MIN;
    if (value > VOLTAGE_MAX) value = VOLTAGE_MAX;
    return value;
}

static void apply_value(GtkEntry *entry, double value) {
    value = snap_and_clamp(value);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", value);
    gtk_editable_set_text(GTK_EDITABLE(entry), buf);
}

static void on_up_clicked(GtkButton *button, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));
    apply_value(entry, value + VOLTAGE_STEP);
}

static void on_down_clicked(GtkButton *button, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));
    apply_value(entry, value - VOLTAGE_STEP);
}

// Cross platform serial communication

#ifdef _WIN32

static DWORD baud_from_string(const char *s) {
    return (DWORD)atoi(s); // Windows accepts the raw integer directly
}

static serial_fd_t open_serial(const char *port, DWORD baud) {
    // Windows requires the "\\.\COMn" prefix for ports above COM9
    char win_port[64];
    if (strncmp(port, "\\\\.\\", 4) != 0)
        snprintf(win_port, sizeof(win_port), "\\\\.\\%s", port);
    else
        snprintf(win_port, sizeof(win_port), "%s", port);

    HANDLE hPort = CreateFileA(
        win_port,
        GENERIC_READ | GENERIC_WRITE,
        0,       // no sharing
        NULL,    // default security
        OPEN_EXISTING,
        0,       // non-overlapped I/O
        NULL);
    if (hPort == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.WriteTotalTimeoutConstant   = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hPort, &timeouts);

    return hPort;
}

static gboolean serial_write(serial_fd_t fd, const char *buf, size_t len) {
    size_t total_written = 0;
    while (total_written < len) {
        DWORD written = 0;
        if (!WriteFile(fd, buf + total_written, (DWORD)(len - total_written), &written, NULL) || written == 0) {
            g_printerr("Serial write failed (Windows error %lu).\n", GetLastError());
            return FALSE;
        }
        total_written += (size_t)written;
    }
    return TRUE;
}

static void serial_close(serial_fd_t fd) {
    CloseHandle(fd);
}

static int serial_is_invalid(serial_fd_t fd) {
    return fd == INVALID_HANDLE_VALUE;
}

#else /* POSIX */

static speed_t baud_from_string(const char *s) {
    int b = atoi(s);
    switch (b) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B115200;
    }
}

static serial_fd_t open_serial(const char *port, speed_t baud) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

static gboolean serial_write(serial_fd_t fd, const char *buf, size_t len) {
    size_t total_written = 0;
    while (total_written < len) {
        ssize_t written = write(fd, buf + total_written, len - total_written);
        if (written < 0) {
            if (errno == EINTR) continue;
            g_printerr("Serial write failed: %s\n", g_strerror(errno));
            return FALSE;
        }
        if (written == 0) {
            g_printerr("Serial write failed: zero bytes written.\n");
            return FALSE;
        }
        total_written += (size_t)written;
    }
    return TRUE;
}

static void serial_close(serial_fd_t fd) {
    close(fd);
}

static int serial_is_invalid(serial_fd_t fd) {
    return fd < 0;
}

#endif /* _WIN32 */

// Reads and normalizes the serial port and selected baud rate. A number by
// itself is treated as a Windows COM port (for example, "12" becomes "COM12").
static gboolean get_port_and_baud(ConnectData *conn, const char **port_out, const char **baud_out) {
    GtkStringObject *baud_obj = GTK_STRING_OBJECT(
        gtk_drop_down_get_selected_item(conn->baud_dropdown));
    const char *entry_text = gtk_editable_get_text(GTK_EDITABLE(conn->port_entry));

    if (!baud_obj || !entry_text) return FALSE;

    char *trimmed_port = g_strdup(entry_text);
    g_strstrip(trimmed_port);
    if (trimmed_port[0] == '\0') {
        g_free(trimmed_port);
        return FALSE;
    }

    const char *number_text = trimmed_port;
    if (g_ascii_strncasecmp(number_text, "COM", 3) == 0) number_text += 3;

    gboolean is_com_port = number_text[0] != '\0';
    for (const char *p = number_text; *p != '\0'; p++) {
        if (!g_ascii_isdigit((guchar)*p)) {
            is_com_port = FALSE;
            break;
        }
    }

    int written;
    if (is_com_port) {
        errno = 0;
        char *end = NULL;
        guint64 port_number = g_ascii_strtoull(number_text, &end, 10);
        if (errno == ERANGE || !end || *end != '\0' || port_number == 0) {
            g_free(trimmed_port);
            return FALSE;
        }
        written = g_snprintf(conn->port_name, sizeof(conn->port_name),
                             "COM%" G_GUINT64_FORMAT, port_number);
    } else {
#ifdef _WIN32
        g_free(trimmed_port);
        return FALSE;
#else
        written = g_snprintf(conn->port_name, sizeof(conn->port_name), "%s", trimmed_port);
#endif
    }
    g_free(trimmed_port);

    if (written < 0 || (size_t)written >= sizeof(conn->port_name)) return FALSE;

    *port_out = conn->port_name;
    *baud_out = gtk_string_object_get_string(baud_obj);
    return TRUE;
}

// Returns SERIAL_INVALID on any failure (no selection, or the port couldn't open).
static serial_fd_t open_serial_from_conn(ConnectData *conn) {
    const char *port, *baud_str;
    if (!get_port_and_baud(conn, &port, &baud_str)) {
        g_printerr("No COM port or baud rate selected.\n");
        return SERIAL_INVALID;
    }

    serial_fd_t fd = open_serial(port, baud_from_string(baud_str));
    if (serial_is_invalid(fd)) {
        g_printerr("Failed to open serial port: %s\n", port);
    }
    return fd;
}

// Builds and sends the "<off> <dac> <voltage>\n" command.
static void send_voltage_command(serial_fd_t fd, int off, int channel, double value) {
    char msg[32];
    snprintf(msg, sizeof(msg), "1 %d %d %.4f\n", off, channel, value);
    g_print("Sending: %s", msg); // keep terminal echo for debugging
    serial_write(fd, msg, strlen(msg));
}

// Updates an off/on toggle button's label and CSS name to match its state.
static void set_off_button_visual(GtkWidget *btn, gboolean is_on) {
    gtk_button_set_label(GTK_BUTTON(btn), is_on ? "ON" : "OFF");
    gtk_widget_set_name(btn, is_on ? "btn-on" : "btn-off");
}

// Allocates a BtnCallbackData for either an Off or Update button.
static BtnCallbackData *make_btn_data(int ch, GtkWidget *entry, ConnectData *conn) {
    BtnCallbackData *data = g_new(BtnCallbackData, 1);
    data->ch    = ch;
    data->entry = entry;
    data->conn  = conn;
    return data;
}

// Struct to hold widgets that display received SPI data
typedef struct {
    GtkWidget *received_labels[3];
} IncomingDataWidgets;

// Global pointer to background listeners
static IncomingDataWidgets *global_rx_widgets = NULL;
static GMutex spi_rx_state_mutex;
static int spi_rx_expected_count = 0;
static int spi_rx_next_index = 0;

static void set_spi_error(RadioBtnSelect *data, const char *message) {
    gtk_label_set_text(GTK_LABEL(data->error_label), message ? message : "");
    gtk_widget_set_visible(data->error_label, message && message[0] != '\0');
}

static gboolean parse_hex_byte(const char *text, uint8_t *value_out) {
    size_t length = text ? strlen(text) : 0;
    if (length < 1 || length > 2) return FALSE;

    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)text[i])) return FALSE;
    }

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 16);
    if (errno != 0 || !end || *end != '\0' || value > 0xFFUL) return FALSE;

    *value_out = (uint8_t)value;
    return TRUE;
}

static void reset_spi_rx_display(int expected_count) {
    g_mutex_lock(&spi_rx_state_mutex);
    spi_rx_expected_count = expected_count;
    spi_rx_next_index = 0;
    g_mutex_unlock(&spi_rx_state_mutex);

    if (!global_rx_widgets) return;
    for (int i = 0; i < 3; i++) {
        gtk_label_set_markup(
            GTK_LABEL(global_rx_widgets->received_labels[i]),
            i < expected_count ? "<i>Waiting...</i>" : "<i>Not requested</i>");
    }
}

static gboolean reserve_spi_rx_index(int *index_out) {
    gboolean reserved = FALSE;
    g_mutex_lock(&spi_rx_state_mutex);
    if (spi_rx_next_index < spi_rx_expected_count && spi_rx_next_index < 3) {
        *index_out = spi_rx_next_index++;
        reserved = TRUE;
    }
    g_mutex_unlock(&spi_rx_state_mutex);
    return reserved;
}

// Send button callback depends on which radio button is selected
static void on_send_clicked(GtkButton *button, gpointer user_data) {
	RadioBtnSelect *data = (RadioBtnSelect *)user_data;

    // Safety check to ensure connection information is wired correctly
    if (!data->conn) {
        g_printerr("Error: Connection data not linked to SPI section.\n");
        set_spi_error(data, "SPI controls are not connected correctly.");
        return;
    }

	int bytes_to_send = 0;
	if (gtk_check_button_get_active( GTK_CHECK_BUTTON(data->radio_btn0) )) {
		bytes_to_send = 1;
	}
	else if (gtk_check_button_get_active( GTK_CHECK_BUTTON(data->radio_btn1) )) {
		bytes_to_send = 2;
	}
	else if (gtk_check_button_get_active( GTK_CHECK_BUTTON(data->radio_btn2) )) {
		bytes_to_send = 3;
	}

    if (bytes_to_send == 0) {
        set_spi_error(data, "Select how many SPI bytes to send.");
        return;
    }

    uint8_t values[3] = {0};
    for (int i = 0; i < bytes_to_send; i++) {
        const char *hex_val = gtk_editable_get_text(GTK_EDITABLE(data->byte_entries[i]));
        if (!parse_hex_byte(hex_val, &values[i])) {
            char error_message[96];
            snprintf(error_message, sizeof(error_message),
                     "Byte %d must contain one or two hexadecimal digits (00-FF).", i);
            set_spi_error(data, error_message);
            gtk_widget_grab_focus(data->byte_entries[i]);
            return;
        }
    }

    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Error: Cannot send. Port not connected!\n");
        set_spi_error(data, "Connect to the Feather before sending SPI data.");
        return;
    }

    char msg[32];
    size_t used = (size_t)snprintf(msg, sizeof(msg), "0");
    for (int i = 0; i < bytes_to_send; i++) {
        int written = snprintf(msg + used, sizeof(msg) - used, " %02X", values[i]);
        if (written < 0 || (size_t)written >= sizeof(msg) - used) {
            set_spi_error(data, "Internal error while building the SPI command.");
            return;
        }
        used += (size_t)written;
    }
    if (used + 1 >= sizeof(msg)) {
        set_spi_error(data, "Internal error while building the SPI command.");
        return;
    }
    msg[used++] = '\n';
    msg[used] = '\0';

    set_spi_error(data, NULL);
    reset_spi_rx_display(bytes_to_send);
    g_print("SPI serial command (%d byte%s): %s",
            bytes_to_send, bytes_to_send == 1 ? "" : "s", msg);

    if (!serial_write(active_serial_fd, msg, used)) {
        set_spi_error(data, "The serial command could not be written completely.");
        return;
    }
}

// When 'update' clicked, send that voltage to the corresponding channel
static void on_update_clicked(GtkButton *button, gpointer user_data) {
    BtnCallbackData *data = (BtnCallbackData *)user_data;
    int       CH    = data->ch;
    GtkEntry *entry = GTK_ENTRY(data->entry);

    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));

    //serial_fd_t fd = open_serial_from_conn(data->conn);
    //if (serial_is_invalid(fd)) return;

    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Error: Cannot send. Port not connected!\n");
        return;
    }
    send_voltage_command(active_serial_fd, 0, CH, value);

    //serial_close(fd);
}

// Power off the channel
static void on_off_clicked(GtkToggleButton *button, gpointer user_data) {
    BtnCallbackData *data = (BtnCallbackData *)user_data;
    int CH = data->ch;
    GtkEntry *entry = GTK_ENTRY(data->entry);
    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));

    gboolean is_on = gtk_toggle_button_get_active(button);
    set_off_button_visual(GTK_WIDGET(button), is_on);
    int OFF = is_on ? 0 : 1; // 0 = DAC powered on, 1 = DAC powered off
    
    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Error: Cannot send. Port not connected!\n");
        return;
    }

    send_voltage_command(active_serial_fd, OFF, CH, value);
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));
    apply_value(entry, value);
}

static void on_entry_focus_leave(GtkEventController *controller, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));
    apply_value(entry, value);
}

static gboolean serial_rx_buffer_append(SerialRxBuffer *buffer, const char *data, size_t length) {
    if (length > sizeof(buffer->data) - buffer->length) {
        g_printerr("Serial receive line exceeded %d bytes; discarding the incomplete record.\n",
                   SERIAL_RX_BUFFER_SIZE - 1);
        buffer->length = 0;
        return FALSE;
    }
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return TRUE;
}

static gboolean serial_rx_buffer_next_line(SerialRxBuffer *buffer, char *line_out, size_t line_size) {
    char *newline = memchr(buffer->data, '\n', buffer->length);
    if (!newline) return FALSE;

    size_t line_length = (size_t)(newline - buffer->data);
    if (line_length > 0 && buffer->data[line_length - 1] == '\r') line_length--;
    if (line_length >= line_size) line_length = line_size - 1;
    memcpy(line_out, buffer->data, line_length);
    line_out[line_length] = '\0';

    size_t consumed = (size_t)(newline - buffer->data) + 1;
    memmove(buffer->data, buffer->data + consumed, buffer->length - consumed);
    buffer->length -= consumed;
    return TRUE;
}

static gboolean parse_rx_record(const char *line, unsigned int *value_out) {
    if (strlen(line) != 6 || strncmp(line, "RX: ", 4) != 0 ||
        !isxdigit((unsigned char)line[4]) || !isxdigit((unsigned char)line[5])) {
        return FALSE;
    }

    char token[3] = { line[4], line[5], '\0' };
    *value_out = (unsigned int)strtoul(token, NULL, 16);
    return TRUE;
}

#ifndef _WIN32
// Callback triggered when the serial port has data to read (Linux)
static gboolean on_serial_data_available(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    SerialRxBuffer *rx_buffer = (SerialRxBuffer *)user_data;
    if (condition & (G_IO_IN | G_IO_PRI)) {
        int fd = g_io_channel_unix_get_fd(source);
        char buf[256];
        ssize_t bytes_read = read(fd, buf, sizeof(buf));
        
        if (bytes_read > 0 && serial_rx_buffer_append(rx_buffer, buf, (size_t)bytes_read)) {
            char line[SERIAL_RX_BUFFER_SIZE];
            while (serial_rx_buffer_next_line(rx_buffer, line, sizeof(line))) {
                g_print("Feather serial: %s\n", line);
                unsigned int hex_val;
                int byte_index;
                if (parse_rx_record(line, &hex_val) && reserve_spi_rx_index(&byte_index)) {
                    if (global_rx_widgets) {
                        char markup[64];
                        snprintf(markup, sizeof(markup), "<b>0x%02X</b>", hex_val);
                        gtk_label_set_markup(GTK_LABEL(global_rx_widgets->received_labels[byte_index]), markup);
                    }
                }
            }
        }
    }
    
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        g_printerr("Serial connection closed or lost.\n");
        return FALSE; // Removes the watch hook
    }
    
    return TRUE; // Continue monitoring
}
#endif

#ifdef _WIN32

// Forward-declare the payload struct at file scope so both functions can see it
typedef struct {
    int index;
    unsigned int val;
} UpdatePayload;

// Named idle callback — replaces the C++ lambda
static gboolean update_rx_label_cb(gpointer data) {
    UpdatePayload *p = (UpdatePayload *)data;
    if (global_rx_widgets) {
        char markup[64];
        snprintf(markup, sizeof(markup), "<b>0x%02X</b>", p->val);
        gtk_label_set_markup(GTK_LABEL(global_rx_widgets->received_labels[p->index]), markup);
    }
    g_free(p);
    return FALSE; // Run once
}

// Structure to pass connection info to the thread
typedef struct {
    HANDLE hPort;
} WinThreadData;

// Background thread function for Windows serial parsing
static gpointer windows_serial_thread_func(gpointer user_data) {
    WinThreadData *tdata = (WinThreadData *)user_data;
    HANDLE hPort = tdata->hPort;
    g_free(tdata);

    char rx_buf[512];
    DWORD bytes_read;
    SerialRxBuffer rx_buffer = {0};

    while (TRUE) {
        BOOL read_ok = ReadFile(hPort, rx_buf, sizeof(rx_buf), &bytes_read, NULL);
        if (!read_ok) {
            g_printerr("Windows serial reader stopped (error %lu).\n", GetLastError());
            break;
        }
        if (bytes_read > 0 && serial_rx_buffer_append(&rx_buffer, rx_buf, (size_t)bytes_read)) {
            char line[SERIAL_RX_BUFFER_SIZE];
            while (serial_rx_buffer_next_line(&rx_buffer, line, sizeof(line))) {
                g_print("Feather serial: %s\n", line);
                unsigned int hex_val;
                int byte_index;
                if (parse_rx_record(line, &hex_val) && reserve_spi_rx_index(&byte_index)) {
                    UpdatePayload *payload = g_new(UpdatePayload, 1);
                    payload->index = byte_index;
                    payload->val = hex_val;

                    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, update_rx_label_cb, payload, NULL);
                }
            }
        }
        g_usleep(10000);
    }
    return NULL;
}
#endif


static void on_connect_clicked(GtkButton *button, gpointer user_data) {
    ConnectData *data = (ConnectData *)user_data;

    const char *port;
    const char *baud;
    if (!get_port_and_baud(data, &port, &baud)) {
        g_printerr("Enter a valid serial port before connecting.\n");
        gtk_widget_add_css_class(GTK_WIDGET(data->port_entry), "error");
        gtk_widget_grab_focus(GTK_WIDGET(data->port_entry));
        return;
    }
    gtk_widget_remove_css_class(GTK_WIDGET(data->port_entry), "error");

    // If an older connection is active, clean it up first
    if (!serial_is_invalid(active_serial_fd)) {
        g_print("Closing existing connection...\n");
        serial_close(active_serial_fd);
        active_serial_fd = SERIAL_INVALID;
    }

    g_print("Connecting on %s at %s baud...\n", port, baud);

    active_serial_fd = open_serial(port, baud_from_string(baud));
    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Warning: could not open %s — check the port.\n", port);
        return;
    }
    g_print("Port %s opened successfully and listening.\n", port);

    // Hook up asynchronous reading based on OS
#ifdef _WIN32
    WinThreadData *tdata = g_new(WinThreadData, 1);
    tdata->hPort = active_serial_fd;
    g_thread_new("windows_serial_reader", windows_serial_thread_func, tdata);
#else
    GIOChannel *channel = g_io_channel_unix_new(active_serial_fd);
    SerialRxBuffer *rx_buffer = g_new0(SerialRxBuffer, 1);
    g_io_add_watch_full(channel, G_PRIORITY_DEFAULT,
                        G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                        on_serial_data_available, rx_buffer, g_free);
    g_io_channel_unref(channel); // The main loop keeps its own reference now
#endif
}

// Change toggle 'on/off' state without running callback
static void set_toggle_silently(GtkToggleButton *tb, BtnCallbackData *off_data, gboolean is_on) {
    g_signal_handlers_block_by_func(tb, G_CALLBACK(on_off_clicked), off_data);
    gtk_toggle_button_set_active(tb, is_on);
    set_off_button_visual(GTK_WIDGET(tb), is_on);
    g_signal_handlers_unblock_by_func(tb, G_CALLBACK(on_off_clicked), off_data);
}

// Change toggle buttons' state to 'off'
static void on_alloff_clicked(GtkButton *button, gpointer user_data) {
    AllBtnData *data = (AllBtnData *)user_data;

    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Error: Cannot send. Port not connected!\n");
        return;
    }

    for (int i = 0; i < 12; i++) {
        GtkToggleButton *tb = GTK_TOGGLE_BUTTON(data->off_buttons[i]);
        GtkEntry *entry = GTK_ENTRY(data->entry_boxes[i]);
        double value = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));

        // Update the UI state without triggering the per-channel signal
        set_toggle_silently(tb, data->off_data[i], FALSE);

        // Execute the logic: Send the OFF command over serial
        send_voltage_command(active_serial_fd, 1, data->channel_values[i], value);
    }
}

// Change toggle buttons' state to 'on'
static void on_allon_clicked(GtkButton *button, gpointer user_data) {
    AllBtnData *data = (AllBtnData *)user_data;

    for (int i = 0; i < 12; i++) {
        set_toggle_silently(GTK_TOGGLE_BUTTON(data->off_buttons[i]), data->off_data[i], TRUE);
    }
}

// Opens the serial port and sends a voltage command for every channel
static void on_updateall_clicked(GtkButton *button, gpointer user_data) {
    AllBtnData *data = (AllBtnData *)user_data;

    // Reuse the already-open connection; see comment in on_alloff_clicked.
    if (serial_is_invalid(active_serial_fd)) {
        g_printerr("Error: Cannot send. Port not connected!\n");
        return;
    }

    // Send one command per channel over the single open connection
    for (int i = 0; i < 12; i++) {
        GtkEntry *entry = GTK_ENTRY(data->entry_boxes[i]);
        double value    = atof(gtk_editable_get_text(GTK_EDITABLE(entry)));

        send_voltage_command(active_serial_fd, 0, data->channel_values[i], value);
    }
}

// When the byte entry value is changed, check if it's a valid hex value
gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
	guint keycode, GdkModifierType state, gpointer user_date) {
	
	GtkWidget *entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	GtkEditable *editable = GTK_EDITABLE(entry);
	(void)editable;
	(void)keycode;
	(void)user_date;

    // Let GTK handle copy/paste/select-all; SEND performs authoritative validation.
    if (state & GDK_CONTROL_MASK) return FALSE;
	
    // Allow standard navigation and control keys (Backspace, Delete, Left, Right, Tab, Enter)
    if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete ||
        keyval == GDK_KEY_Left      || keyval == GDK_KEY_Right  ||
        keyval == GDK_KEY_Tab       || keyval == GDK_KEY_Return || 
        keyval == GDK_KEY_KP_Enter) {
        return FALSE; // Let GTK handle these standard text actions
    }

    // Convert keyval to a Unicode character. 
    // If it returns 0, it's a non-text key (like a standalone Ctrl, Alt, Shift, or F1-F12 key).
    guint32 key_char = gdk_keyval_to_unicode(keyval);
    if (key_char == 0) {
        return FALSE; // Pass it through safely
    }
    
    // Validate that the text character is a hex digit
    if (!isxdigit((unsigned char)key_char)) {
        gtk_widget_error_bell(entry); 
        return TRUE; // Block invalid character
    }	
	
	return FALSE; // Accept the valid hex character
}

// Initialize GUI widgets inside activate
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    
    // Setting up CSS from virtual resource path
    /*GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css_provider, "/com/example/myresource/style.css");
    GdkDisplay *display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(
        display,
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );*/
    
    window = gtk_application_window_new(app);
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

    // Serial connection row (built first so conn_data exists for channels)
    GtkWidget *conn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), conn_box);

    GtkWidget *com_label = gtk_label_new("COM Port:");
    gtk_widget_set_halign(com_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(conn_box), com_label);

    GtkWidget *port_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(port_entry), 63);
    gtk_editable_set_width_chars(GTK_EDITABLE(port_entry), 14);
#ifdef _WIN32
    gtk_editable_set_text(GTK_EDITABLE(port_entry), "1");
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "e.g. 12");
    gtk_widget_set_tooltip_text(port_entry,
        "Enter the COM port number (for example, 12 for COM12). COM12 is also accepted.");
#else
    gtk_editable_set_text(GTK_EDITABLE(port_entry), "/dev/ttyACM0");
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "e.g. /dev/ttyACM0");
    gtk_widget_set_tooltip_text(port_entry,
        "Enter a serial device path, or enter a number to use a Windows COM port.");
#endif
    gtk_box_append(GTK_BOX(conn_box), port_entry);

    GtkWidget *baud_label = gtk_label_new("Baud Rate:");
    gtk_widget_set_halign(baud_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(baud_label, 8);
    gtk_box_append(GTK_BOX(conn_box), baud_label);

    const char *baud_rates[] = {
        "1200", "2400", "4800", "9600",
        "19200", "38400", "57600", "115200",
        "230400", "460800", "921600",
        NULL
    };
    GtkStringList *baud_list = gtk_string_list_new(baud_rates);
    GtkWidget *baud_dropdown = gtk_drop_down_new(G_LIST_MODEL(baud_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(baud_dropdown), 7); // 115200
    gtk_box_append(GTK_BOX(conn_box), baud_dropdown);

    GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
    gtk_widget_set_margin_start(connect_btn, 8);

    ConnectData *conn_data = g_new0(ConnectData, 1);
    conn_data->port_entry    = GTK_ENTRY(port_entry);
    conn_data->baud_dropdown = GTK_DROP_DOWN(baud_dropdown);
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_clicked), conn_data);
    gtk_box_append(GTK_BOX(conn_box), connect_btn);

    // Channel grid with channel number, voltage entry, increase/decrease, etc
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(vbox), grid);
 
    GtkWidget       *entry_boxes[12];
    GtkWidget       *off_buttons[12];
    BtnCallbackData *off_cb_data_arr[12]; // one BtnCallbackData* per off button
    GtkWidget       *update_buttons[12];

    for (int i = 0; i < 12; i++) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Channel %d:", i);
        GtkWidget *label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);

        GtkWidget *entry = gtk_entry_new();
        apply_value(GTK_ENTRY(entry), 0.0);
        gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_NUMBER);
        gtk_widget_set_hexpand(entry, TRUE);

        g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), NULL);
        GtkEventController *focus = gtk_event_controller_focus_new();
        g_signal_connect(focus, "leave", G_CALLBACK(on_entry_focus_leave), entry);
        gtk_widget_add_controller(entry, focus);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, i, 1, 1);

        entry_boxes[i] = entry;

        // column 2: increase voltage value
        GtkWidget *up_btn = gtk_button_new_from_icon_name("go-up-symbolic");
        gtk_widget_set_tooltip_text(up_btn, "Increase value (+100 µV)");
        g_signal_connect(up_btn, "clicked", G_CALLBACK(on_up_clicked), entry_boxes[i]);
        gtk_grid_attach(GTK_GRID(grid), up_btn, 2, i, 1, 1);

        // column 3: decrease voltage value
        GtkWidget *down_btn = gtk_button_new_from_icon_name("go-down-symbolic");
        gtk_widget_set_tooltip_text(down_btn, "Decrease value (-100 µV)");
        g_signal_connect(down_btn, "clicked", G_CALLBACK(on_down_clicked), entry_boxes[i]);
        gtk_grid_attach(GTK_GRID(grid), down_btn, 3, i, 1, 1);

        // column 4: power off channel
        GtkWidget *off_btn = gtk_toggle_button_new_with_label("OFF");
        gtk_widget_set_tooltip_text(off_btn, "Set channel to 0 V");
        BtnCallbackData *off_cb_data = make_btn_data(i, entry_boxes[i], conn_data);
        g_signal_connect(off_btn, "clicked", G_CALLBACK(on_off_clicked), off_cb_data);
        gtk_grid_attach(GTK_GRID(grid), off_btn, 4, i, 1, 1);
        off_buttons[i]      = off_btn;
        off_cb_data_arr[i]  = off_cb_data; // save for AllBtnData below

        // column 5: send voltage value to serial port
        BtnCallbackData *cb_data = make_btn_data(i, entry_boxes[i], conn_data); 
        GtkWidget *update_btn = gtk_button_new_with_label("UPDATE");
        g_signal_connect(update_btn, "clicked", G_CALLBACK(on_update_clicked), cb_data);
        gtk_grid_attach(GTK_GRID(grid), update_btn, 5, i, 1, 1);
        update_buttons[i] = update_btn;
    }

    gtk_window_set_default_size(GTK_WINDOW(window), 680, 950);
    gtk_window_present(GTK_WINDOW(window));

    // power off all channels, power on all channels, update all channels
    GtkWidget *all_off_btn = gtk_button_new_with_label("All OFF");
    GtkWidget *all_on_btn = gtk_button_new_with_label("All ON");
    GtkWidget *all_update_btn = gtk_button_new_with_label("All UPDATE");

    /* Allocate and populate AllBtnData so the three bulk callbacks can reach
     * the per-channel widgets and the shared serial connection controls. */
    AllBtnData *all_data = g_new0(AllBtnData, 1);
    all_data->conn = conn_data;
    for (int i = 0; i < 12; i++) {
        all_data->off_buttons[i]    = off_buttons[i];
        all_data->off_data[i]       = off_cb_data_arr[i];
        all_data->entry_boxes[i]    = entry_boxes[i];
        all_data->channel_values[i] = i; 
    }

    g_signal_connect(all_off_btn,    "clicked", G_CALLBACK(on_alloff_clicked),    all_data);
    g_signal_connect(all_on_btn,     "clicked", G_CALLBACK(on_allon_clicked),     all_data);
    g_signal_connect(all_update_btn, "clicked", G_CALLBACK(on_updateall_clicked), all_data);

    // Horizontal button bar
    GtkWidget *all_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_hexpand(all_box, TRUE);
	gtk_widget_set_halign(all_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(all_box), all_off_btn);
    gtk_box_append(GTK_BOX(all_box), all_on_btn);
    gtk_box_append(GTK_BOX(all_box), all_update_btn);
    gtk_box_append(GTK_BOX(vbox), all_box);
	
	// SPI Communication section
	GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(sep2, 8);
	gtk_widget_set_margin_bottom(sep2, 8);
	gtk_box_append(GTK_BOX(vbox), sep2);
	
	// section title
	GtkWidget *spi_title = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(spi_title), "<b>SPI Communication</b>");
	gtk_box_append(GTK_BOX(vbox), spi_title);
	
	// SPI byte grid
	GtkWidget *spi_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(spi_grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(spi_grid), 8);
	gtk_box_append(GTK_BOX(vbox), spi_grid);

	// 1st row: labels for byte 0, 1, 2
	GtkWidget *byte_labels[3];
	for (int i=0; i<3; i++) {

		// create center-aligned label in format "byte <i>"
		char label_text[32];
		snprintf(label_text, sizeof(label_text), "Byte %d", i);
		GtkWidget *label = gtk_label_new(label_text);
		gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
		gtk_grid_attach(GTK_GRID(spi_grid), label, i, 0, 1, 1); // column i, row 0

		// save to array
		byte_labels[i] = label;
	}

	// 2nd row: entry boxes for byte 0, 1, 2
	GtkWidget *byte_entries[3];
	for (int i=0; i<3; i++) {
		
		// create center-aligned entry box that takes 2 hex digits
		GtkWidget *byte_entry = gtk_entry_new();
		
		// limit entry to 2 characters
		gtk_entry_set_max_length(GTK_ENTRY(byte_entry), 2);
		
		// widget width and grid placement	
		gtk_widget_set_hexpand(byte_entry, TRUE);
		gtk_grid_attach(GTK_GRID(spi_grid), byte_entry, i, 1, 1, 1); // column i, row 1

        GtkEventController *key_controller = gtk_event_controller_key_new();
        g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
        gtk_widget_add_controller(byte_entry, key_controller);
		
		// save to array
		byte_entries[i] = byte_entry;
	}
	
	// byte select
	GtkWidget *send_label = gtk_label_new("Select which bytes to send.");
	gtk_widget_set_margin_top(send_label, 12);
	gtk_box_append(GTK_BOX(vbox), send_label);
	
	// Checkbuttons acquire the behavior of radio buttons when grouped	
	GtkWidget *byte_radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_append(GTK_BOX(vbox), byte_radio_box);
	
	GtkWidget *byte_radio_btn0 = gtk_check_button_new_with_label("Byte 0");
	GtkWidget *byte_radio_btn1 = gtk_check_button_new_with_label("Bytes 0-1");
	GtkWidget *byte_radio_btn2 = gtk_check_button_new_with_label("Bytes 0-2");
	
	gtk_check_button_set_group(GTK_CHECK_BUTTON(byte_radio_btn1), GTK_CHECK_BUTTON(byte_radio_btn0));
	gtk_check_button_set_group(GTK_CHECK_BUTTON(byte_radio_btn2), GTK_CHECK_BUTTON(byte_radio_btn0));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(byte_radio_btn0), TRUE);
	
	gtk_box_append(GTK_BOX(byte_radio_box), byte_radio_btn0);
	gtk_box_append(GTK_BOX(byte_radio_box), byte_radio_btn1);
	gtk_box_append(GTK_BOX(byte_radio_box), byte_radio_btn2);
	gtk_widget_set_halign(byte_radio_box, GTK_ALIGN_CENTER);

    GtkWidget *spi_error_label = gtk_label_new("");
    gtk_widget_add_css_class(spi_error_label, "error");
    gtk_label_set_wrap(GTK_LABEL(spi_error_label), TRUE);
    gtk_widget_set_halign(spi_error_label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(spi_error_label, FALSE);
    gtk_box_append(GTK_BOX(vbox), spi_error_label);
	
	// Allocate struct memory	
	RadioBtnSelect *radio_btn_data = g_new0(RadioBtnSelect, 1);
	radio_btn_data->radio_btn0 = byte_radio_btn0;
	radio_btn_data->radio_btn1 = byte_radio_btn1;
	radio_btn_data->radio_btn2 = byte_radio_btn2;
    for (int i=0; i<3; i++) {
        radio_btn_data->byte_entries[i] = byte_entries[i];
    }
    radio_btn_data->error_label = spi_error_label;
    radio_btn_data->conn = conn_data;

	// 'Send' button will send byte via SPI
	GtkWidget *send_btn = gtk_button_new_with_label("SEND");
	gtk_box_append(GTK_BOX(byte_radio_box), send_btn);

    // 'Send' callback
	g_signal_connect(send_btn, "clicked", G_CALLBACK(on_send_clicked), radio_btn_data);

	// Clean up struct data when send btn is destroyed
	g_signal_connect_swapped(send_btn, "destroy", G_CALLBACK(g_free), radio_btn_data);

    // Bytes-received section: Labels will update to reflect bytes received via SPI
    GtkWidget *received_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(received_title), "<b>Bytes Received</b>");
    gtk_box_append(GTK_BOX(vbox), received_title);

    // Allocate the global tracking reference
    global_rx_widgets = g_new0(IncomingDataWidgets, 1);

    // Bytes-received grid
    GtkWidget *received_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(received_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(received_grid), 8);
    gtk_box_append(GTK_BOX(vbox), received_grid);

    // 1st row: labels for byte 0, 1, 2
    for (int i=0; i<3; i++) {

        // create center-aligned label in format "byte <i>"
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Byte %d", i);
        GtkWidget *label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(received_grid), label, i, 0, 1, 1); // column i, row 0
    }

    // 2nd row: labels displaying byte 0, 1, 2 received
    for (int i=0; i<3; i++) {
        
        // create center-aligned entry box that takes 2 hex digits
        GtkWidget *received_byte = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(received_byte), "<i>Nothing Received</i>");
        
        // widget width and grid placement	
        gtk_widget_set_hexpand(received_byte, TRUE);
        gtk_grid_attach(GTK_GRID(received_grid), received_byte, i, 1, 1, 1); // column i, row 1
                
        // Save to global reference
        global_rx_widgets->received_labels[i] = received_byte;
    }
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.example.adjustvoltage", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
