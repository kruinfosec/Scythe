#ifndef TERMINAL_H
#define TERMINAL_H

#include "common.h"

// Define a struct for tracking terminal widgets
typedef struct {
    GtkWidget *box;        // Container box
    GtkWidget *terminal;   // The VTE terminal
    GtkWidget *parent;     // Parent container (paned or box)
    gboolean is_split;     // Flag to indicate if this terminal is split
    gboolean is_active;    // Flag to track active terminal
} TerminalWidget;

TerminalWidget* create_terminal_widget(GtkWidget *parent);
void add_terminal(GtkNotebook *notebook);
void split_terminal_horizontal(GtkWidget *widget, gpointer data);
void split_terminal_vertical(GtkWidget *widget, gpointer data);
void collapse_subterminal(GtkWidget *widget, gpointer data);
VteTerminal* get_current_terminal(GtkNotebook *notebook);
gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
void find_terminals(GtkWidget *container);
void navigate_terminal_split(GtkNotebook *notebook, GtkDirectionType direction);

#endif // TERMINAL_H
