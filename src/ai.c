#include "ai.h"
#include "config.h" // Access current_settings
#include "webview_ai.h"
#include "terminal.h" // For get_current_terminal

// State for AI panel toggle
gboolean ai_panel_visible = FALSE;  // Hidden by default on startup
gint last_paned_position = 700;  // Will be updated on realize

// Obsolete functions removed (ensure_tags, append_chunk, json_to_markdown, render_json_table, render_table, parse_and_append)

// Callback for when a chunk of data is read from the stream
static void ai_stream_read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GInputStream *stream = G_INPUT_STREAM(source_object);
    AiStreamData *stream_data = (AiStreamData *)user_data;
    GError *error = NULL;
    
    GBytes *bytes = g_input_stream_read_bytes_finish(stream, res, &error);
    
    if (bytes) {
        gsize size;
        const char *data = g_bytes_get_data(bytes, &size);
        
        if (size > 0) {
            // Append chunk to buffer
            g_string_append_len(stream_data->buffer, data, size);
            
            // Process lines in buffer
            char *line_start = stream_data->buffer->str;
            char *newline;
            
            while ((newline = strchr(line_start, '\n')) != NULL) {
                *newline = '\0'; // Terminate line
                
                char *line = line_start;
                // Skip empty lines
                if (strlen(line) > 0) {
                    // Check for SSE data
                    if (g_str_has_prefix(line, "data: ")) {
                        char *json_str = line + 6; // Skip "data: "
                        if (g_strcmp0(g_strstrip(json_str), "[DONE]") != 0) {
                            // Parse JSON
                            JsonParser *parser = json_parser_new();
                            if (json_parser_load_from_data(parser, json_str, -1, NULL)) {
                                JsonNode *root = json_parser_get_root(parser);
                                if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                                    JsonObject *obj = json_node_get_object(root);
                                    
                                    // Handle OpenAI/Groq format
                                    if (json_object_has_member(obj, "choices")) {
                                        JsonArray *choices = json_object_get_array_member(obj, "choices");
                                        if (json_array_get_length(choices) > 0) {
                                            JsonObject *choice = json_array_get_object_element(choices, 0);
                                            if (json_object_has_member(choice, "delta")) {
                                                JsonObject *delta = json_object_get_object_member(choice, "delta");
                                                if (json_object_has_member(delta, "content")) {
                                                    const char *content = json_object_get_string_member(delta, "content");
                                                    if (content) {
                                                        g_string_append(stream_data->response_text, content);
                                                        webview_update_ai_message(stream_data->webview, stream_data->response_text->str);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    // Handle Ollama format
                                    else if (json_object_has_member(obj, "response")) {
                                        const char *content = json_object_get_string_member(obj, "response");
                                        if (content) {
                                            g_string_append(stream_data->response_text, content);
                                            webview_update_ai_message(stream_data->webview, stream_data->response_text->str);
                                        }
                                    }
                                }
                            }
                            g_object_unref(parser);
                        }
                    }
                    // Handle Ollama raw JSON (non-SSE) if applicable
                    else if (g_str_has_prefix(line, "{")) {
                         JsonParser *parser = json_parser_new();
                         if (json_parser_load_from_data(parser, line, -1, NULL)) {
                             JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                             if (json_object_has_member(obj, "response")) {
                                 const char *content = json_object_get_string_member(obj, "response");
                                 if (content) {
                                     g_string_append(stream_data->response_text, content);
                                     webview_update_ai_message(stream_data->webview, stream_data->response_text->str);
                                 }
                             }
                         }
                         g_object_unref(parser);
                    }
                }
                
                line_start = newline + 1;
            }
            
            // Keep remaining partial line in buffer
            if (line_start < stream_data->buffer->str + stream_data->buffer->len) {
                g_string_erase(stream_data->buffer, 0, line_start - stream_data->buffer->str);
            } else {
                g_string_truncate(stream_data->buffer, 0);
            }
            
            // Continue reading
            g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, NULL, ai_stream_read_callback, stream_data);
        } else {
            // End of stream
            if (current_conversation && stream_data->response_text->len > 0) {
                 conversation_add_message(current_conversation, "assistant", stream_data->response_text->str);
                 conversation_save(current_conversation);
            }
            
            // Cleanup
            g_input_stream_close(stream, NULL, NULL);
            g_object_unref(stream);
            g_string_free(stream_data->buffer, TRUE);
            g_string_free(stream_data->response_text, TRUE);
            g_free(stream_data);
        }
        g_bytes_unref(bytes);
        return;
    }

    if (error) {
        g_warning("Error reading stream: %s", error->message);
        g_error_free(error);
    }
    
    // Cleanup on error
    g_input_stream_close(stream, NULL, NULL);
    g_object_unref(stream);
    if (stream_data) {
        if (stream_data->buffer) g_string_free(stream_data->buffer, TRUE);
        if (stream_data->response_text) g_string_free(stream_data->response_text, TRUE);
        g_free(stream_data);
    }
}

// Callback for when the initial HTTP headers are received
// Callback for when the initial HTTP headers are received
static void ai_headers_received_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    SoupSession *session = SOUP_SESSION(source_object);
    GInputStream *stream = NULL;
    GError *error = NULL;
    GtkWidget *webview = GTK_WIDGET(user_data);

    stream = soup_session_send_finish(session, res, &error);

    if (error) {
        g_critical("Failed to get AI response stream: %s", error->message);
        g_error_free(error);
        
        // Show error in webview
        webview_update_ai_message(webview, "**Error:** Failed to connect to AI provider.\nCheck your settings and internet connection.");
        return;
    }

    AiStreamData *stream_data = g_new0(AiStreamData, 1);
    stream_data->webview = webview;
    stream_data->buffer = g_string_new("");
    stream_data->response_text = g_string_new("");  // For saving to conversation

    // Start reading the first chunk
    g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, NULL, ai_stream_read_callback, stream_data);
}



