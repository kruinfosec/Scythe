#include "common.h"
#include "config.h"
#include "ai.h"
#include "terminal.h"
#include "scripts.h"
#include "utils.h"
#include "conversation.h"
#include "db.h"
#include "notes.h"
#include <locale.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdlib.h>
#include <json-glib/json-glib.h>
#include "webview_ai.h"

// Forward declarations
static gboolean on_webview_context_menu(WebKitWebView *web_view, WebKitContextMenu *context_menu,
                                        GdkEvent *event, WebKitHitTestResult *hit_test_result,
                                        gpointer user_data);

gchar *app_directory = NULL;
Conversation *current_conversation = NULL;  // Global current conversation
static gboolean g_logging_enabled = TRUE;

// UI Widgets
GtkWidget *ai_webview = NULL;
GtkWidget *ai_scrolled_window = NULL;
GtkWidget *ai_entry_widget = NULL;  // For auto-focus on toggle (global)
static GtkWidget *g_session_btn = NULL; // Session header bar button for label updates

// Callback for WebView load status
static void on_webview_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data) {
    if (load_event == WEBKIT_LOAD_FINISHED) {
        // Colors are now pre-injected into HTML, no need to re-apply theme
        // This prevents the "darkening" effect on startup
        // apply_ui_theme();  // REMOVED - colors already correct in HTML
    }
}


static void update_session_btn_label(void) {
    if (!g_session_btn) return;
    int sid = session_get_current();
    if (sid != -1) {
        char *name = session_get_name(sid);
        if (name && strlen(name) > 0) {
            char label[128];
            snprintf(label, sizeof(label), "Session: %s", name);
            gtk_button_set_label(GTK_BUTTON(g_session_btn), label);
        } else {
            gtk_button_set_label(GTK_BUTTON(g_session_btn), "Session");
        }
        if (name) g_free(name);
    } else {
        gtk_button_set_label(GTK_BUTTON(g_session_btn), "Session");
    }
}

enum {
    COL_SESSION_CHECK = 0,
    COL_SESSION_ID,
    COL_SESSION_NAME,
    COL_SESSION_DATE,
    COL_SESSION_NOTES,
    NUM_SESSION_COLS
};

static void on_session_check_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data) {
    (void)cell;
    GtkTreeModel *model = GTK_TREE_MODEL(user_data);
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gboolean val;
        gtk_tree_model_get(model, &iter, COL_SESSION_CHECK, &val, -1);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_SESSION_CHECK, !val, -1);
    }
    gtk_tree_path_free(path);
}

static void on_select_all_sessions_toggled(GtkToggleButton *btn, gpointer user_data) {
    GtkListStore *store = GTK_LIST_STORE(user_data);
    gboolean active = gtk_toggle_button_get_active(btn);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter)) {
        do {
            gtk_list_store_set(store, &iter, COL_SESSION_CHECK, active, -1);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter));
    }
}

static void populate_session_store(GtkListStore *store) {
    gtk_list_store_clear(store);
    GPtrArray *sessions = session_list_saved();
    if (!sessions) return;
    for (guint i = 0; i < sessions->len; i++) {
        SavedSessionInfo *info = g_ptr_array_index(sessions, i);
        int note_count = session_note_count(info->id);
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            COL_SESSION_CHECK, FALSE,
            COL_SESSION_ID, info->id,
            COL_SESSION_NAME, info->name ? info->name : "Untitled Session",
            COL_SESSION_DATE, info->start_time ? info->start_time : "",
            COL_SESSION_NOTES, note_count,
            -1);
    }
    g_ptr_array_free(sessions, TRUE);
}

static void load_selected_session(GtkTreeView *tree_view, GtkWidget *dialog) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    GArray *checked_ids = g_array_new(FALSE, FALSE, sizeof(int));

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gboolean checked = FALSE;
            int sid = -1;
            gtk_tree_model_get(model, &iter, COL_SESSION_CHECK, &checked, COL_SESSION_ID, &sid, -1);
            if (checked && sid != -1) {
                g_array_append_val(checked_ids, sid);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    int target_id = -1;
    if (checked_ids->len > 0) {
        target_id = g_array_index(checked_ids, int, 0);
    } else {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gtk_tree_model_get(model, &iter, COL_SESSION_ID, &target_id, -1);
        }
    }

    if (target_id == -1) {
        g_array_free(checked_ids, TRUE);
        return;
    }

    session_set_current(target_id);
    notes_refresh();

    // Refresh AI conversation for loaded session
    if (ai_webview) {
        if (current_conversation) {
            conversation_free(current_conversation);
            current_conversation = NULL;
        }
        GPtrArray *convs = conversation_list_by_db_session(target_id);
        if (convs && convs->len > 0) {
            Conversation *last = g_ptr_array_index(convs, 0);
            current_conversation = conversation_load(last->session_id);
            if (current_conversation) {
                webview_load_conversation(ai_webview, current_conversation->messages);
            }
        } else {
            const char *model_name = (current_settings.groq_model && current_settings.groq_model[0]) ? current_settings.groq_model : "llama-3.3-70b-versatile";
            current_conversation = conversation_new("New Conversation", model_name, FALSE);
            webview_clear(ai_webview);
        }
        if (convs) g_ptr_array_free(convs, TRUE);
    }

    update_session_btn_label();

    char *name = session_get_name(target_id);
    char msg_str[256];
    if (checked_ids->len > 1) {
        snprintf(msg_str, sizeof(msg_str), "Loaded %u sessions into workspace successfully!", checked_ids->len);
    } else {
        snprintf(msg_str, sizeof(msg_str), "Session '%s' loaded successfully!", name ? name : "Selected Session");
    }
    if (name) g_free(name);
    g_array_free(checked_ids, TRUE);

    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_DELETE_EVENT);

    GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg_str);
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
}

static void on_session_rename_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkTreeView *tv = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int sid;
    char *old_name = NULL;
    gtk_tree_model_get(model, &iter, COL_SESSION_ID, &sid, COL_SESSION_NAME, &old_name, -1);

    GtkWidget *rename_dialog = gtk_dialog_new_with_buttons(
        "Rename Session",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tv))),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Rename", GTK_RESPONSE_ACCEPT,
        NULL
    );
    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(rename_dialog));
    GtkWidget *ebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(ebox, 12);
    gtk_widget_set_margin_end(ebox, 12);
    gtk_widget_set_margin_top(ebox, 12);
    gtk_widget_set_margin_bottom(ebox, 12);

    GtkWidget *lbl = gtk_label_new("Enter new session name:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(ebox), lbl, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), old_name ? old_name : "");
    gtk_box_pack_start(GTK_BOX(ebox), entry, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(ca), ebox);
    gtk_widget_show_all(ebox);

    if (gtk_dialog_run(GTK_DIALOG(rename_dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name && strlen(new_name) > 0) {
            session_rename(sid, new_name);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_SESSION_NAME, new_name, -1);
            update_session_btn_label();
        }
    }
    gtk_widget_destroy(rename_dialog);
    g_free(old_name);
}

