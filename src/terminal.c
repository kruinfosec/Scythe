#define _GNU_SOURCE
#include "terminal.h"
#include "config.h"
#include "db.h"
#include <stdlib.h>
#include <unistd.h>

// Forward declarations
static void spawn_shell(VteTerminal *terminal);
static void on_shell_spawned(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data);
static gboolean on_terminal_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data);
static gboolean on_terminal_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void on_copy_activate(GtkMenuItem *item, gpointer user_data);
static void on_paste_activate(GtkMenuItem *item, gpointer user_data);

// Callback with the correct signature, executed when the shell is ready
static void on_shell_spawned(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        g_critical("Failed to spawn shell: %s", error->message);
        g_error_free(error);
        return;
    }
    
    // Register terminal in DB
    int session_id = session_get_current();
    if (session_id != -1) {
        VtePty *pty_obj = vte_terminal_get_pty(terminal);
        const char *pty_name = "unknown";
        if (pty_obj) {
            int fd = vte_pty_get_fd(pty_obj);
            if (fd >= 0) {
                pty_name = ptsname(fd);
            }
        }
        int term_id = terminal_register(session_id, 0, pty_name ? pty_name : "unknown");
        
        // Store term_id in widget data for later retrieval
        g_object_set_data(G_OBJECT(terminal), "db_term_id", GINT_TO_POINTER(term_id));
    }
}

// Function to spawn a shell in the terminal
static void spawn_shell(VteTerminal *terminal) {
    const char *shell = g_getenv("SHELL");
    if (!shell) {
        shell = "/bin/bash";
    }
    char *argv[] = { (char *)shell, NULL };

    vte_terminal_spawn_async(
        terminal,
        VTE_PTY_DEFAULT,
        NULL, // working_directory
        argv,
        NULL, // envv
        G_SPAWN_DEFAULT,
        NULL, // child_setup
        NULL, // child_setup_data
        NULL, // child_setup_data_destroy
        -1,   // timeout
        NULL, // cancellable
        on_shell_spawned, // Correctly typed callback
        NULL  // user_data
    );
}

// Function to close a tab
static void close_tab(GtkWidget *widget, gpointer data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(data);
    int page = gtk_notebook_get_current_page(notebook);
    if (page != -1) {
        gtk_notebook_remove_page(notebook, page);
    }
}

static gboolean on_terminal_click_or_focus(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    extern void close_notes_if_unpinned(void);
    close_notes_if_unpinned();
    return FALSE;
}

// Create a terminal widget
TerminalWidget* create_terminal_widget(GtkWidget *parent) {
    TerminalWidget *term_widget = g_new0(TerminalWidget, 1);
    
    term_widget->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    term_widget->terminal = vte_terminal_new();
    term_widget->parent = parent;
    term_widget->is_split = FALSE;
    term_widget->is_active = TRUE;
    
    g_object_set_data_full(G_OBJECT(term_widget->box), "term-widget", term_widget, NULL);
    g_object_set_data_full(G_OBJECT(term_widget->terminal), "term-widget", term_widget, (GDestroyNotify)g_free);
    
    g_signal_connect(term_widget->terminal, "focus-in-event", G_CALLBACK(on_terminal_click_or_focus), NULL);
    g_signal_connect(term_widget->terminal, "button-press-event", G_CALLBACK(on_terminal_click_or_focus), NULL);

    // Apply settings
    apply_settings(VTE_TERMINAL(term_widget->terminal));
    
    // Spawn the shell, which will trigger on_shell_spawned when ready
    spawn_shell(VTE_TERMINAL(term_widget->terminal));
    
    gtk_widget_set_hexpand(term_widget->terminal, TRUE);
    gtk_widget_set_vexpand(term_widget->terminal, TRUE);

    // Connect other signals
    g_signal_connect(term_widget->terminal, "button-press-event", G_CALLBACK(on_terminal_button_press), term_widget);
    g_signal_connect(term_widget->terminal, "focus-in-event", G_CALLBACK(on_terminal_focus), term_widget);
    g_signal_connect(term_widget->terminal, "key-press-event", G_CALLBACK(on_terminal_key_press), NULL);

    gtk_box_pack_start(GTK_BOX(term_widget->box), term_widget->terminal, TRUE, TRUE, 0);
    gtk_widget_show_all(term_widget->box);
    
    return term_widget;
}

