#include "conversation.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <glib.h>
#include <hpdf.h>

// Get the conversation storage directory
const char *conversation_get_storage_dir(void) {
    static char storage_dir[512] = {0};
    if (storage_dir[0] == '\0') {
        const char *home = g_get_home_dir();
        snprintf(storage_dir, sizeof(storage_dir), "%s/.local/share/scythe/conversations", home);
    }
    return storage_dir;
}

// Ensure storage directory exists
void conversation_ensure_storage_dir(void) {
    const char *storage_dir = conversation_get_storage_dir();
    g_mkdir_with_parents(storage_dir, 0700);
}

// Generate a UUID for new conversations
char *conversation_generate_uuid(void) {
    uuid_t uuid;
    char *uuid_str = g_malloc(37);
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);
    return uuid_str;
}

// Free a message (used by GPtrArray)
static void message_free(gpointer data) {
    Message *msg = (Message *)data;
    if (msg) {
        g_free(msg->content);
        g_free(msg);
    }
}

// Create a new conversation
Conversation *conversation_new(const char *title, const char *model, gboolean is_temporary) {
    Conversation *conv = g_new0(Conversation, 1);
    
    // Generate UUID
    char *uuid_str = conversation_generate_uuid();
    strncpy(conv->session_id, uuid_str, sizeof(conv->session_id) - 1);
    g_free(uuid_str);
    
    conv->db_session_id = session_get_current();

    // Set title
    conv->title = g_strdup(title ? title : "New Conversation");
    
    // Set timestamps
    conv->created = time(NULL);
    conv->modified = conv->created;
    
    // Set flags
    conv->is_temporary = is_temporary;
    conv->auto_titled = TRUE;  // Initial title is auto-generated
    
    // Set model
    strncpy(conv->model, model ? model : "llama-3.3-70b-versatile", sizeof(conv->model) - 1);
    
    // Initialize messages array with proper free function
    conv->messages = g_ptr_array_new_with_free_func((GDestroyNotify)message_free);
    
    return conv;
}

// Free a conversation
void conversation_free(Conversation *conv) {
    if (!conv) return;
    
    g_free(conv->title);
    
    // Free messages - the array's free_func will handle individual messages
    if (conv->messages) {
        g_ptr_array_free(conv->messages, TRUE);
    }
    
    g_free(conv);
}

// Add a message to the conversation
void conversation_add_message(Conversation *conv, const char *role, const char *content) {
    if (!conv || !role || !content) return;
    
    Message *msg = g_new0(Message, 1);
    strncpy(msg->role, role, sizeof(msg->role) - 1);
    msg->content = g_strdup(content);
    msg->timestamp = time(NULL);
    
    g_ptr_array_add(conv->messages, msg);
    conv->modified = time(NULL);
    
    // Log to Database
    int session_id = conv->db_session_id != -1 ? conv->db_session_id : session_get_current();
    if (session_id != -1) {
        log_chat_message(session_id, role, content);
    }
    
    // Auto-title: Only if title was auto-generated (not manually set)
    // Update title based on first few user messages to reflect conversation topic
    if (conv->auto_titled && strcmp(role, "user") == 0) {
        // Count user messages
        int user_msg_count = 0;
        for (guint i = 0; i < conv->messages->len; i++) {
            Message *m = g_ptr_array_index(conv->messages, i);
            if (strcmp(m->role, "user") == 0) {
                user_msg_count++;
            }
        }
        
        // Generate title from first 1-2 user messages
        if (user_msg_count <= 2) {
            GString *title_builder = g_string_new("");
            int added = 0;
            
            for (guint i = 0; i < conv->messages->len && added < 2; i++) {
                Message *m = g_ptr_array_index(conv->messages, i);
                if (strcmp(m->role, "user") == 0) {
                    if (added > 0) g_string_append(title_builder, " + ");
                    
                    // Add first 40 chars of this message
                    char snippet[45];
                    strncpy(snippet, m->content, 40);
                    snippet[40] = '\0';
                    
                    // Remove newlines
                    for (int j = 0; snippet[j]; j++) {
                        if (snippet[j] == '\n' || snippet[j] == '\r') {
                            snippet[j] = ' ';
                        }
                    }
                    
                    g_string_append(title_builder, snippet);
                    if (strlen(m->content) > 40) g_string_append(title_builder, "...");
                    added++;
                }
            }
            
            if (title_builder->len > 0) {
                g_free(conv->title);
                conv->title = g_string_free(title_builder, FALSE);
            } else {
                g_string_free(title_builder, TRUE);
            }
        }
    }
}