static void on_session_delete_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkTreeView *tv = GTK_TREE_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tv);
    GtkListStore *store = GTK_LIST_STORE(model);

    // Count checked sessions
    int checked_count = 0;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gboolean checked = FALSE;
            gtk_tree_model_get(model, &iter, COL_SESSION_CHECK, &checked, -1);
            if (checked) checked_count++;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    if (checked_count > 0) {
        char confirm_msg[256];
        snprintf(confirm_msg, sizeof(confirm_msg),
            "Are you sure you want to delete %d selected session(s)?\n\nThis will permanently remove all associated notes, chat history, and terminal logs.",
            checked_count);

        GtkWidget *confirm = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tv))),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "%s", confirm_msg
        );
        gtk_window_set_title(GTK_WINDOW(confirm), "Delete Sessions");

        if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES) {
            gboolean deleted_current = FALSE;
            int cur_sid = session_get_current();
            gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
            while (valid) {
                gboolean checked = FALSE;
                int sid = -1;
                gtk_tree_model_get(model, &iter, COL_SESSION_CHECK, &checked, COL_SESSION_ID, &sid, -1);
                if (checked && sid != -1) {
                    if (sid == cur_sid) {
                        deleted_current = TRUE;
                    }
                    session_delete(sid);
                    valid = gtk_list_store_remove(store, &iter);
                } else {
                    valid = gtk_tree_model_iter_next(model, &iter);
                }
            }
            if (deleted_current) {
                session_set_current(-1);
                notes_reset_state();
                GtkWidget *alert = gtk_message_dialog_new(
                    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tv))),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_WARNING,
                    GTK_BUTTONS_OK,
                    "Current Session Data Deleted\n\nYour active session data has been deleted and cannot be saved. Please load or create a new session."
                );
                gtk_window_set_title(GTK_WINDOW(alert), "Current Session Data Deleted");
                gtk_dialog_run(GTK_DIALOG(alert));
                gtk_widget_destroy(alert);
            }
            update_session_btn_label();
        }
        gtk_widget_destroy(confirm);
    } else {
        // Single focused row fallback
        GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);
        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

        int sid;
        char *sname = NULL;
        gtk_tree_model_get(model, &iter, COL_SESSION_ID, &sid, COL_SESSION_NAME, &sname, -1);

        char confirm_msg[256];
        snprintf(confirm_msg, sizeof(confirm_msg),
            "Are you sure you want to delete session '%s'?\n\nThis will permanently remove all associated notes, chat history, and terminal logs.",
            sname ? sname : "Selected Session");
        g_free(sname);

        GtkWidget *confirm = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tv))),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "%s", confirm_msg
        );
        gtk_window_set_title(GTK_WINDOW(confirm), "Delete Session");

        if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES) {
            int cur_sid = session_get_current();
            session_delete(sid);
            gtk_list_store_remove(store, &iter);
            if (sid == cur_sid) {
                session_set_current(-1);
                notes_reset_state();
                GtkWidget *alert = gtk_message_dialog_new(
                    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tv))),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_WARNING,
                    GTK_BUTTONS_OK,
                    "Current Session Data Deleted\n\nYour active session data has been deleted and cannot be saved. Please load or create a new session."
                );
                gtk_window_set_title(GTK_WINDOW(alert), "Current Session Data Deleted");
                gtk_dialog_run(GTK_DIALOG(alert));
                gtk_widget_destroy(alert);
            }
            update_session_btn_label();
        }
        gtk_widget_destroy(confirm);
    }
}

static void on_session_row_activated(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data) {
    (void)path; (void)col; (void)user_data;
    GtkWidget *dlg = GTK_WIDGET(g_object_get_data(G_OBJECT(tv), "parent-dialog"));
    load_selected_session(tv, dlg);
}

// Function to handle session actions
static void session_action(GtkWidget *widget, gpointer data) {
    MenuItemData *item_data = (MenuItemData *)data;
    const gchar *action = item_data->action;
    
    if (!action) {
        g_critical("Null action received in session_action");
        return;
    }
    
    if (g_strcmp0(action, "Save Session") == 0) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Save Session",
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_DIALOG_MODAL,
            "Cancel", GTK_RESPONSE_CANCEL,
            "Save", GTK_RESPONSE_ACCEPT,
            NULL
        );
        
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter session name...");
        gtk_container_add(GTK_CONTAINER(content_area), entry);
        gtk_widget_show_all(dialog);
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (name && strlen(name) > 0) {
                int session_id = session_get_current();
                if (session_id != -1 && session_save(session_id, name)) {
                    update_session_btn_label();
                    GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Session saved successfully!");
                    gtk_dialog_run(GTK_DIALOG(msg));
                    gtk_widget_destroy(msg);
                } else {
                    GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                        "Current Session Data Deleted\n\nSession data could not be saved because the session was deleted or does not exist.");
                    gtk_window_set_title(GTK_WINDOW(msg), "Current Session Data Deleted");
                    gtk_dialog_run(GTK_DIALOG(msg));
                    gtk_widget_destroy(msg);
                }
            }
        }
        gtk_widget_destroy(dialog);
    } else if (g_strcmp0(action, "Load Session") == 0) {
        GPtrArray *sessions = session_list_saved();
        if (!sessions || sessions->len == 0) {
            GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "No saved sessions found.");
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
            if (sessions) g_ptr_array_free(sessions, TRUE);
            return;
        }
        g_ptr_array_free(sessions, TRUE);

        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Manage Sessions",
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_DIALOG_MODAL,
            "Cancel", GTK_RESPONSE_CANCEL,
            NULL
        );
        gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 350);

        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

        // Top toolbar in content area
        GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(top_box, 8);
        gtk_widget_set_margin_end(top_box, 8);
        gtk_widget_set_margin_top(top_box, 6);
        GtkWidget *select_all_check = gtk_check_button_new_with_label("Select All / Deselect All");
        gtk_box_pack_start(GTK_BOX(top_box), select_all_check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(content_area), top_box, FALSE, FALSE, 0);

        // TreeView with ListStore
        GtkListStore *store = gtk_list_store_new(NUM_SESSION_COLS,
            G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
        populate_session_store(store);

        g_signal_connect(select_all_check, "toggled", G_CALLBACK(on_select_all_sessions_toggled), store);

        GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store);

        GtkCellRenderer *renderer;
        GtkTreeViewColumn *col;

        // Checkbox column
        renderer = gtk_cell_renderer_toggle_new();
        g_signal_connect(renderer, "toggled", G_CALLBACK(on_session_check_toggled), store);
        col = gtk_tree_view_column_new_with_attributes("", renderer, "active", COL_SESSION_CHECK, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

        renderer = gtk_cell_renderer_text_new();
        col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", COL_SESSION_NAME, NULL);
        gtk_tree_view_column_set_expand(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

        renderer = gtk_cell_renderer_text_new();
        col = gtk_tree_view_column_new_with_attributes("Date", renderer, "text", COL_SESSION_DATE, NULL);
        gtk_tree_view_column_set_min_width(col, 160);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

        renderer = gtk_cell_renderer_text_new();
        col = gtk_tree_view_column_new_with_attributes("Notes", renderer, "text", COL_SESSION_NOTES, NULL);
        gtk_tree_view_column_set_min_width(col, 60);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

        GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scrolled), tree_view);
        gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 5);

        // Action buttons
        GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(btn_box, 8);
        gtk_widget_set_margin_end(btn_box, 8);
        gtk_widget_set_margin_bottom(btn_box, 8);

        GtkWidget *load_btn = gtk_button_new_with_label("📂 Load");
        GtkWidget *rename_btn = gtk_button_new_with_label("✏️ Rename");
        GtkWidget *delete_btn = gtk_button_new_with_label("🗑️ Delete");

        gtk_box_pack_start(GTK_BOX(btn_box), load_btn, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(btn_box), rename_btn, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content_area), btn_box, FALSE, FALSE, 0);

        gtk_widget_show_all(dialog);

        // --- Signal handlers ---
        // Load button
        g_signal_connect_swapped(load_btn, "clicked", G_CALLBACK(load_selected_session), tree_view);
        g_object_set_data(G_OBJECT(tree_view), "parent-dialog", dialog);

        // Rename button
        g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_session_rename_clicked), tree_view);

        // Delete button
        g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_session_delete_clicked), tree_view);

        // Double-click to load
        g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_session_row_activated), NULL);

        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void on_logging_toggled(GtkToggleButton *button, gpointer user_data) {
    g_logging_enabled = gtk_toggle_button_get_active(button);
    GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(button));
    if (g_logging_enabled) {
        gtk_style_context_add_class(context, "logging-on");
        g_print("Terminal logging ENABLED\n");
    } else {
        gtk_style_context_remove_class(context, "logging-on");
        g_print("Terminal logging DISABLED\n");
    }
}

