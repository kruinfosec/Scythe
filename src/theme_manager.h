#ifndef SCYTHE_THEME_MANAGER_H
#define SCYTHE_THEME_MANAGER_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include "common.h"

// Initialize theme system (create directory, default themes)
void theme_manager_init(void);

// List available themes (returns GList of strings, caller must free list and strings)
GList* theme_manager_get_list(void);

// Load a specific theme by name into current_settings
// Returns TRUE if successful
gboolean theme_manager_load_theme(const char *theme_name);

// Save current settings as a new theme
// Returns TRUE if successful
gboolean theme_manager_save_theme(const char *theme_name);

// Delete a theme
gboolean theme_manager_delete_theme(const char *theme_name);

// Helper to get path
char* theme_manager_get_path(const char *theme_name);

#endif
