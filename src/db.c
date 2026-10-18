#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static sqlite3 *db = NULL;
static int current_session_id = -1;

// Helper to execute SQL without results
static bool exec_sql(const char *sql) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool db_init(void) {
    // Ensure .scythe directory exists
    const char *home = getenv("HOME");
    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/.scythe", home);
    mkdir(db_path, 0755);
    strncat(db_path, "/scythe.db", sizeof(db_path) - strlen(db_path) - 1);

    int rc = sqlite3_open(db_path, &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return false;
    }

    // Create Tables
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "start_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "end_time DATETIME,"
        "is_saved BOOLEAN DEFAULT 0,"
        "export_path TEXT"
        ");"
        
        "CREATE TABLE IF NOT EXISTS terminals ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "session_id INTEGER,"
        "parent_id INTEGER,"
        "pty_name TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(session_id) REFERENCES sessions(id)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS commands ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "terminal_id INTEGER,"
        "command TEXT,"
        "output TEXT,"
        "cwd TEXT,"
        "exit_code INTEGER,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(terminal_id) REFERENCES terminals(id)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS chat_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "session_id INTEGER,"
        "role TEXT,"
        "content TEXT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(session_id) REFERENCES sessions(id)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "session_id INTEGER NOT NULL,"
        "title TEXT NOT NULL DEFAULT 'Untitled Note',"
        "content TEXT DEFAULT '',"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "modified_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(session_id) REFERENCES sessions(id)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS note_images ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "note_id INTEGER NOT NULL,"
        "filename TEXT NOT NULL,"
        "image_data BLOB NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(note_id) REFERENCES notes(id) ON DELETE CASCADE"
        ");";

    bool ok = exec_sql(schema);
    if (ok) {
        exec_sql("ALTER TABLE note_images ADD COLUMN width INTEGER DEFAULT 450;");
        exec_sql("ALTER TABLE note_images ADD COLUMN alignment TEXT DEFAULT 'center';");
    }
    return ok;
}

void db_close(void) {
    if (db) {
        if (current_session_id != -1) {
            session_end(current_session_id);
        }
        sqlite3_close(db);
        db = NULL;
    }
}

int session_create(const char *name) {
    if (!db) return -1;
    
    char *sql = sqlite3_mprintf("INSERT INTO sessions (name) VALUES ('%q');", name ? name : "Untitled Session");
    if (exec_sql(sql)) {
        current_session_id = (int)sqlite3_last_insert_rowid(db);
        sqlite3_free(sql);
        return current_session_id;
    }
    sqlite3_free(sql);
    return -1;
}

bool session_end(int session_id) {
    if (!db) return false;
    char *sql = sqlite3_mprintf("UPDATE sessions SET end_time = CURRENT_TIMESTAMP WHERE id = %d;", session_id);
    bool result = exec_sql(sql);
    sqlite3_free(sql);
    return result;
}

bool session_save(int session_id, const char *name) {
    if (!db) return false;
    char *sql = sqlite3_mprintf("UPDATE sessions SET is_saved = 1, name = '%q' WHERE id = %d;", name, session_id);
    bool result = exec_sql(sql);
    sqlite3_free(sql);
    return result;
}

int session_get_current(void) {
    return current_session_id;
}

void session_set_current(int session_id) {
    current_session_id = session_id;
}

/* PERF-001 FIX: Custom free function to properly free SavedSessionInfo members */
static void free_saved_session_info(gpointer data) {
    SavedSessionInfo *info = (SavedSessionInfo *)data;
    if (info) {
        g_free(info->name);
        g_free(info->start_time);
        g_free(info);
    }
}