// Handle Ctrl+Shift+C, Ctrl+Shift+V, and Alt+Q
// Helper to check if event matches a configured shortcut string
static gboolean check_shortcut(GdkEventKey *event, const char *shortcut_str) {
    if (!shortcut_str) return FALSE;
    guint key;
    GdkModifierType mods;
    gtk_accelerator_parse(shortcut_str, &key, &mods);
    
    // Mask irrelevant modifiers (NumLock, etc.)
    GdkModifierType state = event->state & gtk_accelerator_get_default_mod_mask();
    
    // Strict modifier check
    if (state != mods) return FALSE;
    
    // Check keyval (normalize case to be safe)
    if (event->keyval == key) return TRUE;
    if (gdk_keyval_to_lower(event->keyval) == gdk_keyval_to_lower(key)) return TRUE;
    
    return FALSE;
}

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

static gboolean snip_idle_cb(gpointer data) {
    notes_trigger_snip();
    return G_SOURCE_REMOVE;
}

static GdkFilterReturn global_key_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data) {
    XEvent *ev = (XEvent *)xevent;
    if (ev->type == KeyPress) {
        guint keyval = 0;
        GdkModifierType mods = 0;
        if (current_settings.key_snip_tool) {
            gtk_accelerator_parse(current_settings.key_snip_tool, &keyval, &mods);
        }
        if (keyval != 0) {
            Display *xdisplay = gdk_x11_get_default_xdisplay();
            KeyCode code = XKeysymToKeycode(xdisplay, gdk_keyval_to_lower(keyval));
            if (ev->xkey.keycode == code) {
                g_idle_add(snip_idle_cb, NULL);
                return GDK_FILTER_REMOVE;
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

void setup_global_snip_shortcut(void) {
    GdkDisplay *gdisp = gdk_display_get_default();
    if (!gdisp || !GDK_IS_X11_DISPLAY(gdisp)) return;

    Display *xdisp = gdk_x11_get_default_xdisplay();
    Window root = DefaultRootWindow(xdisp);

    guint keyval = 0;
    GdkModifierType mods = 0;
    if (current_settings.key_snip_tool) {
        gtk_accelerator_parse(current_settings.key_snip_tool, &keyval, &mods);
    }
    if (keyval == 0) return;

    KeyCode code = XKeysymToKeycode(xdisp, gdk_keyval_to_lower(keyval));
    if (code == 0) return;

    unsigned int xmods = 0;
    if (mods & GDK_CONTROL_MASK) xmods |= ControlMask;
    if (mods & GDK_SHIFT_MASK)   xmods |= ShiftMask;
    if (mods & GDK_MOD1_MASK)    xmods |= Mod1Mask;
    if (mods & GDK_SUPER_MASK)   xmods |= Mod4Mask;

    XUngrabKey(xdisp, AnyKey, AnyModifier, root);
    XGrabKey(xdisp, code, xmods, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdisp, code, xmods | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdisp, code, xmods | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdisp, code, xmods | Mod2Mask | LockMask, root, True, GrabModeAsync, GrabModeAsync);

    gdk_window_add_filter(NULL, global_key_filter, NULL);
}
#else
void setup_global_snip_shortcut(void) {}
#endif

static gboolean on_window_any_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    if (event && event->type == GDK_BUTTON_PRESS) {
        extern gboolean notes_handle_window_click(GdkEventButton *event);
        notes_handle_window_click((GdkEventButton *)event);
    }
    return FALSE;
}

// Handle shortcuts
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    VteTerminal *terminal = get_current_terminal(notebook);

    // Handle Close Scythe (Configurable)
    if (check_shortcut(event, current_settings.key_close_app)) {
        GtkWidget *window = gtk_widget_get_toplevel(widget);
        if (GTK_IS_WINDOW(window)) {
            gtk_window_close(GTK_WINDOW(window));
        }
        return TRUE;
    }

    // --- Global Shortcuts (Do not require active terminal) ---

    // Toggle Notes Panel (Configurable, default Alt+N)
    if (check_shortcut(event, current_settings.key_toggle_notes)) {
        notes_toggle();
        return TRUE;
    }

    // Toggle Notes Pin (Configurable, default Alt+Escape)
    if (check_shortcut(event, current_settings.key_pin_notes)) {
        notes_toggle_pin();
        return TRUE;
    }

    // Snip Tool (Configurable, default Ctrl+Alt+S)
    if (check_shortcut(event, current_settings.key_snip_tool)) {
        notes_trigger_snip();
        return TRUE;
    }

    // Toggle AI Panel
    if (check_shortcut(event, current_settings.key_toggle_ai)) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(notebook));
        if (GTK_IS_WINDOW(toplevel)) {
             // Search for the GtkPaned in the main window structure
             // Structure is Window -> VBox -> HPaned (contains notebook and AI panel)
             GtkWidget *vbox = gtk_bin_get_child(GTK_BIN(toplevel));
             if (GTK_IS_BOX(vbox)) {
                 GList *children = gtk_container_get_children(GTK_CONTAINER(vbox));
                 for (GList *l = children; l != NULL; l = l->next) {
                     if (GTK_IS_PANED(l->data)) {
                         GtkWidget *hpaned = GTK_WIDGET(l->data);
                         GtkWidget *ai_panel = gtk_paned_get_child2(GTK_PANED(hpaned));
                          if (ai_panel) {
                              // Call the common toggle function (same as button)
                              extern void toggle_ai_panel_common(GtkPaned *hpaned);
                              toggle_ai_panel_common(GTK_PANED(hpaned));
                          }
                         break; // Found it
                     }
                 }
                 g_list_free(children);
             }
        }
        return TRUE;
    }
    
    // Open Customise Dialog
    if (check_shortcut(event, current_settings.key_customise)) {
        open_customise_dialog(widget, notebook);
        return TRUE;
    }
    
    // Increase Opacity
    if (check_shortcut(event, current_settings.key_increase_opacity)) {
        current_settings.opacity += 0.05;  // Increase by 5%
        if (current_settings.opacity > 1.0) current_settings.opacity = 1.0;
        current_settings.background_color.alpha = current_settings.opacity;
        apply_settings_to_all_terminals(notebook);
        // save_settings(); // Performance: Avoid disk IO on rapid keypress
        return TRUE;
    }
    
    // Decrease Opacity
    if (check_shortcut(event, current_settings.key_decrease_opacity)) {
        current_settings.opacity -= 0.05;  // Decrease by 5%
        if (current_settings.opacity < 0.0) current_settings.opacity = 0.0;
        current_settings.background_color.alpha = current_settings.opacity;
        apply_settings_to_all_terminals(notebook);
        // save_settings(); // Performance: Avoid disk IO on rapid keypress
        return TRUE;
    }
    
    // New Terminal
    if (check_shortcut(event, current_settings.key_new_term)) {
        add_terminal(notebook);
        return TRUE;
    }

    // --- Terminal-Dependent Shortcuts ---
    if (!terminal) {
        return FALSE;
    }

    // Dynamic Keybindings
    if (check_shortcut(event, current_settings.key_copy)) {
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_paste)) {
        vte_terminal_paste_clipboard(terminal);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_split_v)) {
        TerminalWidget *tw = g_object_get_data(G_OBJECT(terminal), "term-widget");
        if (tw) split_terminal_vertical(NULL, tw);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_split_h)) {
        TerminalWidget *tw = g_object_get_data(G_OBJECT(terminal), "term-widget");
        if (tw) split_terminal_horizontal(NULL, tw);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_collapse)) {
        TerminalWidget *tw = g_object_get_data(G_OBJECT(terminal), "term-widget");
        if (tw) collapse_subterminal(NULL, tw);
        return TRUE;
    }

    // Split Navigation
    if (check_shortcut(event, current_settings.key_focus_left)) {
        navigate_terminal_split(notebook, GTK_DIR_LEFT);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_focus_right)) {
        navigate_terminal_split(notebook, GTK_DIR_RIGHT);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_focus_up)) {
        navigate_terminal_split(notebook, GTK_DIR_UP);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_focus_down)) {
        navigate_terminal_split(notebook, GTK_DIR_DOWN);
        return TRUE;
    }
    
    // Notebook Tab Navigation (Prev/Next) - Requires tabs
    if (check_shortcut(event, current_settings.key_prev_tab)) {
        int page = gtk_notebook_get_current_page(notebook);
        int n_pages = gtk_notebook_get_n_pages(notebook);
        if (n_pages > 1) {
            page--;
            if (page < 0) page = n_pages - 1;
            gtk_notebook_set_current_page(notebook, page);
        }
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_next_tab)) {
        int page = gtk_notebook_get_current_page(notebook);
        int n_pages = gtk_notebook_get_n_pages(notebook);
        if (n_pages > 1) {
            page++;
            if (page >= n_pages) page = 0;
            gtk_notebook_set_current_page(notebook, page);
        }
        return TRUE;
    }
    
    // Direct Tab Access (Configurable)
    if (check_shortcut(event, current_settings.key_tab_1)) {
        if (gtk_notebook_get_n_pages(notebook) > 0) gtk_notebook_set_current_page(notebook, 0);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_2)) {
        if (gtk_notebook_get_n_pages(notebook) > 1) gtk_notebook_set_current_page(notebook, 1);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_3)) {
        if (gtk_notebook_get_n_pages(notebook) > 2) gtk_notebook_set_current_page(notebook, 2);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_4)) {
        if (gtk_notebook_get_n_pages(notebook) > 3) gtk_notebook_set_current_page(notebook, 3);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_5)) {
        if (gtk_notebook_get_n_pages(notebook) > 4) gtk_notebook_set_current_page(notebook, 4);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_6)) {
        if (gtk_notebook_get_n_pages(notebook) > 5) gtk_notebook_set_current_page(notebook, 5);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_7)) {
        if (gtk_notebook_get_n_pages(notebook) > 6) gtk_notebook_set_current_page(notebook, 6);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_8)) {
        if (gtk_notebook_get_n_pages(notebook) > 7) gtk_notebook_set_current_page(notebook, 7);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_9)) {
        if (gtk_notebook_get_n_pages(notebook) > 8) gtk_notebook_set_current_page(notebook, 8);
        return TRUE;
    }
    if (check_shortcut(event, current_settings.key_tab_10)) {
        if (gtk_notebook_get_n_pages(notebook) > 9) gtk_notebook_set_current_page(notebook, 9);
        return TRUE;
    }

    // Close Active Terminal Tab
    if (check_shortcut(event, current_settings.key_close_term)) {
        // ALWAYS close the entire tab (remove page from notebook)
        int page = gtk_notebook_get_current_page(notebook);
        if (page != -1) {
            gtk_notebook_remove_page(notebook, page);
        }
        return TRUE;
    }

    return FALSE;
}

