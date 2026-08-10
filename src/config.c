#include "config.h"
#include "common.h"
#include "db.h"
#include "theme_manager.h"
#include "terminal.h" // Needed for get_current_terminal
#include "webview_ai.h" // Needed for webview theming
#include <sys/stat.h>

static void set_terminal_background_recursively(GtkWidget *widget, const GdkRGBA *color);

// Global instance of settings, with defaults
AppSettings current_settings = {
    .theme = NULL,
    .font = NULL,
    .background_color = {0, 0, 0, 1}, // Black
    .opacity = 1.0,
    .scrollback_lines = 10000,
    .unlimited_scrollback = TRUE,
    .audible_bell = FALSE,
    .cursor_shape = NULL,
    .cursor_blinks = TRUE,
    .ai_provider = NULL,
    .groq_api_key = NULL,
    .groq_api_key = NULL,
    .groq_model = NULL,
    .json_mode_enabled = FALSE,
    .key_copy = NULL,
    .key_paste = NULL,
    .key_split_v = NULL,
    .key_split_h = NULL,
    .key_collapse = NULL,
    .key_toggle_ai = NULL,
    .key_toggle_notes = NULL,
    .key_pin_notes = NULL,
    .key_snip_tool = NULL,
    .key_new_term = NULL,
    .key_prev_tab = NULL,
    .key_next_tab = NULL,
    .key_close_term = NULL,
    .key_customise = NULL,
    .key_tab_1 = NULL,
    .key_tab_2 = NULL,
    .key_tab_3 = NULL,
    .key_tab_4 = NULL,
    .key_tab_5 = NULL,
    .key_tab_6 = NULL,
    .key_tab_7 = NULL,
    .key_tab_8 = NULL,
    .key_tab_9 = NULL,
    .key_tab_10 = NULL,
    .key_close_app = NULL,
    .key_focus_left = NULL,
    .key_focus_right = NULL,
    .key_focus_up = NULL,
    .key_focus_down = NULL,
    .key_increase_opacity = NULL,
    .key_decrease_opacity = NULL
};

void apply_settings(VteTerminal *terminal) {
    // Font
    PangoFontDescription *font_desc = pango_font_description_from_string(current_settings.font);
    vte_terminal_set_font(terminal, font_desc);
    pango_font_description_free(font_desc);

    // Scrollback
    if (current_settings.unlimited_scrollback) {
        vte_terminal_set_scrollback_lines(terminal, -1);
    } else {
        vte_terminal_set_scrollback_lines(terminal, current_settings.scrollback_lines);
    }

    // Audible Bell
    vte_terminal_set_audible_bell(terminal, current_settings.audible_bell);

    // Cursor
    VteCursorShape vte_shape = VTE_CURSOR_SHAPE_BLOCK;
    if (g_strcmp0(current_settings.cursor_shape, "I-Beam") == 0) {
        vte_shape = VTE_CURSOR_SHAPE_IBEAM;
    } else if (g_strcmp0(current_settings.cursor_shape, "Underline") == 0) {
        vte_shape = VTE_CURSOR_SHAPE_UNDERLINE;
    }
    vte_terminal_set_cursor_shape(terminal, vte_shape);
    vte_terminal_set_cursor_blink_mode(terminal, current_settings.cursor_blinks ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);

    // Theme, background color, and ANSI palette
    GdkRGBA bg = current_settings.background_color;
    GdkRGBA fg = current_settings.foreground_color;
    double luminance = 0.2126 * bg.red + 0.7152 * bg.green + 0.0722 * bg.blue;

    if (g_strcmp0(current_settings.theme, "Light") == 0 || luminance > 0.5) {
        // Scoped specifically to Light Theme: Accessible WCAG AA (>= 4.5:1) ANSI color palette
        static const char *light_ansi_hex[16] = {
            "#1F1612", // 0: Black (15.6:1)
            "#B91C1C", // 1: Red (5.9:1)
            "#15803D", // 2: Green (4.7:1)
            "#B45309", // 3: Yellow/Amber (4.6:1)
            "#1D4ED8", // 4: Blue (6.1:1)
            "#6B21A8", // 5: Magenta (7.4:1)
            "#0F766E", // 6: Cyan (5.2:1)
            "#4B5563", // 7: White/Muted (6.8:1)
            "#374151", // 8: Bright Black (10.1:1)
            "#991B1B", // 9: Bright Red (7.6:1)
            "#166534", // 10: Bright Green (6.1:1)
            "#92400E", // 11: Bright Yellow (6.2:1)
            "#1E40AF", // 12: Bright Blue (7.7:1)
            "#581C87", // 13: Bright Magenta (9.1:1)
            "#115E59", // 14: Bright Cyan (6.5:1)
            "#111827"  // 15: Bright White (17.2:1)
        };

        GdkRGBA palette[16];
        for (int i = 0; i < 16; i++) {
            gdk_rgba_parse(&palette[i], light_ansi_hex[i]);
        }

        vte_terminal_set_colors(terminal, &fg, &bg, palette, 16);
    } else {
        // Dark theme: default palette
        vte_terminal_set_colors(terminal, &fg, &bg, NULL, 0);
    }
    
    // Cursor Colors
    vte_terminal_set_color_cursor(terminal, &current_settings.cursor_background_color); // Cursor Box
    vte_terminal_set_color_cursor_foreground(terminal, &current_settings.cursor_foreground_color);
}

// Apply global UI theme (CSS)
void apply_ui_theme() {
    GdkScreen *screen = gdk_screen_get_default();
    if (!screen) return;

    GtkCssProvider *provider = gtk_css_provider_new();
    
    // Convert all customized colors to strings
    char *ui_bg = gdk_rgba_to_string(&current_settings.ui_background_color);
    char *ui_fg = gdk_rgba_to_string(&current_settings.ui_foreground_color);
    
    char *btn_bg = gdk_rgba_to_string(&current_settings.button_background_color);
    char *btn_fg = gdk_rgba_to_string(&current_settings.button_foreground_color);
    char *btn_hover_bg = gdk_rgba_to_string(&current_settings.button_hover_background_color);
    char *btn_hover_fg = gdk_rgba_to_string(&current_settings.button_hover_foreground_color);
    
    char *ai_bg = gdk_rgba_to_string(&current_settings.ai_panel_background_color);
    
    // Headerbar is usually slightly lighter/darker depending on theme, 
    // but we can use ui_bg for consistency or add a specific header color later.
    // For now, let's stick to ui_bg for uniformity or a slight shade.
    // Actually, let's use ui_bg directly for a flat look, or allow custom header color (future).
    
    // Generate CSS
    char *css = g_strdup_printf(
        // Transparent containers to allow VTE transparency to work
        "window, viewport, box, grid {"
        "   background-color: transparent;"
        "   color: %s;"
        "}"
        // Dialogs, popovers, menus, cards, and popdown boxes
        "dialog, messagedialog, popover, menu, menuitem, popover box, dialog box {"
        "   background-color: %s;"
        "   color: %s;"
        "}"
        // Headerbar, titlebar, paned, notebook header/lane
        "headerbar, headerbar.titlebar, titlebar, window titlebar, paned, notebook header {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "   border-bottom: 1px solid shade(%s, 0.85);"
        "}"
        "#main-paned separator {"
        "   background-color: transparent;"
        "   background-image: linear-gradient(to right, transparent 3px, %s 3px, %s 4px, transparent 4px);"
        "   min-width: 7px;"
        "   min-height: 7px;"
        "   margin: 0;"
        "}"
        "#main-paned separator:hover {"
        "   background-image: linear-gradient(to right, transparent 3px, shade(%s, 1.3) 3px, shade(%s, 1.3) 4px, transparent 4px);"
        "}"
        "button, entry, combobox, combo, spinbutton, textview, textview text {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "   border: 1px solid shade(%s, 0.85);"
        "   border-radius: 6px;"
        "   padding: 4px 8px;"
        "   transition: all 0.2s;"
        "}"
        "button:hover {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "   border-color: %s;"
        "}"
        ".flat-button {"
        "   background-color: transparent;"
        "   background-image: none;"
        "   border: none;"
        "   box-shadow: none;"
        "}"
        ".flat-button:hover {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "}"
        "list, listrow, stacksidebar row, stacksidebar row label {"
        "   background-color: transparent;"
        "   background-image: none;"
        "   color: %s;"
        "}"
        "stacksidebar row {"
        "   padding: 6px 10px;"
        "   margin: 2px 4px;"
        "   border-radius: 4px;"
        "}"
        "stacksidebar row:selected, stacksidebar row:selected > label,"
        "listrow:selected, list row:selected {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "   font-weight: bold;"
        "   border-left: 4px solid #2563eb;"
        "}"
        "stacksidebar row:hover, listrow:hover {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "}"
        "notebook {"
        "   background-color: %s;"
        "   border: none;"
        "}"
        "notebook tab {"
        "   background-color: transparent;"
        "   color: shade(%s, 0.7);"
        "   border: none;"
        "   margin: 0 2px;"
        "   padding: 4px 8px;"
        "   border-radius: 4px 4px 0 0;"
        "}"
        "notebook tab:checked {"
        "   background-color: %s;"
        "   background-image: none;"
        "   color: %s;"
        "   font-weight: bold;"
        "   border-top: 3px solid #2563eb;"
        "   border-radius: 4px 4px 0 0;"
        "   margin-bottom: 0px;"
        "}"
        "notebook tab label {"
        "   font-weight: inherit;"
        "   color: inherit;"
        "   background: transparent;"
        "   border: none;"
        "   padding: 2px 6px;"
        "}"
        "button#tab-close-button {"
        "   padding: 2px 4px;"
        "   margin: 0 0 0 4px;"
        "   min-height: 16px;"
        "   min-width: 16px;"
        "   color: inherit;"
        "   background: transparent;"
        "   border: none;"
        "   box-shadow: none;"
        "   border-radius: 3px;"
        "}"
        "button#tab-close-button:hover {"
        "   color: #ff4444;"
        "   background-color: rgba(255, 68, 68, 0.15);"
        "}"
        ".ai-panel, #ai-panel, #ai-response-view, #ai-response-view text, .ai-panel box {"
        "   background-color: %s;"
        "   color: %s;"
        "}"
        "scrollbar slider {"
        "   min-width: 8px;"
        "   min-height: 8px;"
        "   border-radius: 4px;"
        "   background-color: shade(%s, 1.5);"
        "}"
        "#logging-toggle-button {"
        "   background-color: %s;"
        "   color: %s;"
        "}"
        "#logging-toggle-button:checked {"
        "   background-color: #ff0000;"
        "   color: #ffffff;"
        "   border-color: #cc0000;"
        "}"
        "#dama-toggle-button {"
        "   background-color: %s;"
        "   color: %s;"
        "}",
        ui_fg,                  // Window (FG used)
        ui_bg, ui_fg,           // Dialogs/popovers/menus
        ui_bg, ui_fg, ui_bg,    // Headerbar & notebook header
        ui_fg, ui_fg,           // Separator handle
        ui_fg, ui_fg,           // Separator hover
        btn_bg, btn_fg, btn_bg, // Button / Entry / Spinbutton
        btn_hover_bg, btn_hover_fg, btn_hover_bg, // Button Hover
        btn_hover_bg, btn_hover_fg, // Flat Button Hover
        ui_fg,                  // List / StackSidebar text
        btn_hover_bg, btn_hover_fg, // StackSidebar selected bg & text
        btn_hover_bg, btn_hover_fg, // List / StackSidebar hover
        ui_bg,                  // Notebook
        ui_fg,                  // Tab Inactive
        btn_hover_bg, btn_hover_fg, // Tab Checked
        ai_bg, ui_fg,           // AI Panel & Response View
        ui_bg,                  // Scrollbar color
        btn_bg, btn_fg,         // Logging toggle button standard
        btn_bg, btn_fg          // Dama toggle button standard
    );

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    // Update AI Panel WebView colors in real-time
    if (ai_webview) {
        webview_apply_settings(ai_webview);
    }
    
    g_free(ui_bg);
    g_free(ui_fg);
    g_free(btn_bg);
    g_free(btn_fg);
    g_free(btn_hover_bg);
    g_free(btn_hover_fg);
    g_free(ai_bg);
    g_free(css);
    g_object_unref(provider);
}

