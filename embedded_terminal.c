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

// Define a struct for tracking terminal widgets
typedef struct {
    GtkWidget *box;        // Container box
    GtkWidget *terminal;   // The VTE terminal
    GtkWidget *parent;     // Parent container (paned or box)
    gboolean is_split;     // Flag to indicate if this terminal is split
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
static gboolean update_text_view(gchar *message, GtkTextView *text_view);
static VteTerminal* get_current_terminal(GtkNotebook *notebook);
static void run_python_script(VteTerminal *terminal, const char *script_path);
static void cleanup_ai_widgets(GtkWidget *widget, gpointer user_data);
static gboolean on_key_press(GtkWidget *widget,GdkEventKey *event, gpointer user_data);
static gboolean on_button_press(GtkWidget *widget,GdkEventButton *event, gpointer user_data);
static void on_copy_activate(GtkMenuItem *item, gpointer user_data);
static void on_paste_activate(GtkMenuItem *item, gpointer user_data);
static TerminalWidget* create_terminal_widget(GtkWidget *parent);
static void popup_menu_at_button(GtkWidget *button, GtkWidget *menu);
static void free_menu_item_data(gpointer data);

// Function to update the text view with a message
static gboolean update_text_view(gchar *message, GtkTextView *text_view) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_set_text(buffer, message, -1);
    g_free(message);
    return FALSE;
}

// Callback function for AI response
static void ai_response_callback(SoupSession *session, SoupMessage *msg, gpointer user_data) {
    if (!SOUP_IS_SESSION(session) || !SOUP_IS_MESSAGE(msg)) {
        g_critical("Invalid SoupSession or SoupMessage object.");
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    if (!GTK_IS_TEXT_VIEW(text_view)) {
        g_critical("Invalid text view object.");
        return;
    }

    if (msg->status_code != SOUP_STATUS_OK) {
        gchar *message = g_strdup_printf("Error: HTTP %u - %s", msg->status_code, msg->reason_phrase);
        g_idle_add((GSourceFunc)update_text_view, message);
        return;
    }

    if (!msg->response_body) {
        gchar *message = g_strdup("Error: No response body received.");
        g_idle_add((GSourceFunc)update_text_view, message);
        return;
    }

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, msg->response_body->data, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "response")) {
                const gchar *response = json_object_get_string_member(obj, "response");
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
                gtk_text_buffer_set_text(buffer, response, -1);
            } else {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
                gtk_text_buffer_set_text(buffer, "Error: Missing 'response' key in JSON.", -1);
            }
        } else {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
            gtk_text_buffer_set_text(buffer, "Error: Invalid JSON object.", -1);
        }
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        gtk_text_buffer_set_text(buffer, "Error: Failed to parse JSON response.", -1);
    }

    g_object_unref(parser);
}