// Handle window close request
// Callback to enforce panel width on startup
static void on_paned_realize(GtkWidget *widget, gpointer data) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    int width = alloc.width;
    // Set position to (Total Width - 600), enforcing the 600px AI panel
    if (width > 600) {
        int position = width - 600;
        gtk_paned_set_position(GTK_PANED(widget), position);
        // Update last_paned_position so button toggle knows correct position
        extern gint last_paned_position;
        last_paned_position = position;
    }
}

static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    int session_id = session_get_current();
    if (session_id == -1) return FALSE; // No session, just close

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widget),
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_QUESTION,
                                             GTK_BUTTONS_NONE,
                                             "Do you want to save the current session?");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Don't Save", GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Save", GTK_RESPONSE_YES);

    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (result == GTK_RESPONSE_CANCEL) {
        return TRUE; // Prevent closing
    } else if (result == GTK_RESPONSE_YES) {
        // Show save dialog
        GtkWidget *save_dialog = gtk_dialog_new_with_buttons(
            "Save Session",
            GTK_WINDOW(widget),
            GTK_DIALOG_MODAL,
            "Cancel", GTK_RESPONSE_CANCEL,
            "Save", GTK_RESPONSE_ACCEPT,
            NULL
        );
        
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(save_dialog));
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter session name...");
        gtk_container_add(GTK_CONTAINER(content_area), entry);
        gtk_widget_show_all(save_dialog);
        
        if (gtk_dialog_run(GTK_DIALOG(save_dialog)) == GTK_RESPONSE_ACCEPT) {
            const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (name && strlen(name) > 0) {
                session_save(session_id, name);
            }
        }
        gtk_widget_destroy(save_dialog);
    }
    
    return FALSE; // Allow closing
}

