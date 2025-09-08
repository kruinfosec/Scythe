#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib-object.h>
#include <unistd.h>  // For access()

// Define a struct to hold pointers to GtkEntry and GtkTextView widgets
typedef struct {
    GtkEntry *entry;
    GtkTextView *text_view;
} AiWidgets;





// Global variable to store the application's starting directory
static gchar *app_directory = NULL;

// Define a struct for tracking terminal widgets
typedef struct {
    GtkWidget *box;        // Container box
    GtkWidget *terminal;   // The VTE terminal
    GtkWidget *parent;     // Parent container (paned or box)
    gboolean is_split;     // Flag to indicate if this terminal is split
    gboolean is_active;    // Flag to track active terminal
} TerminalWidget;

// Define a struct for passing data to menu item callbacks
typedef struct {
    gchar *action;
    GtkWidget *notebook;
    GtkWidget *window;
} MenuItemData;

// Forward declarations of functions
static void spawn_shell(VteTerminal *terminal);
static void close_tab(GtkWidget *widget, gpointer data);
static void split_terminal_horizontal(GtkWidget *widget, gpointer data);
static void split_terminal_vertical(GtkWidget *widget, gpointer data);
static void collapse_subterminal(GtkWidget *widget, gpointer data);
static gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static void add_terminal(GtkNotebook *notebook);
static GtkWidget* create_tab_label(GtkNotebook *notebook, const char *title);
static void automate_action(GtkWidget *widget, gpointer data);
static void session_action(GtkWidget *widget, gpointer data);
static void send_ai_message(GtkWidget *widget, gpointer user_data);

static VteTerminal* get_current_terminal(GtkNotebook *notebook);
static void run_python_script(VteTerminal *terminal, const char *script_path);
static void cleanup_ai_widgets(GtkWidget *widget, gpointer user_data);
static gboolean on_key_press(GtkWidget *widget,GdkEventKey *event, gpointer user_data);
static void on_copy_activate(GtkMenuItem *item, gpointer user_data);
static void on_paste_activate(GtkMenuItem *item, gpointer user_data);
static TerminalWidget* create_terminal_widget(GtkWidget *parent);
static void popup_menu_at_button(GtkWidget *button, GtkWidget *menu);
static void free_menu_item_data(gpointer data);
static gboolean on_terminal_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data);
static void find_terminals(GtkWidget *container);



// Struct to hold data for streaming AI response
typedef struct {
    GtkTextView *text_view;
    GString *buffer; // Used to buffer partial JSON objects
} AiStreamData;

// Forward declarations for our new streaming functions
static void ai_stream_read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void ai_headers_received_callback(GObject *source_object, GAsyncResult *res, gpointer user_data);

// Appends text to the text view and scrolls to the end
static void append_to_text_view(GtkTextView *text_view, const gchar *text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    gtk_text_buffer_insert(buffer, &end_iter, text, -1);
    GtkTextMark *end_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(text_view, end_mark, 0.0, TRUE, 0.5, 0.5);
}

// Processes the buffer of incoming data, extracts complete JSON objects, and updates the UI
static void process_stream_buffer(AiStreamData *stream_data) {
    char *data = stream_data->buffer->str;
    char *next_newline;

    while ((next_newline = strchr(data, '\n')) != NULL) {
        size_t line_len = next_newline - data;
        char *line = g_strndup(data, line_len);

        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, line, -1, NULL)) {
            JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
            if (json_object_has_member(obj, "response")) {
                const gchar *response_chunk = json_object_get_string_member(obj, "response");
                append_to_text_view(stream_data->text_view, response_chunk);
            }
        }
        g_object_unref(parser);
        g_free(line);

        data = next_newline + 1;
    }

    g_string_assign(stream_data->buffer, data);
}

