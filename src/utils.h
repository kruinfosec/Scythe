#ifndef UTILS_H
#define UTILS_H

#include "common.h"

void free_menu_item_data(gpointer data);
void popup_menu_at_button(GtkWidget *button, GtkWidget *menu);
GtkWidget* create_dropdown_menu(GtkWidget *button, GList *menu_items, GCallback callback);

#endif // UTILS_H
