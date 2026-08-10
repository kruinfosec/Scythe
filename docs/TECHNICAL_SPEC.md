# Scythe Technical Implementation Specification

## Purpose
This document provides implementation-level technical details for developers working on or integrating with Scythe. It complements [ARCHITECTURE.md](ARCHITECTURE.md) with code-level specifications.

## Table of Contents
- [SSE Stream Processing](#sse-stream-processing)
- [Database Implementation](#database-implementation)
- [Memory Management Patterns](#memory-management-patterns)
- [Error Handling Strategies](#error-handling-strategies)
- [Performance Tuning](#performance-tuning)

## SSE Stream Processing

### Protocol Specification
Scythe implements Server-Sent Events (SSE) parsing for real-time AI response streaming.

**Groq API SSE Format**:
```
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"}}]}

data: {"id":"chatcmpl-123","choices":[{"delta":{"content":" there"}}]}

data: [DONE]

```

**Ollama API Format** (alternative):
```json
{"model":"llama3","response":"Hello","done":false}
{"model":"llama3","response":" there","done":false}
{"model":"llama3","response":"","done":true}
```

### Implementation Details

#### Read Buffer Configuration
```c
// From ai.c, line 106
g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, NULL, ai_stream_read_callback, stream_data);
```
- **Buffer Size**: 4096 bytes per read operation
- **Rationale**: Balances latency (smaller = more responsive) and throughput (larger = fewer syscalls)
- **Priority**: `G_PRIORITY_DEFAULT` (0) for normal event loop scheduling

#### Streaming State Management
```c
typedef struct {
    GtkWidget *webview;           // Target WebView for display
    GString *buffer;              // Line buffer for SSE parsing
    GString *response_text;       // Accumulated response for conversation storage
} AiStreamData;
```

**Buffer Management Algorithm**:
1. Append incoming chunk to `buffer`
2. Process complete lines (scan for `\n`)
3. Parse each line for SSE `data:` prefix
4. Extract JSON and accumulate to `response_text`
5. Update WebView incrementally
6. Keep partial line in `buffer` for next chunk

#### JSON Parsing
```c
// Groq format extraction (ai.c, lines 43-64)
JsonObject *obj = json_node_get_object(root);
if (json_object_has_member(obj, "choices")) {
    JsonArray *choices = json_object_get_array_member(obj, "choices");
    JsonObject *choice = json_array_get_object_element(choices, 0);
    JsonObject *delta = json_object_get_object_member(choice, "delta");
    const char *content = json_object_get_string_member(delta, "content");
    // Append...
}
```

**Path**: `root.choices[0].delta.content`

**Error Tolerance**: 
- Missing fields → skip chunk (no error display)
- Invalid JSON → logged to stderr, stream continues
- `[DONE]` marker → graceful stream termination

### Conversation Context Management
```c
// From ai.c, lines 268-284
// Last 10 messages for context window
guint start_idx = 0;
if (current_conversation->messages->len > 10) {
    start_idx = current_conversation->messages->len - 10;
}
```

**Context Window**: 10 messages (5 exchanges) to stay within token limits

**Token Budget Estimate**:
- System prompt: ~500 tokens
- 10 messages @ ~200 tokens avg = ~2000 tokens
- User query: ~100 tokens
- **Total input**: ~2600 tokens
- **Groq limit**: 8192 tokens (llama3-70b-8192)
- **Safety margin**: 3x for response

---

## Database Implementation

### Schema with Constraints
```sql
-- From db.c, lines 38-75
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    start_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    end_time DATETIME,
    is_saved BOOLEAN DEFAULT 0,
    export_path TEXT
);

CREATE TABLE terminals (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER,
    parent_id INTEGER,                    -- NULL for root terminals
    pty_name TEXT,                        -- e.g., '/dev/pts/3'
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(session_id) REFERENCES sessions(id)
);

CREATE TABLE commands (
    id INTEGER PRIMARY KEY AUT OINCREMENT,
    terminal_id INTEGER,
    command TEXT,
    output TEXT,                          -- Reserved (shell integration)
    cwd TEXT,                             -- Reserved (OSC 133)
    exit_code INTEGER,                    -- Reserved
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(terminal_id) REFERENCES terminals(id)
);

CREATE TABLE chat_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER,
    role TEXT,                            -- 'user' | 'assistant'
    content TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(session_id) REFERENCES sessions(id)
);
```

### Prepared Statements Pattern
```c
// From db.c, lines 144-158
sqlite3_stmt *stmt;
const char *sql = "INSERT INTO commands (terminal_id, command, output, cwd, exit_code) VALUES (?, ?, ?, ?, ?);";
    
sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
sqlite3_bind_int(stmt, 1, terminal_id);
sqlite3_bind_text(stmt, 2, command, -1, SQLITE_STATIC);      // SQLITE_STATIC = caller manages memory
sqlite3_bind_text(stmt, 3, output, -1, SQLITE_STATIC);
sqlite3_bind_text(stmt, 4, cwd, -1, SQLITE_STATIC);
sqlite3_bind_int(stmt, 5, exit_code);
    
int rc = sqlite3_step(stmt);
sqlite3_finalize(stmt);
```

**Binding Flags**:
- `SQLITE_STATIC`: Data remains valid for query duration
- `SQLITE_TRANSIENT`: SQLite copies data immediately (safe for temporary buffers)

**Performance Impact**:
- `STATIC`: Zero-copy, but requires caller to manage lifetime
- `TRANSIENT`: Memory copy overhead (~10% slower for large TEXT)

### File Location
```c
// From db.c, lines 24-29
const char *home = getenv("HOME");
char db_path[1024];
snprintf(db_path, sizeof(db_path), "%s/.scythe", home);
mkdir(db_path, 0755);
strncat(db_path, "/scythe.db", sizeof(db_path) - strlen(db_path) - 1);
```

**Path**: `~/.scythe/scythe.db`

**Permissions**: 0755 (rwxr-xr-x) on directory

### Connection Lifecycle
```c
static sqlite3 *db = NULL;              // Global singleton

db_init()  → sqlite3_open()             // Application startup
    ↓
... many operations ...
    ↓
db_close() → sqlite3_close()            // Application shutdown
```

**Thread Safety**: Single connection, all calls on main thread (GTK event loop)

---

## UI & Theme Architecture

### Styling Engine
Scythe uses a dynamic CSS generation system to apply user-customizable themes at runtime.

#### CSS Generation Strategy
```c
// From config.c, apply_ui_theme()
char *css = g_strdup_printf(
    "window, notebook... { background-color: transparent; }" // Transparent container
    "headerbar { background-color: %s; }"                   // Opaque chrome
    ".ai-panel { background-color: %s; }",                  // Opaque AI panel
    ui_bg, ai_bg
);
```
- **Transparency Handling**: The root `window` and `notebook` containers are set to `transparent` to allow the VTE terminal's alpha channel to render against the desktop wallpaper. Opaque UI elements (Headers, AI Panel) are explicitly painted with the theme background color.
- **Variable Injection**: User-selected colors (GdkRGBA) are converted to strings (`gdk_rgba_to_string`) and injected into a GTK3 CSS template.

### AI Panel Integration

#### HTML Template Injection
To prevent "white flash" on startup, the WebView's HTML template is pre-processed before loading.

```c
// From webview_ai.c, create_ai_webview()
char *css_overrides = g_strdup_printf(
    ":root { --bg-color: %s; --text-color: %s; ... }",
    bg_hex, text_hex
);
char *final_html = str_replace(HTML_TEMPLATE, "</head>", css_overrides);
```
- **Pre-injection**: CSS variables are injected directly into the `<head>` of the HTML string.
- **No JS Delay**: This avoids the need for `evaluate_javascript` calls after `load-finished`, ensuring the WebView renders the correct dark theme immediately upon initialization.

### Performance Optimizations

#### Opacity Slider Logic
```c
// From config.c, on_transparency_changed()
current_settings.background_color.alpha = value / 100.0;
vte_terminal_set_color_background(terminal, ...);
// Optimization: Do NOT call full apply_settings() or save_settings() here
```
- **Lag Prevention**: dragging the opacity slider only updates the VTE background color memory.
- **Deferred Persistence**: `save_settings()` (JSON serialization and disk write) is only called on application exit (`main.c`), preventing I/O blocking during UI interaction.

## Memory Management Patterns

### GString Usage
```c
// Buffer for SSE line parsing
GString *buffer = g_string_new("");

// Append data
g_string_append_len(buffer, data, size);

// Erase processed data
g_string_erase(buffer, 0, bytes_processed);

// Free
g_string_free(buffer, TRUE);            // TRUE = free character data too
```

**Amortized Allocation**: GString uses exponential growth (2x) to minimize reallocs

**Typical Growth**: 0 → 16 → 32 → 64 → 128 → 256 → 512 → 1024 bytes

### Heap Allocations
```c
// From ai.c, line 159
AiStreamData *stream_data = g_new0(AiStreamData, 1);
// Equivalent to: calloc(1, sizeof(AiStreamData))

// ... use ...

g_free(stream_data);
```

**`g_new0` vs `g_new`**:
- `g_new0`: Zero-initialized (safe for pointers)
- `g_new`: Uninitialized (faster,but requires manual init)

### String Duplication
```c
char *system_prompt = get_system_prompt();    // Returns allocated string
// ... use system_prompt ...
g_free(system_prompt);                         // Caller frees
```

**Ownership Convention**: Functions returning `char*` transfer ownership unless documented with `(transfer none)` in GObject Introspection annotations.

---

## Error Handling Strategies

### Three-Tier Error Model

#### 1. Critical Errors (Application Failure)
```c
if (!db_init()) {
    g_critical("Failed to initialize database");
    exit(EXIT_FAILURE);
}
```
**Behavior**: Log and terminate

#### 2. Warnings (Degraded Functionality)
```c
if (error) {
    g_warning("Error reading stream: %s", error->message);
    g_error_free(error);
}
```
**Behavior**: Log, cleanup, continue

#### 3. Silent Failures (Non-Critical)
```c
if (!json_parser_load_from_data(parser, json_str, -1, NULL)) {
    // Skip chunk, continue stream
    g_object_unref(parser);
    continue;
}
```
**Behavior**: No log, graceful degradation

### GError Pattern
```c
GError *error = NULL;
if (!g_file_get_contents("Modelfile", &content, &length, &error)) {
    g_print("DEBUG: Failed to read Modelfile: %s\n", error->message);
    g_error_free(error);           // Required: prevents leak
    // ... fallback logic ...
}
```

**Contract**: If function returns error via `GError**`, caller MUST free with `g_error_free()`.

---

## Performance Tuning

### Measured Latencies

| Operation | Typical Latency | Code Location |
|-----------|-----------------|---------------|
| Terminal spawn | 50-100ms | terminal.c: `create_terminal_widget()` |
| SQLite INSERT | \u003c1ms | db.c: `log_command()` |
| AI first token | 200-500ms | ai.c: SSE stream callback |
| Mermaid render | 50-200ms | webview_ai.c: JavaScript mermaid.run() |
| JSON parse (small) | \u003c1ms | ai.c: `json_parser_load_from_data()` |
| WebView script inject | 5-10ms | webkit: `evaluate_javascript()` |

### Bottleneck Analysis

**AI Response Latency Breakdown**:
1. HTTP handshake: 50-100ms (TLS negotiation)
2. API processing: 100-300ms (model inference queue)
3. First token: 100-200ms (model forward pass)
4. Subsequent tokens: 20-50ms each (cached KV)
5. Network transfer: 10-20ms per chunk

**Total to first render**: 300-700ms

### Optimization Opportunities

#### 1. Connection Pooling
```c
// Current: New session per request
SoupSession *session = soup_session_new();
soup_session_send_async(session, msg, ...);
g_object_unref(session);

// Optimized: Reuse persistent session
static SoupSession *global_session = NULL;
if (!global_session) {
    global_session = soup_session_new();
}
soup_session_send_async(global_session, msg, ...);
```

**Benefit**: Eliminates TLS handshake (~50ms) on subsequent requests

#### 2. Prepared Statement Caching
```c
// Current: Prepare on every call
sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
// ... bind, step, finalize ...

// Optimized: Prepare once, reuse
static sqlite3_stmt *log_cmd_stmt = NULL;
if (!log_cmd_stmt) {
    sqlite3_prepare_v2(db, "INSERT ...", -1, &log_cmd_stmt, NULL);
}
sqlite3_reset(log_cmd_stmt);
// ... bind, step (no finalize) ...
```

**Benefit**: ~20% faster INSERTs (eliminates SQL parsing overhead)

#### 3. Batch Inserts with Transactions
```c
// Current: Auto-commit per INSERT
log_command(...);  // Implicit transaction

// Optimized: Explicit transaction for batch
sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
for (int i = 0; i \u003c count; i++) {
    log_command(...);
}
sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
```

**Benefit**: 10-100x faster for large batches (single fsync)

---

**Last Updated**: 2025-12-30  
**Document Version**: 1.0  
**Companion To**: ARCHITECTURE.md, API_REFERENCE.md
