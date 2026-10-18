# Scythe Application - Comprehensive Security & Code Audit Report

**Audit Date:** September 2, 2026  
**Application:** Scythe v2.5  
**Codebase Size:** 11,758 lines of C code  
**Auditor:** Code Review System  
**Audit Scope:** Complete codebase security, functionality, performance, and architecture review

---

## Executive Summary

### Overall Health Assessment
The Scythe application demonstrates a functional GTK-based terminal emulator with AI integration, but contains **multiple critical and high-severity issues** that require immediate attention. The audit identified:

- **1 Critical severity** security vulnerability (Command Injection)
- **1 Critical severity** functional bug (Image Duplication via Buffer Change Loop)
- **2 High severity** memory leaks
- **3 Medium severity** security/reliability issues
- **5 Low severity** code quality concerns

### Key Findings Summary
| Severity | Count | Primary Categories |
|----------|-------|-------------------|
| Critical | 2 | Security (Command Injection), Functional (Image Duplication) |
| High | 2 | Performance (Memory Leaks in db.c) |
| Medium | 3 | Security (XSS potential), Reliability (Error handling) |
| Low | 5 | Code Quality, Architecture |

### Known Issue Status
✅ **CONFIRMED:** The reported image duplication issue after resize/alignment button clicks has been **identified and root cause determined**. The bug is caused by an infinite rendering loop triggered by buffer modification during rendering callbacks.

---

## Part 1: Known Issue Analysis - Image Duplication Bug

### Finding ID: CRITICAL-001
**Title:** Image Duplication via Infinite Buffer-Change Rendering Loop  
**Severity:** Critical  
**Confidence:** Confirmed  
**Category:** Functional / Undo-Redo

### Root Cause Analysis

The image duplication bug occurs due to a **cascading buffer-change event loop** when users interact with image resize or alignment buttons:

#### The Bug Flow:

1. **User clicks resize/alignment button** → `on_preset_btn_clicked()` or `on_align_btn_clicked()` (lines 858-887, `src/notes.c`)

2. **Button handler calls** → `save_image_metadata_canonical(data)` (line 802)

3. **Metadata save calls** → `update_image_tag_in_markdown(filename, width, alignment)` (line 711)

4. **Tag update performs buffer operations:**
   ```c
   gtk_text_buffer_delete(buffer, &tag_open, &tag_close);  // Line 778
   gtk_text_buffer_insert(buffer, &tag_open, new_img_tag, -1);  // Line 779
   ```

5. **Buffer modification fires** → `"changed"` signal (connected at line 3229)

6. **Signal handler** → `on_buffer_changed_render(buffer, ...)` (line 2490)

7. **Render scheduling** → `schedule_render(buffer)` creates idle callback

8. **Idle callback executes** → `render_idle_cb()` (line 639) calls `parse_and_render_images_in_buffer(buffer)` (line 648)

9. **Image parser scans buffer** → Finds `scythe://note_image/` tags (line 1476)

10. **For each image tag found:**
    ```c
    if (!gtk_text_iter_get_child_anchor(&tag_anchor_pos)) {
        GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, &tag_anchor_pos);
        GtkWidget *widget = create_resizable_image_widget(pixbuf, clean_fn, req_w, req_align);
        gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(notes_textview), widget, anchor);
    }
    ```
    **Lines 1533-1538**

11. **THE CRITICAL BUG:**  
    `gtk_text_buffer_create_child_anchor()` **MODIFIES THE BUFFER**, which **FIRES THE "changed" SIGNAL AGAIN**, triggering `on_buffer_changed_render()` → `schedule_render()` → another idle callback → `parse_and_render_images_in_buffer()` → finds the SAME image tag AGAIN → creates ANOTHER child anchor and widget → INFINITE LOOP until crash or visible duplication.

### Why This Affects Resize/Alignment Buttons Specifically

- Initial image insertion (line 1050-1075) only inserts **text** without creating anchors, so no loop occurs
- **But** when user clicks resize/alignment buttons:
  1. `update_image_tag_in_markdown()` **deletes and re-inserts** the `<img>` tag
  2. This re-insertion fires `"changed"` → schedules rendering
  3. Rendering sees the tag and creates a **new** anchor (even though one already exists from the original insertion)
  4. Anchor creation fires `"changed"` again → **loop begins**