// Function to send a message to the AI
static void send_ai_message(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    GtkEntry *entry = widgets->entry;
    GtkTextView *text_view = widgets->text_view;

    if (!entry || !text_view) {
        g_critical("Failed to find required widgets.");
        return;
    }

    const gchar *input_text = gtk_entry_get_text(entry);
    if (!input_text || strlen(input_text) == 0) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        gtk_text_buffer_set_text(buffer, "Please enter a message first.", -1);
        return;
    }

    // Let the user know that their message is being processed
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_set_text(buffer, "Processing your request...", -1);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, "llama3");
    json_builder_set_member_name(builder, "prompt");
    json_builder_add_string_value(builder, input_text);
    json_builder_set_member_name(builder, "stream");
    json_builder_add_boolean_value(builder, false);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *json_str = json_generator_to_data(gen, NULL);

    if (!json_str) {
        g_print("Error: Failed to generate JSON string.\n");
        gtk_text_buffer_set_text(buffer, "Error generating request.", -1);
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
        return;
    }

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", "http://127.0.0.1:11434/api/generate");
    
    if (!session || !msg) {
        g_critical("Failed to create SoupSession or SoupMessage");
        g_free(json_str);
        gtk_text_buffer_set_text(buffer, "Error connecting to AI service.", -1);
        if (session) g_object_unref(session);
        if (msg) g_object_unref(msg);
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
        return;
    }
    
    soup_message_set_request(msg, "application/json", SOUP_MEMORY_COPY, json_str, strlen(json_str));

    // Clear the entry field after sending
    gtk_entry_set_text(entry, "");

    // Queue message and set up callback
    g_object_ref(text_view); // Ensure text_view exists through callback
    soup_session_queue_message(session, msg, ai_response_callback, text_view);

    // Cleanup
    g_free(json_str);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
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
    
    // Set up the terminal
    spawn_shell(VTE_TERMINAL(term_widget->terminal));
    gtk_widget_set_hexpand(term_widget->terminal, TRUE);
    gtk_widget_set_vexpand(term_widget->terminal, TRUE);
    
    // Connect signals for right-click menu
    g_signal_connect(term_widget->terminal, "button-press-event", 
                     G_CALLBACK(on_terminal_button_press), term_widget);

    // ⇨ Connect Ctrl+Shift+C/V handlers for copy & paste
    g_signal_connect(term_widget->terminal, "key-press-event",
                     G_CALLBACK(on_key_press),
                     term_widget->terminal);

    // Add terminal to box
    gtk_box_pack_start(GTK_BOX(term_widget->box), term_widget->terminal, TRUE, TRUE, 0);
    gtk_widget_show_all(term_widget->box);
    
    return term_widget;
}

// Function to split the terminal horizontally
static void split_terminal_horizontal(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *parent = gtk_widget_get_parent(term_widget->box);
    
    // Remove the terminal box from its parent
    g_object_ref(term_widget->box); // Increase reference count so it's not destroyed
    gtk_container_remove(GTK_CONTAINER(parent), term_widget->box);
    
    // Create a new paned container
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 400); // Default split position
    
    // Add the existing terminal to the left pane
    gtk_paned_pack1(GTK_PANED(paned), term_widget->box, TRUE, TRUE);
    g_object_unref(term_widget->box); // Release our reference
    
    // Create a new terminal for the right pane
    TerminalWidget *new_term = create_terminal_widget(paned);
    gtk_paned_pack2(GTK_PANED(paned), new_term->box, TRUE, TRUE);
    
    // Update the original terminal's parent reference
    term_widget->parent = paned;
    term_widget->is_split = TRUE;
    
    // Add the paned widget to the parent container
    gtk_container_add(GTK_CONTAINER(parent), paned);
    gtk_widget_show_all(parent);
}

// Function to split the terminal vertically
static void split_terminal_vertical(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *parent = gtk_widget_get_parent(term_widget->box);
    
    // Remove the terminal box from its parent
    g_object_ref(term_widget->box); // Increase reference count so it's not destroyed
    gtk_container_remove(GTK_CONTAINER(parent), term_widget->box);
    
    // Create a new paned container
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(paned), 300); // Default split position
    
    // Add the existing terminal to the top pane
    gtk_paned_pack1(GTK_PANED(paned), term_widget->box, TRUE, TRUE);
    g_object_unref(term_widget->box); // Release our reference
    
    // Create a new terminal for the bottom pane
    TerminalWidget *new_term = create_terminal_widget(paned);
    gtk_paned_pack2(GTK_PANED(paned), new_term->box, TRUE, TRUE);
    
    // Update the original terminal's parent reference
    term_widget->parent = paned;
    term_widget->is_split = TRUE;
    
    // Add the paned widget to the parent container
    gtk_container_add(GTK_CONTAINER(parent), paned);
    gtk_widget_show_all(parent);
}