// Global label for model footer
static GtkWidget *model_footer_label = NULL;

static void on_model_selected(GtkMenuItem *item, gpointer user_data) {
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        const char *model = gtk_menu_item_get_label(item);
        if (model) {
            if (current_settings.groq_model) g_free(current_settings.groq_model);
            current_settings.groq_model = g_strdup(model);
            
            // Update footer label
            if (model_footer_label) {
                char *text = g_strdup_printf("Model: %s", model);
                gtk_label_set_text(GTK_LABEL(model_footer_label), text);
                g_free(text);
            }
            save_settings();
        }
    }
}

// Conversation Control Callbacks
static void on_new_conversation_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *webview = GTK_WIDGET(user_data);
    
    // Free current conversation (already saved by auto-save after AI response)
    if (current_conversation) {
        conversation_free(current_conversation);
        current_conversation = NULL;
    }
    
    // Create new conversation
    const char *model = current_settings.groq_model ? current_settings.groq_model : "llama-3.3-70b-versatile";
    current_conversation = conversation_new("New Conversation", model, FALSE);
    
    // Clear AI response view
    webview_clear(webview);
}

static void on_rename_conversation_clicked(GtkButton *button, gpointer user_data) {
    if (!current_conversation) return;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Rename Conversation",
        NULL,
        GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Rename", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), current_conversation->title);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_title = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_title && strlen(new_title) > 0) {
            g_free(current_conversation->title);
            current_conversation->title = g_strdup(new_title);
            current_conversation->auto_titled = FALSE;  // Mark as manually titled
            current_conversation->modified = time(NULL);
            conversation_save(current_conversation);
        }
    }
    
    gtk_widget_destroy(dialog);
}

static void on_export_pdf_clicked(GtkMenuItem *item, gpointer user_data) {
    if (!current_conversation) {
        GtkWidget *err_dialog = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "No conversation to export!");
        gtk_dialog_run(GTK_DIALOG(err_dialog));
        gtk_widget_destroy(err_dialog);
        return;
    }
    
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Export as PDF",
        NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Save",
        "_Cancel"
    );
    
    char default_name[256];
    snprintf(default_name, sizeof(default_name), "%s.pdf", current_conversation->title);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), default_name);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        gboolean success = conversation_export_pdf(current_conversation, filename);
        
        GtkWidget *msg_dialog = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL,
            success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            success ? "PDF exported successfully!" : "Failed to export PDF."
        );
        gtk_dialog_run(GTK_DIALOG(msg_dialog));
        gtk_widget_destroy(msg_dialog);
        g_free(filename);
    }
    
    g_object_unref(native);
}

static void on_export_markdown_clicked(GtkMenuItem *item, gpointer user_data) {
    if (!current_conversation) {
        GtkWidget *err_dialog = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "No conversation to export!");
        gtk_dialog_run(GTK_DIALOG(err_dialog));
        gtk_widget_destroy(err_dialog);
        return;
    }
    
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Export as Markdown",
        NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Save",
        "_Cancel"
    );
    
    char default_name[256];
    snprintf(default_name, sizeof(default_name), "%s.md", current_conversation->title);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), default_name);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        gboolean success = conversation_export_markdown(current_conversation, filename);
        
        GtkWidget *msg_dialog = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL,
            success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            success ? "Markdown exported successfully!" : "Failed to export Markdown."
        );
        gtk_dialog_run(GTK_DIALOG(msg_dialog));
        gtk_widget_destroy(msg_dialog);
        g_free(filename);
    }
    
    g_object_unref(native);
}