### Why The Check `if (!gtk_text_iter_get_child_anchor(&tag_anchor_pos))` Fails

The check at line 1533 **should** prevent duplicate anchors, but:
- After `gtk_text_buffer_delete(buffer, &tag_open, &tag_close)` in `update_image_tag_in_markdown()`, the **old anchor is destroyed**
- `gtk_text_buffer_insert(buffer, &tag_open, new_img_tag, -1)` inserts **plain text**
- So when `parse_and_render_images_in_buffer()` runs, `gtk_text_iter_get_child_anchor(&tag_anchor_pos)` returns `NULL` (no anchor exists yet)
- A new anchor is created, triggering another render cycle

### Affected Code Paths

| Function | File | Lines | Issue |
|----------|------|-------|-------|
| `update_image_tag_in_markdown` | src/notes.c | 711-783 | Triggers buffer modification → rendering loop |
| `on_buffer_changed_render` | src/notes.c | 2490-2498 | Fires on EVERY buffer change (including anchor creation) |
| `parse_and_render_images_in_buffer` | src/notes.c | 1469-1552 | Creates anchors that trigger buffer "changed" signal |
| `on_preset_btn_clicked` | src/notes.c | 858-873 | Entry point for resize button |
| `on_align_btn_clicked` | src/notes.c | 876-888 | Entry point for alignment button |

### Reproduction Steps

1. Insert image via Snip tool (`Ctrl+Alt+S`) or Image button
2. Click on the image to activate toolbar
3. Click any **Size button** (S/M/L/Full) **multiple times**
4. **OR** Click any **Alignment button** (Left/Center/Right) **multiple times**
5. Observe duplicate image widgets appearing in the editor

### Impact

- **Data Corruption:** Multiple duplicate image widgets clutter the note
- **Performance Degradation:** Each duplicate widget consumes memory and rendering resources
- **User Experience:** Confusing and frustrating for users
- **Undo/Redo Instability:** Undo stack captures intermediate corrupted states

### Recommended Remediation

**Option 1: Block "changed" Signal During Metadata Updates (Simplest)**
```c
static void update_image_tag_in_markdown(const char *filename, int new_width, const char *alignment) {
    // ... existing code ...
    
    /* Block buffer change signal to prevent render loop */
    g_signal_handlers_block_by_func(buffer, G_CALLBACK(on_buffer_changed_render), NULL);
    
    gtk_text_buffer_delete(buffer, &tag_open, &tag_close);
    gtk_text_buffer_insert(buffer, &tag_open, new_img_tag, -1);
    
    g_signal_handlers_unblock_by_func(buffer, G_CALLBACK(on_buffer_changed_render), NULL);
    
    /* Manually trigger a single render after the atomic update */
    schedule_render(buffer);
    
    save_current_note();
}
```

**Option 2: Prevent Anchor Creation from Firing "changed" Signal**  
Investigate if GTK provides a way to create child anchors without emitting signals, or use a flag to skip rendering when anchor creation is in progress.

**Option 3: Architectural Fix - Separate Image State from Buffer**  
Store image metadata (width, alignment) separately and render from that state, avoiding buffer modifications during interactive updates.

### Regression Prevention

Add automated test:
```c
void test_image_resize_no_duplication() {
    // 1. Insert image
    // 2. Click resize button 5 times
    // 3. Assert only 1 image widget exists in buffer
    // 4. Assert buffer contains only 1 <img> tag
}
```

---

## Part 2: Security Audit Findings

### Finding ID: SEC-001
**Title:** Command Injection via ZIP Export Path  
**Severity:** Critical  
**Confidence:** Confirmed  
**Category:** Security - Injection

#### Affected Code
**File:** `src/notes.c`  
**Lines:** 2929-2932

```c
char *zip_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
// ... 
char zip_cmd[1024];
snprintf(zip_cmd, sizeof(zip_cmd), "cd '%s' && zip -j -q '%s' *", tmp_dir, zip_path);
int res = system(zip_cmd);
```

#### Vulnerability