// Function to split the terminal horizontally
void split_terminal_horizontal(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *parent = gtk_widget_get_parent(term_widget->box);
    
    g_object_ref(term_widget->box);
    gtk_container_remove(GTK_CONTAINER(parent), term_widget->box);
    
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 
                          gtk_widget_get_allocated_width(parent) / 2);
    
    gtk_paned_pack1(GTK_PANED(paned), term_widget->box, TRUE, TRUE);
    g_object_unref(term_widget->box);
    
    TerminalWidget *new_term = create_terminal_widget(paned);
    gtk_paned_pack2(GTK_PANED(paned), new_term->box, TRUE, TRUE);
    
    term_widget->parent = paned;
    term_widget->is_split = TRUE;
    new_term->is_split = TRUE;
    
    gtk_container_add(GTK_CONTAINER(parent), paned);
    gtk_widget_show_all(parent);
    
    // Focus the new terminal
    gtk_widget_grab_focus(new_term->terminal);
}

// Function to split the terminal vertically
void split_terminal_vertical(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *parent = gtk_widget_get_parent(term_widget->box);
    
    g_object_ref(term_widget->box);
    gtk_container_remove(GTK_CONTAINER(parent), term_widget->box);
    
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(paned), 
                          gtk_widget_get_allocated_height(parent) / 2);
    
    gtk_paned_pack1(GTK_PANED(paned), term_widget->box, TRUE, TRUE);
    g_object_unref(term_widget->box);
    
    TerminalWidget *new_term = create_terminal_widget(paned);
    gtk_paned_pack2(GTK_PANED(paned), new_term->box, TRUE, TRUE);
    
    term_widget->parent = paned;
    term_widget->is_split = TRUE;
    new_term->is_split = TRUE;
    
    gtk_container_add(GTK_CONTAINER(parent), paned);
    gtk_widget_show_all(parent);
    
    // Focus the new terminal
    gtk_widget_grab_focus(new_term->terminal);
}

