#ifndef RX_MONITOR_H
#define RX_MONITOR_H

#include <gtk/gtk.h>

#include "serial.h"

/* How many received bytes the "Bytes Received" panel displays. */
#define RX_BYTE_COUNT 3

/* Appends the "Bytes Received" title and label grid to `parent`. */
void rx_monitor_build_section(GtkWidget *parent);

/* Starts watching `fd` for "RX: <hex>" lines and mirroring them into the
 * labels built above. Called once per successful connection. */
void rx_monitor_start(serial_fd_t fd);

#endif /* RX_MONITOR_H */