User-controlled `zip_path` from file chooser dialog is inserted directly into a shell command executed via `system()`. Single quotes (`'`) are used for quoting, but no escaping is performed.

**Attack Scenario:**
1. User selects "Export Notes Archive"
2. In file chooser, user enters filename: `notes'; rm -rf ~; echo '.zip`
3. Constructed command becomes:
   ```bash
   cd '/tmp/scythe_export_XXXXXX' && zip -j -q '/path/notes'; rm -rf ~; echo '.zip' *
   ```
4. Shell executes: `cd /tmp/... && zip -j -q '/path/notes'` then `rm -rf ~` then `echo '.zip' *`
5. **User's home directory is deleted**

#### Impact
- **Arbitrary Command Execution** with user privileges
- **Data Loss** (e.g., `rm -rf ~`)
- **System Compromise** (e.g., download and execute malware)
- **Requires User Interaction:** User must manually type malicious filename in file chooser (medium barrier)

#### Remediation

**Option 1: Use GLib File Operations (Recommended)**
```c
// Replace system() call with GLib's g_spawn functions
gchar *argv[] = {"zip", "-j", "-q", zip_path, NULL};
GError *error = NULL;
g_spawn_sync(tmp_dir, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, &error);
if (error) {
    g_error_free(error);
}
```

**Option 2: Proper Shell Escaping**
```c
char *escaped_zip_path = g_shell_quote(zip_path);
char *escaped_tmp_dir = g_shell_quote(tmp_dir);
snprintf(zip_cmd, sizeof(zip_cmd), "cd %s && zip -j -q %s *", escaped_tmp_dir, escaped_zip_path);
int res = system(zip_cmd);
g_free(escaped_zip_path);
g_free(escaped_tmp_dir);
```

**Priority:** P0 - Immediate

---

### Finding ID: SEC-002
**Title:** Potential XSS in WebView via Insufficient Escaping  
**Severity:** Medium  
**Confidence:** High  
**Category:** Security - XSS

#### Affected Code
**File:** `src/webview_ai.c`  
**Lines:** 866-894

```c
// Escape single quotes in HTML content for JavaScript string
GString *escaped = g_string_new("");
for (const char *p = html_content; *p; p++) {
    if (*p == '\'') {
        g_string_append(escaped, "\\'");
    } else if (*p == '\n') {
        g_string_append(escaped, "\\n");
    } else if (*p == '\\') {
        g_string_append(escaped, "\\\\");
    } else {
        g_string_append_c(escaped, *p);
    }
}

gchar *script = g_strdup_printf(
    "var div = document.createElement('div');"
    "div.className = 'message %s';"
    "div.innerHTML = '<div class=\"role\">%s</div><div class=\"content\">%s</div>';"
    // ...
    role_class, role_label, escaped->str
);
```

#### Vulnerability

The escaping routine only handles single quotes (`'`), newlines, and backslashes for **JavaScript string literals**, but the escaped content is then assigned to `div.innerHTML`, which interprets **HTML**.

**Attack Scenario:**
If AI response or user input contains:
```
<img src=x onerror=alert(document.cookie)>
```

Flow:
1. Content passes through `markdown_to_html()` (may convert to `<p><img src=x onerror=alert(document.cookie)></p>`)
2. JavaScript string escaping only escapes `'`, `\n`, `\\`
3. `div.innerHTML = '...<img src=x onerror=alert(document.cookie)>...'` executes the payload

#### Impact
- **Cross-Site Scripting (XSS)** in WebView context
- **Requires Malicious AI Response or User Input** (low likelihood for Groq API, but possible if API is compromised or user pastes malicious content)
- WebView runs with restricted permissions (isolated storage), limiting impact
- **Mitigation:** WebKitGTK content security is generally sandboxed

#### Remediation

**Option 1: Use `textContent` Instead of `innerHTML`**
```javascript
div.className = 'message %s';
var roleDiv = document.createElement('div');
roleDiv.className = 'role';
roleDiv.textContent = '%s';
var contentDiv = document.createElement('div');
contentDiv.className = 'content';
contentDiv.innerHTML = '%s'; // markdown_to_html already produces safe HTML
div.appendChild(roleDiv);
div.appendChild(contentDiv);
```

