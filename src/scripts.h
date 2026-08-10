#ifndef SCRIPTS_H
#define SCRIPTS_H

#include "common.h"

gchar* get_scripts_directory();
void rebuild_automate_menu(GtkWidget *automate_btn);
void add_automation_script(GtkWidget *widget, gpointer data);
void automate_action(GtkWidget *widget, gpointer data);

#endif // SCRIPTS_H
