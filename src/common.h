#ifndef COMMON_H
#define COMMON_H

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib-object.h>
#include <unistd.h>
#include <gio/gio.h>
#include "conversation.h"

// Global variable to store the application's starting directory
extern gchar *app_directory;

// Global current conversation
extern Conversation *current_conversation;

// Global AI Panel WebView (for theming)
extern GtkWidget *ai_webview;

// Global AI Entry Widget (for auto-focus)
extern GtkWidget *ai_entry_widget;

// Global AI Panel visibility state
extern gboolean ai_panel_visible;

// Define a struct to hold all customizable settings
typedef struct {
    gchar *theme;
    gchar *font;
    GdkRGBA background_color;
    gdouble opacity;
    gint scrollback_lines;
    gboolean unlimited_scrollback;
    gboolean audible_bell;
    gchar *cursor_shape;
    gboolean cursor_blinks;
    gchar *ai_provider;
    gchar *groq_api_key;
    gchar *groq_model;
    gboolean json_mode_enabled;
    gchar *key_copy;
    gchar *key_paste;
    gchar *key_split_v;
    gchar *key_split_h;
    gchar *key_collapse;
    gchar *key_toggle_ai;
    gchar *key_toggle_notes;
    gchar *key_pin_notes;
    gchar *key_snip_tool;
    gchar *key_new_term;
    gchar *key_prev_tab;
    gchar *key_next_tab;
    gchar *key_close_term;
    gchar *key_customise;
    gchar *key_tab_1;
    gchar *key_tab_2;
    gchar *key_tab_3;
    gchar *key_tab_4;
    gchar *key_tab_5;
    gchar *key_tab_6;
    gchar *key_tab_7;
    gchar *key_tab_8;
    gchar *key_tab_9;
    gchar *key_tab_10;
    gchar *key_close_app;
    gchar *key_focus_left;
    gchar *key_focus_right;
    gchar *key_focus_up;
    gchar *key_focus_down;
    gchar *key_increase_opacity;
    gchar *key_decrease_opacity;

    // Theme Colors
    GdkRGBA foreground_color;          // Terminal text
    GdkRGBA cursor_background_color;   // Terminal cursor bg
    GdkRGBA cursor_foreground_color;   // Terminal cursor text
    
    GdkRGBA ui_background_color;       // Window/Header background
    GdkRGBA ui_foreground_color;       // General UI text
    
    GdkRGBA button_background_color;
    GdkRGBA button_foreground_color;
    GdkRGBA button_hover_background_color;
    GdkRGBA button_hover_foreground_color;
    
    GdkRGBA ai_panel_background_color;
    GdkRGBA ai_panel_text_color;
    GdkRGBA ai_user_msg_bg_color;
    GdkRGBA ai_user_msg_text_color;
    GdkRGBA ai_assistant_msg_bg_color;
    GdkRGBA ai_assistant_msg_text_color;
} AppSettings;

extern AppSettings current_settings;

// Define a struct for passing data to menu item callbacks
typedef struct {
    gchar *action;
    GtkWidget *notebook;
    GtkWidget *window;
} MenuItemData;

#endif // COMMON_H