// Function to send a message to the AI (rewritten for true streaming)
// Helper to get system prompt from Modelfile
static char* get_system_prompt() {
    char *content = NULL;
    gsize length = 0;
    GError *error = NULL;
    
    g_print("DEBUG: Attempting to read Modelfile...\n");
    
    // Try to read Modelfile
    if (g_file_get_contents("Modelfile", &content, &length, &error)) {
        g_print("DEBUG: Modelfile read successfully.\n");
        // Find SYSTEM """
        char *start = strstr(content, "SYSTEM \"\"\"");
        if (start) {
            start += 10; // Skip SYSTEM """
            char *end = strstr(start, "\"\"\"");
            if (end) {
                *end = '\0'; // Terminate string
                
                // Construct dynamic identity
                const char *model_name = "Unknown Model";
                if (g_strcmp0(current_settings.ai_provider, "Groq") == 0) {
                    model_name = current_settings.groq_model ? current_settings.groq_model : "Groq Model";
                } else {
                    model_name = "Ollama Model";
                }
                
                char *full_prompt = g_strdup_printf(
                    "%s\n\nIMPORTANT IDENTITY INSTRUCTION:\n"
                    "You are Scythe, powered by %s. You are NOT ChatGPT, OpenAI, or any other assistant. "
                    "Always identify as Scythe.",
                    start, model_name
                );
                
                g_print("DEBUG: System prompt extracted: %.100s...\n", full_prompt);
                g_free(content);
                return full_prompt;
            } else {
                g_print("DEBUG: Could not find closing quotes for SYSTEM prompt.\n");
            }
        } else {
            g_print("DEBUG: Could not find 'SYSTEM \"\"\"' in Modelfile.\n");
        }
        g_free(content);
    } else {
        g_print("DEBUG: Failed to read Modelfile: %s\n", error->message);
    }
    
    if (error) g_error_free(error);
    
    // Fallback if Modelfile not found or parse error
    g_print("DEBUG: Using fallback system prompt.\n");
    return g_strdup("You are Scythe, a helpful AI assistant. You are not ChatGPT or OpenAI.");
}

// Function to send a message to the AI (rewritten for true streaming)
void send_ai_message(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    GtkEntry *entry = widgets->entry;
    GtkWidget *webview = widgets->webview;

    const gchar *input_text = gtk_entry_get_text(entry);
    if (!input_text || strlen(input_text) == 0) {
        return; // Don't send empty messages
    }

    // Add user message to WebView
    webview_append_message(webview, "user", input_text);
    
    // Prepare for AI Response (empty bubble)
    webview_start_ai_message(webview);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    gboolean is_groq = (g_strcmp0(current_settings.ai_provider, "Groq") == 0);
    const char *url;

    if (is_groq) {
        url = "https://api.groq.com/openai/v1/chat/completions";
        json_builder_set_member_name(builder, "model");
        json_builder_add_string_value(builder, current_settings.groq_model ? current_settings.groq_model : "llama3-70b-8192");
        
        json_builder_set_member_name(builder, "messages");
        json_builder_begin_array(builder);
        
        // System Prompt
        char *system_prompt = get_system_prompt();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "role");
        json_builder_add_string_value(builder, "system");
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, system_prompt);
        json_builder_end_object(builder);
        g_free(system_prompt);
        
        // Add conversation history (last 10 messages for context)
        if (current_conversation && current_conversation->messages->len > 0) {
            guint start_idx = 0;
            if (current_conversation->messages->len > 10) {
                start_idx = current_conversation->messages->len - 10;
            }
            
            for (guint i = start_idx; i < current_conversation->messages->len; i++) {
                Message *msg = g_ptr_array_index(current_conversation->messages, i);
                
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "role");
                json_builder_add_string_value(builder, msg->role);
                json_builder_set_member_name(builder, "content");
                json_builder_add_string_value(builder, msg->content);
                json_builder_end_object(builder);
            }
        }
        
        // Add current user message
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "role");
        json_builder_add_string_value(builder, "user");
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, input_text);
        json_builder_end_object(builder);
        json_builder_end_array(builder);

        json_builder_set_member_name(builder, "stream");
        json_builder_add_boolean_value(builder, true);
    } else {
        // Ollama default
        url = "http://127.0.0.1:11434/api/generate";
        json_builder_set_member_name(builder, "model");
        json_builder_add_string_value(builder, "dama");
        json_builder_set_member_name(builder, "prompt");
        json_builder_add_string_value(builder, input_text);
        json_builder_set_member_name(builder, "stream");
        json_builder_add_boolean_value(builder, true);
    }
    
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *json_str = json_generator_to_data(gen, NULL);

    // Create and send the request
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    
    GBytes *body = g_bytes_new(json_str, strlen(json_str));
    soup_message_set_request_body_from_bytes(msg, "application/json", body);
    g_bytes_unref(body);

    if (is_groq) {
        if (current_settings.groq_api_key && strlen(current_settings.groq_api_key) > 0) {
            gchar *auth_header = g_strdup_printf("Bearer %s", current_settings.groq_api_key);
            soup_message_headers_append(soup_message_get_request_headers(msg), "Authorization", auth_header);
            g_free(auth_header);
        } else {
             // Show error in webview
             webview_update_ai_message(webview, "**Error:** Groq API Key is missing. Please check settings.");
             g_free(json_str);
             json_node_free(root);
             g_object_unref(gen);
             g_object_unref(builder);
             g_object_unref(session);
             return;
        }
    }

    // Clear the input field
    gtk_entry_set_text(entry, "");

    // Send the request asynchronously - pass webview as user_data
    soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, NULL, ai_headers_received_callback, webview);

    // Cleanup
    g_free(json_str);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    g_object_unref(session);
}

