#ifndef CHANNELS_H
#define CHANNELS_H

#include <gtk/gtk.h>

/* Appends the per-channel voltage grid and the All OFF / All ON /
 * All UPDATE bar to `parent`. */
void channels_build(GtkWidget *parent);

#endif /* CHANNELS_H */