// Helper function to recursively find and apply settings to all VTE terminals in a widget
static void apply_settings_to_terminals_recursive(GtkWidget *widget) {
    if (VTE_IS_TERMINAL(widget)) {
        apply_settings(VTE_TERMINAL(widget));
        return;
    }
    
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            apply_settings_to_terminals_recursive(GTK_WIDGET(l->data));
        }
        g_list_free(children);
    }
}

// Apply settings to all terminals in the notebook
void apply_settings_to_all_terminals(GtkNotebook *notebook) {
    int n_pages = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (page) {
            apply_settings_to_terminals_recursive(page);
        }
    }
}

void save_settings() {
    const char *config_dir = g_get_user_config_dir();
    char *config_path = g_build_filename(config_dir, "Scythe", NULL);
    char *settings_file = g_build_filename(config_path, "settings.json", NULL);

    g_mkdir_with_parents(config_path, 0755);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "theme");
    json_builder_add_string_value(builder, current_settings.theme);

    json_builder_set_member_name(builder, "font");
    json_builder_add_string_value(builder, current_settings.font);

    char *color_str = gdk_rgba_to_string(&current_settings.background_color);
    json_builder_set_member_name(builder, "background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.foreground_color);
    json_builder_set_member_name(builder, "foreground_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.cursor_background_color);
    json_builder_set_member_name(builder, "cursor_background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.cursor_foreground_color);
    json_builder_set_member_name(builder, "cursor_foreground_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ui_background_color);
    json_builder_set_member_name(builder, "ui_background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ui_foreground_color);
    json_builder_set_member_name(builder, "ui_foreground_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.button_background_color);
    json_builder_set_member_name(builder, "button_background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.button_foreground_color);
    json_builder_set_member_name(builder, "button_foreground_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.button_hover_background_color);
    json_builder_set_member_name(builder, "button_hover_background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.button_hover_foreground_color);
    json_builder_set_member_name(builder, "button_hover_foreground_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_panel_background_color);
    json_builder_set_member_name(builder, "ai_panel_background_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_panel_text_color);
    json_builder_set_member_name(builder, "ai_panel_text_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_user_msg_bg_color);
    json_builder_set_member_name(builder, "ai_user_msg_bg_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_user_msg_text_color);
    json_builder_set_member_name(builder, "ai_user_msg_text_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_assistant_msg_bg_color);
    json_builder_set_member_name(builder, "ai_assistant_msg_bg_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    color_str = gdk_rgba_to_string(&current_settings.ai_assistant_msg_text_color);
    json_builder_set_member_name(builder, "ai_assistant_msg_text_color");
    json_builder_add_string_value(builder, color_str);
    g_free(color_str);

    json_builder_set_member_name(builder, "opacity");
    json_builder_add_double_value(builder, current_settings.opacity);

    json_builder_set_member_name(builder, "scrollback_lines");
    json_builder_add_int_value(builder, current_settings.scrollback_lines);
    
    json_builder_set_member_name(builder, "unlimited_scrollback");
    json_builder_add_boolean_value(builder, current_settings.unlimited_scrollback);

    json_builder_set_member_name(builder, "audible_bell");
    json_builder_add_boolean_value(builder, current_settings.audible_bell);

    json_builder_set_member_name(builder, "cursor_shape");
    json_builder_add_string_value(builder, current_settings.cursor_shape);

    json_builder_set_member_name(builder, "cursor_blinks");
    json_builder_add_boolean_value(builder, current_settings.cursor_blinks);

    json_builder_set_member_name(builder, "ai_provider");
    json_builder_add_string_value(builder, current_settings.ai_provider);

    json_builder_set_member_name(builder, "groq_api_key");
    json_builder_add_string_value(builder, current_settings.groq_api_key);

    json_builder_set_member_name(builder, "groq_model");
    json_builder_add_string_value(builder, current_settings.groq_model);

    json_builder_set_member_name(builder, "json_mode_enabled");
    json_builder_add_boolean_value(builder, current_settings.json_mode_enabled);

    json_builder_set_member_name(builder, "key_copy");
    json_builder_add_string_value(builder, current_settings.key_copy);

    json_builder_set_member_name(builder, "key_paste");
    json_builder_add_string_value(builder, current_settings.key_paste);

    json_builder_set_member_name(builder, "key_split_v");
    json_builder_add_string_value(builder, current_settings.key_split_v);

    json_builder_set_member_name(builder, "key_split_h");
    json_builder_add_string_value(builder, current_settings.key_split_h);

    json_builder_set_member_name(builder, "key_collapse");
    json_builder_add_string_value(builder, current_settings.key_collapse);

    json_builder_set_member_name(builder, "key_toggle_ai");
    json_builder_add_string_value(builder, current_settings.key_toggle_ai);

    json_builder_set_member_name(builder, "key_toggle_notes");
    json_builder_add_string_value(builder, current_settings.key_toggle_notes);

    json_builder_set_member_name(builder, "key_pin_notes");
    json_builder_add_string_value(builder, current_settings.key_pin_notes);

    json_builder_set_member_name(builder, "key_snip_tool");
    json_builder_add_string_value(builder, current_settings.key_snip_tool);

    json_builder_set_member_name(builder, "key_new_term");
    json_builder_add_string_value(builder, current_settings.key_new_term);

    json_builder_set_member_name(builder, "key_prev_tab");
    json_builder_add_string_value(builder, current_settings.key_prev_tab);

    json_builder_set_member_name(builder, "key_next_tab");
    json_builder_add_string_value(builder, current_settings.key_next_tab);

    json_builder_set_member_name(builder, "key_close_term");
    json_builder_add_string_value(builder, current_settings.key_close_term);

    json_builder_set_member_name(builder, "key_customise");
    json_builder_add_string_value(builder, current_settings.key_customise);

    json_builder_set_member_name(builder, "key_tab_1"); json_builder_add_string_value(builder, current_settings.key_tab_1);
    json_builder_set_member_name(builder, "key_tab_2"); json_builder_add_string_value(builder, current_settings.key_tab_2);
    json_builder_set_member_name(builder, "key_tab_3"); json_builder_add_string_value(builder, current_settings.key_tab_3);
    json_builder_set_member_name(builder, "key_tab_4"); json_builder_add_string_value(builder, current_settings.key_tab_4);
    json_builder_set_member_name(builder, "key_tab_5"); json_builder_add_string_value(builder, current_settings.key_tab_5);
    json_builder_set_member_name(builder, "key_tab_6"); json_builder_add_string_value(builder, current_settings.key_tab_6);
    json_builder_set_member_name(builder, "key_tab_7"); json_builder_add_string_value(builder, current_settings.key_tab_7);
    json_builder_set_member_name(builder, "key_tab_8"); json_builder_add_string_value(builder, current_settings.key_tab_8);
    json_builder_set_member_name(builder, "key_tab_9"); json_builder_add_string_value(builder, current_settings.key_tab_9);
    json_builder_set_member_name(builder, "key_tab_10"); json_builder_add_string_value(builder, current_settings.key_tab_10);

    json_builder_set_member_name(builder, "key_close_app");
    json_builder_add_string_value(builder, current_settings.key_close_app);

    json_builder_set_member_name(builder, "key_focus_left"); json_builder_add_string_value(builder, current_settings.key_focus_left);
    json_builder_set_member_name(builder, "key_focus_right"); json_builder_add_string_value(builder, current_settings.key_focus_right);
    json_builder_set_member_name(builder, "key_focus_up"); json_builder_add_string_value(builder, current_settings.key_focus_up);
    json_builder_set_member_name(builder, "key_focus_down"); json_builder_add_string_value(builder, current_settings.key_focus_down);
    json_builder_set_member_name(builder, "key_increase_opacity"); json_builder_add_string_value(builder, current_settings.key_increase_opacity);
    json_builder_set_member_name(builder, "key_decrease_opacity"); json_builder_add_string_value(builder, current_settings.key_decrease_opacity);

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    json_generator_to_file(gen, settings_file, NULL);

    char *json = json_generator_to_data(gen, NULL);
    g_file_set_contents(settings_file, json, -1, NULL);
    g_free(json);

    g_object_unref(gen);
    g_object_unref(builder);
    g_free(config_path);
    g_free(settings_file);

    apply_ui_theme();
}

void load_settings() {
    const char *config_dir = g_get_user_config_dir();
    char *config_path = g_build_filename(config_dir, "Scythe", NULL);
    char *settings_file = g_build_filename(config_path, "settings.json", NULL);

    if (!g_file_test(settings_file, G_FILE_TEST_EXISTS)) {
        g_free(config_path);
        g_free(settings_file);
        // Apply defaults
        if (!current_settings.theme) current_settings.theme = g_strdup("Default");
        if (!current_settings.font) current_settings.font = g_strdup("Monospace 12");
        if (!current_settings.cursor_shape) current_settings.cursor_shape = g_strdup("Block");
        if (!current_settings.ai_provider) current_settings.ai_provider = g_strdup("Ollama");
        if (!current_settings.groq_api_key) current_settings.groq_api_key = g_strdup("");
        if (!current_settings.groq_model) current_settings.groq_model = g_strdup("llama-3.3-70b-versatile");
    // Default Keybindings
        if (!current_settings.key_copy) current_settings.key_copy = g_strdup("<Control><Shift>C");
        if (!current_settings.key_paste) current_settings.key_paste = g_strdup("<Control><Shift>V");
        if (!current_settings.key_split_v) current_settings.key_split_v = g_strdup("<Control><Shift>D");
        if (!current_settings.key_split_h) current_settings.key_split_h = g_strdup("<Control><Shift>R");
        if (!current_settings.key_collapse) current_settings.key_collapse = g_strdup("<Control><Shift>E");

        // Use default theme colors (Dark) if no settings file
        gdk_rgba_parse(&current_settings.background_color, "#292c37");
        gdk_rgba_parse(&current_settings.foreground_color, "#d4d4d4");
        gdk_rgba_parse(&current_settings.cursor_background_color, "#d4d4d4");
        gdk_rgba_parse(&current_settings.cursor_foreground_color, "#292c37");
        
        gdk_rgba_parse(&current_settings.ui_background_color, "#292c37");
        gdk_rgba_parse(&current_settings.ui_foreground_color, "#ffffff");
        
        gdk_rgba_parse(&current_settings.button_background_color, "#3e4149");
        gdk_rgba_parse(&current_settings.button_foreground_color, "#ffffff");
        gdk_rgba_parse(&current_settings.button_hover_background_color, "#4e525b");
        gdk_rgba_parse(&current_settings.button_hover_foreground_color, "#ffffff");
        
        gdk_rgba_parse(&current_settings.ai_panel_background_color, "#292c37");
        gdk_rgba_parse(&current_settings.ai_panel_text_color, "#d4d4d4");
        gdk_rgba_parse(&current_settings.ai_user_msg_bg_color, "rgba(59, 130, 246, 0.12)");
        gdk_rgba_parse(&current_settings.ai_user_msg_text_color, "#ffffff");
        gdk_rgba_parse(&current_settings.ai_assistant_msg_bg_color, "rgba(16, 185, 129, 0.12)");
        gdk_rgba_parse(&current_settings.ai_assistant_msg_text_color, "#d4d4d4");
        
        return; 
    }

    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    json_parser_load_from_file(parser, settings_file, &error);
    if (error) {
        g_warning("Error loading settings file: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        g_free(config_path);
        g_free(settings_file);
        return;
    }

    JsonObject *root = json_node_get_object(json_parser_get_root(parser));

    if (json_object_has_member(root, "theme")) {
        current_settings.theme = g_strdup(json_object_get_string_member(root, "theme"));
    }
    if (json_object_has_member(root, "font")) {
        current_settings.font = g_strdup(json_object_get_string_member(root, "font"));
    }
    if (json_object_has_member(root, "background_color")) {
        const char *color_str = json_object_get_string_member(root, "background_color");
        gdk_rgba_parse(&current_settings.background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.background_color, "#292c37"); }

    if (json_object_has_member(root, "foreground_color")) {
        const char *color_str = json_object_get_string_member(root, "foreground_color");
        gdk_rgba_parse(&current_settings.foreground_color, color_str);
    } else { gdk_rgba_parse(&current_settings.foreground_color, "#d4d4d4"); }

    if (json_object_has_member(root, "cursor_background_color")) {
        const char *color_str = json_object_get_string_member(root, "cursor_background_color");
        gdk_rgba_parse(&current_settings.cursor_background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.cursor_background_color, "#d4d4d4"); }

    if (json_object_has_member(root, "cursor_foreground_color")) {
        const char *color_str = json_object_get_string_member(root, "cursor_foreground_color");
        gdk_rgba_parse(&current_settings.cursor_foreground_color, color_str);
    } else { gdk_rgba_parse(&current_settings.cursor_foreground_color, "#292c37"); }

    if (json_object_has_member(root, "ui_background_color")) {
        const char *color_str = json_object_get_string_member(root, "ui_background_color");
        gdk_rgba_parse(&current_settings.ui_background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ui_background_color, "#292c37"); }

    if (json_object_has_member(root, "ui_foreground_color")) {
        const char *color_str = json_object_get_string_member(root, "ui_foreground_color");
        gdk_rgba_parse(&current_settings.ui_foreground_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ui_foreground_color, "#ffffff"); }

    if (json_object_has_member(root, "button_background_color")) {
        const char *color_str = json_object_get_string_member(root, "button_background_color");
        gdk_rgba_parse(&current_settings.button_background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.button_background_color, "#3e4149"); }

    if (json_object_has_member(root, "button_foreground_color")) {
        const char *color_str = json_object_get_string_member(root, "button_foreground_color");
        gdk_rgba_parse(&current_settings.button_foreground_color, color_str);
    } else { gdk_rgba_parse(&current_settings.button_foreground_color, "#ffffff"); }

    if (json_object_has_member(root, "button_hover_background_color")) {
        const char *color_str = json_object_get_string_member(root, "button_hover_background_color");
        gdk_rgba_parse(&current_settings.button_hover_background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.button_hover_background_color, "#4e525b"); }

    if (json_object_has_member(root, "button_hover_foreground_color")) {
        const char *color_str = json_object_get_string_member(root, "button_hover_foreground_color");
        gdk_rgba_parse(&current_settings.button_hover_foreground_color, color_str);
    } else { gdk_rgba_parse(&current_settings.button_hover_foreground_color, "#ffffff"); }

    if (json_object_has_member(root, "ai_panel_background_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_panel_background_color");
        gdk_rgba_parse(&current_settings.ai_panel_background_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_panel_background_color, "#292c37"); }

    if (json_object_has_member(root, "ai_panel_text_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_panel_text_color");
        gdk_rgba_parse(&current_settings.ai_panel_text_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_panel_text_color, "#d4d4d4"); }

    if (json_object_has_member(root, "ai_user_msg_bg_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_user_msg_bg_color");
        gdk_rgba_parse(&current_settings.ai_user_msg_bg_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_user_msg_bg_color, "rgba(59, 130, 246, 0.12)"); }

    if (json_object_has_member(root, "ai_user_msg_text_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_user_msg_text_color");
        gdk_rgba_parse(&current_settings.ai_user_msg_text_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_user_msg_text_color, "#ffffff"); }

    if (json_object_has_member(root, "ai_assistant_msg_bg_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_assistant_msg_bg_color");
        gdk_rgba_parse(&current_settings.ai_assistant_msg_bg_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_assistant_msg_bg_color, "rgba(16, 185, 129, 0.12)"); }

    if (json_object_has_member(root, "ai_assistant_msg_text_color")) {
        const char *color_str = json_object_get_string_member(root, "ai_assistant_msg_text_color");
        gdk_rgba_parse(&current_settings.ai_assistant_msg_text_color, color_str);
    } else { gdk_rgba_parse(&current_settings.ai_assistant_msg_text_color, "#d4d4d4"); }

    current_settings.opacity = json_object_get_double_member_with_default(root, "opacity", 1.0);
    current_settings.background_color.alpha = current_settings.opacity;
    current_settings.scrollback_lines = json_object_get_int_member_with_default(root, "scrollback_lines", 10000);
    current_settings.unlimited_scrollback = json_object_get_boolean_member_with_default(root, "unlimited_scrollback", TRUE);
    current_settings.audible_bell = json_object_get_boolean_member_with_default(root, "audible_bell", FALSE);
    current_settings.cursor_blinks = json_object_get_boolean_member_with_default(root, "cursor_blinks", TRUE);
    if (json_object_has_member(root, "cursor_shape")) {
        current_settings.cursor_shape = g_strdup(json_object_get_string_member(root, "cursor_shape"));
    }
    if (json_object_has_member(root, "ai_provider")) {
        current_settings.ai_provider = g_strdup(json_object_get_string_member(root, "ai_provider"));
    }
    if (json_object_has_member(root, "groq_api_key")) {
        current_settings.groq_api_key = g_strdup(json_object_get_string_member(root, "groq_api_key"));
    }
    if (json_object_has_member(root, "groq_model")) {
        current_settings.groq_model = g_strdup(json_object_get_string_member(root, "groq_model"));
    }
    if (json_object_has_member(root, "json_mode_enabled")) {
        current_settings.json_mode_enabled = json_object_get_boolean_member(root, "json_mode_enabled");
    } else {
        current_settings.json_mode_enabled = FALSE; // Default: natural responses
    }

    if (json_object_has_member(root, "key_copy")) current_settings.key_copy = g_strdup(json_object_get_string_member(root, "key_copy"));
    if (json_object_has_member(root, "key_paste")) current_settings.key_paste = g_strdup(json_object_get_string_member(root, "key_paste"));
    if (json_object_has_member(root, "key_split_v")) current_settings.key_split_v = g_strdup(json_object_get_string_member(root, "key_split_v"));
    if (json_object_has_member(root, "key_split_h")) current_settings.key_split_h = g_strdup(json_object_get_string_member(root, "key_split_h"));
    if (json_object_has_member(root, "key_collapse")) current_settings.key_collapse = g_strdup(json_object_get_string_member(root, "key_collapse"));
    if (json_object_has_member(root, "key_toggle_ai")) current_settings.key_toggle_ai = g_strdup(json_object_get_string_member(root, "key_toggle_ai"));
    if (json_object_has_member(root, "key_toggle_notes")) current_settings.key_toggle_notes = g_strdup(json_object_get_string_member(root, "key_toggle_notes"));
    if (json_object_has_member(root, "key_pin_notes")) current_settings.key_pin_notes = g_strdup(json_object_get_string_member(root, "key_pin_notes"));
    if (json_object_has_member(root, "key_snip_tool")) current_settings.key_snip_tool = g_strdup(json_object_get_string_member(root, "key_snip_tool"));
    if (json_object_has_member(root, "key_new_term")) current_settings.key_new_term = g_strdup(json_object_get_string_member(root, "key_new_term"));
    if (json_object_has_member(root, "key_prev_tab")) current_settings.key_prev_tab = g_strdup(json_object_get_string_member(root, "key_prev_tab"));
    if (json_object_has_member(root, "key_next_tab")) current_settings.key_next_tab = g_strdup(json_object_get_string_member(root, "key_next_tab"));
    if (json_object_has_member(root, "key_close_term")) current_settings.key_close_term = g_strdup(json_object_get_string_member(root, "key_close_term"));
    if (json_object_has_member(root, "key_customise")) current_settings.key_customise = g_strdup(json_object_get_string_member(root, "key_customise"));
    
    if (json_object_has_member(root, "key_tab_1")) current_settings.key_tab_1 = g_strdup(json_object_get_string_member(root, "key_tab_1"));
    if (json_object_has_member(root, "key_tab_2")) current_settings.key_tab_2 = g_strdup(json_object_get_string_member(root, "key_tab_2"));
    if (json_object_has_member(root, "key_tab_3")) current_settings.key_tab_3 = g_strdup(json_object_get_string_member(root, "key_tab_3"));
    if (json_object_has_member(root, "key_tab_4")) current_settings.key_tab_4 = g_strdup(json_object_get_string_member(root, "key_tab_4"));
    if (json_object_has_member(root, "key_tab_5")) current_settings.key_tab_5 = g_strdup(json_object_get_string_member(root, "key_tab_5"));
    if (json_object_has_member(root, "key_tab_6")) current_settings.key_tab_6 = g_strdup(json_object_get_string_member(root, "key_tab_6"));
    if (json_object_has_member(root, "key_tab_7")) current_settings.key_tab_7 = g_strdup(json_object_get_string_member(root, "key_tab_7"));
    if (json_object_has_member(root, "key_tab_8")) current_settings.key_tab_8 = g_strdup(json_object_get_string_member(root, "key_tab_8"));
    if (json_object_has_member(root, "key_tab_9")) current_settings.key_tab_9 = g_strdup(json_object_get_string_member(root, "key_tab_9"));
    if (json_object_has_member(root, "key_tab_10")) current_settings.key_tab_10 = g_strdup(json_object_get_string_member(root, "key_tab_10"));
    if (json_object_has_member(root, "key_close_app")) current_settings.key_close_app = g_strdup(json_object_get_string_member(root, "key_close_app"));
    
    if (json_object_has_member(root, "key_focus_left")) current_settings.key_focus_left = g_strdup(json_object_get_string_member(root, "key_focus_left"));
    if (json_object_has_member(root, "key_focus_right")) current_settings.key_focus_right = g_strdup(json_object_get_string_member(root, "key_focus_right"));
    if (json_object_has_member(root, "key_focus_up")) current_settings.key_focus_up = g_strdup(json_object_get_string_member(root, "key_focus_up"));
    if (json_object_has_member(root, "key_focus_down")) current_settings.key_focus_down = g_strdup(json_object_get_string_member(root, "key_focus_down"));
    if (json_object_has_member(root, "key_increase_opacity")) current_settings.key_increase_opacity = g_strdup(json_object_get_string_member(root, "key_increase_opacity"));
    if (json_object_has_member(root, "key_decrease_opacity")) current_settings.key_decrease_opacity = g_strdup(json_object_get_string_member(root, "key_decrease_opacity"));

    g_object_unref(parser);
    g_free(config_path);
    g_free(settings_file);

    // Apply defaults if not loaded
    if (!current_settings.theme) current_settings.theme = g_strdup("Default");
    if (!current_settings.font) current_settings.font = g_strdup("Monospace 12");
    if (!current_settings.cursor_shape) current_settings.cursor_shape = g_strdup("Block");
    if (!current_settings.ai_provider) current_settings.ai_provider = g_strdup("Ollama");
    if (!current_settings.groq_api_key) current_settings.groq_api_key = g_strdup("");
    if (!current_settings.groq_model) current_settings.groq_model = g_strdup("llama-3.3-70b-versatile");
    if (!current_settings.key_copy) current_settings.key_copy = g_strdup("<Control><Shift>C");
    if (!current_settings.key_paste) current_settings.key_paste = g_strdup("<Control><Shift>V");
    if (!current_settings.key_split_v) current_settings.key_split_v = g_strdup("<Control><Shift>D");
    if (!current_settings.key_split_h) current_settings.key_split_h = g_strdup("<Control><Shift>R");
    if (!current_settings.key_collapse) current_settings.key_collapse = g_strdup("<Control><Shift>E");
    if (!current_settings.key_toggle_ai) current_settings.key_toggle_ai = g_strdup("<Control><Shift>A");
    if (!current_settings.key_toggle_notes) current_settings.key_toggle_notes = g_strdup("<Alt>N");
    if (!current_settings.key_pin_notes) current_settings.key_pin_notes = g_strdup("<Alt>P");
    if (!current_settings.key_snip_tool) current_settings.key_snip_tool = g_strdup("<Control><Alt>S");
    if (!current_settings.key_new_term) current_settings.key_new_term = g_strdup("<Control><Shift>T");
    if (!current_settings.key_prev_tab) current_settings.key_prev_tab = g_strdup("<Alt><Shift>Left");
    if (!current_settings.key_next_tab) current_settings.key_next_tab = g_strdup("<Alt><Shift>Right");
    if (!current_settings.key_close_term) current_settings.key_close_term = g_strdup("<Control><Shift>W");
    if (!current_settings.key_customise) current_settings.key_customise = g_strdup("<Control><Shift>P");
    
    if (!current_settings.key_tab_1) current_settings.key_tab_1 = g_strdup("<Alt>1");
    if (!current_settings.key_tab_2) current_settings.key_tab_2 = g_strdup("<Alt>2");
    if (!current_settings.key_tab_3) current_settings.key_tab_3 = g_strdup("<Alt>3");
    if (!current_settings.key_tab_4) current_settings.key_tab_4 = g_strdup("<Alt>4");
    if (!current_settings.key_tab_5) current_settings.key_tab_5 = g_strdup("<Alt>5");
    if (!current_settings.key_tab_6) current_settings.key_tab_6 = g_strdup("<Alt>6");
    if (!current_settings.key_tab_7) current_settings.key_tab_7 = g_strdup("<Alt>7");
    if (!current_settings.key_tab_8) current_settings.key_tab_8 = g_strdup("<Alt>8");
    if (!current_settings.key_tab_9) current_settings.key_tab_9 = g_strdup("<Alt>9");
    if (!current_settings.key_tab_10) current_settings.key_tab_10 = g_strdup("<Alt>0");
    if (!current_settings.key_close_app) current_settings.key_close_app = g_strdup("<Alt>q");
    
    if (!current_settings.key_focus_left) current_settings.key_focus_left = g_strdup("<Alt>Left");
    if (!current_settings.key_focus_right) current_settings.key_focus_right = g_strdup("<Alt>Right");
    if (!current_settings.key_focus_up) current_settings.key_focus_up = g_strdup("<Alt>Up");
    if (!current_settings.key_focus_up) current_settings.key_focus_up = g_strdup("<Alt>Up");
    if (!current_settings.key_focus_down) current_settings.key_focus_down = g_strdup("<Alt>Down");
    if (!current_settings.key_increase_opacity) current_settings.key_increase_opacity = g_strdup("<Primary>equal");
    if (!current_settings.key_decrease_opacity) current_settings.key_decrease_opacity = g_strdup("<Primary>minus");

    apply_ui_theme();
}

// Callbacks for settings changes

static void on_bg_color_set(GtkColorButton *button, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    if (!notebook) return;
    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);

    GtkScale *transparency_scale = g_object_get_data(G_OBJECT(button), "transparency_scale");
    if (transparency_scale) {
        current_settings.opacity = gtk_range_get_value(GTK_RANGE(transparency_scale)) / 100.0;
    }

    color.alpha = current_settings.opacity;
    current_settings.background_color = color;

    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(button), &color);
    apply_settings_to_all_terminals(notebook);
    apply_ui_theme(); // Ensure UI/AI Panel updates with solid color
    // save_settings(); // Removed auto-save
}

static void on_font_changed(GtkComboBox *combo_box, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    VteTerminal *terminal = get_current_terminal(notebook);
    if (!terminal) return;
    
    const gchar *font_selection = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (!font_selection) return;
    
    gchar *font_name;
    if (g_strcmp0(font_selection, "Hack (Regular)") == 0) {
        font_name = g_strdup("Hack 12");
    } else if (g_strcmp0(font_selection, "Hack (Bold)") == 0) {
        font_name = g_strdup("Hack Bold 12");
    } else if (g_strcmp0(font_selection, "Fira Code (Regular)") == 0) {
        font_name = g_strdup("Fira Code 12");
    } else if (g_strcmp0(font_selection, "Fira Code (Medium)") == 0) {
        font_name = g_strdup("Fira Code Medium 12");
    } else if (g_strcmp0(font_selection, "Fira Code (Bold)") == 0) {
        font_name = g_strdup("Fira Code Bold 12");
    } else if (g_strcmp0(font_selection, "DejaVu Sans Mono") == 0) {
        font_name = g_strdup("DejaVu Sans Mono 12");
    } else if (g_strcmp0(font_selection, "Liberation Mono") == 0) {
        font_name = g_strdup("Liberation Mono 12");
    } else if (g_strcmp0(font_selection, "Courier") == 0) {
        font_name = g_strdup("Courier 12");
    } else if (g_strcmp0(font_selection, "Courier 10 Pitch") == 0) {
        font_name = g_strdup("Courier 10 Pitch 12");
    } else if (g_strcmp0(font_selection, "FreeMono") == 0) {
        font_name = g_strdup("FreeMono 12");
    } else if (g_strcmp0(font_selection, "Noto Sans Mono") == 0) {
        font_name = g_strdup("Noto Sans Mono 12");
    } else {
        font_name = g_strdup("DejaVu Sans Mono 12");
    }
    
    PangoFontDescription *font_desc = pango_font_description_from_string(font_name);
    vte_terminal_set_font(terminal, font_desc);
    pango_font_description_free(font_desc);
    
    if (current_settings.font) g_free(current_settings.font);
    current_settings.font = g_strdup(font_name);
    // save_settings(); // Removed auto-save
    g_free(font_name);
}

static void on_transparency_changed(GtkScale *scale, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    if (!notebook) return;

    double value = gtk_range_get_value(GTK_RANGE(scale));
    current_settings.opacity = value / 100.0;
    current_settings.background_color.alpha = current_settings.opacity;
    
    // Sync the background color picker to show the current alpha
    GtkWidget *bg_color_button = g_object_get_data(G_OBJECT(scale), "bg_color_button");
    if (bg_color_button) {
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(bg_color_button), &current_settings.background_color);
    }
    
    // Recursively update background color on all terminals (handles split panes)
    int n_pages = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (page) {
            set_terminal_background_recursively(page, &current_settings.background_color);
        }
    }
}

static void on_scrollback_changed(GtkSpinButton *spin_button, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    if (!notebook) return;
    
    gint value = gtk_spin_button_get_value_as_int(spin_button);
    current_settings.scrollback_lines = value;
    
    if (!current_settings.unlimited_scrollback) {
        apply_settings_to_all_terminals(notebook);
    }
    // save_settings(); // Removed auto-save
}

static void on_unlimited_toggled(GtkToggleButton *toggle_button, gpointer user_data) {
    GtkWidget *scrollback_spin = GTK_WIDGET(user_data);
    GtkNotebook *notebook = g_object_get_data(G_OBJECT(scrollback_spin), "notebook");
    if (!notebook) return;

    gboolean is_active = gtk_toggle_button_get_active(toggle_button);
    gtk_widget_set_sensitive(scrollback_spin, !is_active);
    
    current_settings.unlimited_scrollback = is_active;
    if (!is_active) {
        current_settings.scrollback_lines = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(scrollback_spin));
    }
    
    apply_settings_to_all_terminals(notebook);
    // save_settings(); // Removed auto-save
}

static void on_bell_toggled(GtkToggleButton *toggle_button, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    VteTerminal *terminal = get_current_terminal(notebook);
    if (!terminal) return;
    vte_terminal_set_audible_bell(terminal, gtk_toggle_button_get_active(toggle_button));

    current_settings.audible_bell = gtk_toggle_button_get_active(toggle_button);
    // save_settings(); // Removed auto-save
}

static void on_cursor_shape_changed(GtkComboBox *combo_box, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    VteTerminal *terminal = get_current_terminal(notebook);
    if (!terminal) return;
    
    const gchar *shape = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (!shape) return;

    VteCursorShape vte_shape = VTE_CURSOR_SHAPE_BLOCK;
    if (g_strcmp0(shape, "I-Beam") == 0) {
        vte_shape = VTE_CURSOR_SHAPE_IBEAM;
    } else if (g_strcmp0(shape, "Underline") == 0) {
        vte_shape = VTE_CURSOR_SHAPE_UNDERLINE;
    }
    vte_terminal_set_cursor_shape(terminal, vte_shape);

    if (current_settings.cursor_shape) g_free(current_settings.cursor_shape);
    current_settings.cursor_shape = g_strdup(shape);
    // save_settings(); // Removed auto-save
}

static void on_cursor_blink_toggled(GtkToggleButton *toggle_button, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    VteTerminal *terminal = get_current_terminal(notebook);
    if (!terminal) return;
    
    VteCursorBlinkMode mode = gtk_toggle_button_get_active(toggle_button) ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF;
    vte_terminal_set_cursor_blink_mode(terminal, mode);

    current_settings.cursor_blinks = gtk_toggle_button_get_active(toggle_button);
    // save_settings(); // Removed auto-save
}

static void on_set_shortcut_clicked(GtkWidget *widget, gpointer user_data) {
    const char *de = g_getenv("XDG_CURRENT_DESKTOP");

    if (de && (g_str_has_prefix(de, "XFCE") || g_str_has_prefix(de, "xfce"))) {
        g_spawn_command_line_async("xfce4-keyboard-settings", NULL);
    } else if (de && (g_str_has_prefix(de, "GNOME") || g_str_has_prefix(de, "gnome"))) {
        g_spawn_command_line_async("gnome-control-center keyboard", NULL);
    } else if (de && (g_str_has_prefix(de, "KDE") || g_str_has_prefix(de, "kde"))) {
        g_spawn_command_line_async("kcmshell5 keys", NULL);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "Could not detect your Desktop Environment.");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "Please open your system's keyboard settings manually, find the 'Custom Shortcuts' section, and add a new shortcut with the command:\n\nscythe-fixed");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void on_ai_provider_changed(GtkComboBox *combo_box, gpointer user_data) {
    const gchar *provider = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (provider) {
        if (current_settings.ai_provider) g_free(current_settings.ai_provider);
        current_settings.ai_provider = g_strdup(provider);
        // save_settings(); // Removed auto-save
    }
}

static void on_groq_api_key_changed(GtkEntry *entry, gpointer user_data) {
    const gchar *key = gtk_entry_get_text(entry);
    if (current_settings.groq_api_key) g_free(current_settings.groq_api_key);
    current_settings.groq_api_key = g_strdup(key);
    // save_settings(); // Removed auto-save
}

static void on_groq_model_changed(GtkComboBox *combo_box, gpointer user_data) {
    const gchar *model = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (model) {
        if (current_settings.groq_model) g_free(current_settings.groq_model);
        current_settings.groq_model = g_strdup(model);
        // save_settings(); // Removed auto-save
    }
}

static void on_api_key_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user_data) {
    gboolean visible = gtk_entry_get_visibility(entry);
    gtk_entry_set_visibility(entry, !visible);
    gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, visible ? "view-remove-symbolic" : "view-reveal-symbolic");
}

// --- Shortcut Configuration UI ---

typedef struct {
    gchar **setting_ptr;
    GtkWidget *button;
    GtkWidget *dialog;
} ShortcutData;

static gboolean on_shortcut_capture_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    ShortcutData *data = (ShortcutData *)user_data;
    
    // Ignore modifier-only presses
    if (event->is_modifier) return FALSE;

    // Handle Escape to cancel
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(data->dialog);
        g_free(data);
        return TRUE;
    }

    // Convert to string
    // Mask out consumed modifiers/locks (e.g. NumLock/Mod2)
    GdkModifierType mask = gtk_accelerator_get_default_mod_mask();
    gchar *accel_string = gtk_accelerator_name(event->keyval, event->state & mask);
    
    // Update setting
    if (*(data->setting_ptr)) g_free(*(data->setting_ptr));
    *(data->setting_ptr) = accel_string; // Transfer ownership
    
    // Update button label
    gtk_button_set_label(GTK_BUTTON(data->button), accel_string);
    
    // Save settings immediately
    save_settings();

    gtk_widget_destroy(data->dialog);
    g_free(data);
    return TRUE;
}

static void on_shortcut_button_clicked(GtkButton *button, gpointer user_data) {
    gchar **setting_ptr = (gchar **)user_data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Shortcut",
                                                    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Press the new shortcut keys...\n(Press Esc to cancel)");
    gtk_widget_set_margin_top(label, 20);
    gtk_widget_set_margin_bottom(label, 20);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    gtk_widget_show_all(content_area);

    ShortcutData *data = g_new0(ShortcutData, 1);
    data->setting_ptr = setting_ptr;
    data->button = GTK_WIDGET(button);
    data->dialog = dialog;

    g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_shortcut_capture_key), data);

    gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (GTK_IS_WIDGET(dialog)) gtk_widget_destroy(dialog);
}

// Recursively set background color on all VTE terminals in a widget tree
static void set_terminal_background_recursively(GtkWidget *widget, const GdkRGBA *color) {
    if (VTE_IS_TERMINAL(widget)) {
        vte_terminal_set_color_background(VTE_TERMINAL(widget), color);
        return;
    }
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            set_terminal_background_recursively(GTK_WIDGET(l->data), color);
        }
        g_list_free(children);
    }
}

// Helper to refresh all terminals with new settings (recursive for split panes)
static void apply_theme_to_all_terminals(GtkNotebook *notebook) {
    int n_pages = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (page) {
            apply_settings_to_terminals_recursive(page);
        }
    }
}

// Callback for Color Pickers
static void on_custom_color_set(GtkColorButton *button, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    GdkRGBA *target = g_object_get_data(G_OBJECT(button), "target_color");
    
    if (target) {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), target);
    }
    
    // Apply changes
    apply_ui_theme();
    apply_theme_to_all_terminals(notebook);
}