**Option 2: HTML Entity Encoding**
Escape `<`, `>`, `&`, `"` in addition to `'`, `\n`, `\\` before inserting into JavaScript string that will be used with `innerHTML`.

**Priority:** P2 - Medium

---

### Finding ID: SEC-003
**Title:** API Key Exposure Risk in Settings File  
**Severity:** Low (Informational)  
**Confidence:** Confirmed  
**Category:** Security - Data Exposure

#### Affected Code
**File:** `src/config.c`  
**Lines:** 478-479

```c
json_builder_set_member_name(builder, "groq_api_key");
json_builder_add_string_value(builder, current_settings.groq_api_key);
```

Settings saved to: `~/.config/scythe/settings.json`

#### Vulnerability

Groq API key is stored in **plaintext** in `~/.config/scythe/settings.json`. While file permissions typically restrict access to the user, this exposes the key to:
- **Local privilege escalation attacks**
- **Malware with user-level access**
- **Accidental exposure** (e.g., dotfiles backup, config syncing)

#### Impact
- **API Key Compromise** if attacker gains read access to user's home directory
- **Financial Impact:** Unauthorized API usage charged to user's account

#### Remediation

**Option 1: Use OS Keyring (Recommended)**
```c
// Use libsecret to store API key in GNOME Keyring / KDE Wallet
#include <libsecret/secret.h>

const SecretSchema *schema = secret_schema_new(
    "com.scythe.APIKey", SECRET_SCHEMA_NONE,
    "api-provider", SECRET_SCHEMA_ATTRIBUTE_STRING,
    NULL
);

// Store
secret_password_store_sync(schema, SECRET_COLLECTION_DEFAULT,
    "Scythe Groq API Key", api_key, NULL, NULL,
    "api-provider", "groq", NULL);

// Retrieve
gchar *api_key = secret_password_lookup_sync(schema, NULL, NULL,
    "api-provider", "groq", NULL);
```

**Option 2: Encrypt Settings File**
Use GPG or libgcrypt to encrypt `settings.json` with a key derived from user's login session.

**Priority:** P3 - Low (industry-standard practice is plaintext config files with restricted permissions)

---

## Part 3: Performance Audit Findings

### Finding ID: PERF-001
**Title:** Memory Leak in `session_list_saved()` - SavedSessionInfo Struct Members Not Freed  
**Severity:** High  
**Confidence:** Confirmed  
**Category:** Performance - Memory Leak

#### Affected Code
**File:** `src/db.c`  
**Lines:** 151-172

```c
GPtrArray *list = g_ptr_array_new_with_free_func(g_free);

while (sqlite3_step(stmt) == SQLITE_ROW) {
    SavedSessionInfo *info = g_malloc(sizeof(SavedSessionInfo));
    info->id = sqlite3_column_int(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *time_str = (const char *)sqlite3_column_text(stmt, 2);
    info->name = g_strdup(name ? name : "Untitled Session");
    info->start_time = g_strdup(time_str ? time_str : "");
    g_ptr_array_add(list, info);
}
```

#### Vulnerability

`GPtrArray` is configured with `g_free` as the free function, which **only frees the struct pointer (`info`)**. The dynamically allocated strings `info->name` and `info->start_time` are **never freed**, causing a memory leak on every call to `session_list_saved()`.

#### Impact
- **Memory Leak:** Every call to `session_list_saved()` leaks `strlen(name) + strlen(time_str) + 2` bytes per session
- **Accumulation:** Leak grows with number of saved sessions and frequency of session list refreshes
- **Typical Scenario:** User loads session manager dialog multiple times → leak accumulates

**Estimated Leak Rate:** ~100-200 bytes per session per refresh. For 10 sessions refreshed 100 times: ~100 KB leaked.

#### Remediation

**Option 1: Custom Free Function**
```c
static void free_saved_session_info(gpointer data) {
    SavedSessionInfo *info = (SavedSessionInfo *)data;
    if (info) {
        g_free(info->name);
        g_free(info->start_time);
        g_free(info);
    }
}

GPtrArray *list = g_ptr_array_new_with_free_func(free_saved_session_info);
```