// Callback for conversation title button in popover
static void on_conversation_title_clicked(GtkButton *button, gpointer user_data) {
    const char *session_id = (const char *)g_object_get_data(G_OBJECT(button), "session_id");
    GtkWidget *popover = (GtkWidget *)g_object_get_data(G_OBJECT(button), "popover");
    GtkWidget *webview = (GtkWidget *)g_object_get_data(G_OBJECT(button), "webview");
    
    if (!session_id) return;
    
    // Load the selected conversation
    Conversation *selected = conversation_load(session_id);
    if (!selected) return;
    
    // Replace current conversation
    if (current_conversation) {
        conversation_free(current_conversation);
    }
    current_conversation = selected;
    
    // Clear and populate AI response view with conversation history
    webview_clear(webview);
    
    // Display all messages
    for (guint i = 0; i < current_conversation->messages->len; i++) {
        Message *msg = g_ptr_array_index(current_conversation->messages, i);
        webview_append_message(webview, msg->role, msg->content);
    }
    
    // Close popover
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

// Callback for delete button in popover
static void on_delete_button_clicked_popover(GtkButton *button, gpointer user_data) {
    const char *session_id = (const char *)g_object_get_data(G_OBJECT(button), "session_id");
    GtkWidget *row_box = (GtkWidget *)g_object_get_data(G_OBJECT(button), "row_box");
    
    if (!session_id) {
        return;
    }
    
    // Delete immediately without confirmation
    conversation_delete(session_id);
    
    // If deleted conversation was current, create new one
    if (current_conversation && strcmp(current_conversation->session_id, session_id) == 0) {
        conversation_free(current_conversation);
        const char *model = current_settings.groq_model ? current_settings.groq_model : "llama-3.3-70b-versatile";
        current_conversation = conversation_new("New Conversation", model, FALSE);
        webview_clear(ai_webview);
    }
    
    // Remove row from popover
    if (row_box) {
        gtk_widget_destroy(row_box);
    }
}

// Comparison function for sorting conversations by modified time
static gint conversation_compare_modified(gconstpointer a, gconstpointer b) {
    Conversation *conv_a = *(Conversation **)a;
    Conversation *conv_b = *(Conversation **)b;
    return conv_b->modified - conv_a->modified;
}

// Callback for All Chats button click - populate and show popover
static void on_all_chats_clicked(gpointer user_data) {
    
    typedef struct { GtkWidget *popover; GtkWidget *chats_box; GtkWidget *webview; } PopoverData;
    PopoverData *data = (PopoverData *)user_data;
    
    GtkWidget *chats_box = data->chats_box;
    GtkWidget *popover = data->popover;
    GtkWidget *webview = data->webview;
    
    // Clear existing items
    GList *children = gtk_container_get_children(GTK_CONTAINER(chats_box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    
    // Get conversations for current session
    GPtrArray *conversations = conversation_list_by_db_session(session_get_current());
    
    if (conversations->len == 0) {
        GtkWidget *label = gtk_label_new("No conversations yet");
        gtk_widget_set_sensitive(label, FALSE);
        gtk_box_pack_start(GTK_BOX(chats_box), label, FALSE, FALSE, 5);
    } else {
        // Sort by modified time (most recent first)
        g_ptr_array_sort(conversations, conversation_compare_modified);
        
        for (guint i = 0; i < conversations->len; i++) {
            Conversation *conv = g_ptr_array_index(conversations, i);
            
            // Create row for this conversation
            GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_set_margin_top(row_box, 2);
            gtk_widget_set_margin_bottom(row_box, 2);
            gtk_widget_set_margin_start(row_box, 5);
            gtk_widget_set_margin_end(row_box, 5);
            
            // Conversation title label (clickable)
            char label_text[300];
            if (current_conversation && strcmp(conv->session_id, current_conversation->session_id) == 0) {
                snprintf(label_text, sizeof(label_text), "🟢 %s", conv->title);
            } else {
                snprintf(label_text, sizeof(label_text), "%s", conv->title);
            }
            
            GtkWidget *title_button = gtk_button_new_with_label(label_text);
            gtk_button_set_relief(GTK_BUTTON(title_button), GTK_RELIEF_NONE);
            gtk_widget_set_halign(title_button, GTK_ALIGN_START);
            g_object_set_data_full(G_OBJECT(title_button), "session_id", 
                                  g_strdup(conv->session_id), g_free);
            g_object_set_data(G_OBJECT(title_button), "popover", popover);
            g_object_set_data(G_OBJECT(title_button), "webview", webview);
            gtk_box_pack_start(GTK_BOX(row_box), title_button, TRUE, TRUE, 0);
            
            // Delete button
            GtkWidget *delete_btn = gtk_button_new_with_label("🗑️");
            gtk_button_set_relief(GTK_BUTTON(delete_btn), GTK_RELIEF_NONE);
            gtk_widget_set_tooltip_text(delete_btn, "Delete Conversation");
            g_object_set_data_full(G_OBJECT(delete_btn), "session_id", 
                                  g_strdup(conv->session_id), g_free);
            g_object_set_data(G_OBJECT(delete_btn), "row_box", row_box);
            gtk_box_pack_end(GTK_BOX(row_box), delete_btn, FALSE, FALSE, 0);
            
            // Connect signals
            g_signal_connect(title_button, "clicked", 
                           G_CALLBACK(on_conversation_title_clicked), NULL);
            g_signal_connect(delete_btn, "clicked", 
                           G_CALLBACK(on_delete_button_clicked_popover), NULL);
            
            gtk_box_pack_start(GTK_BOX(chats_box), row_box, FALSE, FALSE, 0);
        }
    }
    
    g_ptr_array_free(conversations, TRUE);
    
    gtk_widget_show_all(chats_box);
    gtk_widget_show_all(popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void activate(GtkApplication *app, gpointer user_data) {
    load_settings();


    // Initialize Database
    if (!db_init()) {
        fprintf(stderr, "Failed to initialize database\n");
    } else {
        // Start a new session
        session_create(NULL);
    }

    // Load CSS
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const gchar *css_data = 
        "#ai-response-view {\n"
        "    font-family: 'Monospace';\n"
        "    font-size: 11pt;\n"
        "}\n"
        "\n"
        ".ai-code-view {\n"
        "    background-color: #1e1e1e;\n"
        "    color: #d4d4d4;\n"
        "    font-family: 'Monospace';\n"
        "    font-size: 10pt;\n"
        "    padding: 8px;\n"
        "    border: 1px solid #555;\n"
        "}\n"
        "\n"
        "#ai-response-view text {\n"
        "    background-color: transparent;\n"
        "}\n"
        "\n"
        "window, notebook { background-color: transparent; }\n";
    
    gtk_css_provider_load_from_data(css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Scythe Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 700);

    // Enable transparency
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *notebook = gtk_notebook_new();
    
    GtkWidget *new_terminal_btn = gtk_button_new_with_label("New Terminal");
    g_signal_connect_swapped(new_terminal_btn, "clicked", G_CALLBACK(add_terminal), notebook);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_terminal_btn);
    
    // Create the Automate button and build its dynamic menu
    GtkWidget *automate_btn = gtk_button_new_with_label("Automate");
    g_object_set_data(G_OBJECT(automate_btn), "notebook", notebook);
    g_object_set_data(G_OBJECT(automate_btn), "window", window);
    rebuild_automate_menu(automate_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), automate_btn);

    // Create the Session button with its dropdown menu
    GtkWidget *session_btn = gtk_button_new_with_label("Session");
    g_session_btn = session_btn;
    g_object_set_data(G_OBJECT(session_btn), "notebook", notebook);
    g_object_set_data(G_OBJECT(session_btn), "window", window);
    const char *session_items[] = {"Save Session", "Load Session"};
    GList *session_list = NULL;
    for(int i=0; i<2; i++) session_list = g_list_append(session_list, (gpointer)session_items[i]);
    create_dropdown_menu(session_btn, session_list, G_CALLBACK(session_action));
    g_list_free(session_list);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), session_btn);

    // Create the Customise button
    GtkWidget *customise_btn = gtk_button_new_with_label("Customise");
    g_signal_connect(customise_btn, "clicked", G_CALLBACK(open_customise_dialog), notebook);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), customise_btn);

    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(hpaned, "main-paned");

    GtkWidget *toggle_ai_btn = gtk_button_new_with_label("Dama");
    gtk_widget_set_name(toggle_ai_btn, "dama-toggle-button");
    g_signal_connect(toggle_ai_btn, "clicked", G_CALLBACK(on_toggle_ai_panel), hpaned);
    
    GtkWidget *logging_toggle_btn = gtk_toggle_button_new_with_label("Log");
    gtk_widget_set_name(logging_toggle_btn, "logging-toggle-button");
    g_signal_connect(logging_toggle_btn, "toggled", G_CALLBACK(on_logging_toggled), NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(logging_toggle_btn), g_logging_enabled);
    on_logging_toggled(GTK_TOGGLE_BUTTON(logging_toggle_btn), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), toggle_ai_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), logging_toggle_btn);
    
    // Initialize Notes UI and button
    notes_init(header);

    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);
    
    // Set close-to-final position immediately to minimize visible adjustment
    // Assume typical window width and set position for 600px AI panel
    gint estimated_pos = 1200; // Safe for 1920px screens (most common)
    gtk_paned_set_position(GTK_PANED(hpaned), estimated_pos);
    
    // Fine-tune after window is actually realized with real dimensions
    g_signal_connect(hpaned, "realize", G_CALLBACK(on_paned_realize), NULL);

    // Allow the notebook (terminal area) to be shrunk significantly to fit the AI panel
    gtk_widget_set_size_request(notebook, 200, -1);
    gtk_paned_pack1(GTK_PANED(hpaned), notebook, TRUE, TRUE);
    add_terminal(GTK_NOTEBOOK(notebook));

    // Set up the AI Assistant panel
    // AI Panel (Vertical Box)
    GtkWidget *ai_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); // Remove spacing between header and webview
    gtk_widget_set_name(ai_panel, "ai-panel");
    gtk_style_context_add_class(gtk_widget_get_style_context(ai_panel), "ai-panel");
    gtk_widget_set_size_request(ai_panel, 600, -1);
    // Padding handled by CSS (.ai-panel)

    GtkWidget *ai_header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(ai_panel), ai_header_box, FALSE, FALSE, 5);

    GtkWidget *ai_label = gtk_label_new("Ai Assistance");
    gtk_label_set_xalign(GTK_LABEL(ai_label), 0.5);
    gtk_box_pack_start(GTK_BOX(ai_header_box), ai_label, TRUE, TRUE, 5);

    GtkWidget *model_menu_btn = gtk_menu_button_new();
    GtkWidget *icon = gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(model_menu_btn), icon);
    gtk_widget_set_tooltip_text(model_menu_btn, "Select AI Model");
    gtk_style_context_add_class(gtk_widget_get_style_context(model_menu_btn), "flat-button");
    
    GtkWidget *model_menu = gtk_menu_new();
    const char *models[] = {
        "llama-3.3-70b-versatile",
        "llama-3.1-8b-instant",
        "openai/gpt-oss-120b",
        "openai/gpt-oss-20b",
        "qwen/qwen3-32b"
    };
    
    GSList *group = NULL;
    for (int i = 0; i < 5; i++) {
        GtkWidget *item = gtk_radio_menu_item_new_with_label(group, models[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        
        // Check if this is the current model
        if (current_settings.groq_model && g_strcmp0(current_settings.groq_model, models[i]) == 0) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        }
        
        g_signal_connect(item, "toggled", G_CALLBACK(on_model_selected), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(model_menu), item);
    }
    
    gtk_widget_show_all(model_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(model_menu_btn), model_menu);
    gtk_box_pack_start(GTK_BOX(ai_header_box), model_menu_btn, FALSE, FALSE, 0);

    // Conversation Control Buttons
    // New Conversation Button
    GtkWidget *new_conv_btn = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(new_conv_btn, "Start New Conversation");
    gtk_style_context_add_class(gtk_widget_get_style_context(new_conv_btn), "flat-button");
    gtk_box_pack_start(GTK_BOX(ai_header_box), new_conv_btn, FALSE, FALSE, 2);

    // All Chats Button (with dropdown)
    GtkWidget *all_chats_btn = gtk_button_new_with_label("💬");
    gtk_widget_set_tooltip_text(all_chats_btn, "View All Conversations");
    gtk_style_context_add_class(gtk_widget_get_style_context(all_chats_btn), "flat-button");
    gtk_box_pack_start(GTK_BOX(ai_header_box), all_chats_btn, FALSE, FALSE, 2);

    // Rename Button  
    GtkWidget *rename_btn = gtk_button_new_with_label("✏️");
    gtk_widget_set_tooltip_text(rename_btn, "Rename Current Conversation");
    gtk_style_context_add_class(gtk_widget_get_style_context(rename_btn), "flat-button");
    gtk_box_pack_start(GTK_BOX(ai_header_box), rename_btn, FALSE, FALSE, 2);

    // More Menu Button
    GtkWidget *more_menu_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(more_menu_btn), "⋮");
    gtk_widget_set_tooltip_text(more_menu_btn, "More Options");
    gtk_style_context_add_class(gtk_widget_get_style_context(more_menu_btn), "flat-button");
    
    GtkWidget *more_menu = gtk_menu_new();
    GtkWidget *export_pdf_item = gtk_menu_item_new_with_label("📤 Export as PDF");
    GtkWidget *export_md_item = gtk_menu_item_new_with_label("📤 Export as Markdown");
    gtk_menu_shell_append(GTK_MENU_SHELL(more_menu), export_pdf_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(more_menu), export_md_item);
    gtk_widget_show_all(more_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(more_menu_btn), more_menu);
    gtk_box_pack_start(GTK_BOX(ai_header_box), more_menu_btn, FALSE, FALSE, 2);

    // Initialize conversation storage directory
    conversation_ensure_storage_dir();
    
    //Note: response_view will be created later, need to connect after
    // Store button widgets for later connection
    GtkWidget *stored_new_conv_btn = new_conv_btn;
    GtkWidget *stored_rename_btn = rename_btn;
    GtkWidget *stored_export_pdf_item = export_pdf_item;
    GtkWidget *stored_export_md_item = export_md_item;
    GtkWidget *stored_all_chats_btn = all_chats_btn;
    
    // Create popover for All Chats
    GtkWidget *chats_popover = gtk_popover_new(all_chats_btn);
    gtk_popover_set_position(GTK_POPOVER(chats_popover), GTK_POS_BOTTOM);
    
    // Create a scrollable container for the conversation list
    GtkWidget *scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll_window), 400);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll_window), TRUE);
    gtk_widget_set_size_request(scroll_window, 450, -1);
    
    // Box to hold conversation list
    GtkWidget *chats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(scroll_window), chats_box);
    gtk_container_add(GTK_CONTAINER(chats_popover), scroll_window);
    
    // Store widgets for later access
    g_object_set_data(G_OBJECT(stored_all_chats_btn), "popover", chats_popover);
    g_object_set_data(G_OBJECT(stored_all_chats_btn), "chats_box", chats_box);
    
    // Create initial conversation if not exists
    if (!current_conversation) {
        const char *model = current_settings.groq_model ? current_settings.groq_model : "llama-3.3-70b-versatile";
        current_conversation = conversation_new("New Conversation", model, FALSE);
    }

    GtkWidget *ai_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ai_entry), "Ask AI...");
    gtk_widget_set_margin_start(ai_entry, 4); // Added margin
    gtk_box_pack_start(GTK_BOX(ai_panel), ai_entry, FALSE, FALSE, 5);
    
    // Store globally for auto-focus on toggle
    ai_entry_widget = ai_entry;

    // Button Box
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_start(button_box, 4); // Added margin
    gtk_box_pack_start(GTK_BOX(ai_panel), button_box, FALSE, FALSE, 5);

    GtkWidget *send_button = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(button_box), send_button, TRUE, TRUE, 0);
    
    GtkWidget *visualize_button = gtk_button_new_with_label("Visualize");
    gtk_widget_set_tooltip_text(visualize_button, "Generate Diagram from Input");
    gtk_box_pack_start(GTK_BOX(button_box), visualize_button, FALSE, FALSE, 0);

    // Create WebView for AI responses
    ai_webview = create_ai_webview();
    gtk_widget_set_name(ai_webview, "ai-response-view");
    gtk_widget_set_size_request(ai_webview, 1, -1);
    g_signal_connect(ai_webview, "context-menu", G_CALLBACK(on_webview_context_menu), NULL);

    ai_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ai_scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ai_scrolled_window), GTK_SHADOW_NONE); // Remove "grey margin"
    
    // Scrolled window container for AI WebView
    // WebView already created above
    
    // Ensure theme is applied after HTML is fully loaded
    g_signal_connect(ai_webview, "load-changed", G_CALLBACK(on_webview_load_changed), NULL);

    gtk_container_add(GTK_CONTAINER(ai_scrolled_window), ai_webview);
    gtk_box_pack_start(GTK_BOX(ai_panel), ai_scrolled_window, TRUE, TRUE, 0);

    // Footer Label
    model_footer_label = gtk_label_new(NULL);
    if (current_settings.groq_model) {
        char *text = g_strdup_printf("Model: %s", current_settings.groq_model);
        gtk_label_set_text(GTK_LABEL(model_footer_label), text);
        g_free(text);
    } else {
        gtk_label_set_text(GTK_LABEL(model_footer_label), "Model: Default");
    }
    gtk_widget_set_halign(model_footer_label, GTK_ALIGN_END);
    gtk_widget_set_margin_end(model_footer_label, 10);
    gtk_widget_set_margin_bottom(model_footer_label, 5);
    gtk_widget_set_opacity(model_footer_label, 0.7);
    
    gtk_box_pack_end(GTK_BOX(ai_panel), model_footer_label, FALSE, FALSE, 0);

    // Connect conversation control button signals  
    g_signal_connect(stored_new_conv_btn, "clicked", G_CALLBACK(on_new_conversation_clicked), ai_webview);
    g_signal_connect(stored_rename_btn, "clicked", G_CALLBACK(on_rename_conversation_clicked), NULL);
    g_signal_connect(stored_export_pdf_item, "activate", G_CALLBACK(on_export_pdf_clicked), NULL);
    g_signal_connect(stored_export_md_item, "activate", G_CALLBACK(on_export_markdown_clicked), NULL);
    
    // Connect All Chats button to populate and show popover
    typedef struct { GtkWidget *popover; GtkWidget *chats_box; GtkWidget *webview; } PopoverData;
    PopoverData *popover_data = g_new(PopoverData, 1);
    popover_data->popover = chats_popover;
    popover_data->chats_box = chats_box;
    popover_data->webview = ai_webview;
    g_signal_connect_swapped(stored_all_chats_btn, "clicked", 
                           G_CALLBACK(on_all_chats_clicked), popover_data);

    AiWidgets *widgets = g_new(AiWidgets, 1);
    if (!widgets) {
        g_critical("Failed to allocate memory for AI widgets");
        return;
    }
    widgets->entry = GTK_ENTRY(ai_entry);
    widgets->webview = ai_webview;

    g_signal_connect(send_button, "clicked", G_CALLBACK(send_ai_message), widgets);
    g_signal_connect(visualize_button, "clicked", G_CALLBACK(send_visualization_request), widgets);
    g_signal_connect(ai_entry, "activate", G_CALLBACK(send_ai_message), widgets);
    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_ai_widgets), widgets);

    gtk_paned_pack2(GTK_PANED(hpaned), ai_panel, FALSE, TRUE);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), notebook);
    g_signal_connect(window, "event", G_CALLBACK(on_window_any_event), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete_event), NULL);
    gtk_widget_show_all(GTK_WIDGET(header));
    gtk_widget_show_all(window);
    
    // Hide AI panel by default on startup (prevents resize/loading artifacts)
    gtk_widget_set_visible(ai_panel, FALSE);
    gtk_window_maximize(GTK_WINDOW(window));

    // Apply UI theme (CSS) now that screen is available
    apply_ui_theme();
    setup_global_snip_shortcut();

    VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
    if (terminal) {
        gtk_widget_grab_focus(GTK_WIDGET(terminal));
    }
}

#include "theme_manager.h"

// Disable WebKit context menu
static gboolean on_webview_context_menu(WebKitWebView *web_view, WebKitContextMenu *context_menu,
                                        GdkEvent *event, WebKitHitTestResult *hit_test_result,
                                        gpointer user_data) {
    return TRUE; // Return TRUE to suppress the default context menu
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    g_setenv("GTK_USE_PORTAL", "1", TRUE);
    g_set_prgname("scythe-terminal");
    gtk_window_set_default_icon_name("scythe");
    app_directory = g_get_current_dir();
    
    // Initialize systems
    theme_manager_init(); // Create default themes directory
    load_settings();

    gchar *scripts_dir = get_scripts_directory();
    if (scripts_dir) g_free(scripts_dir);
    GtkApplication *app = gtk_application_new("com.scythe.terminal", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    // Cleanup
    save_settings(); // Persist settings on exit
    db_close();

    return status;
}
