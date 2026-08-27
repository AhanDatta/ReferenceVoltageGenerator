#ifndef SPI_PANEL_H
#define SPI_PANEL_H

#include <gtk/gtk.h>

/* Appends the SPI Communication section to `parent`: bus settings, the
 * three byte entries, the byte-count selector and the SEND button. */
void spi_panel_build(GtkWidget *parent);

#endif /* SPI_PANEL_H */