// Clear all messages from a conversation
void conversation_clear_messages(Conversation *conv) {
    if (!conv || !conv->messages) return;
    
    for (guint i = 0; i < conv->messages->len; i++) {
        Message *msg = g_ptr_array_index(conv->messages, i);
        if (msg) {
            g_free(msg->content);
            g_free(msg);
        }
    }
    g_ptr_array_set_size(conv->messages, 0);
}

// Save conversation to JSON file
gboolean conversation_save(Conversation *conv) {
    if (!conv || conv->is_temporary) return FALSE;
    
    // Don't save empty conversations
    if (conv->messages->len == 0) {
        return FALSE;
    }
    
    conversation_ensure_storage_dir();
    
    // Build file path
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", 
             conversation_get_storage_dir(), conv->session_id);
    
    // Create JSON object
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "session_id");
    json_builder_add_string_value(builder, conv->session_id);
    
    json_builder_set_member_name(builder, "db_session_id");
    json_builder_add_int_value(builder, (gint64)conv->db_session_id);
    
    json_builder_set_member_name(builder, "title");
    json_builder_add_string_value(builder, conv->title);
    
    json_builder_set_member_name(builder, "created");
    json_builder_add_int_value(builder, (gint64)conv->created);
    
    json_builder_set_member_name(builder, "modified");
    json_builder_add_int_value(builder, (gint64)conv->modified);
    
    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, conv->model);
    
    // Add messages array
    json_builder_set_member_name(builder, "messages");
    json_builder_begin_array(builder);
    
    for (guint i = 0; i < conv->messages->len; i++) {
        Message *msg = g_ptr_array_index(conv->messages, i);
        json_builder_begin_object(builder);
        
        json_builder_set_member_name(builder, "role");
        json_builder_add_string_value(builder, msg->role);
        
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, msg->content);
        
        json_builder_set_member_name(builder, "timestamp");
        json_builder_add_int_value(builder, (gint64)msg->timestamp);
        
        json_builder_end_object(builder);
    }
    
    json_builder_end_array(builder);
    json_builder_end_object(builder);
    
    // Generate JSON
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    
    // Write to file
    GError *error = NULL;
    gboolean success = json_generator_to_file(generator, filepath, &error);
    
    if (!success) {
        g_warning("Failed to save conversation: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
    }
    
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    
    return success;
}

// Load conversation from JSON file
Conversation *conversation_load(const char *session_id) {
    if (!session_id) return NULL;
    
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", 
             conversation_get_storage_dir(), session_id);
    
    // Check if file exists
    if (!g_file_test(filepath, G_FILE_TEST_EXISTS)) {
        return NULL;
    }
    
    // Parse JSON
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    
    if (!json_parser_load_from_file(parser, filepath, &error)) {
        g_warning("Failed to load conversation: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }
    
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }
    
    JsonObject *obj = json_node_get_object(root);
    
    // Create conversation
    Conversation *conv = g_new0(Conversation, 1);
    
    strncpy(conv->session_id, json_object_get_string_member(obj, "session_id"), 
            sizeof(conv->session_id) - 1);
    
    if (json_object_has_member(obj, "db_session_id")) {
        conv->db_session_id = (int)json_object_get_int_member(obj, "db_session_id");
    } else {
        conv->db_session_id = -1;
    }
    
    conv->title = g_strdup(json_object_get_string_member(obj, "title"));
    conv->created = (time_t)json_object_get_int_member(obj, "created");
    conv->modified = (time_t)json_object_get_int_member(obj, "modified");
    strncpy(conv->model, json_object_get_string_member(obj, "model"), 
            sizeof(conv->model) - 1);
    conv->is_temporary = FALSE;
    
    // Load messages
    conv->messages = g_ptr_array_new_with_free_func((GDestroyNotify)message_free);
    
    if (json_object_has_member(obj, "messages")) {
        JsonArray *messages = json_object_get_array_member(obj, "messages");
        for (guint i = 0; i < json_array_get_length(messages); i++) {
            JsonObject *msg_obj = json_array_get_object_element(messages, i);
            
            Message *msg = g_new0(Message, 1);
            strncpy(msg->role, json_object_get_string_member(msg_obj, "role"), 
                    sizeof(msg->role) - 1);
            msg->content = g_strdup(json_object_get_string_member(msg_obj, "content"));
            msg->timestamp = (time_t)json_object_get_int_member(msg_obj, "timestamp");
            
            g_ptr_array_add(conv->messages, msg);
        }
    }
    
    g_object_unref(parser);
    return conv;
}