// Callback for when a chunk of data is read from the stream
static void ai_stream_read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GInputStream *stream = G_INPUT_STREAM(source_object);
    AiStreamData *stream_data = (AiStreamData *)user_data;
    GError *error = NULL;

    GBytes *bytes = g_input_stream_read_bytes_finish(stream, res, &error);

    if (error) {
        g_critical("Error reading from AI stream: %s", error->message);
        g_error_free(error);
        goto cleanup;
    }

    if (g_bytes_get_size(bytes) > 0) {
        gsize size;
        const char *chunk = g_bytes_get_data(bytes, &size);
        g_string_append_len(stream_data->buffer, chunk, size);
        process_stream_buffer(stream_data);

        // Continue reading
        g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, NULL, ai_stream_read_callback, user_data);
    } else {
        // End of stream
        append_to_text_view(stream_data->text_view, "\n"); // Add a final newline
        goto cleanup;
    }

    g_bytes_unref(bytes);
    return;

cleanup:
    if (bytes) g_bytes_unref(bytes);
    g_input_stream_close(stream, NULL, NULL);
    g_object_unref(stream);
    g_string_free(stream_data->buffer, TRUE);
    g_free(stream_data);
}

// Callback for when the initial HTTP headers are received
static void ai_headers_received_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    SoupSession *session = SOUP_SESSION(source_object);
    GInputStream *stream = NULL;
    GError *error = NULL;

    stream = soup_session_send_finish(session, res, &error);

    if (error) {
        g_critical("Failed to get AI response stream: %s", error->message);
        g_error_free(error);
        return;
    }

    // Clear the "Thinking..." message before showing the response
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(user_data));
    gtk_text_buffer_set_text(buffer, "", -1);

    AiStreamData *stream_data = g_new(AiStreamData, 1);
    stream_data->text_view = GTK_TEXT_VIEW(user_data);
    stream_data->buffer = g_string_new("");

    // Start reading the first chunk
    g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, NULL, ai_stream_read_callback, stream_data);
}

// Function to send a message to the AI (rewritten for true streaming)
static void send_ai_message(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    GtkEntry *entry = widgets->entry;
    GtkTextView *text_view = widgets->text_view;

    const gchar *input_text = gtk_entry_get_text(entry);
    if (!input_text || strlen(input_text) == 0) {
        return; // Don't send empty messages
    }

    // Clear previous response and show processing message
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_set_text(buffer, "🤖 Thinking...", -1);

    // Build JSON request
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, "dama");
    json_builder_set_member_name(builder, "prompt");
    json_builder_add_string_value(builder, input_text);
    json_builder_set_member_name(builder, "stream");
    json_builder_add_boolean_value(builder, true);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *json_str = json_generator_to_data(gen, NULL);

    // Create and send the request
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", "http://127.0.0.1:11434/api/generate");
    soup_message_set_request(msg, "application/json", SOUP_MEMORY_COPY, json_str, strlen(json_str));

    // Clear the input field
    gtk_entry_set_text(entry, "");

    // Send the request asynchronously
    soup_session_send_async(session, msg, NULL, ai_headers_received_callback, text_view);

    // Cleanup
    g_free(json_str);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    g_object_unref(session);
}



