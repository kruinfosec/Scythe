
// Helper to ensure tags exist
static void ensure_tags(GtkTextBuffer *buffer) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(table, "bold")) {
        gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "italic")) {
        gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "monospace")) {
        gtk_text_buffer_create_tag(buffer, "monospace", "family", "Monospace", NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "code_block")) {
        GdkRGBA bg_color;
        gdk_rgba_parse(&bg_color, "#2d2d2d"); // Dark grey background
        gtk_text_buffer_create_tag(buffer, "code_block", 
            "family", "Monospace", 
            "background-rgba", &bg_color,
            "foreground", "white", // Ensure visible text
            NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "header")) {
        gtk_text_buffer_create_tag(buffer, "header", "scale", 1.5, "weight", PANGO_WEIGHT_BOLD, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    }
}

// Helper to append text with active tags
static void append_chunk(GtkTextView *text_view, const char *text, AiParserState *state) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    ensure_tags(buffer);
    
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    
    // Collect active tag names
    GSList *tags = NULL;
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    
    if (state->in_code_block) {
        tags = g_slist_prepend(tags, gtk_text_tag_table_lookup(table, "code_block"));
    } else {
        if (state->in_bold_block) tags = g_slist_prepend(tags, gtk_text_tag_table_lookup(table, "bold"));
        if (state->in_italic_block) tags = g_slist_prepend(tags, gtk_text_tag_table_lookup(table, "italic"));
        // Note: Headers are handled per-line usually, but let's see if we can do it here
    }
    
    // Reverse list to preserve order (though order doesn't matter much for these specific tags)
    tags = g_slist_reverse(tags);
    
    // Insert with tags
    if (tags) {
        // GtkTextBuffer doesn't have a "insert with tag list" function easily accessible without creating a custom one or iterating.
        // gtk_text_buffer_insert_with_tags is varargs.
        // Let's stick to applying tags after insertion or using specific combinations.
        // For simplicity, let's just use insert_with_tags_by_name for single tags, or apply multiple.
        
        gtk_text_buffer_insert(buffer, &end_iter, text, -1);
        
        // Apply tags to the newly inserted region
        GtkTextIter start_iter = end_iter;
        gtk_text_iter_backward_chars(&start_iter, g_utf8_strlen(text, -1));
        
        for (GSList *l = tags; l != NULL; l = l->next) {
            gtk_text_buffer_apply_tag(buffer, GTK_TEXT_TAG(l->data), &start_iter, &end_iter);
        }
        g_slist_free(tags);
    } else {
        gtk_text_buffer_insert(buffer, &end_iter, text, -1);
    }
    
    GtkTextMark *end_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(text_view, end_mark, 0.0, TRUE, 0.5, 0.5);
}

