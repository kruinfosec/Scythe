#include "utils.h"

void free_menu_item_data(gpointer data) {
    if (data) {
        MenuItemData *item_data = (MenuItemData *)data;
        g_free(item_data->action);
        g_free(item_data);
    }
}

// Helper function to show menu at button
void popup_menu_at_button(GtkWidget *button, GtkWidget *menu) {
    gtk_menu_popup_at_widget(GTK_MENU(menu), button, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

// Function to create a dropdown menu - updated version to handle GList
GtkWidget* create_dropdown_menu(GtkWidget *button, GList *menu_items, GCallback callback) {
    GtkWidget *old_menu = g_object_get_data(G_OBJECT(button), "dropdown-menu");
    if (old_menu) {
        gtk_widget_destroy(old_menu);
    }

    GtkWidget *menu = gtk_menu_new();
    GtkWidget *notebook = g_object_get_data(G_OBJECT(button), "notebook");
    GtkWidget *window = g_object_get_data(G_OBJECT(button), "window");

    for (GList *l = menu_items; l != NULL; l = l->next) {
        const gchar *label = (const gchar *)l->data;
        GtkWidget *item;

        if (g_strcmp0(label, "---") == 0) {
            item = gtk_separator_menu_item_new();
        } else {
            item = gtk_menu_item_new_with_label(label);
            MenuItemData *item_data = g_new(MenuItemData, 1);
            item_data->action = g_strdup(label);
            item_data->notebook = notebook;
            item_data->window = window;
            g_signal_connect(item, "activate", callback, item_data);
            g_object_set_data_full(G_OBJECT(item), "item-data", item_data, (GDestroyNotify)free_menu_item_data);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_widget_show_all(menu);
    g_object_set_data_full(G_OBJECT(button), "dropdown-menu", menu, (GDestroyNotify)gtk_widget_destroy);
    g_signal_handlers_disconnect_by_func(button, G_CALLBACK(popup_menu_at_button), old_menu);
    g_signal_connect(button, "clicked", G_CALLBACK(popup_menu_at_button), menu);

    return menu;
}
