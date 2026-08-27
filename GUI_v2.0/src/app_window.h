#ifndef APP_WINDOW_H
#define APP_WINDOW_H

#include <gtk/gtk.h>

/* GtkApplication "activate" handler: builds the main window out of the
 * connection row, the channel grid, the SPI panel and the receive panel. */
void app_window_activate(GtkApplication *app, gpointer user_data);

#endif /* APP_WINDOW_H */
