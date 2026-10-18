#ifndef NOTES_H
#define NOTES_H

#include <gtk/gtk.h>

// Initialize the Notes UI. This will create the "Note" toggle button,
// set up the popover with the markdown editor, and pack the button
// into the provided header_bar.
void notes_init(GtkWidget *header_bar);
void notes_refresh(void);
void notes_toggle(void);
void notes_toggle_pin(void);
void notes_trigger_snip(void);
gboolean notes_handle_window_click(GdkEventButton *event);
void close_notes_if_unpinned(void);
void notes_reset_state(void);

#endif // NOTES_H
