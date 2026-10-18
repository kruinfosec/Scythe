#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <stdbool.h>
#include <time.h>
#include <glib.h>

// Database Initialization
bool db_init(void);
void db_close(void);

// Session Management
typedef struct {
    int id;
    char *name;
    char *start_time;
} SavedSessionInfo;

int session_create(const char *name);
bool session_end(int session_id);
bool session_save(int session_id, const char *name);
int session_get_current(void);
void session_set_current(int session_id);
GPtrArray* session_list_saved(void);
char* session_get_name(int session_id);
bool session_rename(int session_id, const char *name);
bool session_delete(int session_id);
int session_note_count(int session_id);

// Terminal Tracking
int terminal_register(int session_id, int parent_id, const char *pty_name);

// Logging
bool log_command(int terminal_id, const char *command, const char *output, const char *cwd, int exit_code);
bool log_chat_message(int session_id, const char *role, const char *content);

// Retrieval (Basic)
// More advanced retrieval will be added for RAG later
char* get_session_summary(int session_id);

// Notes Management
int note_create(int session_id, const char *title);
bool note_update_content(int note_id, const char *content);
bool note_rename(int note_id, const char *title);
bool note_delete(int note_id);
char* note_get_content(int note_id);
GPtrArray* note_list_by_session(int session_id); // Returns array of char* (allocated title strings) or a struct. We'll return just titles for now or IDs. Better: a struct containing id and title. We'll define a simple struct or use parallel arrays. For simplicity, let's just return a list of ids and the caller can query titles, or we return a formatted string. Actually, returning a GPtrArray of custom struct is standard, but let's use a simpler approach: 
// struct NoteInfo { int id; char *title; };
// For now, let's declare a struct here.

typedef struct {
    int id;
    char *title;
} NoteInfo;

GPtrArray* note_list_by_session(int session_id); // Returns array of NoteInfo*
bool note_save_image(int note_id, const char *filename, const void *data, int size);
bool note_get_image(int note_id, const char *filename, void **out_data, int *out_size);
bool note_update_image_metadata(int note_id, const char *filename, int width, const char *alignment);
bool note_get_image_metadata(int note_id, const char *filename, int *out_width, char **out_alignment);

#endif // DB_H