// List all conversations
GPtrArray *conversation_list_all(void) {
    conversation_ensure_storage_dir();
    
    GDir *dir;
    GError *error = NULL;
    const char *storage_dir = conversation_get_storage_dir();
    
    dir = g_dir_open(storage_dir, 0, &error);
    if (!dir) {
        if (error) g_error_free(error);
        return g_ptr_array_new_with_free_func((GDestroyNotify)conversation_free);
    }
    
    GPtrArray *conversations = g_ptr_array_new_with_free_func((GDestroyNotify)conversation_free);
    
    const gchar *filename;
    while ((filename = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(filename, ".json")) {
            char session_id[37] = {0};
            strncpy(session_id, filename, sizeof(session_id) - 1);
            session_id[36] = '\0';
            char *dot = strrchr(session_id, '.');
            if (dot) *dot = '\0';
            
            Conversation *conv = conversation_load(session_id);
            if (conv) {
                g_ptr_array_add(conversations, conv);
            }
        }
    }
    
    g_dir_close(dir);
    return conversations;
}

GPtrArray *conversation_list_by_db_session(int db_session_id) {
    GPtrArray *all = conversation_list_all();
    if (!all) return g_ptr_array_new_with_free_func((GDestroyNotify)conversation_free);

    GPtrArray *filtered = g_ptr_array_new_with_free_func((GDestroyNotify)conversation_free);
    for (guint i = 0; i < all->len; i++) {
        Conversation *conv = g_ptr_array_index(all, i);
        if (conv) {
            if (conv->db_session_id == db_session_id || conv->db_session_id == -1) {
                g_ptr_array_index(all, i) = NULL; // Steal ref
                g_ptr_array_add(filtered, conv);
            }
        }
    }
    g_ptr_array_free(all, TRUE);
    return filtered;
}

// Delete a conversation
gboolean conversation_delete(const char *session_id) {
    if (!session_id) return FALSE;
    
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", 
             conversation_get_storage_dir(), session_id);
    
    GFile *file = g_file_new_for_path(filepath);
    GError *error = NULL;
    gboolean success = g_file_delete(file, NULL, &error);
    
    if (!success && error) {
        g_warning("Failed to delete conversation: %s", error->message);
        g_error_free(error);
    }
    
    g_object_unref(file);
    return success;
}

// Rename a conversation
gboolean conversation_rename(const char *session_id, const char *new_title) {
    if (!session_id || !new_title) return FALSE;
    
    Conversation *conv = conversation_load(session_id);
    if (!conv) return FALSE;
    
    g_free(conv->title);
    conv->title = g_strdup(new_title);
    conv->modified = time(NULL);
    
    gboolean success = conversation_save(conv);
    conversation_free(conv);
    
    return success;
}

// Export conversation as Markdown
gboolean conversation_export_markdown(Conversation *conv, const char *output_path) {
    if (!conv || !output_path) return FALSE;
    
    FILE *fp = fopen(output_path, "w");
    if (!fp) return FALSE;
    
    // Write header
    fprintf(fp, "# %s\n\n", conv->title);
    fprintf(fp, "**Model:** %s  \n", conv->model);
    fprintf(fp, "**Created:** %s\n", ctime(&conv->created));
    fprintf(fp, "---\n\n");
    
    // Write messages
    for (guint i = 0; i < conv->messages->len; i++) {
        Message *msg = g_ptr_array_index(conv->messages, i);
        
        if (strcmp(msg->role, "user") == 0) {
            fprintf(fp, "## 👤 User\n\n%s\n\n", msg->content);
        } else {
            fprintf(fp, "## 🤖 Dama\n\n%s\n\n", msg->content);
        }
        fprintf(fp, "---\n\n");
    }
    
    fclose(fp);
    return TRUE;
}

