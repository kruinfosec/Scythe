// Helper function to render a buffered table with proper column alignment
static void render_table(GtkTextView *text_view, GPtrArray *table_rows) {
    if (!table_rows || table_rows->len == 0) return;
    
    // Step 1: Parse all rows and find column count
    guint max_cols = 0;
    for (guint i = 0; i < table_rows->len; i++) {
        gchar *row = g_ptr_array_index(table_rows, i);
        gchar **cells = g_strsplit(row, "|", -1);
        guint col_count = g_strv_length(cells) - 2; // Exclude first and last empty splits
        if (col_count > max_cols) max_cols = col_count;
        g_strfreev(cells);
    }
    
    if (max_cols == 0) return;
    
    // Step 2: Calculate max width for each column
    guint *col_widths = g_new0(guint, max_cols);
    
    for (guint i = 0; i < table_rows->len; i++) {
        gchar *row = g_ptr_array_index(table_rows, i);
        gchar **cells = g_strsplit(row, "|", -1);
        
        for (guint j = 1; j < g_strv_length(cells) - 1 && (j-1) < max_cols; j++) {
            gchar *trimmed = g_strstrip(g_strdup(cells[j]));
            guint len = g_utf8_strlen(trimmed, -1);
            if (len > col_widths[j-1]) col_widths[j-1] = len;
            g_free(trimmed);
        }
        g_strfreev(cells);
    }
    
    // Step 3: Render each row with proper padding
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end_iter;
    
    for (guint i = 0; i < table_rows->len; i++) {
        gchar *row = g_ptr_array_index(table_rows, i);
        gchar **cells = g_strsplit(row, "|", -1);
        
        GString *formatted_row = g_string_new("| ");
        
        for (guint j = 1; j < g_strv_length(cells) - 1 && (j-1) < max_cols; j++) {
            gchar *trimmed = g_strstrip(g_strdup(cells[j]));
            guint len = g_utf8_strlen(trimmed, -1);
            guint padding = col_widths[j-1] - len;
            
            g_string_append(formatted_row, trimmed);
            for (guint k = 0; k < padding; k++) {
                g_string_append_c(formatted_row, ' ');
            }
            g_string_append(formatted_row, " | ");
            g_free(trimmed);
        }
        
        g_string_append_c(formatted_row, '\n');
        
        // Insert with monospace tag
        gtk_text_buffer_get_end_iter(buffer, &end_iter);
        GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, &end_iter, TRUE);
        gtk_text_buffer_insert(buffer, &end_iter, formatted_row->str, -1);
        
        GtkTextIter start_iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);
        gtk_text_buffer_get_end_iter(buffer, &end_iter);
        
        GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
        GtkTextTag *mono_tag = gtk_text_tag_table_lookup(tag_table, "monospace");
        if (mono_tag) {
            gtk_text_buffer_apply_tag(buffer, mono_tag, &start_iter, &end_iter);
        }
        
        gtk_text_buffer_delete_mark(buffer, start_mark);
        
        g_string_free(formatted_row, TRUE);
        g_strfreev(cells);
    }
    
    g_free(col_widths);
    
    // Scroll to show new content
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    GtkTextMark *end_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(text_view, end_mark, 0.0, TRUE, 0.5, 0.5);
}
