#include "scripts.h"
#include "terminal.h"
#include "utils.h"

// Gets the path to the user's custom scripts directory, creating it if it doesn't exist.
gchar* get_scripts_directory() {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *scripts_dir = g_build_filename(config_dir, "Scythe", "scripts", NULL);

    if (g_mkdir_with_parents(scripts_dir, 0755) != 0) {
        g_warning("Failed to create scripts directory: %s", scripts_dir);
        g_free(scripts_dir);
        return NULL;
    }

    return scripts_dir;
}

static void on_remove_script_button_clicked(GtkWidget *button, gpointer data) {
    GtkWidget *row = GTK_WIDGET(data);
    const gchar *filename = g_object_get_data(G_OBJECT(row), "filename");
    
    gchar *scripts_dir = get_scripts_directory();
    if (scripts_dir && filename) {
        gchar *file_path = g_build_filename(scripts_dir, filename, NULL);
        GFile *file = g_file_new_for_path(file_path);
        GError *error = NULL;
        if (g_file_delete(file, NULL, &error)) {
            gtk_widget_destroy(row);
        } else {
            g_warning("Failed to delete script file: %s", error->message);
            g_error_free(error);
        }
        g_object_unref(file);
        g_free(file_path);
    }
    g_free(scripts_dir);
}

typedef struct {
    GtkWidget *main_window;
    GtkWidget *automate_btn;
} ScriptRemoveDialogData;

static void on_script_remove_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    ScriptRemoveDialogData *dialog_data = (ScriptRemoveDialogData *)user_data;
    rebuild_automate_menu(dialog_data->automate_btn);
    gtk_widget_destroy(GTK_WIDGET(dialog));
    g_free(dialog_data);
}

static void show_remove_scripts_dialog(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    GtkWidget *parent_window = gtk_widget_get_toplevel(item_data->window);

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Remove Script",
                                                    GTK_WINDOW(parent_window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close",
                                                    GTK_RESPONSE_CLOSE,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(content_area, 8);
    gtk_widget_set_margin_end(content_area, 8);
    gtk_widget_set_margin_top(content_area, 8);
    gtk_widget_set_margin_bottom(content_area, 8);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled_window, TRUE, TRUE, 0);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), list_box);

    gchar *scripts_dir = get_scripts_directory();
    if (scripts_dir) {
        GDir *dir = g_dir_open(scripts_dir, 0, NULL);
        if (dir) {
            const gchar *filename;
            while ((filename = g_dir_read_name(dir))) {
                GtkWidget *row = gtk_list_box_row_new();
                g_object_set_data(G_OBJECT(row), "filename", g_strdup(filename));

                GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_start(hbox, 6);
                gtk_widget_set_margin_end(hbox, 6);
                gtk_widget_set_margin_top(hbox, 4);
                gtk_widget_set_margin_bottom(hbox, 4);
                gtk_container_add(GTK_CONTAINER(row), hbox);

                GtkWidget *label = gtk_label_new(filename);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

                GtkWidget *remove_button = gtk_button_new_with_label("Remove");
                g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_script_button_clicked), row);
                gtk_box_pack_end(GTK_BOX(hbox), remove_button, FALSE, FALSE, 0);
                
                gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);
            }
            g_dir_close(dir);
        }
        g_free(scripts_dir);
    }
    
    GtkWidget *header = gtk_window_get_titlebar(GTK_WINDOW(parent_window));
    GList *children = gtk_container_get_children(GTK_CONTAINER(header));
    GtkWidget *automate_btn = NULL;
    for (GList *l = children; l != NULL; l = l->next) {
        const gchar *name = gtk_button_get_label(GTK_BUTTON(l->data));
        if (name && g_strcmp0(name, "Automate") == 0) {
            automate_btn = GTK_WIDGET(l->data);
            break;
        }
    }
    g_list_free(children);

    ScriptRemoveDialogData *dialog_data = g_new(ScriptRemoveDialogData, 1);
    dialog_data->main_window = parent_window;
    dialog_data->automate_btn = automate_btn;

    g_signal_connect(dialog, "response", G_CALLBACK(on_script_remove_dialog_response), dialog_data);

    gtk_widget_show_all(dialog);
}

