
// Global label for model footer
static GtkWidget *model_footer_label = NULL;

static void on_model_selected(GtkMenuItem *item, gpointer user_data) {
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        const char *model = gtk_menu_item_get_label(item);
        if (current_settings.groq_model) g_free(current_settings.groq_model);
        current_settings.groq_model = g_strdup(model);
        save_settings();
        
        // Update footer label
        if (model_footer_label) {
            char *text = g_strdup_printf("Model: %s", model);
            gtk_label_set_text(GTK_LABEL(model_footer_label), text);
            g_free(text);
        }
    }
}

// In create_ai_panel:
    // ... header creation ...
    
    // Model Selector with Radio Items
    GtkWidget *model_menu_btn = gtk_menu_button_new();
    GtkWidget *model_icon = gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(model_menu_btn), model_icon);
    gtk_widget_set_tooltip_text(model_menu_btn, "Select AI Model");
    
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
        if (g_strcmp0(current_settings.groq_model, models[i]) == 0) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        }
        
        g_signal_connect(item, "toggled", G_CALLBACK(on_model_selected), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(model_menu), item);
    }
    
    gtk_widget_show_all(model_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(model_menu_btn), model_menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), model_menu_btn);

    // ... box packing ...

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
    
    // Add opacity to make it subtle
    gtk_widget_set_opacity(model_footer_label, 0.7);
    
    gtk_box_pack_end(GTK_BOX(box), model_footer_label, FALSE, FALSE, 0);

