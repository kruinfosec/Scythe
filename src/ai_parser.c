
// Helper to append text with optional bold tag
static void append_text_with_tag(GtkTextView *text_view, const char *text, gboolean bold) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    
    if (bold) {
        // Ensure bold tag exists
        GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
        if (!gtk_text_tag_table_lookup(table, "bold")) {
            gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
        }
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
    
    const char *data = state->partial_buffer->str;
    size_t len = state->partial_buffer->len;
    size_t processed_len = 0;
    
    for (size_t i = 0; i < len; ) {
        // Check for <think> start
        if (!state->in_think_block && g_str_has_prefix(data + i, "<think>")) {
            state->in_think_block = TRUE;
            i += 7;
            processed_len = i;
            continue;
        }
        
        // Check for </think> end
        if (state->in_think_block && g_str_has_prefix(data + i, "</think>")) {
            state->in_think_block = FALSE;
            i += 8;
            processed_len = i;
            continue;
        }
        
        // Check for ** bold toggle
        if (!state->in_think_block && g_str_has_prefix(data + i, "**")) {
            state->in_bold_block = !state->in_bold_block;
            i += 2;
            processed_len = i;
            continue;
        }
        
        // If inside think block, skip content
        if (state->in_think_block) {
            i++;
            processed_len = i;
            continue;
        }
        
        // Handle normal text
        // Find next special sequence or end of string
        size_t next_special = len;
        const char *next_think = strstr(data + i, "<think>");
        const char *next_end_think = strstr(data + i, "</think>");
        const char *next_bold = strstr(data + i, "**");
        
        if (next_think && (size_t)(next_think - data) < next_special) next_special = next_think - data;
        if (next_end_think && (size_t)(next_end_think - data) < next_special) next_special = next_end_think - data;
        if (next_bold && (size_t)(next_bold - data) < next_special) next_special = next_bold - data;
        
        if (next_special > i) {
            char *chunk = g_strndup(data + i, next_special - i);
            append_text_with_tag(text_view, chunk, state->in_bold_block);
            g_free(chunk);
            i = next_special;
            processed_len = i;
        } else {
            // We are at a potential partial tag or just moving forward
             // Check if we might be at the start of a tag that is cut off
            if (len - i < 8) { // Max tag length needed to detect (<think>)
                 break; // Stop processing and leave in buffer
            }
            i++;
        }
    }
    
    // Remove processed part from buffer
    g_string_erase(state->partial_buffer, 0, processed_len);
}