// Function to collapse a subterminal
void collapse_subterminal(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *paned = gtk_widget_get_parent(term_widget->box);
    
    if (!GTK_IS_PANED(paned)) {
        return;
    }
    
    GtkWidget *grandparent = gtk_widget_get_parent(paned);
    if (!grandparent) {
        return;
    }

    // Get both terminals from the current paned widget
    GtkWidget *child1 = gtk_paned_get_child1(GTK_PANED(paned));
    GtkWidget *child2 = gtk_paned_get_child2(GTK_PANED(paned));
    
    // Determine which child is being collapsed and which should remain
    GtkWidget *collapsing_widget = term_widget->box;
    GtkWidget *remaining_widget = (collapsing_widget == child1) ? child2 : child1;
    
    // Store the paned position for recreating the layout
    int position = gtk_paned_get_position(GTK_PANED(paned));
    
    // Reference the remaining widget to prevent destruction
    g_object_ref(remaining_widget);
    gtk_container_remove(GTK_CONTAINER(paned), remaining_widget);
    
    // If the remaining widget is itself a paned widget, preserve its hierarchy
    if (GTK_IS_PANED(remaining_widget)) {
        // Get the position in the grandparent
        gboolean is_first = (gtk_paned_get_child1(GTK_PANED(grandparent)) == paned);
        
        // Remove the old paned from grandparent
        gtk_container_remove(GTK_CONTAINER(grandparent), paned);
        
        if (GTK_IS_PANED(grandparent)) {
            // If grandparent is a paned, replace the old paned with the remaining hierarchy
            if (is_first) {
                gtk_paned_pack1(GTK_PANED(grandparent), remaining_widget, TRUE, TRUE);
            } else {
                gtk_paned_pack2(GTK_PANED(grandparent), remaining_widget, TRUE, TRUE);
            }
            // Try to maintain the same split ratio
            gtk_paned_set_position(GTK_PANED(remaining_widget), position);
        } else {
            // If grandparent is not a paned, just add the remaining hierarchy
            gtk_container_add(GTK_CONTAINER(grandparent), remaining_widget);
        }
    } else {
        // For simple terminals, just replace the paned with the remaining terminal
        gtk_container_remove(GTK_CONTAINER(grandparent), paned);
        gtk_container_add(GTK_CONTAINER(grandparent), remaining_widget);
    }
    
    g_object_unref(remaining_widget);
    
    // Update terminal widget states
    if (GTK_IS_CONTAINER(remaining_widget)) {
        // Recursively find and update all terminal widgets in the remaining hierarchy
        GList *terminals = NULL;
        find_terminals(remaining_widget);
        g_list_free(terminals);
    } else if (VTE_IS_TERMINAL(remaining_widget)) {
        TerminalWidget *remaining_term = g_object_get_data(G_OBJECT(remaining_widget), "term-widget");
        if (remaining_term) {
            remaining_term->parent = grandparent;
            remaining_term->is_split = GTK_IS_PANED(gtk_widget_get_parent(remaining_widget));
            remaining_term->is_active = TRUE;
        }
    }
    
    gtk_widget_show_all(grandparent);
    
    // Focus the appropriate terminal in the remaining hierarchy
    if (GTK_IS_CONTAINER(remaining_widget)) {
        // Find the first terminal in the hierarchy and focus it
        GtkWidget *terminal_to_focus = NULL;
        void find_first_terminal(GtkWidget *container) {
            if (!container) return;
            if (VTE_IS_TERMINAL(container)) {
                terminal_to_focus = container;
                return;
            }
            if (GTK_IS_CONTAINER(container)) {
                GList *children = gtk_container_get_children(GTK_CONTAINER(container));
                for (GList *l = children; l && !terminal_to_focus; l = l->next) {
                    find_first_terminal(GTK_WIDGET(l->data));
                }
                g_list_free(children);
            }
        }
        find_first_terminal(remaining_widget);
        if (terminal_to_focus) {
            gtk_widget_grab_focus(terminal_to_focus);
        }
    } else if (VTE_IS_TERMINAL(remaining_widget)) {
        gtk_widget_grab_focus(remaining_widget);
    }
}

// Function to handle Right-click context menu
gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        TerminalWidget *term_widget = (TerminalWidget *)data;
        GtkWidget *menu = gtk_menu_new();

        // Copy/Paste items
        GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy");
        GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste");
        GtkWidget *separator1 = gtk_separator_menu_item_new();

        // Split items
        GtkWidget *split_horizontally = gtk_menu_item_new_with_label("Split Horizontally");
        GtkWidget *split_vertically = gtk_menu_item_new_with_label("Split Vertically");
        GtkWidget *collapse_item = gtk_menu_item_new_with_label("Collapse Subterminal");

        // Connect signals
        g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy_activate), widget);
        g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste_activate), widget);
        g_signal_connect(split_horizontally, "activate", G_CALLBACK(split_terminal_horizontal), term_widget);
        g_signal_connect(split_vertically, "activate", G_CALLBACK(split_terminal_vertical), term_widget);
        g_signal_connect(collapse_item, "activate", G_CALLBACK(collapse_subterminal), term_widget);

        // Disable collapse option if terminal is not split
        gtk_widget_set_sensitive(collapse_item, term_widget->is_split);

        // Add all items to menu
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator1);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), split_horizontally);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), split_vertically);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), collapse_item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