static void parse_and_append(GtkTextView *text_view, const char *text, AiParserState *state) {
    if (!text || *text == '\0') return;

    g_string_append(state->partial_buffer, text);
    char *data = state->partial_buffer->str;
    size_t len = state->partial_buffer->len;
    size_t i = 0;
    
    while (i < len) {
        // 1. Check for <think> start
        if (!state->in_think_block && !state->in_code_block && g_str_has_prefix(data + i, "<think>")) {
            state->in_think_block = TRUE;
            i += 7;
            
            // Create Expander
            GtkTextBuffer *main_buffer = gtk_text_view_get_buffer(text_view);
            GtkTextIter end_iter;
            gtk_text_buffer_get_end_iter(main_buffer, &end_iter);
            
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(main_buffer, &end_iter);
            GtkWidget *expander = gtk_expander_new("Thinking Process...");
            GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
            gtk_widget_set_size_request(scroll, -1, 150); // Limit height
            GtkWidget *think_view = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(think_view), FALSE);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(think_view), GTK_WRAP_WORD_CHAR);
            
            gtk_container_add(GTK_CONTAINER(scroll), think_view);
            gtk_container_add(GTK_CONTAINER(expander), scroll);
            gtk_widget_show_all(expander);
            
            gtk_text_view_add_child_at_anchor(text_view, expander, anchor);
            state->think_text_view = GTK_TEXT_VIEW(think_view);
            continue;
        }
        
        // 2. Check for </think> end
        if (state->in_think_block && g_str_has_prefix(data + i, "</think>")) {
            state->in_think_block = FALSE;
            state->think_text_view = NULL;
            i += 8;
            continue;
        }
        
        // If inside think block, redirect output
        if (state->in_think_block) {
            // We need to process the think content too, but for now let's just dump it raw or recursively parse?
            // Let's dump raw for safety first, or simple append.
            // But wait, we are inside a loop. We need to find the END of the think block or the end of the buffer.
            char *end_think = strstr(data + i, "</think>");
            size_t chunk_len = end_think ? (size_t)(end_think - (data + i)) : (len - i);
            
            char *chunk = g_strndup(data + i, chunk_len);
            if (state->think_text_view) {
                GtkTextBuffer *tb = gtk_text_view_get_buffer(state->think_text_view);
                GtkTextIter ti;
                gtk_text_buffer_get_end_iter(tb, &ti);
                gtk_text_buffer_insert(tb, &ti, chunk, -1);
                // Scroll think view
                GtkTextMark *tm = gtk_text_buffer_get_insert(tb);
                gtk_text_view_scroll_to_mark(state->think_text_view, tm, 0.0, TRUE, 0.5, 0.5);
            }
            g_free(chunk);
            i += chunk_len;
            continue;
        }

        // 3. Check for Code Block ```
        if (g_str_has_prefix(data + i, "```")) {
            state->in_code_block = !state->in_code_block;
            i += 3;
            // Consume optional language name if starting?
            // For simplicity, just consume the backticks. 
            // If we want to hide the backticks, we skip them.
            // If we want to show them, we append them.
            // Let's hide them for cleaner UI? Or keep them? Standard is usually to hide or style.
            // Let's skip appending them.
            continue;
        }

        // 4. Check for Bold **
        if (!state->in_code_block && g_str_has_prefix(data + i, "**")) {
            state->in_bold_block = !state->in_bold_block;
            i += 2;
            continue;
        }
        
        // 5. Check for Italic * (need to be careful not to match **)
        // If we see *, check if next char is *. If so, it's bold (handled above).
        // If not, it's italic.
        if (!state->in_code_block && data[i] == '*' && (i + 1 >= len || data[i+1] != '*')) {
             state->in_italic_block = !state->in_italic_block;
             i += 1;
             continue;
        }
        
        // 6. Check for Header # (only at start of line)
        // This is tricky in a stream. We need to know if we are at start of line.
        // Let's simplify: If we see # and previous char was \n (or start of buffer), apply header tag to the line?
        // Implementing full header logic in stream is hard.
        // Alternative: Just check if we are appending a chunk that starts with #.
        
        // Find next special token
        size_t next_special = len;
        // ... search logic ...
        // To avoid infinite complexity, let's iterate char by char or small chunks for now?
        // Or find the nearest special token.
        
        const char *ptr = data + i;
        const char *next_think = strstr(ptr, "<think>");
        const char *next_code = strstr(ptr, "```");
        const char *next_bold = strstr(ptr, "**");
        const char *next_italic = strchr(ptr, '*'); // Could be bold or italic
        
        if (next_think && (size_t)(next_think - data) < next_special) next_special = next_think - data;
        if (next_code && (size_t)(next_code - data) < next_special) next_special = next_code - data;
        if (next_bold && (size_t)(next_bold - data) < next_special) next_special = next_bold - data;
        if (next_italic && (size_t)(next_italic - data) < next_special) next_special = next_italic - data;
        
        if (next_special > i) {
            char *chunk = g_strndup(data + i, next_special - i);
            append_chunk(text_view, chunk, state);
            g_free(chunk);
            i = next_special;
        } else {
            // Potential partial tag at end?
            if (len - i < 8) break; // Wait for more data
            
            // Just one char
            char chunk[2] = {data[i], '\0'};
            append_chunk(text_view, chunk, state);
            i++;
        }
    }
    
    g_string_erase(state->partial_buffer, 0, i);
}
