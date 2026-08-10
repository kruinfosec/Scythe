#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

void load_settings();
void save_settings();
void apply_settings(VteTerminal *terminal);
void apply_settings_to_all_terminals(GtkNotebook *notebook);
void apply_ui_theme();
void open_customise_dialog(GtkWidget *widget, gpointer user_data);

extern AppSettings current_settings;

#endif // CONFIG_H
