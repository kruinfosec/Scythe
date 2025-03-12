#include <gtk/gtk.h>
#include <vte/vte.h>

// Function to spawn a new terminal
static void spawn_terminal(GtkWidget *widget, gpointer terminal) {
    vte_terminal_spawn_async(VTE_TERMINAL(terminal),
        VTE_PTY_DEFAULT, NULL, (char *[]){g_strdup(g_getenv("SHELL")), NULL}, NULL,
        0, NULL, NULL, NULL, -1, NULL, NULL, NULL);
}

// Automation Menu Actions
static void automate_action(GtkWidget *widget, gpointer data) {
    g_print("Automation Option Selected: %s\n", (char *)data);
}

// Session Menu Actions
static void session_action(GtkWidget *widget, gpointer data) {
    g_print("Session Option Selected: %s\n", (char *)data);
}

// AI Interaction Function
static void send_ai_message(GtkWidget *widget, gpointer user_data) {
    GtkWidget **widgets = (GtkWidget **)user_data;
    GtkWidget *entry = widgets[0];
    GtkWidget *response_label = widgets[1];

    const gchar *input_text = gtk_entry_get_text(GTK_ENTRY(entry));
    
    if (g_strcmp0(input_text, "") != 0) {
        gchar *response_text = g_strdup_printf("AI Response: %s", input_text);  // Placeholder response
        gtk_label_set_text(GTK_LABEL(response_label), response_text);
        g_free(response_text);
        gtk_entry_set_text(GTK_ENTRY(entry), ""); // Clear input field
    }
}

// Create a drop-down menu
static GtkWidget* create_dropdown_menu(GtkWidget *button, const char *menu_items[], int num_items, GCallback callback) {
    GtkWidget *menu = gtk_menu_new();
    
    for (int i = 0; i < num_items; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(menu_items[i]);
        g_signal_connect(item, "activate", callback, (gpointer)menu_items[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_widget_show_all(menu);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_menu_popup_at_widget), menu);
    
    return menu;
}

// Main UI
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window, *main_box, *left_box, *right_box, *header;
    GtkWidget *new_terminal_btn, *automate_btn, *session_btn;
    GtkWidget *terminal, *ai_entry, *send_btn, *ai_response_label;
    
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Scythe Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // Left Side (Terminal)
    left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), left_box, TRUE, TRUE, 0);

    // Toolbar (Header Bar)
    header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    // New Terminal Button
    new_terminal_btn = gtk_button_new_with_label("New Terminal");
    g_signal_connect(new_terminal_btn, "clicked", G_CALLBACK(spawn_terminal), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_terminal_btn);

    // Automate Drop-down Menu
    const char *automate_items[] = {"Auto Recon", "Scan Network", "Exploit Search"};
    automate_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(automate_btn), "Automate");
    GtkWidget *automate_menu = create_dropdown_menu(automate_btn, automate_items, 3, G_CALLBACK(automate_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), automate_btn);

    // Session Drop-down Menu
    const char *session_items[] = {"Save Session", "Load Session", "Close All"};
    session_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(session_btn), "Session");
    GtkWidget *session_menu = create_dropdown_menu(session_btn, session_items, 3, G_CALLBACK(session_action));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), session_btn);

    // Terminal Workspace
    terminal = vte_terminal_new();
    gtk_box_pack_start(GTK_BOX(left_box), terminal, TRUE, TRUE, 0);
    spawn_terminal(NULL, terminal); // Start initial terminal session

    // Right Side (AI Assistant)
    right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_set_homogeneous(GTK_BOX(right_box), FALSE);
    gtk_box_pack_start(GTK_BOX(main_box), right_box, FALSE, FALSE, 0);

    // AI Input Entry
    ai_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ai_entry), "Ask AI...");
    gtk_box_pack_start(GTK_BOX(right_box), ai_entry, FALSE, FALSE, 5);

    // Send Button
    send_btn = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(right_box), send_btn, FALSE, FALSE, 5);

    // AI Response Label
    ai_response_label = gtk_label_new("AI Response will appear here...");
    gtk_label_set_xalign(GTK_LABEL(ai_response_label), 0);
    gtk_box_pack_start(GTK_BOX(right_box), ai_response_label, FALSE, FALSE, 5);

    // Connect AI Send Button
    GtkWidget *ai_widgets[] = {ai_entry, ai_response_label};
    g_signal_connect(send_btn, "clicked", G_CALLBACK(send_ai_message), ai_widgets);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.scythe.terminal", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