**Option 2: Manual Cleanup Before Free**
```c
// In calling code before g_ptr_array_free(list, TRUE):
for (guint i = 0; i < list->len; i++) {
    SavedSessionInfo *info = g_ptr_array_index(list, i);
    g_free(info->name);
    g_free(info->start_time);
}
g_ptr_array_free(list, TRUE);
```

**Priority:** P1 - High

---

### Finding ID: PERF-002
**Title:** Memory Leak in `note_list_by_session()` - NoteInfo->title Not Freed  
**Severity:** High  
**Confidence:** Confirmed  
**Category:** Performance - Memory Leak

#### Affected Code
**File:** `src/db.c`  
**Lines:** 390-411

```c
GPtrArray *notes = g_ptr_array_new_with_free_func(g_free);

while (sqlite3_step(stmt) == SQLITE_ROW) {
    NoteInfo *info = g_malloc(sizeof(NoteInfo));
    info->id = sqlite3_column_int(stmt, 0);
    const char *title = (const char *)sqlite3_column_text(stmt, 1);
    info->title = g_strdup(title ? title : "Untitled Note");
    g_ptr_array_add(notes, info);
}
```

#### Vulnerability

Identical issue to PERF-001. `g_ptr_array_new_with_free_func(g_free)` only frees the struct, not `info->title`.

#### Impact
- **Memory Leak:** Every call to `note_list_by_session()` leaks `strlen(title)` bytes per note
- **High Frequency:** Called on **every note switch, save, and refresh** (multiple times per session)
- **Typical Scenario:** User with 20 notes switches between them 50 times: ~20 KB leaked

**Estimated Leak Rate:** ~50-100 bytes per note per call. Much higher frequency than PERF-001.

#### Remediation

```c
static void free_note_info(gpointer data) {
    NoteInfo *info = (NoteInfo *)data;
    if (info) {
        g_free(info->title);
        g_free(info);
    }
}

GPtrArray *notes = g_ptr_array_new_with_free_func(free_note_info);
```

**Priority:** P1 - High

---

### Finding ID: PERF-003
**Title:** Excessive Rendering on Every Buffer Change  
**Severity:** Medium  
**Confidence:** High  
**Category:** Performance - Unnecessary Operations

#### Affected Code
**File:** `src/notes.c`  
**Lines:** 2490-2498, 639-656

```c
static void on_buffer_changed_render(GtkTextBuffer *buffer, gpointer user_data) {
    if (!buffer) return;
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);
    current_edit_line = gtk_text_iter_get_line(&iter);
    schedule_render(buffer);  // Schedules full markdown + image parsing
    schedule_undo_push(buffer);
}
```

#### Vulnerability

Every single keystroke triggers:
1. `schedule_render(buffer)` → idle callback
2. Idle callback executes `parse_and_render_images_in_buffer(buffer)` (scans entire buffer for images)
3. `apply_markdown_tags(buffer)` (re-applies all markdown formatting)

For a 1000-line note, this means scanning 1000 lines on **every character typed**.

#### Impact
- **Typing Latency:** Noticeable lag when typing in large notes
- **CPU Usage:** Continuous idle callbacks consume CPU
- **Battery Drain:** Excessive rendering on laptops

#### Remediation

**Option 1: Debounce Rendering**
```c
static guint render_debounce_timer_id = 0;

static gboolean debounced_render_cb(gpointer user_data) {
    render_debounce_timer_id = 0;
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    schedule_render(buffer);
    g_object_unref(buffer);
    return G_SOURCE_REMOVE;
}

static void on_buffer_changed_render(GtkTextBuffer *buffer, gpointer user_data) {
    // ... existing code ...
    
    // Debounce rendering: only schedule render after 200ms of inactivity
    if (render_debounce_timer_id != 0) {
        g_source_remove(render_debounce_timer_id);
    }
    render_debounce_timer_id = g_timeout_add(200, debounced_render_cb, g_object_ref(buffer));
    
    schedule_undo_push(buffer);
}
```

**Option 2: Incremental Rendering**
Only re-render the current line or changed region instead of the entire buffer.

**Priority:** P2 - Medium

---

## Part 4: Functional Audit Findings