// Function to cleanup AI widgets when window is destroyed
// Function to send a visualization request
void send_visualization_request(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    GtkEntry *entry = widgets->entry;
    
    // Get current input
    const gchar *input_text = gtk_entry_get_text(entry);
    char *prompt = NULL;
    
    if (input_text && strlen(input_text) > 0) {
        // Use user input
        prompt = g_strdup_printf("Create a Mermaid.js diagram for: %s", input_text);
    } else {
        // Default context-aware prompt
        prompt = g_strdup("Based on our conversation so far, create a Mermaid.js diagram (flowchart, sequence, mindmap, etc.) that best visualizes the key concepts or processes we discussed.");
    }
    
    // Set the prompt in the entry so send_ai_message can pick it up
    gtk_entry_set_text(entry, prompt);
    
    // Trigger the standard send message function
    send_ai_message(widget, user_data);
    
    g_free(prompt);
}

// Function to cleanup AI widgets when window is destroyed
void cleanup_ai_widgets(GtkWidget *widget, gpointer user_data) {
    AiWidgets *widgets = (AiWidgets *)user_data;
    if (widgets) {
        g_free(widgets);
    }
}

// Single unified toggle function for AI panel (used by both button and shortcut)
void toggle_ai_panel_common(GtkPaned *hpaned) {
    if (!hpaned) return;

    GtkWidget *ai_panel = gtk_paned_get_child2(hpaned);
    if (!ai_panel) return;

    if (ai_panel_visible) {
        // CLOSING: Save position, hide panel, focus terminal
        last_paned_position = gtk_paned_get_position(hpaned);
        gint max_pos = gtk_widget_get_allocated_width(GTK_WIDGET(hpaned));
        gtk_paned_set_position(hpaned, max_pos);
        gtk_widget_set_visible(ai_panel, FALSE);
        ai_panel_visible = FALSE;
        
        // Return focus to terminal
        GtkWidget *notebook = gtk_paned_get_child1(hpaned);
        if (notebook && GTK_IS_NOTEBOOK(notebook)) {
            VteTerminal *terminal = get_current_terminal(GTK_NOTEBOOK(notebook));
            if (terminal) {
                gtk_widget_grab_focus(GTK_WIDGET(terminal));
            }
        }
    } else {
        // OPENING: Restore position, show panel, focus AI entry
        gtk_paned_set_position(hpaned, last_paned_position);
        gtk_widget_set_visible(ai_panel, TRUE);
        ai_panel_visible = TRUE;
        
        // Auto-focus AI entry
        if (ai_entry_widget) {
            gtk_widget_grab_focus(ai_entry_widget);
        }
    }
}

// Button callback - just updates button style and calls common function
void on_toggle_ai_panel(GtkWidget *widget, gpointer user_data) {
    GtkPaned *hpaned = GTK_PANED(user_data);
    if (!hpaned) return;

    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    
    // Update button style
    if (ai_panel_visible) {
        gtk_style_context_add_class(context, "dama-off");
    } else {
        gtk_style_context_remove_class(context, "dama-off");
    }
    
    // Call common toggle logic
    toggle_ai_panel_common(hpaned);
}