GPtrArray* session_list_saved(void) {
    if (!db) return NULL;

    GPtrArray *list = g_ptr_array_new_with_free_func(free_saved_session_info);

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, start_time FROM sessions WHERE is_saved = 1 ORDER BY id DESC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SavedSessionInfo *info = g_malloc(sizeof(SavedSessionInfo));
        info->id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *time_str = (const char *)sqlite3_column_text(stmt, 2);
        info->name = g_strdup(name ? name : "Untitled Session");
        info->start_time = g_strdup(time_str ? time_str : "");
        g_ptr_array_add(list, info);
    }

    sqlite3_finalize(stmt);
    return list;
}

char* session_get_name(int session_id) {
    if (!db) return NULL;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM sessions WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, session_id);
    
    char *name = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *t = (const char *)sqlite3_column_text(stmt, 0);
        if (t) name = g_strdup(t);
    }
    sqlite3_finalize(stmt);
    return name;
}

bool session_rename(int session_id, const char *name) {
    if (!db || !name) return false;
    char *sql = sqlite3_mprintf("UPDATE sessions SET name = '%q' WHERE id = %d;", name, session_id);
    bool result = exec_sql(sql);
    sqlite3_free(sql);
    return result;
}

bool session_delete(int session_id) {
    if (!db) return false;

    // Cascade delete all related data
    char *sql;

    // 1. Delete note_images for notes in this session
    sql = sqlite3_mprintf("DELETE FROM note_images WHERE note_id IN (SELECT id FROM notes WHERE session_id = %d);", session_id);
    exec_sql(sql); sqlite3_free(sql);

    // 2. Delete notes
    sql = sqlite3_mprintf("DELETE FROM notes WHERE session_id = %d;", session_id);
    exec_sql(sql); sqlite3_free(sql);

    // 3. Delete chat_history
    sql = sqlite3_mprintf("DELETE FROM chat_history WHERE session_id = %d;", session_id);
    exec_sql(sql); sqlite3_free(sql);

    // 4. Delete commands for terminals in this session
    sql = sqlite3_mprintf("DELETE FROM commands WHERE terminal_id IN (SELECT id FROM terminals WHERE session_id = %d);", session_id);
    exec_sql(sql); sqlite3_free(sql);

    // 5. Delete terminals
    sql = sqlite3_mprintf("DELETE FROM terminals WHERE session_id = %d;", session_id);
    exec_sql(sql); sqlite3_free(sql);

    // 6. Delete the session itself
    sql = sqlite3_mprintf("DELETE FROM sessions WHERE id = %d;", session_id);
    bool result = exec_sql(sql);
    sqlite3_free(sql);

    // If we deleted the current session, reset it
    if (session_id == current_session_id) {
        current_session_id = -1;
    }

    return result;
}

int session_note_count(int session_id) {
    if (!db) return 0;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM notes WHERE session_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, session_id);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int terminal_register(int session_id, int parent_id, const char *pty_name) {
    if (!db) return -1;
    
    char *sql = sqlite3_mprintf(
        "INSERT INTO terminals (session_id, parent_id, pty_name) VALUES (%d, %d, '%q');",
        session_id, parent_id, pty_name
    );
    
    if (exec_sql(sql)) {
        int id = (int)sqlite3_last_insert_rowid(db);
        sqlite3_free(sql);
        return id;
    }
    sqlite3_free(sql);
    return -1;
}

