/*
 * Reference Voltage Generator - GUI v2.0
 *
 * Build with the Makefile in the directory above this one:
 *
 *     make          # builds ./main (main.exe on Windows)
 *     make run
 *
 * Layout:
 *   config.h      compile-time limits (voltage ceiling, DAC count, ports)
 *   serial.[ch]   cross-platform serial port open/read/write/close
 *   protocol.[ch] the byte frames understood by the microcontroller
 *   connection.c  COM port / baud row, and the one shared open port
 *   channels.c    the twelve voltage rows, including per-channel maxima
 *   spi_panel.c   SPI settings, staged bytes and SEND
 *   rx_monitor.c  reads the port and shows the bytes received
 *   app_window.c  assembles the above into the main window
 */

#include <gtk/gtk.h>

#include "app_window.h"

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.example.adjustvoltage",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(app_window_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