// Function to cleanup AI widgets when window is destroyed
static void cleanup_ai_widgets(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    if (widgets) {
        g_free(widgets);
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
        NULL,
        argv,
        NULL,
        0,
        NULL,
        NULL,
        NULL,
        -1,
        NULL,
        NULL,
        NULL
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

// Create a terminal widget
static TerminalWidget* create_terminal_widget(GtkWidget *parent) {
    TerminalWidget *term_widget = g_new0(TerminalWidget, 1);
    
    term_widget->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    term_widget->terminal = vte_terminal_new();
    term_widget->parent = parent;
    term_widget->is_split = FALSE;
    term_widget->is_active = TRUE;
    
    // Store terminal widget data in both the box and terminal
    if (GTK_IS_WIDGET(term_widget->box)) {
        g_object_set_data_full(G_OBJECT(term_widget->box), 
                              "term-widget", 
                              term_widget,
                              NULL);  // Don't free here as we need it for the terminal
    }
    
    if (VTE_IS_TERMINAL(term_widget->terminal)) {
        g_object_set_data_full(G_OBJECT(term_widget->terminal), 
                              "term-widget", 
                              term_widget,
                              (GDestroyNotify)g_free);  // Free when terminal is destroyed
    }
    
    // Set up the terminal
    spawn_shell(VTE_TERMINAL(term_widget->terminal));
    gtk_widget_set_hexpand(term_widget->terminal, TRUE);
    gtk_widget_set_vexpand(term_widget->terminal, TRUE);
    
    // Connect signals
    g_signal_connect(term_widget->terminal, "button-press-event", 
                     G_CALLBACK(on_terminal_button_press), term_widget);
    g_signal_connect(term_widget->terminal, "focus-in-event",
                     G_CALLBACK(on_terminal_focus), term_widget);
    g_signal_connect(term_widget->terminal, "key-press-event",
                     G_CALLBACK(on_key_press), term_widget->terminal);

    gtk_box_pack_start(GTK_BOX(term_widget->box), term_widget->terminal, TRUE, TRUE, 0);
    gtk_widget_show_all(term_widget->box);
    
    return term_widget;
}

// Function to split the terminal horizontally
static void split_terminal_horizontal(GtkWidget *widget, gpointer data) {
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
static void split_terminal_vertical(GtkWidget *widget, gpointer data) {
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
static void collapse_subterminal(GtkWidget *widget, gpointer data) {
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
static gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
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
    // Create a horizontal box with spacing
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Create label
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_margin_start(label, 3);
    gtk_widget_set_margin_end(label, 3);
    
    // Create close button with square shape and × symbol
    GtkWidget *close_btn = gtk_button_new();
    GtkWidget *close_label = gtk_label_new("×");
    gtk_container_add(GTK_CONTAINER(close_btn), close_label);
    
    // Style the close button
    gtk_widget_set_name(close_btn, "tab-close-button");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    
    // Set fixed square size
    gtk_widget_set_size_request(close_btn, 20, 20);
    
    // Pack widgets
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), close_btn, FALSE, FALSE, 0);
    
    // Important: Connect the close signal to the notebook
    g_signal_connect(close_btn, "clicked", G_CALLBACK(close_tab), notebook);
    
    // Show all widgets
    gtk_widget_show_all(hbox);
    
    return hbox;
}

// Function to add a new terminal
static void add_terminal(GtkNotebook *notebook) {
    GtkWidget *tab_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    TerminalWidget *term_widget = create_terminal_widget(tab_container);
    
    gtk_container_add(GTK_CONTAINER(tab_container), term_widget->box);

    int current_page = gtk_notebook_get_current_page(notebook);
    int new_page = (current_page >= 0) ? current_page + 1 : 0;
    gtk_notebook_insert_page(notebook, tab_container, create_tab_label(notebook, "Terminal"), new_page);
    gtk_notebook_set_current_page(notebook, new_page);
    gtk_widget_show_all(GTK_WIDGET(notebook));
}

static VteTerminal* get_current_terminal(GtkNotebook *notebook) {
    int current_page = gtk_notebook_get_current_page(notebook);
    if (current_page == -1) {
        g_critical("No current page in notebook");
        return NULL;
    }
    
    GtkWidget *tab_container = gtk_notebook_get_nth_page(notebook, current_page);
    if (!tab_container) {
        g_critical("Could not get terminal container from notebook page");
        return NULL;
    }
    
    // Find the VTE terminal in the container hierarchy
    GList *children = gtk_container_get_children(GTK_CONTAINER(tab_container));
    if (!children) {
        g_critical("No children found in tab container");
        return NULL;
    }
    
    // The first child should be our box containing the terminal
    GtkWidget *term_box = (GtkWidget *)g_list_nth_data(children, 0);
    g_list_free(children);
    
    if (!term_box) {
        g_critical("No terminal box found");
        return NULL;
    }
    
    children = gtk_container_get_children(GTK_CONTAINER(term_box));
    if (!children) {
        g_critical("No children found in terminal box");
        return NULL;
    }
    
    // The first child should be the terminal or a paned widget
    GtkWidget *widget = (GtkWidget *)g_list_nth_data(children, 0);
    g_list_free(children);
    
    if (VTE_IS_TERMINAL(widget)) {
        return VTE_TERMINAL(widget);
    } else if (GTK_IS_PANED(widget)) {
        // If it's a paned widget, recursively search for the first terminal
        GtkWidget *child1 = gtk_paned_get_child1(GTK_PANED(widget));
        children = gtk_container_get_children(GTK_CONTAINER(child1));
        if (children) {
            widget = (GtkWidget *)g_list_nth_data(children, 0);
            g_list_free(children);
            if (VTE_IS_TERMINAL(widget)) {
                return VTE_TERMINAL(widget);
            }
        }
    }
    
    g_critical("Could not find VTE terminal widget");
    return NULL;
}

static void run_python_script(VteTerminal *terminal, const char *script_path) {
    if (!terminal || !script_path) {
        g_critical("Invalid terminal or script path");
        return;
    }

    // Check if file exists
    if (access(script_path, F_OK) == -1) {
        g_critical("Python script does not exist: %s", script_path);
        return;
    }

    g_print("Running Python script: %s\n", script_path);
    
    // Construct the command string to be fed to the shell
    gchar *command = g_strdup_printf("python3 %s\n", script_path);
    
    // Feed the command to the terminal's child (the shell)
    vte_terminal_feed_child(terminal, command, -1);
    
    g_free(command);
}

static void automate_action(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    if (!item_data) {
        g_critical("Null data received in automate_action");
        return;
    }
    
    const gchar *action = item_data->action;
    GtkWidget *notebook = item_data->notebook;
    
    g_print("Automation action: %s\n", action);

    VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
    if (!terminal) {
        g_critical("Could not get current terminal");
        return;
    }

    gchar *script_path = NULL;
    
    if (g_strcmp0(action, "EXTSEC") == 0) {
        script_path = g_build_filename(app_directory, "server.py", NULL);
    }
    else if (g_strcmp0(action, "CVE-Lookup") == 0) {
        script_path = g_build_filename(app_directory, "CVE-Lookup.py", NULL);
    }

    if (script_path) {
        if (access(script_path, F_OK) == -1) {
            g_critical("Script not found: %s", script_path);
        } else {
            g_print("Running script: %s\n", script_path);
            run_python_script(terminal, script_path);
        }
        g_free(script_path);
    }
}

// Function to handle session actions
static void session_action(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    const gchar *action = item_data->action;
    
    if (!action) {
        g_critical("Null action received in session_action");
        return;
    }
    
    g_print("Session action: %s\n", action);
    
    // These are placeholders - in a real implementation, they would perform the actual actions
    if (g_strcmp0(action, "Save Session") == 0) {
        g_print("Save session functionality would be implemented here\n");
    } else if (g_strcmp0(action, "Load Session") == 0) {
        g_print("Load session functionality would be implemented here\n");
    } else if (g_strcmp0(action, "Close All") == 0) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        if (!GTK_IS_WINDOW(toplevel)) {
            return;
        }
        
        // Find the notebook widget more systematically
        GtkWidget *notebook = NULL;
        GList *all_widgets = gtk_container_get_children(GTK_CONTAINER(toplevel));
        for (GList *l = all_widgets; l != NULL; l = l->next) {
            if (GTK_IS_BOX(l->data)) {
                GList *box_children = gtk_container_get_children(GTK_CONTAINER(l->data));
                for (GList *cl = box_children; cl != NULL; cl = cl->next) {
                    if (GTK_IS_BOX(cl->data)) {
                        GList *inner_children = gtk_container_get_children(GTK_CONTAINER(cl->data));
                        for (GList *ic = inner_children; ic != NULL; ic = ic->next) {
                            if (GTK_IS_NOTEBOOK(ic->data)) {
                                notebook = GTK_WIDGET(ic->data);
                                break;
                            }
                        }
                        g_list_free(inner_children);
                        if (notebook) break;
                    } else if (GTK_IS_NOTEBOOK(cl->data)) {
                        notebook = GTK_WIDGET(cl->data);
                        break;
                    }
                }
                g_list_free(box_children);
                if (notebook) break;
            }
        }
        g_list_free(all_widgets);
        
        if (notebook) {
            while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) {
                gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), 0);
            }
            // Add a new terminal after closing all
            add_terminal(GTK_NOTEBOOK(notebook));
        } else {
            g_critical("Could not find notebook widget");
        }
    }
}




