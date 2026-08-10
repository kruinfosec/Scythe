
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
            
            GtkTextBuffer *main_buffer = gtk_text_view_get_buffer(text_view);
            GtkTextIter end_iter;
            gtk_text_buffer_get_end_iter(main_buffer, &end_iter);
            
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(main_buffer, &end_iter);
            GtkWidget *expander = gtk_expander_new("Thinking Process...");
            GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
            gtk_widget_set_size_request(scroll, -1, 200); // Dynamic width, fixed height
            gtk_widget_set_hexpand(scroll, TRUE);
            gtk_widget_set_vexpand(scroll, TRUE);
            
            GtkWidget *think_view = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(think_view), FALSE);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(think_view), GTK_WRAP_WORD_CHAR);
            gtk_widget_set_hexpand(think_view, TRUE);
            
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
            char *end_think = strstr(data + i, "</think>");
            size_t chunk_len = end_think ? (size_t)(end_think - (data + i)) : (len - i);
            
            char *chunk = g_strndup(data + i, chunk_len);
            if (state->think_text_view) {
                GtkTextBuffer *tb = gtk_text_view_get_buffer(state->think_text_view);
                GtkTextIter ti;
                gtk_text_buffer_get_end_iter(tb, &ti);
                gtk_text_buffer_insert(tb, &ti, chunk, -1);
                GtkTextMark *tm = gtk_text_buffer_get_insert(tb);
                gtk_text_view_scroll_to_mark(state->think_text_view, tm, 0.0, TRUE, 0.5, 0.5);
            }
            g_free(chunk);
            i += chunk_len;
            continue;
        }

        // 3. Check for Code Block ``` start
        if (!state->in_code_block && g_str_has_prefix(data + i, "```")) {
            state->in_code_block = TRUE;
            i += 3;
            
            // Create Code Box
            GtkTextBuffer *main_buffer = gtk_text_view_get_buffer(text_view);
            GtkTextIter end_iter;
            gtk_text_buffer_get_end_iter(main_buffer, &end_iter);
            
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(main_buffer, &end_iter);
            
            GtkWidget *frame = gtk_frame_new(NULL);
            gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
            gtk_widget_set_margin_top(frame, 5);
            gtk_widget_set_margin_bottom(frame, 5);
            
            GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
            gtk_widget_set_size_request(scroll, -1, -1); // Auto size? Or max height?
            // Let's set a reasonable min height or let it grow.
            // For code blocks, we usually want them to grow but maybe scroll if too huge?
            // Let's let it grow for now, but maybe limit max height?
            // GTK3 TextView inside ScrolledWindow grows unless we limit it.
            gtk_widget_set_vexpand(scroll, FALSE); // Don't take all space
            
            GtkWidget *code_view = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(code_view), FALSE);
            gtk_text_view_set_monospace(GTK_TEXT_VIEW(code_view), TRUE);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(code_view), GTK_WRAP_NONE); // Code usually doesn't wrap
            
            // Style the code view
            GtkStyleContext *context = gtk_widget_get_style_context(code_view);
            gtk_style_context_add_class(context, "code-view");
            // We can add CSS provider later or just set background here if possible?
            // CSS is better. For now let's rely on default or simple style.
            // Or use the tag trick inside? No, we want the whole view background.
            // Let's try to set a dark background via CSS in main.c or here?
            // For now, just the frame is a good start.
            
            gtk_container_add(GTK_CONTAINER(scroll), code_view);
            gtk_container_add(GTK_CONTAINER(frame), scroll);
            gtk_widget_show_all(frame);
            
            gtk_text_view_add_child_at_anchor(text_view, frame, anchor);
            state->code_text_view = GTK_TEXT_VIEW(code_view);
            
            // Skip optional language identifier (up to newline)
            // This is tricky in stream. We might just print it.
            // Let's just continue and let the next loop iteration handle text.
            continue;
        }

        // 4. Check for Code Block ``` end
        if (state->in_code_block && g_str_has_prefix(data + i, "```")) {
            state->in_code_block = FALSE;
            state->code_text_view = NULL;
            i += 3;
            continue;
        }

        // If inside code block, redirect output
        if (state->in_code_block) {
            char *end_code = strstr(data + i, "```");
            size_t chunk_len = end_code ? (size_t)(end_code - (data + i)) : (len - i);
            
            char *chunk = g_strndup(data + i, chunk_len);
            if (state->code_text_view) {
                GtkTextBuffer *tb = gtk_text_view_get_buffer(state->code_text_view);
                GtkTextIter ti;
                gtk_text_buffer_get_end_iter(tb, &ti);
                gtk_text_buffer_insert(tb, &ti, chunk, -1);
            }
            g_free(chunk);
            i += chunk_len;
            continue;
        }

        // 5. Check for Bold **
        if (!state->in_code_block && g_str_has_prefix(data + i, "**")) {
            state->in_bold_block = !state->in_bold_block;
            i += 2;
            continue;
        }
        
        // 6. Check for Italic * 
        if (!state->in_code_block && data[i] == '*' && (i + 1 >= len || data[i+1] != '*')) {
             state->in_italic_block = !state->in_italic_block;
             i += 1;
             continue;
        }
        
        // Find next special token
        size_t next_special = len;
        const char *ptr = data + i;
        const char *next_think = strstr(ptr, "<think>");
        const char *next_code = strstr(ptr, "```");
        const char *next_bold = strstr(ptr, "**");
        const char *next_italic = strchr(ptr, '*');
        
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
            if (len - i < 8) break; // Wait for more data
            char chunk[2] = {data[i], '\0'};
            append_chunk(text_view, chunk, state);
            i++;
        }
    }
    
    g_string_erase(state->partial_buffer, 0, i);
}
