#include "theme_manager.h"
#include <sys/stat.h>
#include <string.h>
#include <glib/gstdio.h>

static char *get_themes_dir(void) {
    const char *config_home = g_get_user_config_dir();
    return g_build_filename(config_home, "scythe", "themes", NULL);
}

// Helper to save a specific color to JSON builder
static void serialize_color(JsonBuilder *builder, const char *name, GdkRGBA *color) {
    char *hex = gdk_rgba_to_string(color);
    json_builder_set_member_name(builder, name);
    json_builder_add_string_value(builder, hex);
    g_free(hex);
}

// Helper to parse color from JSON object
static void parse_color(JsonObject *root, const char *name, GdkRGBA *target) {
    if (json_object_has_member(root, name)) {
        const char *hex = json_object_get_string_member(root, name);
        gdk_rgba_parse(target, hex);
    }
}

void theme_manager_init(void) {
    char *path = get_themes_dir();
    g_mkdir_with_parents(path, 0755);

    // Always write default themes to ensure users get the latest defaults
    char *dark_path = g_build_filename(path, "Dark.json", NULL);
    const char *dark_json = 
    "{\n"
    "  \"background_color\": \"#0C1015\",\n"
    "  \"foreground_color\": \"#39FF14\",\n"
    "  \"cursor_background_color\": \"#39FF14\",\n"
    "  \"cursor_foreground_color\": \"#0C1015\",\n"
    "  \"ui_background_color\": \"#0C1015\",\n"
    "  \"ui_foreground_color\": \"#39FF14\",\n"
    "  \"button_background_color\": \"#1C2025\",\n"
    "  \"button_foreground_color\": \"#39FF14\",\n"
    "  \"button_hover_background_color\": \"#2C3035\",\n"
    "  \"button_hover_foreground_color\": \"#50FF50\",\n"
    "  \"ai_panel_background_color\": \"#0C1015\",\n"
    "  \"ai_panel_text_color\": \"#39FF14\",\n"
    "  \"ai_user_msg_bg_color\": \"rgba(57, 255, 20, 0.1)\",\n"
    "  \"ai_user_msg_text_color\": \"#39FF14\",\n"
    "  \"ai_assistant_msg_bg_color\": \"rgba(57, 255, 20, 0.05)\",\n"
    "  \"ai_assistant_msg_text_color\": \"#39FF14\"\n"
    "}";
    g_file_set_contents(dark_path, dark_json, -1, NULL);
    
    char *light_path = g_build_filename(path, "Light.json", NULL);
    const char *light_json = 
    "{\n"
    "  \"background_color\": \"#FDF7E8\",\n"
    "  \"foreground_color\": \"#1F1612\",\n"
    "  \"cursor_background_color\": \"#2563EB\",\n"
    "  \"cursor_foreground_color\": \"#FDF7E8\",\n"
    "  \"ui_background_color\": \"#F4ECCF\",\n"
    "  \"ui_foreground_color\": \"#1F1612\",\n"
    "  \"button_background_color\": \"#EADFBC\",\n"
    "  \"button_foreground_color\": \"#1F1612\",\n"
    "  \"button_hover_background_color\": \"#DDD0A4\",\n"
    "  \"button_hover_foreground_color\": \"#1F1612\",\n"
    "  \"ai_panel_background_color\": \"#FDF7E8\",\n"
    "  \"ai_panel_text_color\": \"#1F1612\",\n"
    "  \"ai_user_msg_bg_color\": \"rgba(37, 99, 235, 0.10)\",\n"
    "  \"ai_user_msg_text_color\": \"#1F1612\",\n"
    "  \"ai_assistant_msg_bg_color\": \"rgba(244, 236, 207, 0.85)\",\n"
    "  \"ai_assistant_msg_text_color\": \"#1F1612\"\n"
    "}";
    g_file_set_contents(light_path, light_json, -1, NULL);

    g_free(dark_path);
    g_free(light_path);
    g_free(path);
}

GList* theme_manager_get_list(void) {
    GList *list = NULL;
    char *dir_path = get_themes_dir();
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    
    if (dir) {
        const char *filename;
        while ((filename = g_dir_read_name(dir))) {
            if (g_str_has_suffix(filename, ".json")) {
                // Strip extension
                char *name = g_strndup(filename, strlen(filename) - 5);
                list = g_list_append(list, name);
            }
        }
        g_dir_close(dir);
    }
    g_free(dir_path);
    return list;
}

