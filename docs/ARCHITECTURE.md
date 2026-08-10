# Scythe Architecture

## Table of Contents
- [System Overview](#system-overview)
- [Technology Stack](#technology-stack)
- [Component Architecture](#component-architecture)
- [Data Flow](#data-flow)
- [Module Interaction](#module-interaction)
- [Threading Model](#threading-model)
- [Database Schema](#database-schema)
- [WebView Integration](#webview-integration)

## System Overview

Scythe is a modern GTK+3 terminal emulator with integrated AI assistance capabilities. It combines traditional terminal functionality with advanced features including session logging, conversation management, and real-time AI interaction through a WebView-based interface.

### Design Philosophy
- **Modularity**: Clear separation of concerns with distinct modules for UI, terminal, AI, and data management
- **Modern UI**: Flat, VS Code-inspired interface with dynamic theming
- **Extensibility**: Plugin system through bash scripts and configurable keybindings
- **Performance**: Efficient VTE-based terminal emulation with minimal overhead

## Technology Stack

### Core Technologies
- **UI Framework**: GTK+ 3.0
- **Terminal Emulation**: VTE 2.91
- **AI Interface**: WebKitGTK 4.1 (WebView)
- **Database**: SQLite3
- **Build System**: GNU Make + GCC

### External Dependencies
- **JSON Parsing**: json-glib-1.0
- **HTTP Client**: libsoup-3.0
- **PDF Export**: libharu (hpdf)
- **UUID Generation**: libuuid

### AI Backend
- **API**: Groq Cloud API (configurable)
- **Models**: llama-3.3-70b-versatile, llama-3.1-8b-instant, openai/gpt-oss variants
- **Protocol**: Server-Sent Events (SSE) for streaming responses

## Component Architecture

\`\`\`mermaid
graph TB
    Main[Main Application<br/>main.c] --> UI[UI Components<br/>GTK+3]
    Main --> Terminal[Terminal Manager<br/>terminal.c]
    Main --> AI[AI Assistant<br/>ai.c]
    Main --> Config[Configuration<br/>config.c]
    
    UI --> Header[Header Bar]
    UI --> Notebook[Tab Notebook]
    UI --> AIPanel[AI Panel]
    
    Terminal --> VTE[VTE Widget]
    Terminal --> Split[Split Management]
    Terminal --> Logger[Command Logger]
    
    AI --> WebView[WebView Widget<br/>webview_ai.c]
    AI --> HTTP[HTTP Client<br/>libsoup]
    AI --> Conv[Conversation Manager<br/>conversation.c]
    
    Config --> Settings[Settings Manager]
    Config --> Themes[Theme Engine]
    Config --> Shortcuts[Keybinding System]
    
    Logger --> DB[(SQLite Database<br/>db.c)]
    Conv --> DB
    
    WebView --> Markdown[Markdown Parser]
    WebView --> Mermaid[Mermaid.js Renderer]
\`\`\`

## Data Flow

### Terminal Input/Output Flow
\`\`\`mermaid
sequenceDiagram
    participant User
    participant GTK as GTK Event Loop
    participant Terminal as Terminal Widget
    participant VTE as VTE Backend
    participant Shell as Shell Process
    participant Logger as Command Logger
    participant DB as SQLite

    User->>GTK: Keypress
    GTK->>Terminal: on_terminal_key_press
    Terminal->>Logger: log_terminal_input (on Enter)
    Logger->>DB: INSERT command
    Terminal->>VTE: Forward keypress
    VTE->>Shell: Write to PTY
    Shell->>VTE: Output to PTY
    VTE->>Terminal: content-changed signal
    Terminal->>User: Display output
\`\`\`

### AI Request Flow
\`\`\`mermaid
sequenceDiagram
    participant User
    participant AIPanel as AI Panel
    participant WebView
    participant AI as ai.c
    participant HTTP as libsoup
    participant API as Groq API
    participant Conv as Conversation

    User->>AIPanel: Enter message
    AIPanel->>WebView: webview_append_message (user)
    AIPanel->>AI: send_ai_message
    AI->>Conv: Add to conversation
    AI->>HTTP: Send SSE request
    HTTP->>API: POST /chat/completions
    loop Streaming Response
        API->>HTTP: SSE chunk
        HTTP->>AI: Stream callback
        AI->>WebView: webview_update_ai_message
        WebView->>User: Display streaming text
    end
    AI->>Conv: Save complete response
\`\`\`

### Configuration Load/Save Flow
\`\`\`mermaid
flowchart LR
    Start([Application Start]) --> Load[load_settings]
    Load --> Check{Config exists?}
    Check -->|Yes| Parse[Parse JSON]
    Check -->|No| Defaults[Use defaults]
    Parse --> Apply[apply_settings]
    Defaults --> Apply
    Apply --> UI[Update UI]
    
    User([User Changes Setting]) --> Dialog[Settings Dialog]
    Dialog --> Update[Update AppSettings]
    Update --> Save[save_settings]
    Save --> Write[Write JSON]
    Write --> Reload[apply_settings]
\`\`\`

## Module Interaction

### Core Modules

#### 1. Main Application (main.c)
- **Responsibilities**: Application lifecycle, window management, widget initialization
- **Interactions**: Creates and coordinates all other modules
- **Key Functions**: `activate()`, window setup, global shortcuts

#### 2. Terminal Manager (terminal.c)
- **Responsibilities**: VTE widget management, split terminals, command logging
- **Interactions**: Communicates with db.c for logging, main.c for UI updates
- **Key Functions**: `create_terminal_widget()`, `add_terminal()`, split operations

#### 3. AI Integration (ai.c, webview_ai.c)
- **Responsibilities**: AI API communication, WebView management, markdown rendering
- **Interactions**: Uses libsoup for HTTP, conversation.c for history, webview_ai.c for UI
- **Key Functions**: `send_ai_message()`, `webview_append_message()`, `markdown_to_html()`

#### 4. Configuration System (config.c)
- **Responsibilities**: Settings persistence, theme management, keybinding configuration
- **Interactions**: Accessed by all modules for settings, updates UI on changes
- **Key Functions**: `load_settings()`, `save_settings()`, `apply_ui_theme()`

#### 5. Database Manager (db.c)
- **Responsibilities**: SQLite operations, session management, query logging
- **Interactions**: Used by terminal.c and conversation.c for persistence
- **Key Functions**: `db_init()`, `session_create()`, `log_command()`

#### 6. Conversation Manager (conversation.c)
- **Responsibilities**: AI conversation history, multi-conversation support
- **Interactions**: Stores in filesystem (JSON), loads into AI panel
- **Key Functions**: `conversation_new()`, `conversation_save()`, `conversation_load()`

#### 7. Scripts Engine (scripts.c)
- **Responsibilities**: Bash script discovery, execution, menu population
- **Interactions**: Creates dynamic menus in main.c, spawns shell processes
- **Key Functions**: `discover_scripts()`, `execute_script()`

### Module Dependencies
\`\`\`mermaid
graph LR
    main[main.c] --> terminal[terminal.c]
    main --> ai[ai.c]
    main --> config[config.c]
    main --> scripts[scripts.c]
    
    terminal --> db[db.c]
    terminal --> utils[utils.c]
    
    ai --> webview[webview_ai.c]
    ai --> conversation[conversation.c]
    
    conversation --> utils
    
    config --> |global access| all[All Modules]
\`\`\`

## Threading Model

### GTK Main Thread
Scythe follows GTK's single-threaded architecture:
- All UI updates occur on the GTK main event loop
- VTE handles PTY I/O in background, emits signals to main thread
- WebKit manages rendering in separate processes but callbacks run on main thread

### Asynchronous Operations
- **libsoup HTTP**: Async API with callbacks on main thread
- **VTE spawn**: `vte_terminal_spawn_async` for non-blocking shell creation
- **WebKit JavaScript**: `webkit_web_view_evaluate_javascript` runs async

### Concurrency Considerations
- **No explicit locks needed**: Single-threaded event loop prevents race conditions
- **Callback safety**: All callbacks executed on main thread
- **Signal handlers**: GTK signals delivered synchronously

## Database Schema

### Tables

#### sessions
Tracks terminal sessions for logging and replay.
\`\`\`sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    start_time TEXT NOT NULL,
    end_time TEXT,
    notes TEXT
);
\`\`\`

#### terminals
Tracks individual terminal instances within sessions.
\`\`\`sql
CREATE TABLE terminals (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    parent_terminal_id INTEGER,
    pty_name TEXT NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id),
    FOREIGN KEY (parent_terminal_id) REFERENCES terminals(id)
);
\`\`\`

#### commands
Logs commands executed in terminals.
\`\`\`sql
CREATE TABLE commands (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    terminal_id INTEGER NOT NULL,
    timestamp TEXT NOT NULL,
    command TEXT NOT NULL,
    output TEXT,
    working_directory TEXT,
    exit_code INTEGER,
    FOREIGN KEY (terminal_id) REFERENCES terminals(id)
);
\`\`\`

#### ai_messages
Stores AI chat history (alternative to JSON files).
\`\`\`sql
CREATE TABLE ai_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    timestamp TEXT NOT NULL
);
\`\`\`

### Relationships
\`\`\`mermaid
erDiagram
    sessions ||--o{ terminals : contains
    terminals ||--o{ commands : logs
    terminals ||--o{ terminals : "split from"
\`\`\`

## WebView Integration

### Architecture
The AI panel uses WebKitGTK to render rich markdown content with:
- HTML/CSS for styling
- JavaScript for interactivity (Mermaid.js, collapsible sections)
- C ↔ JavaScript bridge for dynamic updates

### Communication Flow
\`\`\`mermaid
sequenceDiagram
    participant C as C Code (ai.c)
    participant WebKit as WebKit Process
    participant JS as JavaScript Runtime
    participant DOM as HTML DOM

    C->>WebKit: webkit_web_view_load_html (initial)
    WebKit->>JS: Parse \u0026 Execute inline JS
    JS->>DOM: Initialize Mermaid, define functions
    
    Note over C,DOM: User sends message
    C->>WebKit: webkit_web_view_evaluate_javascript
    WebKit->>JS: Execute injected script
    JS->>DOM: createElement, appendChild
    DOM->>WebKit: Render update
    WebKit->>C: Completion callback (unused)
\`\`\`

### JavaScript API
Functions exposed in WebView:
- `setTheme(theme)`: Switch between 'light' and 'dark' themes
- `setCustomColors(bg, fg)`: Apply custom color scheme
- `toggleThinking(header)`: Collapse/expand thinking blocks
- `mermaid.run()`: Render diagram elements

### Content Security
- **Security Level**: 'loose' for Mermaid execution
- **XSS Protection**: All C strings are escaped before injection
- **Isolated Storage**: WebView uses separate cookie/cache storage

## Event Handling

### GTK Signal Flow
\`\`\`mermaid
flowchart TB
    Event[GTK Event] --> Handler{Signal Handler}
    Handler -->|key-press-event| Keys[on_key_press]
    Handler -->|clicked| Button[Button callbacks]
    Handler -->|toggled| Toggle[Toggle callbacks]
    Handler -->|activate| Entry[on_entry_activate]
    
    Keys --> Global[Global shortcuts]
    Keys --> Local[Widget-specific]
    
    Global --> Actions[Execute actions]
    Local --> Propagate[Propagate to focused widget]
\`\`\`

### Custom Event Integration
- **VTE Signals**: `commit`, `contents-changed`, `child-exited`
- **WebView Signals**: `load-changed`, `decide-policy`
- **GtkNotebook Signals**: `switch-page`, `page-added`, `page-removed`

## Performance Considerations

### Optimization Strategies
1. **Lazy Loading**: Scripts discovered on-demand, not at startup
2. **Incremental Rendering**: WebView updates stream incrementally, not batched
3. **Minimal Redraws**: Only affected widgets redraw on theme changes
4. **Efficient Logging**: Prepared SQL statements, batch inserts

### Resource Management
- **Memory**: GStrings freed after use, conversation history capped
- **File Handles**: SQLite connection pooling (single connection)
- **Processes**: Shell processes managed by VTE, cleaned up on exit

---

**Last Updated**: 2025-12-30  
**Maintained By**: Scythe Development Team
