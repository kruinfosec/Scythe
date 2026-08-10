
static void on_ai_provider_changed(GtkComboBox *combo_box, gpointer user_data) {
    const gchar *provider = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (provider) {
        if (current_settings.ai_provider) g_free(current_settings.ai_provider);
        current_settings.ai_provider = g_strdup(provider);
        save_settings();
    }
}

static void on_groq_api_key_changed(GtkEntry *entry, gpointer user_data) {
    const gchar *key = gtk_entry_get_text(entry);
    if (current_settings.groq_api_key) g_free(current_settings.groq_api_key);
    current_settings.groq_api_key = g_strdup(key);
    save_settings();
}

static void on_groq_model_changed(GtkComboBox *combo_box, gpointer user_data) {
    const gchar *model = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));
    if (model) {
        if (current_settings.groq_model) g_free(current_settings.groq_model);
        current_settings.groq_model = g_strdup(model);
        save_settings();
    }
}

// ... inside open_customise_dialog ...
// I will append this to the file and then use multi_replace to insert it into the function