static void free_menu_item_data(gpointer data) {
    if (data) {
        MenuItemData *item_data = (MenuItemData *)data;
        g_free(item_data->action);
        g_free(item_data);
    }
}

// Helper function to show menu at button
static void popup_menu_at_button(GtkWidget *button, GtkWidget *menu) {
    gtk_menu_popup_at_widget(GTK_MENU(menu), button, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

// Function to create a dropdown menu - updated version
static GtkWidget* create_dropdown_menu(GtkWidget *button, const char *menu_items[], int num_items, GCallback callback) {
    // First, remove any existing menu
    GtkWidget *old_menu = g_object_get_data(G_OBJECT(button), "dropdown-menu");
    if (old_menu) {
        gtk_widget_destroy(old_menu);
        g_object_set_data(G_OBJECT(button), "dropdown-menu", NULL);
    }
    
    // Create a new menu
    GtkWidget *menu = gtk_menu_new();

    // Get the references from the button
    GtkWidget *notebook = g_object_get_data(G_OBJECT(button), "notebook");
    GtkWidget *window = g_object_get_data(G_OBJECT(button), "window");

    // Add menu items
    for (int i = 0; i < num_items; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(menu_items[i]);
        
        // Create the data structure to pass multiple pieces of data
        MenuItemData *item_data = g_new(MenuItemData, 1);
        item_data->action = g_strdup(menu_items[i]);
        item_data->notebook = notebook;
        item_data->window = window;
        
        // Connect signal with this data
        g_signal_connect(item, "activate", callback, item_data);
        
        // Set up cleanup when the item is destroyed
        g_object_set_data_full(G_OBJECT(item), "item-data", item_data, 
            (GDestroyNotify)free_menu_item_data);
        
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        
        // Debug print to confirm item was added
        g_print("Added menu item: %s\n", menu_items[i]);
    };
    
    gtk_widget_show_all(menu);
    
    // Store the menu as data on the button
    g_object_set_data_full(G_OBJECT(button), "dropdown-menu", menu, (GDestroyNotify)gtk_widget_destroy);
    
    // Disconnect any previous handlers and connect the button to the menu
    g_signal_handlers_disconnect_by_func(button, G_CALLBACK(popup_menu_at_button), old_menu);
    g_signal_connect(button, "clicked", G_CALLBACK(popup_menu_at_button), menu);
    
    return menu;
}

// Function to activate the application
static void activate(GtkApplication *app, gpointer user_data) {
    // Add CSS provider
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "button#tab-close-button { "
        "    padding: 0; "
        "    margin: 0; "
        "    min-height: 20px; "
        "    min-width: 20px; "
        "    color: #888888; "
        "    background: #353535; "
        "    border-radius: 3px; "
        "}"
        "button#tab-close-button:hover { "
        "    color: #ffffff; "
        "    background: #ff4444; "
        "}"
        "button#tab-close-button label { "
        "    font-weight: bold; "
        "    font-size: 16px; "
        "}"
        "paned separator { "
        "    background: #2b2b2b; "
        "    min-width: 3px; "
        "    min-height: 3px; "
        "}"
        "paned separator:hover { "
        "    background: #3b3b3b; "
        "    min-width: 3px; "
        "    min-height: 3px; "
        "}"
        "button#tab-close-button { "
        "    padding: 0px 4px; "
        "    margin: 0px; "
        "    color: #888888; "
        "    font-weight: bold; "
        "}"
        "button#tab-close-button:hover { "
        "    color: #ff0000; "
        "}"
        "notebook tab label { "
        "    min-width: 50px; "
        "    padding: 2px; "
        "    color: #ffffff; "
        "}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Scythe Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 700);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *notebook = gtk_notebook_new();
    
    GtkWidget *new_terminal_btn = gtk_button_new_with_label("New Terminal");
    g_signal_connect_swapped(new_terminal_btn, "clicked", G_CALLBACK(add_terminal), notebook);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_terminal_btn);
    
    // Create the Automate button with its dropdown menu
    GtkWidget *automate_btn = gtk_button_new_with_label("Automate");
    g_object_set_data(G_OBJECT(automate_btn), "notebook", notebook);
    g_object_set_data(G_OBJECT(automate_btn), "window", window);
    const char *automate_items[] = {"EXTSEC", "CVE-Lookup"};
    create_dropdown_menu(automate_btn, automate_items, 2, G_CALLBACK(automate_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), automate_btn);

    // Create the Session button with its dropdown menu
    GtkWidget *session_btn = gtk_button_new_with_label("Session");
    g_object_set_data(G_OBJECT(session_btn), "notebook", notebook);
    g_object_set_data(G_OBJECT(session_btn), "window", window);
    const char *session_items[] = {"Save Session", "Load Session", "Close All"};
    create_dropdown_menu(session_btn, session_items, 3, G_CALLBACK(session_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), session_btn);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);
    add_terminal(GTK_NOTEBOOK(notebook));

    // Set up the AI Assistant panel
    GtkWidget *ai_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(ai_panel, 500, -1);
    GtkWidget *ai_label = gtk_label_new("AI Assistant");
    gtk_box_pack_start(GTK_BOX(ai_panel), ai_label, FALSE, FALSE, 5);

    GtkWidget *ai_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ai_entry), "Ask AI...");
    gtk_box_pack_start(GTK_BOX(ai_panel), ai_entry, FALSE, FALSE, 5);

    GtkWidget *send_button = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(ai_panel), send_button, FALSE, FALSE, 5);

    GtkWidget *response_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(response_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(response_view), GTK_WRAP_WORD_CHAR);

    GtkWidget *response_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(response_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(response_scrolled, -1, 700);
    gtk_container_add(GTK_CONTAINER(response_scrolled), response_view);
    gtk_box_pack_start(GTK_BOX(ai_panel), response_scrolled, TRUE, TRUE, 5);

    // Create a struct to hold both widgets and pass it to the callback
    AiWidgets *widgets = g_new(AiWidgets, 1);
    if (!widgets) {
        g_critical("Failed to allocate memory for AI widgets");
        return;
    }
    widgets->entry = GTK_ENTRY(ai_entry);
    widgets->text_view = GTK_TEXT_VIEW(response_view);

    g_signal_connect(send_button, "clicked", G_CALLBACK(send_ai_message), widgets);
    g_signal_connect(ai_entry, "activate", G_CALLBACK(send_ai_message), widgets);
    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_ai_widgets), widgets);

    gtk_box_pack_start(GTK_BOX(hbox), ai_panel, FALSE, FALSE, 0);
    gtk_widget_show_all(GTK_WIDGET(header));
    gtk_widget_show_all(window);
    gtk_window_maximize(GTK_WINDOW(window));
}

