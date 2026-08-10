
// Helper to append text with optional tags
static void append_text_with_tag(GtkTextView *text_view, const char *text, gboolean bold, gboolean monospace) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    
    // Ensure tags exist
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(table, "bold")) {
        gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    if (!gtk_text_tag_table_lookup(table, "monospace")) {
        gtk_text_buffer_create_tag(buffer, "monospace", "family", "Monospace", NULL);
    }

    if (monospace) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end_iter, text, -1, "monospace", NULL);
    } else if (bold) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end_iter, text, -1, "bold", NULL);
    } else {
        gtk_text_buffer_insert(buffer, &end_iter, text, -1);
    }
    
    GtkTextMark *end_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(text_view, end_mark, 0.0, TRUE, 0.5, 0.5);
}

static void parse_and_append(GtkTextView *text_view, const char *text, AiParserState *state) {
    if (!text || *text == '\0') return;

    // Append new text to partial buffer
    g_string_append(state->partial_buffer, text);
    
    // Process line by line for table detection
    char *data = state->partial_buffer->str;
    char *next_newline;
    size_t processed_len = 0;

    while ((next_newline = strchr(data + processed_len, '\n')) != NULL) {
        size_t line_len = next_newline - (data + processed_len) + 1; // Include newline
        char *line = g_strndup(data + processed_len, line_len);
        
        // Check if line is a table row (starts with |)
        // Skip leading whitespace
        char *trimmed = g_strstrip(g_strdup(line));
        gboolean is_table_row = g_str_has_prefix(trimmed, "|");
        g_free(trimmed);

        if (is_table_row) {
             append_text_with_tag(text_view, line, FALSE, TRUE);
        } else {
            // Process tags within the line (bold, think)
            // Note: This simplifies logic by processing bold/think only on non-table lines
            // Ideally we should process bold inside tables too, but for now let's keep it simple
            // or we can reuse the previous character-based loop for non-table lines.
            
            // Reuse the character-based logic for this line
            size_t i = 0;
            size_t len = strlen(line);
             while (i < len) {
                // Check for <think> start
                if (!state->in_think_block && g_str_has_prefix(line + i, "<think>")) {
                    state->in_think_block = TRUE;
                    i += 7;
                    continue;
                }
                // Check for </think> end
                if (state->in_think_block && g_str_has_prefix(line + i, "</think>")) {
                    state->in_think_block = FALSE;
                    i += 8;
                    continue;
                }
                // Check for ** bold toggle
                if (!state->in_think_block && g_str_has_prefix(line + i, "**")) {
                    state->in_bold_block = !state->in_bold_block;
                    i += 2;
                    continue;
                }
                // If inside think block, skip content
                if (state->in_think_block) {
                    i++;
                    continue;
                }
                
                // Find next special sequence
                size_t next_special = len;
                const char *ptr = line + i;
                const char *next_think = strstr(ptr, "<think>");
                const char *next_end_think = strstr(ptr, "</think>");
                const char *next_bold = strstr(ptr, "**");
                
                if (next_think && (size_t)(next_think - line) < next_special) next_special = next_think - line;
                if (next_end_think && (size_t)(next_end_think - line) < next_special) next_special = next_end_think - line;
                if (next_bold && (size_t)(next_bold - line) < next_special) next_special = next_bold - line;
                
                if (next_special > i) {
                    char *chunk = g_strndup(line + i, next_special - i);
                    append_text_with_tag(text_view, chunk, state->in_bold_block, FALSE);
                    g_free(chunk);
                    i = next_special;
                } else {
                    char chunk[2] = {line[i], '\0'};
                    append_text_with_tag(text_view, chunk, state->in_bold_block, FALSE);
                    i++;
                }
            }
        }
        g_free(line);
        processed_len += line_len;
    }
    
    // Remove processed lines from buffer
    g_string_erase(state->partial_buffer, 0, processed_len);
}