// Export conversation as PDF using libharu
gboolean conversation_export_pdf(Conversation *conv, const char *output_path) {
    if (!conv || !output_path) return FALSE;
    
    HPDF_Doc pdf = HPDF_New(NULL, NULL);
    if (!pdf) {
        g_warning("Failed to create PDF document");
        return FALSE;
    }
    
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    
    // Add a page
    HPDF_Page page = HPDF_AddPage(pdf);
    HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
    
    // Get fonts
    HPDF_Font title_font = HPDF_GetFont(pdf, "Helvetica-Bold", NULL);
    HPDF_Font heading_font = HPDF_GetFont(pdf, "Helvetica-Bold", NULL);
    HPDF_Font body_font = HPDF_GetFont(pdf, "Helvetica", NULL);
    
    float width = HPDF_Page_GetWidth(page);
    float height = HPDF_Page_GetHeight(page);
    float margin = 50;
    float y_pos = height - margin;
    
    // Title
    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, title_font, 20);
    HPDF_Page_MoveTextPos(page, margin, y_pos);
    HPDF_Page_ShowText(page, conv->title);
    HPDF_Page_EndText(page);
    y_pos -= 30;
    
    // Metadata
    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, body_font, 10);
    HPDF_Page_SetRGBFill(page, 0.5, 0.5, 0.5);
    HPDF_Page_MoveTextPos(page, margin, y_pos);
    
    char metadata[256];
    snprintf(metadata, sizeof(metadata), "Model: %s | Created: %s", 
             conv->model, ctime(&conv->created));
    HPDF_Page_ShowText(page, metadata);
    HPDF_Page_EndText(page);
    y_pos -= 30;
    
    // Separator line
    HPDF_Page_SetLineWidth(page, 1);
    HPDF_Page_MoveTo(page, margin, y_pos);
    HPDF_Page_LineTo(page, width - margin, y_pos);
    HPDF_Page_Stroke(page);
    y_pos -= 20;
    
    // Messages
    for (guint i = 0; i < conv->messages->len; i++) {
        Message *msg = g_ptr_array_index(conv->messages, i);
        
        // Check if we need a new page
        if (y_pos < margin + 100) {
            page = HPDF_AddPage(pdf);
            HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
            y_pos = height - margin;
        }
        
        // Role label
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, heading_font, 12);
        HPDF_Page_SetRGBFill(page, 0, 0, 0);
        HPDF_Page_MoveTextPos(page, margin, y_pos);
        
        if (strcmp(msg->role, "user") == 0) {
            HPDF_Page_SetRGBFill(page, 0.2, 0.4, 0.8);
            HPDF_Page_ShowText(page, "User:");
        } else {
            HPDF_Page_SetRGBFill(page, 0.4, 0.2, 0.8);
            HPDF_Page_ShowText(page, "Dama:");
        }
        HPDF_Page_EndText(page);
        y_pos -= 20;
        
        // Message content (word-wrapped)
        HPDF_Page_SetRGBFill(page, 0, 0, 0);
        HPDF_Page_SetFontAndSize(page, body_font, 10);
        
        // Simple word wrapping
        char *content = g_strdup(msg->content);
        char *line = strtok(content, "\n");
        
        while (line) {
            if (y_pos < margin + 50) {
                page = HPDF_AddPage(pdf);
                HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                y_pos = height - margin;
            }
            
            HPDF_Page_BeginText(page);
            HPDF_Page_MoveTextPos(page, margin + 10, y_pos);
            HPDF_Page_ShowText(page, line);
            HPDF_Page_EndText(page);
            y_pos -= 15;
            
            line = strtok(NULL, "\n");
        }
        
        g_free(content);
        y_pos -= 10;
    }
    
    // Save PDF
    HPDF_SaveToFile(pdf, output_path);
    HPDF_Free(pdf);
    
    return TRUE;
}