// --- Theme Manager Callbacks ---

static void refresh_theme_combo(GtkComboBoxText *combo) {
    gtk_combo_box_text_remove_all(combo);
    
    GList *themes = theme_manager_get_list();
    GList *iter;
    for (iter = themes; iter != NULL; iter = iter->next) {
        gtk_combo_box_text_append_text(combo, (char *)iter->data);
        g_free(iter->data);
    }
    g_list_free(themes);
    
    // Select current theme if available
    if (current_settings.theme) {
        // gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), current_settings.theme); 
        // Note: append_text doesn't use IDs easily unless we use append(id, text).
        // Let's loop to find active.
        // Actually, let's use append(id, text) for easier selection.
    }
}

static void on_theme_combo_changed(GtkComboBoxText *combo, gpointer user_data) {
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);
    const char *text = gtk_combo_box_text_get_active_text(combo);
    if (!text) return;
    
    if (theme_manager_load_theme(text)) {
        current_settings.background_color.alpha = current_settings.opacity;
        apply_ui_theme();
        apply_theme_to_all_terminals(notebook);
    }
    g_free((char*)text);
    // Note: 'text' is newly allocated by get_active_text, but we don't free it? 
    // GTK doc says "The returned string is allocated and should be freed with g_free()".
    // We should free it if we copy it, but load_theme duplicates what it needs.
    // Wait, theme_manager_load_theme takes const char*, so we should free 'text' after call.
    // Let's verify GTK version documentation. Yes, g_free needed.
    // char *mutable_text = (char*)text; // just to be clear
    // ...
    // g_free(mutable_text); 
}