### Finding ID: FUNC-001
**Title:** Undo Stack Pollution from Automatic Buffer Changes  
**Severity:** Medium  
**Confidence:** High  
**Category:** Functional - Undo/Redo

#### Affected Code
**File:** `src/notes.c`  
**Lines:** 2490-2498 (`on_buffer_changed_render`)

#### Issue

Every buffer change (including automatic changes like anchor creation, markdown tag application, and metadata updates) triggers `schedule_undo_push(buffer)`, polluting the undo stack with non-user changes.

**Scenario:**
1. User types "Hello"
2. Markdown parser runs → applies formatting → triggers `on_buffer_changed_render` → pushes state to undo
3. User presses Ctrl+Z expecting to undo typing "Hello"
4. Instead, undoes the automatic markdown formatting application

#### Impact
- **Confusing Undo Behavior:** Undo doesn't match user expectations
- **Undo Stack Bloat:** Multiple intermediate states for a single user action

#### Remediation

Only push undo states for **user-initiated changes**:
```c
static gboolean user_initiated_change = TRUE;

static void on_buffer_changed_render(GtkTextBuffer *buffer, gpointer user_data) {
    // ... existing code ...
    
    if (user_initiated_change) {
        schedule_undo_push(buffer);
    }
}

// In functions that make automatic changes:
static void apply_markdown_tags(GtkTextBuffer *buffer) {
    user_initiated_change = FALSE;
    // ... apply tags ...
    user_initiated_change = TRUE;
}
```

**Priority:** P2 - Medium

---

### Finding ID: FUNC-002
**Title:** No Error Handling for Image Load Failures  
**Severity:** Low  
**Confidence:** Confirmed  
**Category:** Functional - Error Handling

#### Affected Code
**File:** `src/notes.c`  
**Lines:** 1507-1519

```c
GdkPixbuf *pixbuf = NULL;
if (g_file_test(disk_path, G_FILE_TEST_EXISTS)) {
    pixbuf = gdk_pixbuf_new_from_file(disk_path, NULL);
} else if (current_note_id != -1) {
    void *db_data = NULL;
    int db_size = 0;
    if (note_get_image(current_note_id, clean_fn, &db_data, &db_size)) {
        GInputStream *stream = g_memory_input_stream_new_from_data(db_data, db_size, g_free);
        pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, NULL);
        g_object_unref(stream);
        gdk_pixbuf_save(pixbuf, disk_path, "png", NULL, NULL);
    }
}
```

#### Issue

If `gdk_pixbuf_new_from_file()` or `gdk_pixbuf_new_from_stream()` fails (corrupt image, unsupported format), `pixbuf` is `NULL`, but no error is shown to user. The `<img>` tag remains in the document but displays nothing.

#### Impact
- **Silent Failure:** User doesn't know why image isn't displaying
- **Confusion:** User may repeatedly try to insert the same image

#### Remediation

```c
GError *error = NULL;
pixbuf = gdk_pixbuf_new_from_file(disk_path, &error);
if (error) {
    g_warning("Failed to load image %s: %s", disk_path, error->message);
    // Insert placeholder or error message in note
    g_error_free(error);
}
```

**Priority:** P3 - Low

---

## Part 5: Architecture & Code Quality Findings

### Finding ID: ARCH-001
**Title:** Tight Coupling Between UI and Business Logic  
**Severity:** Low (Informational)  
**Confidence:** High  
**Category:** Architecture

#### Observation

Throughout the codebase, UI operations (GTK widget manipulation) are tightly coupled with business logic (database operations, state management). Example from `src/notes.c`:

```c
static void save_current_note(void) {
    // ... 30 lines mixing buffer extraction, database calls, and UI updates ...
}
```

#### Impact
- **Testability:** Difficult to unit test business logic without GTK infrastructure
- **Maintainability:** Changes to UI require changes to business logic and vice versa
- **Reusability:** Cannot reuse business logic in non-GTK contexts (e.g., CLI tools, tests)

#### Recommendation

