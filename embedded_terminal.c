#include <gtk/gtk.h>
#include <vte/vte.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib-object.h>

// Forward declarations
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
static GtkWidget* create_dropdown_menu(GtkWidget *button, const char *menu_items[], int num_items, GCallback callback);
static void send_ai_message(GtkWidget *widget, gpointer user_data);
static gboolean update_text_view(gchar *message, GtkTextView *text_view);

typedef struct {
    GtkEntry *entry;
    GtkTextView *text_view;
} AiWidgets;

static gboolean update_text_view(gchar *message, GtkTextView *text_view) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_set_text(buffer, message, -1);
    g_free(message);
    return FALSE;
}

static void ai_response_callback(SoupSession *session, SoupMessage *msg, gpointer user_data) {
    if (!SOUP_IS_SESSION(session) || !SOUP_IS_MESSAGE(msg)) {
        g_critical("Invalid SoupSession or SoupMessage object.");
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

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

static void send_ai_message(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    GtkEntry *entry = widgets->entry;
    GtkTextView *text_view = widgets->text_view;

    if (!entry || !text_view) {
        g_critical("Failed to find required widgets.");
        return;
    }

    const gchar *input_text = gtk_entry_get_text(entry);

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
        goto cleanup;
    }

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", "http://127.0.0.1:11434/api/generate");
    soup_message_set_request(msg, "application/json", SOUP_MEMORY_COPY, json_str, strlen(json_str));

    g_object_ref(session);
    g_object_ref(msg);

    soup_session_queue_message(session, msg, ai_response_callback, (gpointer)text_view);

cleanup:
    g_free(json_str);
    g_object_unref(gen);
    g_object_unref(builder);
}

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

static void close_tab(GtkWidget *widget, gpointer data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(data);
    int page = gtk_notebook_get_current_page(notebook);
    if (page != -1) {
        gtk_notebook_remove_page(notebook, page);
    }
}

static void split_terminal_horizontal(GtkWidget *widget, gpointer data) {
    GtkWidget *container = GTK_WIDGET(data);

    if (!GTK_IS_PANED(container)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(container));
        if (!children) return;
        GtkWidget *old_terminal = (GtkWidget *)g_list_nth_data(children, 0);
        g_list_free(children);
        gtk_container_remove(GTK_CONTAINER(container), old_terminal);

        GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(paned), old_terminal, TRUE, TRUE);

        VteTerminal *new_terminal = VTE_TERMINAL(vte_terminal_new());
        spawn_shell(new_terminal);
        gtk_widget_set_hexpand(GTK_WIDGET(new_terminal), TRUE);
        gtk_widget_set_vexpand(GTK_WIDGET(new_terminal), TRUE);
        gtk_paned_pack2(GTK_PANED(paned), GTK_WIDGET(new_terminal), TRUE, TRUE);

        gtk_box_pack_start(GTK_BOX(container), paned, TRUE, TRUE, 0);
        gtk_widget_show_all(container);
    } else {
        g_print("Already split horizontally.\n");
    }
}

static void split_terminal_vertical(GtkWidget *widget, gpointer data) {
    GtkWidget *container = GTK_WIDGET(data);

    if (!GTK_IS_PANED(container)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(container));
        if (!children) return;
        GtkWidget *old_terminal = (GtkWidget *)g_list_nth_data(children, 0);
        g_list_free(children);
        gtk_container_remove(GTK_CONTAINER(container), old_terminal);

        GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        gtk_paned_pack1(GTK_PANED(paned), old_terminal, TRUE, TRUE);

        VteTerminal *new_terminal = VTE_TERMINAL(vte_terminal_new());
        spawn_shell(new_terminal);
        gtk_widget_set_hexpand(GTK_WIDGET(new_terminal), TRUE);
        gtk_widget_set_vexpand(GTK_WIDGET(new_terminal), TRUE);
        gtk_paned_pack2(GTK_PANED(paned), GTK_WIDGET(new_terminal), TRUE, TRUE);

        gtk_box_pack_start(GTK_BOX(container), paned, TRUE, TRUE, 0);
        gtk_widget_show_all(container);
    } else {
        g_print("Already split vertically.\n");
    }
}