bool log_command(int terminal_id, const char *command, const char *output, const char *cwd, int exit_code) {
    if (!db) return false;
    
    // Prepare statement to handle potentially large output safely
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO commands (terminal_id, command, output, cwd, exit_code) VALUES (?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, terminal_id);
    sqlite3_bind_text(stmt, 2, command, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, output, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, cwd, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, exit_code);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool log_chat_message(int session_id, const char *role, const char *content) {
    if (!db) return false;
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO chat_history (session_id, role, content) VALUES (?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, session_id);
    sqlite3_bind_text(stmt, 2, role, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

char* get_session_summary(int session_id) {
    // Placeholder - implemented if needed
    return g_strdup("Session summary not implemented yet.");
}

// --- Notes Management ---

int note_create(int session_id, const char *title) {
    if (!db) return -1;
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO notes (session_id, title) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, session_id);
    sqlite3_bind_text(stmt, 2, title ? title : "Untitled Note", -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        return (int)sqlite3_last_insert_rowid(db);
    }
    return -1;
}

bool note_update_content(int note_id, const char *content) {
    if (!db) return false;
    
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE notes SET content = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, content ? content : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, note_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool note_rename(int note_id, const char *title) {
    if (!db) return false;
    
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE notes SET title = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, note_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool note_delete(int note_id) {
    if (!db) return false;
    char *sql = sqlite3_mprintf("DELETE FROM notes WHERE id = %d;", note_id);
    bool res = exec_sql(sql);
    sqlite3_free(sql);
    return res;
}

char* note_get_content(int note_id) {
    if (!db) return NULL;
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT content FROM notes WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    
    sqlite3_bind_int(stmt, 1, note_id);
    
    char *content = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *db_content = (const char *)sqlite3_column_text(stmt, 0);
        content = db_content ? g_strdup(db_content) : g_strdup("");
    }
    
    sqlite3_finalize(stmt);
    return content;
}

/* PERF-002 FIX: Custom free function to properly free NoteInfo members */
static void free_note_info(gpointer data) {
    NoteInfo *info = (NoteInfo *)data;
    if (info) {
        g_free(info->title);
        g_free(info);
    }
}

GPtrArray* note_list_by_session(int session_id) {
    if (!db) return NULL;

    GPtrArray *notes = g_ptr_array_new_with_free_func(free_note_info);

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, title FROM notes WHERE session_id = ? ORDER BY modified_at DESC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return notes;

    sqlite3_bind_int(stmt, 1, session_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NoteInfo *info = g_malloc(sizeof(NoteInfo));
        info->id = sqlite3_column_int(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);
        info->title = g_strdup(title ? title : "Untitled Note");
        g_ptr_array_add(notes, info);
    }

    sqlite3_finalize(stmt);
    return notes;
}

bool note_save_image(int note_id, const char *filename, const void *data, int size) {
    if (!db || !data || size <= 0) return false;
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO note_images (note_id, filename, image_data) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, note_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 3, data, size, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool note_get_image(int note_id, const char *filename, void **out_data, int *out_size) {
    if (!db || !filename || !out_data || !out_size) return false;
    *out_data = NULL;
    *out_size = 0;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT image_data FROM note_images WHERE note_id = ? AND filename = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, note_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int size = sqlite3_column_bytes(stmt, 0);
        if (blob && size > 0) {
            *out_data = g_malloc(size);
            memcpy(*out_data, blob, size);
            *out_size = size;
            success = true;
        }
    }
    sqlite3_finalize(stmt);
    return success;
}

bool note_update_image_metadata(int note_id, const char *filename, int width, const char *alignment) {
    if (!db || !filename) return false;
    if (width < 80) width = 80;
    if (width > 2000) width = 2000;
    const char *align = alignment ? alignment : "center";

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE note_images SET width = ?, alignment = ? WHERE note_id = ? AND filename = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, width);
    sqlite3_bind_text(stmt, 2, align, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, note_id);
    sqlite3_bind_text(stmt, 4, filename, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool note_get_image_metadata(int note_id, const char *filename, int *out_width, char **out_alignment) {
    if (!db || !filename) return false;
    if (out_width) *out_width = 450;
    if (out_alignment) *out_alignment = g_strdup("center");

    sqlite3_stmt *stmt;
    const char *sql = "SELECT width, alignment FROM note_images WHERE note_id = ? AND filename = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, note_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int w = sqlite3_column_int(stmt, 0);
        const char *a = (const char *)sqlite3_column_text(stmt, 1);
        if (out_width && w > 0) *out_width = w;
        if (out_alignment && a) {
            if (*out_alignment) g_free(*out_alignment);
            *out_alignment = g_strdup(a);
        }
        success = true;
    }
    sqlite3_finalize(stmt);
    return success;
}
