# Scythe Development Guide

## Table of Contents
- [Getting Started](#getting-started)
- [Build System](#build-system)
- [Project Structure](#project-structure)
- [Coding Conventions](#coding-conventions)
- [Adding Features](#adding-features)
- [Debugging](#debugging)
- [Testing](#testing)
- [Contributing](#contributing)

---

## Getting Started

### Prerequisites
- **OS**: Linux (tested on Ubuntu 22.04+, Arch, Fedora)
- **Compiler**: GCC 11+ with C11 support
- **Build Tools**: GNU Make 4.0+
- **Desktop Environment**: X11 or Wayland with GTK support

### Dependencies
Install required development libraries:

```bash
# Ubuntu/Debian
sudo apt install \
  libgtk-3-dev \
  libvte-2.91-dev \
  libjson-glib-dev \
  libsoup-3.0-dev \
  libwebkit2gtk-4.1-dev \
  uuid-dev \
  libharu-dev \
  libsqlite3-dev

# Arch Linux
sudo pacman -S \
  gtk3 \
  vte3 \
  json-glib \
  libsoup3 \
  webkit2gtk-4.1 \
  util-linux-libs \
  libharu \
  sqlite

# Fedora
sudo dnf install \
  gtk3-devel \
  vte291-devel \
  json-glib-devel \
  libsoup3-devel \
  webkit2gtk4.1-devel \
  libuuid-devel \
  libharu-devel \
  sqlite-devel
```

### Quick Build
```bash
cd /path/to/Scythe
make clean
make
./bin/scythe-fixed
```

---

## Build System

### Makefile Overview
The project uses a simple Makefile for compilation.

**Key Targets**:
- `make` or `make all`: Build the application
- `make clean`: Remove build artifacts
- `make run`: Build and run
- `make install`: Install to system (planned)

### Compilation Flags
```makefile
CFLAGS = -Wall `pkg-config --cflags gtk+-3.0 vte-2.91 json-glib-1.0 libsoup-3.0 webkit2gtk-4.1`
LIBS = `pkg-config --libs gtk+-3.0 vte-2.91 json-glib-1.0 libsoup-3.0 webkit2gtk-4.1` -luuid -lhpdf -lsqlite3
```

**Warning Level**: `-Wall` enabled for all builds

**Optimization**: Default is debug build (no `-O` flag). For release:
```bash
make CFLAGS+="-O2 -DNDEBUG"
```

### Build Output
- **Object files**: `obj/*.o`
- **Binary**: `bin/scythe-fixed`
- **Clean state**: `make clean` removes `obj/` and `bin/` directories

### Incremental Builds
Makefile tracks dependencies. Changing a `.c` file only rebuilds affected objects.

**Force full rebuild**:
```bash
make clean && make
```

---

## Project Structure

### Directory Layout
```
Scythe/
├── src/                    # Source code
│   ├── main.c             # Application entry, UI setup
│   ├── terminal.c/h       # Terminal emulation logic
│   ├── ai.c/h             # AI backend integration
│   ├── webview_ai.c/h     # WebView-based AI UI
│   ├── config.c/h         # Settings management
│   ├── db.c/h             # SQLite database operations
│   ├── conversation.c/h   # Conversation persistence
│   ├── scripts.c/h        # Script discovery and execution
│   └── utils.c/h          # Utility functions
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md    # System design
│   ├── API_REFERENCE.md   # Function reference
│   ├── FEATURES.md        # User guide
│   ├── DEVELOPMENT.md     # This file
│   └── FILE_STRUCTURE.md  # File organization
├── scripts/                # Build/install scripts
│   └── Dependencies.sh    # Dependency installer
├── Makefile               # Build configuration
├── Modelfile              # AI system prompt
└── bin/                   # Build output (created by make)
```

### Module Responsibilities
Refer to [docs/ARCHITECTURE.md](ARCHITECTURE.md) for detailed module interaction diagrams.

---

## Coding Conventions

### Style Guide

#### Naming
- **Functions**: `snake_case` (e.g., `create_terminal_widget`)
- **Types/Structs**: `PascalCase` (e.g., `TerminalWidget`)
- **Variables**: `snake_case` (e.g., `current_terminal`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `MAX_TERMINALS`)
- **Static functions**: Prefix with `static`, use `snake_case`

#### Formatting
- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line)
  ```c
  if (condition) {
      // code
  } else {
      // code
  }
  ```
- **Line length**: Prefer \u003c100 characters, hard limit at 120
- **Pointer style**: `char *ptr` (asterisk with type)

#### Comments
- **File headers**: Brief description of module purpose
- **Function headers**: Document non-obvious behavior, not obvious syntax
  ```c
  // Good: Explains *why*
  // Call apply_ui_theme after theme changes to update the WebView colors
  
  // Bad: Repeats *what* code does
  // This function sets the theme
  ```
- **Inline comments**: Use sparingly, prefer self-documenting code

### Memory Management
- **Allocation**: Use GLib functions (`g_new`, `g_malloc`, `g_strdup`)
- **Deallocation**: Always free allocated memory (`g_free`, `g_object_unref`)
- **Strings**: Use `GString` for dynamic strings, `g_strdup` for copies
- **Arrays**: Use `GPtrArray` for dynamic arrays of pointers

**Example**:
```c
// Good
char *text = g_strdup(source);
// ... use text ...
g_free(text);

// Bad (potential leak)
char *text = strdup(source);
// ... forgot to free ...
```

### Error Handling
- **GTK/GLib errors**: Use `GError` for operations that can fail
- **NULL checks**: Always check pointers before dereferencing
- **Logging**: Use `g_warning`, `g_critical` for errors

**Example**:
```c
GError *error = NULL;
if (!do_something(\u0026error)) {
    g_warning("Failed to do something: %s", error->message);
    g_error_free(error);
    return;
}
```

### Signal Connections
- **Naming**: Prefix handlers with `on_` (e.g., `on_button_clicked`)
- **Data passing**: Use `g_object_set_data` to attach context
- **Cleanup**: Disconnect signals if widget might outlive handler

**Example**:
```c
// Attach data to widget
g_object_set_data(G_OBJECT(button), "terminal-id", GINT_TO_POINTER(term_id));

// Connect signal
g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);

// Retrieve in callback
static void on_button_clicked(GtkWidget *widget, gpointer data) {
    int term_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "terminal-id"));
    // Use term_id...
}
```

---

## Adding Features

### Workflow
1. **Plan**: Document feature in issue or design doc
2. **Design**: Identify affected modules
3. **Implement**: Write code following conventions
4. **Test**: Verify functionality
5. **Document**: Update relevant docs (FEATURES.md, API_REFERENCE.md)
6. **Commit**: Use descriptive commit messages

### Example: Adding a New Shortcut

#### Step 1: Update AppSettings
Edit `src/config.h`:
```c
typedef struct {
    // ... existing fields ...
    char *my_new_shortcut;
} AppSettings;
```

#### Step 2: Add Default Value
Edit `src/config.c` in `load_settings()`:
```c
if (!current_settings.my_new_shortcut) {
    current_settings.my_new_shortcut = g_strdup("Ctrl+Shift+M");
}
```

#### Step 3: Add UI Control
Edit `src/config.c` in `open_customise_dialog()`:
```c
add_shortcut_row(shortcuts_grid, row++, "My New Action", 
                 \u0026current_settings.my_new_shortcut);
```

#### Step 4: Implement Handler
Edit `src/main.c`:
```c
static void on_my_new_action(GtkWidget *widget, gpointer data) {
    // Implementation
}
```

#### Step 5: Register Shortcut
Edit `src/main.c` in `on_key_press()`:
```c
if (matches_shortcut(event, current_settings.my_new_shortcut)) {
    on_my_new_action(NULL, notebook);
    return TRUE;
}
```

#### Step 6: Document
Update `docs/FEATURES.md` with new shortcut.

### Example: Adding a Database Table

#### Step 1: Define Schema
Edit `src/db.c` in `db_init()`:
```c
const char *create_my_table = 
    "CREATE TABLE IF NOT EXISTS my_table ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "data TEXT NOT NULL);";
sqlite3_exec(db, create_my_table, NULL, NULL, NULL);
```

#### Step 2: Add Access Functions
Edit `src/db.c`:
```c
int my_table_insert(const char *data) {
    const char *sql = "INSERT INTO my_table (data) VALUES (?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, \u0026stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, data, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return id;
}
```

#### Step 3: Expose in Header
Edit `src/db.h`:
```c
int my_table_insert(const char *data);
```

#### Step 4: Document
Update `docs/API_REFERENCE.md` and `docs/ARCHITECTURE.md`.

---

## Debugging

### Common Issues

#### 1. WebView Not Loading
**Symptom**: Blank AI panel

**Check**:
- WebKitGTK installed correctly
- Console output for JavaScript errors
- `webkit_settings_set_enable_write_console_messages_to_stdout` enabled

**Debug**:
```c
// In webview_ai.c, enable debug output
webkit_settings_set_enable_developer_extras(settings, TRUE);
webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);
```

#### 2. Segmentation Fault
**Common Causes**:
- NULL pointer dereference
- Use-after-free
- Incorrect GTK object lifecycle

**Debug with GDB**:
```bash
gdb ./bin/scythe-fixed
(gdb) run
# ... crash occurs ...
(gdb) backtrace
```

**Enable core dumps**:
```bash
ulimit -c unlimited
./bin/scythe-fixed
# ... after crash ...
gdb ./bin/scythe-fixed core
```

#### 3. Memory Leaks
**Tool**: Valgrind

```bash
valgrind --leak-check=full --show-leak-kinds=all ./bin/scythe-fixed
```

**Suppress GTK/GLib internal "leaks"**:
```bash
valgrind --leak-check=full --suppressions=/usr/share/glib-2.0/valgrind/glib.supp ./bin/scythe-fixed
```

#### 4. Terminal Not Responding
**Check**:
- VTE version compatibility
- PTY allocation (`vte_terminal_spawn_async` callback)
- Shell environment variables

**Debug**:
```c
// In terminal.c, check spawn callback
static void on_shell_spawned(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        g_critical("Shell spawn failed: %s", error->message);
        return;
    }
    g_message("Shell spawned with PID %d", pid);
}
```

### Logging
Use GLib logging functions:
```c
g_message("Info: %s", info);    // General info
g_warning("Warning: %s", msg);  // Potential issues
g_critical("Error: %s", err);   // Errors (non-fatal)
g_error("Fatal: %s", fatal);    // Fatal (aborts)
```

**Set log domains**:
```c
#define G_LOG_DOMAIN "Scythe"
```

**Run with debug output**:
```bash
G_MESSAGES_DEBUG=all ./bin/scythe-fixed
```

---

## Testing

### Manual Testing Checklist
- [ ] Terminal spawns correctly
- [ ] Tabs can be created and closed
- [ ] Split terminals work (horizontal and vertical)
- [ ] AI assistant responds to messages
- [ ] Conversations save and load
- [ ] Shortcuts work as expected
- [ ] Settings persist across restarts
- [ ] Session logging captures commands
- [ ] Scripts execute from Automate menu
- [ ] Theme changes apply correctly
- [ ] Copy/paste works in all terminals

### Automated Testing
*Planned - not yet implemented*

---

## Contributing

### Reporting Bugs
Include:
1. **Scythe version**: Output of `./bin/scythe-fixed --version` (if implemented)
2. **OS and version**: `uname -a` output
3. **Steps to reproduce**: Exact sequence to trigger bug
4. **Expected behavior**: What should happen
5. **Actual behavior**: What actually happens
6. **Logs**: Relevant console output

### Feature Requests
Describe:
1. **Use case**: Why is this feature needed?
2. **Proposed solution**: How should it work?
3. **Alternatives**: Other ways to achieve the goal

### Pull Requests
1. **Fork** the repository
2. **Create feature branch**: `git checkout -b feature/my-feature`
3. **Implement** following coding conventions
4. **Test** thoroughly
5. **Document** in relevant docs
6. **Commit** with clear messages
7. **Push** and create pull request

**Commit Message Format**:
```
Short description (50 chars or less)

Longer explanation if needed. Wrap at 72 characters.
Explain *why* the change is needed and *what* it does.

Fixes #123
```

---

## Resources

### Documentation
- [Architecture](ARCHITECTURE.md): System design
- [API Reference](API_REFERENCE.md): Function documentation
- [Features](FEATURES.md): User guide
- [File Structure](FILE_STRUCTURE.md): Code organization

### External References
- [GTK 3 Documentation](https://docs.gtk.org/gtk3/)
- [VTE Documentation](https://gnome.pages.gitlab.gnome.org/vte/)
- [WebKitGTK API](https://webkitgtk.org/reference/webkit2gtk/stable/)
- [GLib Reference](https://docs.gtk.org/glib/)
- [SQLite C API](https://www.sqlite.org/c3ref/intro.html)

---

**Last Updated**: 2025-12-30  
**Maintainer**: Scythe Development Team