static void collapse_subterminal(GtkWidget *widget, gpointer data) {
    GtkWidget *container = GTK_WIDGET(data);
    if (GTK_IS_PANED(container)) {
        GtkWidget *child1 = gtk_paned_get_child1(GTK_PANED(container));
        GtkWidget *parent = gtk_widget_get_parent(container);
        gtk_container_remove(GTK_CONTAINER(parent), container);
        gtk_container_add(GTK_CONTAINER(parent), child1);
        gtk_widget_show_all(parent);
    } else {
        g_print("No subterminal to collapse.\n");
    }
}

static gboolean on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        GtkWidget *terminal_box = GTK_WIDGET(data);
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *split_horizontally = gtk_menu_item_new_with_label("Split Horizontally");
        GtkWidget *split_vertically = gtk_menu_item_new_with_label("Split Vertically");
        GtkWidget *collapse_item = gtk_menu_item_new_with_label("Collapse Subterminal");

        g_signal_connect(split_horizontally, "activate", G_CALLBACK(split_terminal_horizontal), terminal_box);
        g_signal_connect(split_vertically, "activate", G_CALLBACK(split_terminal_vertical), terminal_box);
        g_signal_connect(collapse_item, "activate", G_CALLBACK(collapse_subterminal), terminal_box);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), split_horizontally);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), split_vertically);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), collapse_item);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

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

static void add_terminal(GtkNotebook *notebook) {
    GtkWidget *terminal_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    VteTerminal *terminal = VTE_TERMINAL(vte_terminal_new());
    spawn_shell(terminal);
    gtk_widget_set_hexpand(GTK_WIDGET(terminal), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(terminal), TRUE);
    g_signal_connect(GTK_WIDGET(terminal), "button-press-event", G_CALLBACK(on_terminal_button_press), terminal_box);
    gtk_box_pack_start(GTK_BOX(terminal_box), GTK_WIDGET(terminal), TRUE, TRUE, 0);

    int current_page = gtk_notebook_get_current_page(notebook);
    int new_page = (current_page >= 0) ? current_page + 1 : 0;
    gtk_notebook_insert_page(notebook, terminal_box, create_tab_label(notebook, "Terminal"), new_page);
    gtk_notebook_set_current_page(notebook, new_page);
    gtk_widget_show_all(GTK_WIDGET(notebook));
}

static void automate_action(GtkWidget *widget, gpointer data) {
    g_print("Automation Option Selected: %s\n", (char *)data);
}

static void session_action(GtkWidget *widget, gpointer data) {
    g_print("Session Option Selected: %s\n", (char *)data);
}

static GtkWidget* create_dropdown_menu(GtkWidget *button, const char *menu_items[], int num_items, GCallback callback) {
    if (!button || !menu_items || num_items <= 0) {
        g_critical("Invalid parameters passed to create_dropdown_menu");
        return NULL;
    }

    GtkWidget *menu = gtk_menu_new();
    for (int i = 0; i < num_items; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(menu_items[i]);
        g_signal_connect(item, "activate", callback, g_strdup(menu_items[i]));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_menu_popup_at_widget), menu);
    return menu;
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Scythe Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 700);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *new_terminal_btn = gtk_button_new_with_label("New Terminal");
    g_signal_connect(new_terminal_btn, "clicked", G_CALLBACK(add_terminal), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_terminal_btn);

    const char *automate_items[] = {"Auto Recon", "Scan Network", "Exploit Search"};
    GtkWidget *automate_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(automate_btn), "Automate");
    create_dropdown_menu(automate_btn, automate_items, 3, G_CALLBACK(automate_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), automate_btn);

    const char *session_items[] = {"Save Session", "Load Session", "Close All"};
    GtkWidget *session_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(session_btn), "Session");
    create_dropdown_menu(session_btn, session_items, 3, G_CALLBACK(session_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), session_btn);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);
    add_terminal(GTK_NOTEBOOK(notebook));

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
    gtk_box_pack_start(GTK_BOX(ai_panel), response_scrolled, FALSE, FALSE, 5);

    // Create a struct to hold both widgets
    AiWidgets *widgets = g_new(AiWidgets, 1);
    widgets->entry = GTK_ENTRY(ai_entry);
    widgets->text_view = GTK_TEXT_VIEW(response_view);

    g_signal_connect(send_button, "clicked", G_CALLBACK(send_ai_message), widgets);

    gtk_box_pack_start(GTK_BOX(hbox), ai_panel, FALSE, FALSE, 0);
    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.scythe.terminal", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}