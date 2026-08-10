#ifndef CONVERSATION_H
#define CONVERSATION_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <time.h>

// Message structure
typedef struct {
    char role[16];              // "user" or "assistant"
    char *content;              // Message text (dynamically allocated)
    time_t timestamp;           // When the message was sent
} Message;

// Conversation structure
typedef struct {
    char session_id[37];        // UUID (36 chars + null terminator)
    int db_session_id;          // Linked Scythe DB Session ID (-1 if unassigned)
    char *title;                // User-editable title
    time_t created;             // Creation timestamp
    time_t modified;            // Last modified timestamp
    gboolean is_temporary;      // If true, don't save to disk
    gboolean auto_titled;     // TRUE if title was auto-generated, FALSE if manually set
    char model[64];             // AI model used
    GPtrArray *messages;        // Array of Message structs
} Conversation;

// Conversation management functions
Conversation *conversation_new(const char *title, const char *model, gboolean is_temporary);
void conversation_free(Conversation *conv);
gboolean conversation_save(Conversation *conv);
Conversation *conversation_load(const char *session_id);
GPtrArray *conversation_list_all(void);
GPtrArray *conversation_list_by_db_session(int db_session_id);
gboolean conversation_delete(const char *session_id);
gboolean conversation_rename(const char *session_id, const char *new_title);

// Message functions
void conversation_add_message(Conversation *conv, const char *role, const char *content);
void conversation_clear_messages(Conversation *conv);

// Export functions
gboolean conversation_export_pdf(Conversation *conv, const char *output_path);
gboolean conversation_export_markdown(Conversation *conv, const char *output_path);

// Utility functions
const char *conversation_get_storage_dir(void);
void conversation_ensure_storage_dir(void);
char *conversation_generate_uuid(void);

#endif // CONVERSATION_H