Separate concerns into layers:
```c
// Business Logic Layer (pure C, no GTK)
bool notes_service_save(int note_id, const char *content);

// UI Layer
static void save_current_note(void) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(...);
    char *content = capture_clean_undo_state(buffer);
    
    if (!notes_service_save(current_note_id, content)) {
        show_error_dialog("Save failed");
    }
    g_free(content);
}
```

**Priority:** P3 - Technical Debt

---

### Finding ID: ARCH-002
**Title:** Global Mutable State  
**Severity:** Low (Informational)  
**Confidence:** High  
**Category:** Architecture

#### Observation

Extensive use of global static variables:
- `src/notes.c`: `notes_popover`, `notes_textview`, `notes_combo`, `current_note_id`, `undo_stack`, `redo_stack`, etc. (30+ globals)
- `src/db.c`: `db`, `current_session_id`
- `src/config.c`: `current_settings`

#### Impact
- **Thread Safety:** Cannot safely use from multiple threads (though GTK is single-threaded)
- **Testability:** Cannot easily create isolated test contexts
- **Multiple Windows:** Cannot open multiple Scythe windows with independent state

#### Recommendation

Encapsulate state in structs passed as context:
```c
typedef struct {
    GtkWidget *popover;
    GtkWidget *textview;
    int current_note_id;
    GPtrArray *undo_stack;
    // ...
} NotesContext;

void notes_init(NotesContext *ctx, GtkWidget *header_bar);
```

**Priority:** P3 - Technical Debt

---

### Finding ID: ARCH-003
**Title:** Inconsistent Error Handling  
**Severity:** Low  
**Confidence:** High  
**Category:** Code Quality

#### Observation

Mixed error handling patterns:
- Some functions return `bool` success/failure
- Some return `-1` for error, `>= 0` for success
- Some return `NULL` for error
- Some print to `stderr`, some use `g_warning()`, some use `fprintf()`
- Some show UI dialogs, some don't

#### Recommendation

Standardize error handling:
```c
typedef enum {
    SCYTHE_OK = 0,
    SCYTHE_ERR_INVALID_PARAM,
    SCYTHE_ERR_DB_ERROR,
    SCYTHE_ERR_NOT_FOUND,
    // ...
} ScytheError;

// Return error codes + output parameters
ScytheError notes_service_get(int note_id, char **out_content);
```

**Priority:** P3 - Technical Debt

---

## Part 6: Testing & Validation Summary

### Existing Test Suite
- **test_webview.c:** WebView rendering and Mermaid.js integration
- **No automated tests** for core functionality (terminals, notes, database, AI)
- **No regression tests** for fixed bugs
- **No security tests**

### Critical Missing Test Coverage
1. **Undo/Redo Operations:** Multi-step undo/redo sequences, undo after image operations
2. **Image Operations:** Insert, resize, align, delete, export
3. **Buffer State Management:** Concurrent modifications, signal handling
4. **Database Operations:** CRUD for sessions, notes, images
5. **Input Validation:** Malicious filenames, SQL injection attempts
6. **Memory Leaks:** Valgrind/AddressSanitizer runs on all operations
7. **Command Injection:** ZIP export with malicious filenames

### Recommended Test Infrastructure
```bash
# Unit Tests
tests/
  test_notes_core.c      # Notes business logic (no GTK)
  test_db.c              # Database operations
  test_markdown.c        # Markdown rendering
  
# Integration Tests
tests/
  test_notes_ui.c        # Full notes UI workflow
  test_image_operations.c # Image insert/resize/delete
  
# Security Tests
tests/
  test_command_injection.c
  test_xss.c
  test_sql_injection.c
  
# Memory Tests
make valgrind    # Run all tests under Valgrind
make asan        # Compile with AddressSanitizer
```

---

## Part 7: Findings Summary Table

