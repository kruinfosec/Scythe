# Scythe API Reference

## Table of Contents
- [Terminal Management](#terminal-management)
- [AI Integration](#ai-integration)
- [WebView Interface](#webview-interface)
- [Configuration System](#configuration-system)
- [Database Operations](#database-operations)
- [Conversation Management](#conversation-management)
- [Script Automation](#script-automation)
- [Utility Functions](#utility-functions)
- [Data Structures](#data-structures)

---

## Terminal Management

**Module**: `terminal.c`, `terminal.h`

### Core Functions

#### `TerminalWidget* create_terminal_widget(GtkWidget *parent)`
Creates a new terminal widget instance.

**Parameters**:
- `parent`: Parent GTK widget (typically a notebook or paned container)

**Returns**: Pointer to `TerminalWidget` struct containing VTE terminal and metadata

**Usage**:
```c
GtkWidget *notebook = gtk_notebook_new();
TerminalWidget *term = create_terminal_widget(notebook);
```

#### `void add_terminal(GtkNotebook *notebook)`
Adds a new terminal tab to the notebook.

**Parameters**:
- `notebook`: Target notebook widget

**Side Effects**:
- Creates new terminal widget
- Spawns shell process
- Registers terminal in database (if session active)
- Sets focus to new terminal

#### `void split_terminal_horizontal(GtkWidget *widget, gpointer data)`
Splits the current terminal horizontally (side-by-side).

**Parameters**:
- `widget`: Triggering widget (unused)
- `data`: User data (unused)

**Behavior**:
- Creates GtkPaned with HORIZONTAL orientation
- Clones parent terminal
- Updates `is_split` flag on parent

#### `void split_terminal_vertical(GtkWidget *widget, gpointer data)`
Splits the current terminal vertically (top/bottom).

**Parameters**: Same as horizontal split

#### `void collapse_terminal(GtkWidget *widget, gpointer data)`
Collapses a split terminal back to single pane.

**Behavior**:
- Removes inactive sub-terminal
- Restores parent terminal to full size
- Updates `is_split` flag

#### `VteTerminal* get_current_terminal(GtkNotebook *notebook)`
Retrieves the currently focused terminal widget.

**Parameters**:
- `notebook`: Notebook containing terminals

**Returns**: Pointer to active `VteTerminal` or `NULL` if none found

**Thread Safety**: Must be called from GTK main thread

---

## AI Integration

**Module**: `ai.c`, `ai.h`

### Message Sending

#### `void send_ai_message(GtkWidget *widget, gpointer user_data)`
Sends user message to AI assistant and initiates streaming response.

**Parameters**:
- `widget`: Triggering widget (send button or entry)
- `user_data`: Pointer to `AiWidgets` struct containing entry and webview

**Behavior**:
1. Extracts text from entry widget
2. Clears entry
3. Appends user message to webview
4. Adds message to current conversation
5. Initiates SSE HTTP request
6. Streams response chunks to webview

**Error Handling**: Displays error message in webview on failure

#### `void send_visualization_request(GtkWidget *widget, gpointer user_data)`
Sends request for AI to generate a Mermaid diagram.

**Parameters**: Same as `send_ai_message`

**Behavior**:
- Wraps user input in visualization prompt
- Instructs AI to respond with Mermaid diagram
- Follows same flow as regular message

### Callbacks

#### `static void ai_stream_read_callback(GObject *source, GAsyncResult *res, gpointer user_data)`
Handles SSE stream chunks from AI API.

**Parameters**:
- `source`: GInputStream source object
- `res`: Async operation result
- `user_data`: Pointer to `StreamData` struct

**Behavior**:
- Parses SSE "data: {...}" format
- Extracts JSON message content
- Appends to accumulated response
- Updates webview incrementally
- Recurses for next chunk or completes stream

**Data Format**:
```
data: {"choices":[{"delta":{"content":"text"}}]}

data: [DONE]
```

#### `char* get_system_prompt(const char *model_name)`
Loads system prompt from Modelfile.

**Parameters**:
- `model_name`: Name of selected AI model

**Returns**: Allocated string containing system message (must be freed)

**File Format**:
```
SYSTEM """
Your system message...
"""
```

### Panel Toggle

#### `void toggle_ai_panel_common(GtkPaned *hpaned)`
Core AI panel toggle logic (single source of truth).

**Parameters**:
- `hpaned`: GtkPaned containing terminal notebook and AI panel

**Behavior**:
- **Closing**: Saves paned position, hides panel, moves divider to max, focuses terminal
- **Opening**: Restores saved position, shows panel, focuses AI input

**State Management**:
- Updates `ai_panel_visible` global flag
- Maintains `last_paned_position` for width consistency

**Called By**: 
- Button callback (`on_toggle_ai_panel`)
- Keyboard shortcut handler (Ctrl+Shift+A)

**Thread Safety**: Must be called from GTK main thread

#### `void on_toggle_ai_panel(GtkWidget *widget, gpointer user_data)`
Button callback for AI panel toggle.

**Parameters**:
- `widget`: Toggle button widget
- `user_data`: Pointer to GtkPaned container

**Behavior**:
- Updates button CSS class ("dama-off" when closed)
- Calls `toggle_ai_panel_common()` for actual toggle logic

**Usage**: Connected to "Dama" button in header bar

---

## WebView Interface

**Module**: `webview_ai.c`, `webview_ai.h`

### WebView Creation

#### `GtkWidget* create_ai_webview(void)`
Creates and configures WebKit WebView for AI responses.

**Returns**: GTK widget pointer to WebView

**Configuration**:
- Loads HTML template with CSS and JavaScript
- Enables developer tools
- Sets default background color
- Initializes Mermaid.js

**Usage**:
```c
GtkWidget *webview = create_ai_webview();
gtk_container_add(GTK_CONTAINER(scrolled_window), webview);
```

### Content Manipulation

#### `void webview_append_message(GtkWidget *webview, const char *role, const char *content)`
Appends a complete message to the WebView.

**Parameters**:
- `webview`: Target WebView widget
- `role`: "user" or "assistant"
- `content`: Markdown-formatted message text

**Behavior**:
- Converts markdown to HTML
- Escapes content for JavaScript injection
- Creates message div with role styling
- Renders Mermaid diagrams if present
- Scrolls to bottom

**Markdown Support**:
- Headings (h1-h3)
- **Bold**, *italic*, `inline code`
- Code blocks with syntax highlighting
- Tables
- Lists (ordered and unordered)
- Mermaid.js diagrams

#### `void webview_start_ai_message(GtkWidget *webview)`
Creates placeholder div for streaming AI response.

**Behavior**:
- Removes ID from any previous placeholder
- Creates new div with `id="current-ai-response"`
- Adds AI role label
- Prepares empty content div

#### `void webview_update_ai_message(GtkWidget *webview, const char *content)`
Updates the streaming AI message with new content.

**Parameters**:
- `webview`: Target WebView
- `content`: Updated markdown content (complete message so far)

**Behavior**:
- Finds div with `id="current-ai-response"`
- Replaces content with new markdown-rendered HTML
- Re-renders Mermaid diagrams
- Auto-scrolls to bottom

#### `char* markdown_to_html(const char *text)`
Converts markdown text to HTML.

**Parameters**:
- `text`: Markdown-formatted string

**Returns**: Allocated HTML string (must be freed)

**Features**:
- Handles nested formatting (**_bold italic_**)
- Detects incomplete Mermaid blocks (shows loading state)
- Auto-corrects common AI syntax errors (e.g., `|\u003e` → `|`)
- Escapes HTML in code blocks

### Theme Control

#### `void webview_set_theme(GtkWidget *webview, const char *theme)`
Switches WebView theme.

**Parameters**:
- `webview`: Target WebView
- `theme`: "light" or "dark"

**Implementation**: Calls JavaScript `setTheme()` function

#### `void webview_set_custom_theme(GtkWidget *webview, const char *bg_color, const char *fg_color)`
Sets custom color scheme.

**Parameters**:
- `webview`: Target WebView
- `bg_color`: Background color (CSS format, e.g., "#292c37")
- `fg_color`: Foreground/text color

**Implementation**: Calls JavaScript `setCustomColors()` if defined

#### `void webview_clear(GtkWidget *webview)`
Clears all messages from WebView.

**Behavior**: Empties the `content` div via JavaScript

---

## Configuration System

**Module**: `config.c`, `config.h`

### Data Structures

#### `AppSettings`
Global configuration structure.

```c
typedef struct {
    char *bg_color;          // Terminal background color (hex)
    char *fg_color;          // Terminal foreground color
    char *groq_api_key;      // AI API key
    char *groq_model;        // Selected AI model name
    char *theme;             // "dark" or "light"
    int opacity;             // Terminal opacity (0-100)
    char *font;              // Terminal font (Pango descriptor)
    
    // Keybindings
    char *toggle_ai_shortcut;
    char *new_terminal_shortcut;
    char *previous_tab_shortcut;
    char *next_tab_shortcut;
    char *split_horizontal_shortcut;
    char *split_vertical_shortcut;
    char *collapse_terminal_shortcut;
    char *close_terminal_shortcut;
    char *customise_shortcut;
    char *close_app_shortcut;
    
    // Configurable tab jumps
    char *tab_jump_shortcuts[10];
    
    // Split navigation
    char *split_left_shortcut;
    char *split_right_shortcut;
    char *split_up_shortcut;
    char *split_down_shortcut;
} AppSettings;
```

**Global Variable**: `extern AppSettings current_settings;`

### Settings Management

#### `void load_settings(void)`
Loads settings from `~/.config/scythe/settings.json`.

**Behavior**:
- Parses JSON file
- Populates `current_settings` global
- Uses defaults if file missing or parsing fails

**Default Values**:
- theme: "dark"
- opacity: 90
- font: "Monospace 10"
- bg_color: "#292c37"
- fg_color: "#ffffff"

#### `void save_settings(void)`
Saves current settings to JSON file.

**File Location**: `~/.config/scythe/settings.json`

**Format**:
```json
{
  "theme": "dark",
  "opacity": 90,
  "font": "Monospace 10",
  "bg_color": "#292c37",
  "fg_color": "#ffffff",
  "groq_api_key": "***",
  "groq_model": "llama-3.3-70b-versatile",
  "shortcuts": {
    "toggle_ai": "Ctrl+Shift+A",
    "new_terminal": "Ctrl+Shift+T",
    ...
  }
}
```

#### `void apply_settings(VteTerminal *terminal)`
Applies current settings to a terminal widget.

**Parameters**:
- `terminal`: Target VTE terminal

**Applies**:
- Font
- Colors (foreground, background, cursor)
- Opacity
- Scrollback lines

#### `void apply_ui_theme(void)`
Applies theme to entire UI (CSS injection).

**Behavior**:
- Generates GTK CSS from current settings
- Updates window, header, tabs, panels
- Updates WebView colors via `webview_set_custom_theme`

**Thread Safety**: Must be called from GTK main thread

### Dialog Functions

#### `void open_customise_dialog(GtkWidget *widget, gpointer user_data)`
Opens settings/customization dialog.

**Features**:
- Theme picker (Dark/Light)
- Color pickers (background/foreground)
- Opacity slider
- Font selector
- API key input
- Model selector
- Keybinding configuration table

**Behavior**: Changes apply immediately, saved on dialog close

---

## Database Operations

**Module**: `db.c`, `db.h`

### Initialization

#### `int db_init(void)`
Initializes SQLite database connection.

**Returns**: 
- `0` on success
- Non-zero on failure

**File**: `~/.local/share/scythe/sessions.db`

**Schema**: Creates tables if not exist (sessions, terminals, commands, ai_messages)

#### `void db_close(void)`
Closes database connection.

**Behavior**: Should be called on application exit

### Session Management

#### `int session_create(const char *notes)`
Creates new logging session.

**Parameters**:
- `notes`: Optional session description

**Returns**: Session ID or `-1` on error

**SQL**:
```sql
INSERT INTO sessions (start_time, notes) VALUES (datetime('now'), ?);
```

#### `void session_end(int session_id)`
Marks session as ended.

**Parameters**:
- `session_id`: ID of session to close

**SQL**:
```sql
UPDATE sessions SET end_time = datetime('now') WHERE id = ?;
```

#### `int session_get_current(void)`
Returns active session ID.

**Returns**: Current session ID or `-1` if no active session

**Storage**: Stored in global variable `g_current_session_id`

### Terminal Registration

#### `int terminal_register(int session_id, int parent_terminal_id, const char *pty_name)`
Registers terminal in database.

**Parameters**:
- `session_id`: Parent session ID
- `parent_terminal_id`: ID of parent terminal (0 if root)
- `pty_name`: PTY device path (e.g., "/dev/pts/3")

**Returns**: Terminal ID or `-1` on error

**Usage**: Called automatically when terminal spawns

### Command Logging

#### `void log_command(int terminal_id, const char *command, const char *output, const char *cwd, int exit_code)`
Logs executed command.

**Parameters**:
- `terminal_id`: Terminal where command ran
- `command`: Command string
- `output`: Command output (optional, currently unused)
- `cwd`: Working directory (optional)
- `exit_code`: Process exit code (optional)

**SQL**:
```sql
INSERT INTO commands (terminal_id, timestamp, command, output, working_directory, exit_code)
VALUES (?, datetime('now'), ?, ?, ?, ?);
```

**Trigger**: Called on Enter key in terminal

---

## Conversation Management

**Module**: `conversation.c`, `conversation.h`

### Data Structures

#### `ConversationMessage`
Represents single message in conversation.

```c
typedef struct {
    char *role;      // "user" or "assistant"
    char *content;   // Message text
    char *timestamp; // ISO 8601 format
} ConversationMessage;
```

#### `Conversation`
Complete conversation structure.

```c
typedef struct {
    char *id;                         // Unique UUID
    char *title;                      // Display name
    char *model;                      // AI model used
    char *created_at;                 // Creation timestamp
    GPtrArray *messages;              // Array of ConversationMessage*
    gboolean is_current;              // Currently active?
} Conversation;
```

### Global State

```c
extern Conversation *current_conversation; // Active conversation
```

### Core Functions

#### `Conversation* conversation_new(const char *title, const char *model, gboolean set_as_current)`
Creates new conversation.

**Parameters**:
- `title`: Conversation name
- `model`: AI model identifier
- `set_as_current`: Whether to make this the active conversation

**Returns**: Pointer to new `Conversation` struct

**Behavior**:
- Generates UUID for `id`
- Sets timestamp
- Initializes empty message array
- Optionally updates `current_conversation` global

#### `void conversation_add_message(Conversation *conv, const char *role, const char *content)`
Adds message to conversation.

**Parameters**:
- `conv`: Target conversation
- `role`: "user" or "assistant"
- `content`: Message text

**Behavior**:
- Creates `ConversationMessage` with timestamp
- Appends to `conv->messages` array

#### `void conversation_save(Conversation *conv)`
Saves conversation to JSON file.

**File Path**: `~/.local/share/scythe/conversations/{id}.json`

**Format**:
```json
{
  "id": "uuid-here",
  "title": "Conversation Title",
  "model": "llama-3.3-70b-versatile",
  "created_at": "2025-12-30T02:00:00Z",
  "messages": [
    {
      "role": "user",
      "content": "Hello",
      "timestamp": "2025-12-30T02:00:05Z"
    },
    {
      "role": "assistant",
      "content": "Hi there!",
      "timestamp": "2025-12-30T02:00:07Z"
    }
  ]
}
```

#### `Conversation* conversation_load(const char *filepath)`
Loads conversation from JSON file.

**Parameters**:
- `filepath`: Path to conversation JSON

**Returns**: Pointer to loaded `Conversation` or `NULL` on error

**Error Handling**: Returns `NULL` on parse failure, logs error to stderr

#### `void conversation_free(Conversation *conv)`
Frees conversation and all messages.

**Parameters**:
- `conv`: Conversation to free

**Behavior**:
- Frees all message strings
- Frees message array
- Frees conversation fields
- Frees conversation struct

#### `GPtrArray* conversation_list_all(void)`
Lists all saved conversations.

**Returns**: Array of conversation file paths

**Directory**: `~/.local/share/scythe/conversations/`

**Usage**:
```c
GPtrArray *convs = conversation_list_all();
for (int i = 0; i < convs->len; i++) {
    char *path = g_ptr_array_index(convs, i);
    Conversation *conv = conversation_load(path);
    // Use conversation...
    conversation_free(conv);
}
g_ptr_array_free(convs, TRUE);
```

#### `void conversation_ensure_storage_dir(void)`
Creates conversations directory if not exists.

**Path**: `~/.local/share/scythe/conversations/`

**Behavior**: Called once at application startup

---

## Script Automation

**Module**: `scripts.c`, `scripts.h`

### Discovery

#### `GList* discover_scripts(const char *base_dir)`
Discovers bash scripts in directory.

**Parameters**:
- `base_dir`: Root directory to search (e.g., "~/.config/scythe/scripts")

**Returns**: GList of script file paths

**Criteria**:
- Files ending in `.sh`
- Executable permission set

**Recursion**: Searches subdirectories (creates submenus)

### Execution

#### `void execute_script(const char *script_path, GtkNotebook *notebook)`
Executes script in current terminal.

**Parameters**:
- `script_path`: Absolute path to script
- `notebook`: Notebook containing terminals

**Behavior**:
- Gets current terminal from active tab
- Feeds script path to terminal input
- Sends Enter keypress
- Logs execution to database (if logging enabled)

**Subprocess**: Script runs in existing shell, not separate process

### Menu Integration

#### `void rebuild_automate_menu(GtkWidget *automate_btn)`
Builds "Automate" dropdown menu from discovered scripts.

**Parameters**:
- `automate_btn`: Button widget to attach menu

**Behavior**:
- Calls `discover_scripts()`
- Creates menu items for each script
- Organizes subdirectories as submenus
- Connects each item to `execute_script`

**Dynamic**: Can be called to refresh menu on demand

---

## Utility Functions

**Module**: `utils.c`, `utils.h`

#### `char* get_scripts_directory(void)`
Gets user scripts directory path.

**Returns**: Allocated path to `~/.config/scythe/scripts` (must be freed)

**Behavior**: Creates directory if not exists

#### `char* get_user_config_dir(void)`
Gets user config directory.

**Returns**: Allocated path to `~/.config/scythe` (must be freed)

#### `char* escape_json_string(const char *str)`
Escapes string for JSON.

**Parameters**:
- `str`: String to escape

**Returns**: Allocated escaped string

**Escapes**:
- `"` → `\"`
- `\` → `\\`
- `\n` → `\\n`
- `\t` → `\\t`

### Notes UI Module

#### `void notes_init(GtkWidget *header_bar)`
Initializes the Markdown Notes popover panel and UI controls, then adds the "Note" toggle button to the main header bar.

**Parameters**:
- `header_bar`: The GTK header bar container to pack the Note toggle button into.

#### `void notes_refresh(void)`
Triggers an auto-save of the active note, updates the notes list from the database, and refreshes the list in the dropdown combo box.

#### `void notes_toggle(void)`
Programmatically toggles the Notes editor popover panel open or closed.

#### `void notes_toggle_pin(void)`
Toggles the Notes editor pinned state. When active, it disables the modal grab, keeping the Notes panel visible while the user types in the terminal.

#### `void notes_trigger_snip(void)`
Triggers the interactive screen snip tool, mapping a translucent selection overlay window globally over the screen.

#### `gboolean notes_handle_window_click(GdkEventButton *event)`
Intercepts GDK mouse clicks entering the window. If Notes is unpinned and open, it pops down the Notes panel and allows the click to focus the target terminal in a single click.

**Parameters**:
- `event`: GdkEventButton pointer containing mouse click coordinates and target.

#### `void close_notes_if_unpinned(void)`
Pops down the Notes popover if it is visible and currently unpinned.

---

## Data Structures

### Enums

#### `VteFormat` (from VTE library)
```c
typedef enum {
    VTE_FORMAT_TEXT = 1,
    VTE_FORMAT_HTML = 2
} VteFormat;
```

### Callback Signatures

#### AI Stream Callback
```c
typedef void (*AiStreamCallback)(
    GObject *source,
    GAsyncResult *res,
    gpointer user_data
);
```

#### Settings Change Callback
```c
typedef void (*SettingsChangeCallback)(
    GtkWidget *widget,
    gpointer user_data
);
```

---

**API Version**: 1.0  
**Last Updated**: 2025-12-30  
**Compatibility**: GTK+ 3.0, VTE 2.91, WebKitGTK 4.1