// Function to create a tab label
static GtkWidget* create_tab_label(GtkNotebook *notebook, const char *title) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_margin_start(label, 3);
    gtk_widget_set_margin_end(label, 3);
    
    GtkWidget *close_btn = gtk_button_new();
    GtkWidget *close_label = gtk_label_new("×");
    gtk_container_add(GTK_CONTAINER(close_btn), close_label);
    
    gtk_widget_set_name(close_btn, "tab-close-button");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(close_btn, 20, 20);
    
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), close_btn, FALSE, FALSE, 0);
    
    g_signal_connect(close_btn, "clicked", G_CALLBACK(close_tab), notebook);
    
    gtk_widget_show_all(hbox);
    
    return hbox;
}

// Function to add a new terminal
void add_terminal(GtkNotebook *notebook) {
    GtkWidget *tab_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    TerminalWidget *term_widget = create_terminal_widget(tab_container);
    
    gtk_container_add(GTK_CONTAINER(tab_container), term_widget->box);
    apply_settings(VTE_TERMINAL(term_widget->terminal));

    int current_page = gtk_notebook_get_current_page(notebook);
    int new_page = (current_page >= 0) ? current_page + 1 : 0;
    gtk_notebook_insert_page(notebook, tab_container, create_tab_label(notebook, "Terminal"), new_page);
    gtk_notebook_set_current_page(notebook, new_page);
    gtk_widget_show_all(GTK_WIDGET(notebook));
}

// Helper to recursively find the active terminal
static VteTerminal* find_active_terminal_helper(GtkWidget *widget) {
    if (!widget) return NULL;

    if (VTE_IS_TERMINAL(widget)) {
        TerminalWidget *tw = g_object_get_data(G_OBJECT(widget), "term-widget");
        if (tw && tw->is_active) {
            return VTE_TERMINAL(widget);
        }
    } else if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            VteTerminal *term = find_active_terminal_helper(GTK_WIDGET(l->data));
            if (term) {
                g_list_free(children);
                return term;
            }
        }
        g_list_free(children);
    }
    return NULL;
}

// Fallback helper to find ANY terminal (first one)
static VteTerminal* find_any_terminal_helper(GtkWidget *widget) {
    if (!widget) return NULL;

    if (VTE_IS_TERMINAL(widget)) {
        return VTE_TERMINAL(widget);
    } else if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            VteTerminal *term = find_any_terminal_helper(GTK_WIDGET(l->data));
            if (term) {
                g_list_free(children);
                return term;
            }
        }
        g_list_free(children);
    }
    return NULL;
}

VteTerminal* get_current_terminal(GtkNotebook *notebook) {
    // 1. Try to get the focused widget in the window
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(notebook));
    if (GTK_IS_WINDOW(toplevel)) {
        GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(toplevel));
        if (focused && VTE_IS_TERMINAL(focused)) {
             return VTE_TERMINAL(focused);
        }
    }

    // 2. Fallback: Search hierarchy of current page for active terminal
    int current_page = gtk_notebook_get_current_page(notebook);
    if (current_page == -1) return NULL;
    
    GtkWidget *page_widget = gtk_notebook_get_nth_page(notebook, current_page);
    if (!page_widget) return NULL;

    VteTerminal *active = find_active_terminal_helper(page_widget);
    if (active) return active;

    // 3. Fallback: Return ANY terminal in the current page
    return find_any_terminal_helper(page_widget);
}

// Helper function to recursively find and deactivate terminals
void find_terminals(GtkWidget *container) {
    if (!container || !GTK_IS_WIDGET(container)) {
        return;
    }

    if (VTE_IS_TERMINAL(container)) {
        if (G_IS_OBJECT(container)) {
            TerminalWidget *tw = g_object_get_data(G_OBJECT(container), "term-widget");
            if (tw != NULL) {
                tw->is_active = FALSE;
            }
        }
    } else if (GTK_IS_CONTAINER(container)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(container));
        for (GList *l = children; l != NULL; l = l->next) {
            if (l->data && GTK_IS_WIDGET(l->data)) {
                find_terminals(GTK_WIDGET(l->data));
            }
        }
        g_list_free(children);
    }
}