// Function to collapse a subterminal
static void collapse_subterminal(GtkWidget *widget, gpointer data) {
    TerminalWidget *term_widget = (TerminalWidget *)data;
    GtkWidget *paned = gtk_widget_get_parent(term_widget->box);
    
    // Only proceed if the parent is actually a paned widget
    if (!GTK_IS_PANED(paned)) {
        g_print("This terminal is not in a split view.\n");
        return;
    }
    
    // Get the paned's parent
    GtkWidget *grandparent = gtk_widget_get_parent(paned);
    if (!grandparent) {
        g_print("Cannot collapse: no grandparent container found.\n");
        return;
    }
    
    // Remove our terminal from the paned
    g_object_ref(term_widget->box); // Increase reference count
    gtk_container_remove(GTK_CONTAINER(paned), term_widget->box);
    
    // Remove the paned from its parent
    gtk_container_remove(GTK_CONTAINER(grandparent), paned);
    
    // Add our terminal directly to the grandparent
    gtk_container_add(GTK_CONTAINER(grandparent), term_widget->box);
    g_object_unref(term_widget->box); // Release our reference
    
    // Update terminal widget state
    term_widget->parent = grandparent;
    term_widget->is_split = FALSE;
    
    gtk_widget_show_all(grandparent);
}

// Function to handle terminal button press events
static gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        TerminalWidget *term_widget = (TerminalWidget *)data;
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *split_horizontally = gtk_menu_item_new_with_label("Split Horizontally");
        GtkWidget *split_vertically = gtk_menu_item_new_with_label("Split Vertically");
        GtkWidget *collapse_item = gtk_menu_item_new_with_label("Collapse Subterminal");

        g_signal_connect(split_horizontally, "activate", G_CALLBACK(split_terminal_horizontal), term_widget);
        g_signal_connect(split_vertically, "activate", G_CALLBACK(split_terminal_vertical), term_widget);
        g_signal_connect(collapse_item, "activate", G_CALLBACK(collapse_subterminal), term_widget);

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
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *label = gtk_label_new(title);
    GtkWidget *close_btn = gtk_button_new_with_label("x");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(close_btn, 15, 15);
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(close_tab), notebook);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), close_btn, FALSE, FALSE, 0);
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
    
    // Create arguments for the Python script
    char *argv[] = { "python3", (char *)script_path, NULL };

    // Spawn the process in the terminal
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

static void automate_action(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    if (!item_data) {
        g_critical("Null data received in automate_action");
        return;
    }
    
    const gchar *action = item_data->action;
    GtkWidget *notebook = item_data->notebook;
    
    g_print("Automation action: %s\n", action);
    
    if (g_strcmp0(action, "EXTSEC") == 0) {
        if (!notebook || !GTK_IS_NOTEBOOK(notebook)) {
            g_critical("Invalid notebook widget");
            return;
        }
        
        // Get current terminal directly using the notebook
        VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
        if (!terminal) {
            g_critical("Could not get current terminal");
            return;
        }
        
        // Run the server.py script
        const char *script_path = "./server.py";
        if (access(script_path, F_OK) == -1) {
            g_critical("Script not found: %s", script_path);
            return;
        }
        
        g_print("Running script: %s\n", script_path);
        run_python_script(terminal, script_path);
    }
    else if (g_strcmp0(action, "CVE-Lookup") == 0) {
        if (!notebook || !GTK_IS_NOTEBOOK(notebook)) {
            g_critical("Invalid notebook widget");
            return;
        }
        
        // Get current terminal directly using the notebook
        VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
        if (!terminal) {
            g_critical("Could not get current terminal");
            return;
        }
        
        // Run the CVE-Lookup.py script
        const char *script_path = "./CVE-Lookup.py";
        if (access(script_path, F_OK) == -1) {
            g_critical("Script not found: %s", script_path);
            return;
        }
        
        g_print("Running script: %s\n", script_path);
        run_python_script(terminal, script_path);
    }
}

// Function to handle session actions
static void session_action(GtkWidget *widget, gpointer data) {
    const gchar *action = (const gchar *)data;
    
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

gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy");
        GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste");

        g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy_activate), user_data);
        g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste_activate), user_data);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste_item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}


// Main function

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.scythe.terminal", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}