void add_automation_script(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    GtkWidget *parent_window = gtk_widget_get_toplevel(item_data->window);

    GtkFileChooserNative *native = gtk_file_chooser_native_new("Select a Script",
                                                                GTK_IS_WINDOW(parent_window) ? GTK_WINDOW(parent_window) : NULL,
                                                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                                                "_Open",
                                                                "_Cancel");

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        gchar *source_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        gchar *scripts_dir = get_scripts_directory();
        if (source_path && scripts_dir) {
            gchar *filename = g_path_get_basename(source_path);
            gchar *dest_path = g_build_filename(scripts_dir, filename, NULL);

            GFile *source_file = g_file_new_for_path(source_path);
            GFile *dest_file = g_file_new_for_path(dest_path);

            GError *error = NULL;
            if (!g_file_copy(source_file, dest_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
                g_warning("Error copying script: %s", error->message);
                g_error_free(error);
            } else {
                GtkWidget *header = gtk_window_get_titlebar(GTK_WINDOW(parent_window));
                GList *children = gtk_container_get_children(GTK_CONTAINER(header));
                for (GList *l = children; l != NULL; l = l->next) {
                    const gchar *name = gtk_button_get_label(GTK_BUTTON(l->data));
                    if (name && g_strcmp0(name, "Automate") == 0) {
                        rebuild_automate_menu(GTK_WIDGET(l->data));
                        break;
                    }
                }
                g_list_free(children);
            }

            g_free(filename);
            g_free(dest_path);
            g_object_unref(source_file);
            g_object_unref(dest_file);
        }
        g_free(source_path);
        g_free(scripts_dir);
    }

    g_object_unref(native);
}

void automate_action(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    if (!item_data) {
        g_critical("Null data received in automate_action");
        return;
    }
    
    const gchar *action = item_data->action;
    GtkWidget *notebook = item_data->notebook;
    
    if (g_strcmp0(action, "Add New Script...") == 0) {
        add_automation_script(widget, data);
        return;
    } else if (g_strcmp0(action, "Remove Script...") == 0) {
        show_remove_scripts_dialog(widget, data);
        return;
    }

    VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
    if (!terminal) {
        g_critical("Could not get current terminal");
        return;
    }

    gchar *script_path = NULL;
    gchar *command = NULL;

    const char *py_bin = (access("/usr/bin/python3", X_OK) == 0) ? "/usr/bin/python3" : "python3";

    if (g_strcmp0(action, "EXTSEC") == 0) {
        script_path = g_build_filename("/usr/share/scythe", "extsec.py", NULL);
        if (access(script_path, F_OK) == -1) {
            g_free(script_path);
            script_path = g_build_filename(app_directory, "extsec.py", NULL);
        }
        command = g_strdup_printf("%s \"%s\"\n", py_bin, script_path);
    } else if (g_strcmp0(action, "CVE-Lookup") == 0) {
        script_path = g_build_filename("/usr/share/scythe", "CVE-Lookup.py", NULL);
        if (access(script_path, F_OK) == -1) {
            g_free(script_path);
            script_path = g_build_filename(app_directory, "CVE-Lookup.py", NULL);
        }
        command = g_strdup_printf("%s \"%s\"\n", py_bin, script_path);
    } else {
        // Handle custom scripts
        gchar *scripts_dir = get_scripts_directory();
        if (scripts_dir) {
            script_path = g_build_filename(scripts_dir, action, NULL);
            if (g_str_has_suffix(action, ".py")) {
                command = g_strdup_printf("%s \"%s\"\n", py_bin, script_path);
            } else if (g_str_has_suffix(action, ".sh")) {
                command = g_strdup_printf("bash \"%s\"\n", script_path);
            } else if (g_file_test(script_path, G_FILE_TEST_IS_EXECUTABLE)) {
                command = g_strdup_printf("\"%s\"\n", script_path);
            } else {
                g_warning("Script is not executable or has an unsupported extension: %s", action);
            }
            g_free(scripts_dir);
        }
    }

    if (command && script_path) {
        if (access(script_path, F_OK) == -1) {
            g_critical("Script not found: %s", script_path);
        } else {
            g_print("Running command: %s", command);
            vte_terminal_feed_child(terminal, command, -1);
        }
    }
    
    g_free(script_path);
    g_free(command);
}

void rebuild_automate_menu(GtkWidget *automate_btn) {
    GList *menu_items = NULL;
    menu_items = g_list_append(menu_items, g_strdup("EXTSEC"));
    menu_items = g_list_append(menu_items, g_strdup("CVE-Lookup"));
    
    gchar *scripts_dir = get_scripts_directory();
    if (scripts_dir) {
        GDir *dir = g_dir_open(scripts_dir, 0, NULL);
        if (dir) {
            const gchar *filename;
            gboolean has_custom_scripts = FALSE;
            while ((filename = g_dir_read_name(dir))) {
                if (!has_custom_scripts) {
                    menu_items = g_list_append(menu_items, g_strdup("---"));
                    has_custom_scripts = TRUE;
                }
                menu_items = g_list_append(menu_items, g_strdup(filename));
            }
            g_dir_close(dir);
        }
        g_free(scripts_dir);
    }

    menu_items = g_list_append(menu_items, g_strdup("---"));
    menu_items = g_list_append(menu_items, g_strdup("Add New Script..."));
    menu_items = g_list_append(menu_items, g_strdup("Remove Script..."));

    create_dropdown_menu(automate_btn, menu_items, G_CALLBACK(automate_action));

    g_list_free_full(menu_items, g_free);
}