static gboolean on_terminal_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    if (!widget || !GTK_IS_WIDGET(widget) || !data) {
        return FALSE;
    }

    TerminalWidget *term_widget = (TerminalWidget *)data;
    if (!term_widget->terminal || !VTE_IS_TERMINAL(term_widget->terminal)) {
        return FALSE;
    }
    
    GtkWidget *parent = widget;
    while (parent && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(parent))) {
        parent = gtk_widget_get_parent(parent);
        if (!parent) break;
    }
    
    if (parent && GTK_IS_CONTAINER(parent)) {
        find_terminals(parent);
    }
    
    term_widget->is_active = TRUE;
    
    return FALSE;
}

// Simple command logger - captures line on Enter
// Note: This is a basic implementation. For robust logging, we'd need shell integration (OSC 133).
static void log_terminal_input(VteTerminal *terminal) {
    int term_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "db_term_id"));
    if (term_id <= 0) return;

    // Get current cursor position
    long col, row;
    vte_terminal_get_cursor_position(terminal, &col, &row);
    
    // Read the current line text
    // vte_terminal_get_text_range(term, start_row, start_col, end_row, end_col, is_selected, user_data, attributes)
    // Use modern, non-deprecated API to get text for the specific row
    gsize length;
    char *text = vte_terminal_get_text_range_format(
        terminal,
        VTE_FORMAT_TEXT,
        row, 0,
        row, -1,
        &length
    );
    
    if (text) {
        // Strip whitespace
        g_strstrip(text);
        if (strlen(text) > 0) {
            // Log as command (output is NULL for now as we can't easily pair it without shell integration)
            // We assume the line content is the command.
            log_command(term_id, text, NULL, NULL, 0);
        }
        g_free(text);
    }
}

static gboolean on_terminal_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        log_terminal_input(VTE_TERMINAL(widget));
    }
    return FALSE; // Propagate event to terminal
}

static void on_copy_activate(GtkMenuItem *item, gpointer user_data) {
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(user_data), VTE_FORMAT_TEXT);
}

static void on_paste_activate(GtkMenuItem *item, gpointer user_data) {
    vte_terminal_paste_clipboard(VTE_TERMINAL(user_data));
}

void navigate_terminal_split(GtkNotebook *notebook, GtkDirectionType direction) {
    VteTerminal *current_term = get_current_terminal(notebook);
    if (!current_term) return;

    GtkWidget *widget = GTK_WIDGET(current_term);

    // We need to walk up the hierarchy to find GtkPaneds
    while (widget) {
        GtkWidget *parent = gtk_widget_get_parent(widget);
        if (!parent) break;

        // Stop if we hit the Notebook Page content (usually a Box)
        if (GTK_IS_NOTEBOOK(parent)) break; 

        if (GTK_IS_PANED(parent)) {
            GtkPaned *paned = GTK_PANED(parent);
            GtkOrientation orient = gtk_orientable_get_orientation(GTK_ORIENTABLE(paned));
            
            GtkWidget *child1 = gtk_paned_get_child1(paned);
            GtkWidget *child2 = gtk_paned_get_child2(paned);

            // Determine which child the current widget is in (or is a descendant of)
            gboolean in_child1 = (widget == child1) || gtk_widget_is_ancestor(widget, child1);
            gboolean in_child2 = (widget == child2) || gtk_widget_is_ancestor(widget, child2);

            if (orient == GTK_ORIENTATION_HORIZONTAL) {
                // Left/Right Navigation
                if (direction == GTK_DIR_LEFT && in_child2) {
                    gtk_widget_child_focus(child1, direction); // Focus logic will try to find suitable child in child1
                    return;
                }
                if (direction == GTK_DIR_RIGHT && in_child1) {
                    gtk_widget_child_focus(child2, direction);
                    return;
                }
            } else {
                // Up/Down Navigation
                if (direction == GTK_DIR_UP && in_child2) {
                    gtk_widget_child_focus(child1, direction);
                    return;
                }
                if (direction == GTK_DIR_DOWN && in_child1) {
                    gtk_widget_child_focus(child2, direction);
                    return;
                }
            }
        }
        
        widget = parent;
    }
}
