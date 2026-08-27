#ifndef CONNECTION_H
#define CONNECTION_H

#include <gtk/gtk.h>

#include "serial.h"

/* Appends the "COM Port / Baud Rate / Connect / Disconnect" row to
 * `parent`. The module owns the resulting widgets and the open port. */
void connection_build_row(GtkWidget *parent);

/* The port opened by Connect, or SERIAL_INVALID while disconnected.
 * Every panel sends through this one handle. */
serial_fd_t connection_get_fd(void);

#endif /* CONNECTION_H */