static void on_save_theme_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Save Theme As...",
                                                    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Theme Name");
    if (current_settings.theme) gtk_entry_set_text(GTK_ENTRY(entry), current_settings.theme);
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && strlen(name) > 0) {
            theme_manager_save_theme(name);
            
            // Allow caller to refresh combo if we pass it? 
            // `user_data` handles refresh? 
            // We can emit a signal or just manually refresh if we had access to the combo.
            // For now, user might need to re-open dialog or we force a refresh if possible.
            // Let's simpler: Set user_data to be a struct { combo, notebook } or just refresh visually.
            // Actually, adding a new theme usually implies we want to select it.
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_delete_theme_clicked(GtkButton *button, gpointer user_data) {
    // user_data will be list/combo to refresh?
    // Let's confirm first.
    GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(user_data);
    char *active_theme = gtk_combo_box_text_get_active_text(combo);
    
    if (!active_theme) return;
    
    // Prevent deleting defaults if we want to protect them?
    // "Dark" and "Light" can be re-generated by init, so deleting them effectively resets them on next run.
    
    if (theme_manager_delete_theme(active_theme)) {
        gtk_combo_box_text_remove(combo, gtk_combo_box_get_active(GTK_COMBO_BOX(combo)));
        // Select first or something safe
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    }
    g_free(active_theme);
}

void open_customise_dialog(GtkWidget *widget, gpointer user_data) {
    GtkWidget *parent_window = gtk_widget_get_toplevel(widget);
    GtkNotebook *notebook = GTK_NOTEBOOK(user_data);

    // Snapshot current settings before dialog opens.
    // If the user cancels, we restore from this snapshot instead of
    // re-reading settings.json (which may be stale if a theme was switched
    // in the combo but never saved).
    AppSettings snapshot;
    memcpy(&snapshot, &current_settings, sizeof(AppSettings));
    // Deep-copy heap-allocated strings so restore won't use freed pointers
    if (snapshot.theme)  snapshot.theme  = g_strdup(snapshot.theme);
    if (snapshot.font)   snapshot.font   = g_strdup(snapshot.font);
    if (snapshot.cursor_shape) snapshot.cursor_shape = g_strdup(snapshot.cursor_shape);
    if (snapshot.ai_provider)  snapshot.ai_provider  = g_strdup(snapshot.ai_provider);
    if (snapshot.groq_api_key) snapshot.groq_api_key = g_strdup(snapshot.groq_api_key);
    if (snapshot.groq_model)   snapshot.groq_model   = g_strdup(snapshot.groq_model);
    if (snapshot.key_copy)  snapshot.key_copy  = g_strdup(snapshot.key_copy);
    if (snapshot.key_paste) snapshot.key_paste = g_strdup(snapshot.key_paste);
    if (snapshot.key_split_v) snapshot.key_split_v = g_strdup(snapshot.key_split_v);
    if (snapshot.key_split_h) snapshot.key_split_h = g_strdup(snapshot.key_split_h);
    if (snapshot.key_collapse) snapshot.key_collapse = g_strdup(snapshot.key_collapse);
    if (snapshot.key_toggle_ai) snapshot.key_toggle_ai = g_strdup(snapshot.key_toggle_ai);
    if (snapshot.key_toggle_notes) snapshot.key_toggle_notes = g_strdup(snapshot.key_toggle_notes);
    if (snapshot.key_pin_notes) snapshot.key_pin_notes = g_strdup(snapshot.key_pin_notes);
    if (snapshot.key_snip_tool) snapshot.key_snip_tool = g_strdup(snapshot.key_snip_tool);
    if (snapshot.key_new_term) snapshot.key_new_term = g_strdup(snapshot.key_new_term);
    if (snapshot.key_prev_tab) snapshot.key_prev_tab = g_strdup(snapshot.key_prev_tab);
    if (snapshot.key_next_tab) snapshot.key_next_tab = g_strdup(snapshot.key_next_tab);
    if (snapshot.key_close_term) snapshot.key_close_term = g_strdup(snapshot.key_close_term);
    if (snapshot.key_customise) snapshot.key_customise = g_strdup(snapshot.key_customise);
    if (snapshot.key_tab_1)  snapshot.key_tab_1  = g_strdup(snapshot.key_tab_1);
    if (snapshot.key_tab_2)  snapshot.key_tab_2  = g_strdup(snapshot.key_tab_2);
    if (snapshot.key_tab_3)  snapshot.key_tab_3  = g_strdup(snapshot.key_tab_3);
    if (snapshot.key_tab_4)  snapshot.key_tab_4  = g_strdup(snapshot.key_tab_4);
    if (snapshot.key_tab_5)  snapshot.key_tab_5  = g_strdup(snapshot.key_tab_5);
    if (snapshot.key_tab_6)  snapshot.key_tab_6  = g_strdup(snapshot.key_tab_6);
    if (snapshot.key_tab_7)  snapshot.key_tab_7  = g_strdup(snapshot.key_tab_7);
    if (snapshot.key_tab_8)  snapshot.key_tab_8  = g_strdup(snapshot.key_tab_8);
    if (snapshot.key_tab_9)  snapshot.key_tab_9  = g_strdup(snapshot.key_tab_9);
    if (snapshot.key_tab_10) snapshot.key_tab_10 = g_strdup(snapshot.key_tab_10);
    if (snapshot.key_close_app) snapshot.key_close_app = g_strdup(snapshot.key_close_app);
    if (snapshot.key_focus_left)  snapshot.key_focus_left  = g_strdup(snapshot.key_focus_left);
    if (snapshot.key_focus_right) snapshot.key_focus_right = g_strdup(snapshot.key_focus_right);
    if (snapshot.key_focus_up)    snapshot.key_focus_up    = g_strdup(snapshot.key_focus_up);
    if (snapshot.key_focus_down)  snapshot.key_focus_down  = g_strdup(snapshot.key_focus_down);
    if (snapshot.key_increase_opacity) snapshot.key_increase_opacity = g_strdup(snapshot.key_increase_opacity);
    if (snapshot.key_decrease_opacity) snapshot.key_decrease_opacity = g_strdup(snapshot.key_decrease_opacity);

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Customise Terminal",
                                                    GTK_WINDOW(parent_window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "_Save",
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);

    GtkWidget *stack_sidebar = gtk_stack_sidebar_new();
    gtk_box_pack_start(GTK_BOX(hbox), stack_sidebar, FALSE, FALSE, 0);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(stack_sidebar), GTK_STACK(stack));
    gtk_box_pack_start(GTK_BOX(hbox), stack, TRUE, TRUE, 0);

    // --- Appearance Page ---
    GtkWidget *appearance_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(appearance_box), 10);
    GtkWidget *app_scrolled = gtk_scrolled_window_new(NULL, NULL); // Add scrolling for many options
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app_scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(app_scrolled), appearance_box);
    gtk_stack_add_titled(GTK_STACK(stack), app_scrolled, "appearance", "Appearance");

    // --- Theme Section ---
    gtk_box_pack_start(GTK_BOX(appearance_box), gtk_label_new("Theme:"), FALSE, FALSE, 0);
    
    GtkWidget *theme_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(appearance_box), theme_row, FALSE, FALSE, 5);
    
    GtkWidget *theme_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(theme_row), theme_combo, TRUE, TRUE, 0);
    
    // Refresh list
    refresh_theme_combo(GTK_COMBO_BOX_TEXT(theme_combo));
    
    // Select current theme
    if (current_settings.theme) {
       // Loop to find valid ID? 
       // Just set active text? No generic API for that in 3.0 easily without ID.
       // Let's iterate manually or just rely on user picking.
       // For now, let's try to select by text if possible or just leave empty (custom).
       // Actually, we can loop:
       /*
       GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(theme_combo));
       GtkTreeIter iter;
       if (gtk_tree_model_get_iter_first(model, &iter)) {
           do {
               char *text;
               gtk_tree_model_get(model, &iter, 0, &text, -1);
               if (g_strcmp0(text, current_settings.theme) == 0) {
                   gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo), &iter);
                   g_free(text);
                   break;
               }
               g_free(text);
           } while (gtk_tree_model_iter_next(model, &iter));
       }
       */
       // Simplified: Just pre-select if it matches a known one. 
       // For now, let's skip auto-select to avoid complexity, user can re-select.
       // Or better:
    }
    
    g_signal_connect(theme_combo, "changed", G_CALLBACK(on_theme_combo_changed), notebook);

    GtkWidget *btn_save = gtk_button_new_from_icon_name("document-save-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn_save, "Save Theme As...");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_theme_clicked), NULL); // Pass combo if we want refresh
    gtk_box_pack_start(GTK_BOX(theme_row), btn_save, FALSE, FALSE, 0);

    GtkWidget *btn_delete = gtk_button_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn_delete, "Delete Selected Theme");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_theme_clicked), theme_combo);
    gtk_box_pack_start(GTK_BOX(theme_row), btn_delete, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(appearance_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);

    // --- Custom Colors ---
    GtkWidget *expander = gtk_expander_new("Custom Colors");
    gtk_box_pack_start(GTK_BOX(appearance_box), expander, FALSE, FALSE, 5);
    
    GtkWidget *app_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(app_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(app_grid), 10);
    gtk_container_add(GTK_CONTAINER(expander), app_grid);
    
    
    // Helper macro to add row
    #define ADD_COLOR_PICKER(r, c, label_text, setting_memb) \
        { \
            gtk_grid_attach(GTK_GRID(app_grid), gtk_label_new(label_text), c, r, 1, 1); \
            GtkWidget *cp = gtk_color_button_new_with_rgba(&current_settings.setting_memb); \
            g_object_set_data(G_OBJECT(cp), "target_color", &current_settings.setting_memb); \
            g_signal_connect(cp, "color-set", G_CALLBACK(on_custom_color_set), notebook); \
            gtk_widget_set_hexpand(cp, TRUE); \
            gtk_grid_attach(GTK_GRID(app_grid), cp, c+1, r, 1, 1); \
        }
        
    ADD_COLOR_PICKER(0, 0, "Window BG:", ui_background_color);
    ADD_COLOR_PICKER(0, 2, "Window Text:", ui_foreground_color);
    
    ADD_COLOR_PICKER(1, 0, "Terminal BG:", background_color);
    ADD_COLOR_PICKER(1, 2, "Terminal Text:", foreground_color);
    
    ADD_COLOR_PICKER(2, 0, "Cursor BG:", cursor_background_color);
    ADD_COLOR_PICKER(2, 2, "Cursor Text:", cursor_foreground_color);
    
    ADD_COLOR_PICKER(3, 0, "Button BG:", button_background_color);
    ADD_COLOR_PICKER(3, 2, "Button Text:", button_foreground_color);
    
    ADD_COLOR_PICKER(4, 0, "Btn Hover BG:", button_hover_background_color);
    ADD_COLOR_PICKER(4, 2, "Btn Hover Text:", button_hover_foreground_color);
    
    ADD_COLOR_PICKER(5, 0, "AI Panel BG:", ai_panel_background_color);
    ADD_COLOR_PICKER(5, 2, "AI Panel Text:", ai_panel_text_color);
    
    ADD_COLOR_PICKER(6, 0, "User Bubble BG:", ai_user_msg_bg_color);
    ADD_COLOR_PICKER(6, 2, "User Bubble Text:", ai_user_msg_text_color);
    
    ADD_COLOR_PICKER(7, 0, "AI Bubble BG:", ai_assistant_msg_bg_color);
    ADD_COLOR_PICKER(7, 2, "AI Bubble Text:", ai_assistant_msg_text_color);

    gtk_box_pack_start(GTK_BOX(appearance_box), gtk_label_new("Font:"), FALSE, FALSE, 0);
    GtkWidget *font_combo = gtk_combo_box_text_new();
    
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Hack (Regular)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Hack (Bold)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Fira Code (Regular)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Fira Code (Medium)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Fira Code (Bold)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "DejaVu Sans Mono");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Liberation Mono");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Courier");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Courier 10 Pitch");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "FreeMono");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo), "Noto Sans Mono");
    
    const char *hacker_fonts[][2] = {
        {"Hack 12", "Hack (Regular)"},
        {"Hack Bold 12", "Hack (Bold)"},
        {"Fira Code 12", "Fira Code (Regular)"},
        {"Fira Code Medium 12", "Fira Code (Medium)"},
        {"Fira Code Bold 12", "Fira Code (Bold)"},
        {"DejaVu Sans Mono 12", "DejaVu Sans Mono"},
        {"Liberation Mono 12", "Liberation Mono"},
        {"Courier 12", "Courier"},
        {"Courier 10 Pitch 12", "Courier 10 Pitch"},
        {"FreeMono 12", "FreeMono"},
        {"Noto Sans Mono 12", "Noto Sans Mono"}
    };
    
    int font_found = 0;
    for (int i = 0; i < 11; i++) {
        if (g_str_has_prefix(current_settings.font, hacker_fonts[i][0]) ||
            g_strcmp0(current_settings.font, hacker_fonts[i][0]) == 0 ||
            strstr(current_settings.font, hacker_fonts[i][1]) != NULL) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(font_combo), i);
            font_found = 1;
            break;
        }
    }
    
    if (!font_found) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(font_combo), 0);
    }
    
    g_signal_connect(font_combo, "changed", G_CALLBACK(on_font_changed), notebook);
    gtk_box_pack_start(GTK_BOX(appearance_box), font_combo, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(appearance_box), gtk_label_new("Opacity:"), FALSE, FALSE, 0);
    GtkWidget *transparency_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(transparency_scale), TRUE);
    gtk_range_set_value(GTK_RANGE(transparency_scale), current_settings.opacity * 100);
    g_signal_connect(transparency_scale, "value-changed", G_CALLBACK(on_transparency_changed), notebook);
    gtk_box_pack_start(GTK_BOX(appearance_box), transparency_scale, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(appearance_box), gtk_label_new("Background Color:"), FALSE, FALSE, 0);
    GtkWidget *bg_color_button = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(bg_color_button), TRUE);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(bg_color_button), &current_settings.background_color);
    g_object_set_data(G_OBJECT(bg_color_button), "transparency_scale", transparency_scale);
    // Link scale -> color button for real-time alpha sync
    g_object_set_data(G_OBJECT(transparency_scale), "bg_color_button", bg_color_button);

    g_signal_connect(bg_color_button, "color-set", G_CALLBACK(on_bg_color_set), notebook);
    gtk_box_pack_start(GTK_BOX(appearance_box), bg_color_button, FALSE, FALSE, 5);

    // --- Behavior Page ---
    GtkWidget *behavior_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(behavior_box), 10);
    gtk_stack_add_titled(GTK_STACK(stack), behavior_box, "behavior", "Behavior");

    gtk_box_pack_start(GTK_BOX(behavior_box), gtk_label_new("Scrollback Limit:"), FALSE, FALSE, 0);
    GtkAdjustment *adjustment = gtk_adjustment_new(current_settings.scrollback_lines, 100, 100000, 100, 1000, 0);
    GtkWidget *scrollback_spin = gtk_spin_button_new(adjustment, 1, 0);
    g_object_set_data(G_OBJECT(scrollback_spin), "notebook", notebook);
    g_signal_connect(scrollback_spin, "value-changed", G_CALLBACK(on_scrollback_changed), notebook);
    gtk_box_pack_start(GTK_BOX(behavior_box), scrollback_spin, FALSE, FALSE, 5);
    
    GtkWidget *unlimited_check = gtk_check_button_new_with_label("Unlimited history");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(unlimited_check), current_settings.unlimited_scrollback);
    g_signal_connect(unlimited_check, "toggled", G_CALLBACK(on_unlimited_toggled), scrollback_spin);
    gtk_box_pack_start(GTK_BOX(behavior_box), unlimited_check, FALSE, FALSE, 5);

    GtkWidget *bell_check = gtk_check_button_new_with_label("Audible Bell");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bell_check), current_settings.audible_bell);
    g_signal_connect(bell_check, "toggled", G_CALLBACK(on_bell_toggled), notebook);
    gtk_box_pack_start(GTK_BOX(behavior_box), bell_check, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(behavior_box), gtk_label_new("Cursor Shape:"), FALSE, FALSE, 0);
    GtkWidget *cursor_shape_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cursor_shape_combo), "Block");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cursor_shape_combo), "I-Beam");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cursor_shape_combo), "Underline");
    
    const char *cursor_shapes[] = {"Block", "I-Beam", "Underline"};
    for (int i = 0; i < 3; i++) {
        if (g_strcmp0(current_settings.cursor_shape, cursor_shapes[i]) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(cursor_shape_combo), i);
            break;
        }
    }
    g_signal_connect(cursor_shape_combo, "changed", G_CALLBACK(on_cursor_shape_changed), notebook);
    gtk_box_pack_start(GTK_BOX(behavior_box), cursor_shape_combo, FALSE, FALSE, 5);

    GtkWidget *cursor_blink_check = gtk_check_button_new_with_label("Blinking Cursor");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cursor_blink_check), current_settings.cursor_blinks);
    g_signal_connect(cursor_blink_check, "toggled", G_CALLBACK(on_cursor_blink_toggled), notebook);
    gtk_box_pack_start(GTK_BOX(behavior_box), cursor_blink_check, FALSE, FALSE, 5);

    // --- Keyboard Shortcuts Page ---
    // --- Shortcuts Page ---
    GtkWidget *shortcuts_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(shortcuts_page), 5);
    gtk_stack_add_titled(GTK_STACK(stack), shortcuts_page, "shortcuts", "Shortcuts");

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_box_pack_start(GTK_BOX(shortcuts_page), scrolled_window, TRUE, TRUE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 50);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    gtk_container_add(GTK_CONTAINER(scrolled_window), grid);

    // Headers
    GtkWidget *header_action = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_action), "<b>Action</b>");
    gtk_widget_set_halign(header_action, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), header_action, 0, 0, 1, 1);

    GtkWidget *header_key = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_key), "<b>Shortcut</b>");
    gtk_widget_set_halign(header_key, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), header_key, 1, 0, 1, 1);

    int row = 1;

    // Helper macro to add rows
    #define ADD_SHORTCUT_ROW(label_text, setting_ptr) \
        do { \
            GtkWidget *lbl = gtk_label_new(label_text); \
            gtk_widget_set_halign(lbl, GTK_ALIGN_START); \
            gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1); \
            GtkWidget *btn = gtk_button_new_with_label(*setting_ptr ? *setting_ptr : "None"); \
            g_signal_connect(btn, "clicked", G_CALLBACK(on_shortcut_button_clicked), setting_ptr); \
            gtk_widget_set_halign(btn, GTK_ALIGN_FILL); \
            gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 1, 1); \
            row++; \
        } while(0)

    ADD_SHORTCUT_ROW("Copy", &current_settings.key_copy);
    ADD_SHORTCUT_ROW("Paste", &current_settings.key_paste);
    ADD_SHORTCUT_ROW("Vertical Split", &current_settings.key_split_v);
    ADD_SHORTCUT_ROW("Horizontal Split", &current_settings.key_split_h);
    ADD_SHORTCUT_ROW("Collapse Split", &current_settings.key_collapse);

    // Split Navigation
    ADD_SHORTCUT_ROW("Left SubTerminal", &current_settings.key_focus_left);
    ADD_SHORTCUT_ROW("Right SubTerminal", &current_settings.key_focus_right);
    ADD_SHORTCUT_ROW("Above SubTerminal", &current_settings.key_focus_up);
    ADD_SHORTCUT_ROW("Below SubTerminal", &current_settings.key_focus_down);
    ADD_SHORTCUT_ROW("Increase Opacity", &current_settings.key_increase_opacity);
    ADD_SHORTCUT_ROW("Decrease Opacity", &current_settings.key_decrease_opacity);

    ADD_SHORTCUT_ROW("Toggle AI Panel", &current_settings.key_toggle_ai);
    ADD_SHORTCUT_ROW("Toggle Notes Panel", &current_settings.key_toggle_notes);
    ADD_SHORTCUT_ROW("Toggle Notes Pin", &current_settings.key_pin_notes);
    ADD_SHORTCUT_ROW("Snip Tool (Capture)", &current_settings.key_snip_tool);
    ADD_SHORTCUT_ROW("New Terminal", &current_settings.key_new_term);
    ADD_SHORTCUT_ROW("Previous Tab", &current_settings.key_prev_tab);
    ADD_SHORTCUT_ROW("Next Tab", &current_settings.key_next_tab);
    ADD_SHORTCUT_ROW("Close Terminal", &current_settings.key_close_term);
    ADD_SHORTCUT_ROW("Open Customise", &current_settings.key_customise);

    // Tab Shortcuts
    ADD_SHORTCUT_ROW("Jump to Tab 1", &current_settings.key_tab_1);
    ADD_SHORTCUT_ROW("Jump to Tab 2", &current_settings.key_tab_2);
    ADD_SHORTCUT_ROW("Jump to Tab 3", &current_settings.key_tab_3);
    ADD_SHORTCUT_ROW("Jump to Tab 4", &current_settings.key_tab_4);
    ADD_SHORTCUT_ROW("Jump to Tab 5", &current_settings.key_tab_5);
    ADD_SHORTCUT_ROW("Jump to Tab 6", &current_settings.key_tab_6);
    ADD_SHORTCUT_ROW("Jump to Tab 7", &current_settings.key_tab_7);
    ADD_SHORTCUT_ROW("Jump to Tab 8", &current_settings.key_tab_8);
    ADD_SHORTCUT_ROW("Jump to Tab 9", &current_settings.key_tab_9);
    ADD_SHORTCUT_ROW("Jump to Tab 10", &current_settings.key_tab_10);

    ADD_SHORTCUT_ROW("Close Scythe", &current_settings.key_close_app);

    // Static Shortcuts (Empty now if all are dynamic)
    #define ADD_STATIC_ROW(label_text, key_text) \
        do { \
            GtkWidget *lbl = gtk_label_new(label_text); \
            gtk_widget_set_halign(lbl, GTK_ALIGN_START); \
            gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1); \
            GtkWidget *key = gtk_label_new(key_text); \
            gtk_widget_set_halign(key, GTK_ALIGN_START); \
            gtk_grid_attach(GTK_GRID(grid), key, 1, row, 1, 1); \
            row++; \
        } while(0)

    // ADD_STATIC_ROW("Close App", "Alt+Q"); // Replaced by dynamic

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(shortcuts_page), separator, FALSE, FALSE, 5);

    GtkWidget *shortcut_button = gtk_button_new_with_label("Set Global Launch Shortcut...");
    gtk_widget_set_tooltip_text(shortcut_button, "Opens your system's keyboard settings.\nOnce there, add a new shortcut with the command 'scythe-fixed' and assign it to Super+S.");
    g_signal_connect(shortcut_button, "clicked", G_CALLBACK(on_set_shortcut_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(shortcuts_page), shortcut_button, FALSE, FALSE, 10);

    // --- AI Page ---
    GtkWidget *ai_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(ai_box), 10);
    gtk_stack_add_titled(GTK_STACK(stack), ai_box, "ai", "AI Assistant");

    gtk_box_pack_start(GTK_BOX(ai_box), gtk_label_new("AI Provider:"), FALSE, FALSE, 0);
    GtkWidget *provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(provider_combo), "Ollama");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(provider_combo), "Groq");
    
    if (g_strcmp0(current_settings.ai_provider, "Groq") == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(provider_combo), 1);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(provider_combo), 0);
    }
    g_signal_connect(provider_combo, "changed", G_CALLBACK(on_ai_provider_changed), NULL);
    gtk_box_pack_start(GTK_BOX(ai_box), provider_combo, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(ai_box), gtk_label_new("Groq API Key:"), FALSE, FALSE, 0);
    GtkWidget *api_key_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(api_key_entry), FALSE);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(api_key_entry), GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
    gtk_entry_set_icon_activatable(GTK_ENTRY(api_key_entry), GTK_ENTRY_ICON_SECONDARY, TRUE);
    g_signal_connect(api_key_entry, "icon-press", G_CALLBACK(on_api_key_icon_press), NULL);

    if (current_settings.groq_api_key) {
        gtk_entry_set_text(GTK_ENTRY(api_key_entry), current_settings.groq_api_key);
    }
    g_signal_connect(api_key_entry, "changed", G_CALLBACK(on_groq_api_key_changed), NULL);
    gtk_box_pack_start(GTK_BOX(ai_box), api_key_entry, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(ai_box), gtk_label_new("Groq Model:"), FALSE, FALSE, 0);
    GtkWidget *model_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), "llama-3.3-70b-versatile");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), "llama-3.1-8b-instant");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), "openai/gpt-oss-120b");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), "openai/gpt-oss-20b");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), "qwen/qwen3-32b");

    if (current_settings.groq_model) {
        if (g_strcmp0(current_settings.groq_model, "llama-3.1-8b-instant") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 1);
        else if (g_strcmp0(current_settings.groq_model, "openai/gpt-oss-120b") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 2);
        else if (g_strcmp0(current_settings.groq_model, "openai/gpt-oss-20b") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 3);
        else if (g_strcmp0(current_settings.groq_model, "qwen/qwen3-32b") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 4);
        else gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 0);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), 0);
    }
    g_signal_connect(model_combo, "changed", G_CALLBACK(on_groq_model_changed), NULL);
    gtk_box_pack_start(GTK_BOX(ai_box), model_combo, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        save_settings();
        extern void setup_global_snip_shortcut(void);
        setup_global_snip_shortcut();
        // Free snapshot strings (no longer needed)
        g_free(snapshot.theme); g_free(snapshot.font); g_free(snapshot.cursor_shape);
        g_free(snapshot.ai_provider); g_free(snapshot.groq_api_key); g_free(snapshot.groq_model);
        g_free(snapshot.key_copy); g_free(snapshot.key_paste); g_free(snapshot.key_split_v);
        g_free(snapshot.key_split_h); g_free(snapshot.key_collapse); g_free(snapshot.key_toggle_ai);
        g_free(snapshot.key_toggle_notes); g_free(snapshot.key_pin_notes); g_free(snapshot.key_snip_tool);
        g_free(snapshot.key_new_term); g_free(snapshot.key_prev_tab); g_free(snapshot.key_next_tab);
        g_free(snapshot.key_close_term); g_free(snapshot.key_customise);
        g_free(snapshot.key_tab_1); g_free(snapshot.key_tab_2); g_free(snapshot.key_tab_3);
        g_free(snapshot.key_tab_4); g_free(snapshot.key_tab_5); g_free(snapshot.key_tab_6);
        g_free(snapshot.key_tab_7); g_free(snapshot.key_tab_8); g_free(snapshot.key_tab_9);
        g_free(snapshot.key_tab_10); g_free(snapshot.key_close_app);
        g_free(snapshot.key_focus_left); g_free(snapshot.key_focus_right);
        g_free(snapshot.key_focus_up); g_free(snapshot.key_focus_down);
        g_free(snapshot.key_increase_opacity); g_free(snapshot.key_decrease_opacity);
    } else {
        // Restore settings from snapshot (avoids stale settings.json read)
        // First free current_settings strings that the dialog may have allocated
        g_free(current_settings.theme); g_free(current_settings.font);
        g_free(current_settings.cursor_shape); g_free(current_settings.ai_provider);
        g_free(current_settings.groq_api_key); g_free(current_settings.groq_model);
        g_free(current_settings.key_copy); g_free(current_settings.key_paste);
        g_free(current_settings.key_split_v); g_free(current_settings.key_split_h);
        g_free(current_settings.key_collapse); g_free(current_settings.key_toggle_ai);
        g_free(current_settings.key_toggle_notes); g_free(current_settings.key_pin_notes); g_free(current_settings.key_snip_tool);
        g_free(current_settings.key_new_term); g_free(current_settings.key_prev_tab);
        g_free(current_settings.key_next_tab); g_free(current_settings.key_close_term);
        g_free(current_settings.key_customise);
        g_free(current_settings.key_tab_1); g_free(current_settings.key_tab_2);
        g_free(current_settings.key_tab_3); g_free(current_settings.key_tab_4);
        g_free(current_settings.key_tab_5); g_free(current_settings.key_tab_6);
        g_free(current_settings.key_tab_7); g_free(current_settings.key_tab_8);
        g_free(current_settings.key_tab_9); g_free(current_settings.key_tab_10);
        g_free(current_settings.key_close_app);
        g_free(current_settings.key_focus_left); g_free(current_settings.key_focus_right);
        g_free(current_settings.key_focus_up); g_free(current_settings.key_focus_down);
        g_free(current_settings.key_increase_opacity); g_free(current_settings.key_decrease_opacity);
        // Copy snapshot back (snapshot strings are already allocated copies)
        memcpy(&current_settings, &snapshot, sizeof(AppSettings));
        // Re-apply restored settings
        apply_ui_theme();
        apply_settings_to_all_terminals(notebook);
    }
    gtk_widget_destroy(dialog);
}