| ID | Severity | Category | Finding | Confidence | Priority |
|----|----------|----------|---------|------------|----------|
| CRITICAL-001 | Critical | Functional | Image Duplication via Buffer-Change Loop | Confirmed | P0 |
| SEC-001 | Critical | Security | Command Injection in ZIP Export | Confirmed | P0 |
| PERF-001 | High | Performance | Memory Leak in session_list_saved() | Confirmed | P1 |
| PERF-002 | High | Performance | Memory Leak in note_list_by_session() | Confirmed | P1 |
| SEC-002 | Medium | Security | Potential XSS in WebView | High | P2 |
| PERF-003 | Medium | Performance | Excessive Rendering on Every Change | High | P2 |
| FUNC-001 | Medium | Functional | Undo Stack Pollution | High | P2 |
| SEC-003 | Low | Security | API Key Plaintext Storage | Confirmed | P3 |
| FUNC-002 | Low | Functional | No Error Handling for Image Load | Confirmed | P3 |
| ARCH-001 | Low | Architecture | Tight UI/Logic Coupling | High | P3 |
| ARCH-002 | Low | Architecture | Global Mutable State | High | P3 |
| ARCH-003 | Low | Code Quality | Inconsistent Error Handling | High | P3 |

---

## Part 8: Risk & Priority Matrix

### P0 - Immediate (Fix This Sprint)
1. **CRITICAL-001:** Image Duplication Bug
   - **Block buffer signals** during `update_image_tag_in_markdown()`
   - **Add regression test**
2. **SEC-001:** Command Injection
   - **Replace `system()` with `g_spawn_sync()`**
   - **Add security test**

### P1 - High Priority (Fix Within 1-2 Sprints)
3. **PERF-001 & PERF-002:** Memory Leaks in db.c
   - **Implement custom free functions**
   - **Run Valgrind to verify fix**

### P2 - Medium Priority (Fix Within 1-2 Months)
4. **SEC-002:** XSS Risk in WebView
   - **Use textContent for role labels**
5. **PERF-003:** Excessive Rendering
   - **Implement 200ms debounce**
6. **FUNC-001:** Undo Stack Pollution
   - **Filter automatic changes from undo**

### P3 - Low Priority (Technical Debt Backlog)
7. **SEC-003:** API Key Storage
   - **Consider libsecret integration**
8. **FUNC-002:** Image Load Error Handling
   - **Add user-visible error messages**
9. **ARCH-001, ARCH-002, ARCH-003:** Architecture Improvements
   - **Refactor gradually during feature work**

---

## Part 9: Recommended Remediation Roadmap

### Phase 1: Critical Fixes (Week 1-2)
```
Day 1-3:  Fix CRITICAL-001 (Image Duplication)
          - Block signals in update_image_tag_in_markdown()
          - Write regression test
          - Verify no duplication occurs
          
Day 4-5:  Fix SEC-001 (Command Injection)
          - Replace system() with g_spawn_sync()
          - Test with malicious filenames
          
Day 6-7:  Code review, testing, documentation
```

### Phase 2: High Priority (Week 3-4)
```
Week 3:   Fix PERF-001 & PERF-002 (Memory Leaks)
          - Implement free_saved_session_info()
          - Implement free_note_info()
          - Run Valgrind, verify no leaks
          
Week 4:   Integration testing
          - Load 100 sessions, monitor memory
          - Switch between 50 notes, monitor memory
```

### Phase 3: Medium Priority (Month 2)
```
Sprint 1: SEC-002, PERF-003
Sprint 2: FUNC-001
```

### Phase 4: Technical Debt (Ongoing)
```
- Refactor architecture issues during new feature development
- Add comprehensive test suite
- Improve error handling consistency
```

---

## Part 10: Conclusion

### Summary
The Scythe application demonstrates solid core functionality but requires immediate attention to **2 critical issues**:
1. **Image duplication bug** (user-facing, data corruption)
2. **Command injection vulnerability** (security risk)

The **memory leaks** in `db.c` should be addressed promptly to prevent gradual performance degradation.

### Strengths
- Well-structured GTK application
- Good use of SQLite for persistence
- Clean WebView integration
- Functional AI assistant integration

### Weaknesses
- Insufficient input validation and sanitization
- Memory management issues in database layer
- Overly aggressive rendering causing performance issues
- Limited error handling and user feedback

### Final Recommendation
**Proceed with remediation in priority order.** The critical issues are well-understood and have clear, low-risk fixes. After addressing P0-P1 items, the application will be significantly more stable and secure.

---

**End of Report**  
**Total Findings:** 12  
**Critical:** 2  
**High:** 2  
**Medium:** 3  
**Low:** 5