gboolean theme_manager_load_theme(const char *theme_name) {
    char *dir = get_themes_dir();
    char *filename = g_strdup_printf("%s.json", theme_name);
    char *path = g_build_filename(dir, filename, NULL);
    gboolean success = FALSE;

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_file(parser, path, NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        JsonObject *root = json_node_get_object(root_node);
        
        parse_color(root, "background_color", &current_settings.background_color);
        parse_color(root, "foreground_color", &current_settings.foreground_color);
        parse_color(root, "cursor_background_color", &current_settings.cursor_background_color);
        parse_color(root, "cursor_foreground_color", &current_settings.cursor_foreground_color);
        parse_color(root, "ui_background_color", &current_settings.ui_background_color);
        parse_color(root, "ui_foreground_color", &current_settings.ui_foreground_color);
        parse_color(root, "button_background_color", &current_settings.button_background_color);
        parse_color(root, "button_foreground_color", &current_settings.button_foreground_color);
        parse_color(root, "button_hover_background_color", &current_settings.button_hover_background_color);
        parse_color(root, "button_hover_foreground_color", &current_settings.button_hover_foreground_color);
        parse_color(root, "ai_panel_background_color", &current_settings.ai_panel_background_color);
        parse_color(root, "ai_panel_text_color", &current_settings.ai_panel_text_color);
        parse_color(root, "ai_user_msg_bg_color", &current_settings.ai_user_msg_bg_color);
        parse_color(root, "ai_user_msg_text_color", &current_settings.ai_user_msg_text_color);
        parse_color(root, "ai_assistant_msg_bg_color", &current_settings.ai_assistant_msg_bg_color);
        parse_color(root, "ai_assistant_msg_text_color", &current_settings.ai_assistant_msg_text_color);
        
        // Update theme name in settings
        if (current_settings.theme) g_free(current_settings.theme);
        current_settings.theme = g_strdup(theme_name);
        
        // Preserve saved opacity setting across theme switches
        current_settings.background_color.alpha = current_settings.opacity;
        
        success = TRUE;
    }
    
    g_object_unref(parser);
    g_free(path);
    g_free(filename);
    g_free(dir);
    return success;
}

gboolean theme_manager_save_theme(const char *theme_name) {
    char *dir = get_themes_dir();
    char *filename = g_strdup_printf("%s.json", theme_name);
    char *path = g_build_filename(dir, filename, NULL);
    
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    serialize_color(builder, "background_color", &current_settings.background_color);
    serialize_color(builder, "foreground_color", &current_settings.foreground_color);
    serialize_color(builder, "cursor_background_color", &current_settings.cursor_background_color);
    serialize_color(builder, "cursor_foreground_color", &current_settings.cursor_foreground_color);
    serialize_color(builder, "ui_background_color", &current_settings.ui_background_color);
    serialize_color(builder, "ui_foreground_color", &current_settings.ui_foreground_color);
    serialize_color(builder, "button_background_color", &current_settings.button_background_color);
    serialize_color(builder, "button_foreground_color", &current_settings.button_foreground_color);
    serialize_color(builder, "button_hover_background_color", &current_settings.button_hover_background_color);
    serialize_color(builder, "button_hover_foreground_color", &current_settings.button_hover_foreground_color);
    serialize_color(builder, "ai_panel_background_color", &current_settings.ai_panel_background_color);
    serialize_color(builder, "ai_panel_text_color", &current_settings.ai_panel_text_color);
    serialize_color(builder, "ai_user_msg_bg_color", &current_settings.ai_user_msg_bg_color);
    serialize_color(builder, "ai_user_msg_text_color", &current_settings.ai_user_msg_text_color);
    serialize_color(builder, "ai_assistant_msg_bg_color", &current_settings.ai_assistant_msg_bg_color);
    serialize_color(builder, "ai_assistant_msg_text_color", &current_settings.ai_assistant_msg_text_color);
    
    json_builder_end_object(builder);
    
    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json_generator_to_file(gen, path, NULL);
    
    // Cleanup
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(path);
    g_free(filename);
    g_free(dir);
    
    return TRUE;
}

gboolean theme_manager_delete_theme(const char *theme_name) {
    char *dir = get_themes_dir();
    char *filename = g_strdup_printf("%s.json", theme_name);
    char *path = g_build_filename(dir, filename, NULL);
    
    int res = g_remove(path);
    
    g_free(path);
    g_free(filename);
    g_free(dir);
    
    return (res == 0);
}