// Handle Ctrl+Shift+C and Ctrl+Shift+V
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    VteTerminal *terminal = VTE_TERMINAL(user_data);

    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_C:
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
                return TRUE;
            case GDK_KEY_V:
                vte_terminal_paste_clipboard(terminal);
                return TRUE;
        }
    }
    return FALSE;
}

// Right-click context menu
void on_copy_activate(GtkMenuItem *item, gpointer user_data) {
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(user_data), VTE_FORMAT_TEXT);
}

void on_paste_activate(GtkMenuItem *item, gpointer user_data) {
    vte_terminal_paste_clipboard(VTE_TERMINAL(user_data));
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

// Add this function definition before create_terminal_widget
static gboolean on_terminal_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    if (!widget || !GTK_IS_WIDGET(widget) || !data) {
        return FALSE;
    }

    TerminalWidget *term_widget = (TerminalWidget *)data;
    if (!term_widget->terminal || !VTE_IS_TERMINAL(term_widget->terminal)) {
        return FALSE;
    }
    
    // Find notebook page
    GtkWidget *parent = widget;
    while (parent && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(parent))) {
        parent = gtk_widget_get_parent(parent);
        if (!parent) break;
    }
    
    if (parent && GTK_IS_CONTAINER(parent)) {
        find_terminals(parent);
    }
    
    // Set current terminal as active
    term_widget->is_active = TRUE;
    
    return FALSE;
}

// Main function
int main(int argc, char **argv) {
    app_directory = g_get_current_dir(); // Get the directory where the app was launched
    GtkApplication *app = gtk_application_new("com.scythe.terminal", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_free(app_directory); // Free the memory for the path
    g_object_unref(app);
    return status;
}
