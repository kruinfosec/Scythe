# Scythe: A CyberSecurity Workbench for Multitasking Security Professionals

[![Status](https://img.shields.io/badge/Status-Active_Development-blue.svg)](#)
[![Version](https://img.shields.io/badge/Version-1.0.0--pre-orange.svg)](#)
[![License](https://img.shields.io/badge/License-GPL--3.0-green.svg)](#)

Scythe is a comprehensive **CyberSecurity Workbench** designed to streamline multitasking for security analysts, penetration testers, and developers. It brings together terminal emulation, real-time AI Q&A assistance, script automation, and command history logging under a unified interface. By serving as an active bridge between command-line execution and LLM-driven intelligence, Scythe keeps the AI context-aware of your session state and directories while capturing all necessary data for final report generation.

---

## 📖 Table of Contents
1. [Key Features](#-key-features)
2. [Architecture Overview](#-architecture-overview)
3. [Technology Stack](#-technology-stack)
4. [File Structure](#-file-structure)
5. [Getting Started](#-getting-started)
   - [Prerequisites](#prerequisites)
   - [Dependency Installation](#dependency-installation)
   - [Building Scythe](#building-scythe)
6. [Configuration & Customization](#-configuration--customization)
   - [API Key Configuration](#api-key-configuration)
   - [Custom Script Automation](#custom-script-automation)
7. [Built-in Tools](#-built-in-tools)
   - [CVE-Lookup Utility](#cve-lookup-utility)
8. [Keyboard Shortcuts](#-keyboard-shortcuts)
9. [Development and Testing](#-development-and-testing)
10. [License](#-license)

---

## 🌟 Key Features

* **🖥️ Multitasking Security Workspace**: Execute shell commands, query the AI, and capture notes/results side-by-side to minimize context switching.
* **🤖 Context-Aware AI QnA (Dama)**: Powered by the Groq API (such as `llama-3.3-70b-versatile`) with Server-Sent Events (SSE) for streaming. Dama remains context-aware of your session state to answer complex penetration testing questions.
* **⚡ 1-Click Script Automation**: Access all your prebuilt scripts and custom tools directly from a dropdown header menu and execute them with a single click in active terminal tabs.
* **💾 Session Logging & Report Generation**: Automatically records terminal sessions, working directories, and executed command outputs to an SQLite3 database. This simplifies tracking your progress and generating structured security assessment reports.
* **📊 Rich Markdown & Diagram Rendering**: Beautiful WebKitGTK-based chat panel displaying syntax-highlighted code blocks, tables, and collapsible reasoning/thinking blocks, with native interactive **Mermaid.js** diagram generation.
* **🎨 Deep UI Customization**: Fully configurable colors, fonts, transparency, padding, and custom themes (including seamless synchronization between the dark terminal shell and the AI pane).

---

## 🏗️ Architecture Overview

Scythe is structured modularly in C, leveraging a single-threaded GTK event loop architecture. Multi-processing and non-blocking I/O are handled natively by WebKit and VTE processes to keep the user interface smooth and responsive.

```
       +---------------------------------------------+
       |           Main Window (main.c)              |
       +----------------------+----------------------+
                              |
              +---------------+---------------+
              |                               |
    +---------v---------+           +---------v---------+
    | Terminal Manager  |           |   AI Panel        |
    | (terminal.c)      |           |   (ai.c)          |
    +---------+---------+           +---------+---------+
              |                               |
       +------v------+                 +------v------+
       | VTE Widget  |                 | WebView UI  |
       | (libs)      |                 | (webview_ai)|
       +------+------+                 +------+------+
              |                               |
       +------v------+                 +------v------+
       | SQLite DB   |                 | Groq API    |
       | (db.c)      |                 | (libsoup3)  |
       +-------------+                 +-------------+
```

* **UI Layer (`main.c`)**: Manages the GTK application shell, header bars, menus, layout boxes, and global keyboard inputs.
* **Terminal Engine (`terminal.c`)**: Wraps VTE terminals, handling splits, collapsings, PTY subprocess spawning, and triggering SQLite history log write calls.
* **AI Core (`ai.c`)**: Prepares system prompts from local `Modelfile` definitions, manages asynchronous `libsoup3` HTTP requests, processes SSE streams, and hands tokens off to the renderer.
* **Markdown/Diagram UI (`webview_ai.c`)**: Embeds WebKitGTK for rich styling and interacts with JavaScript templates to render tables, style code blocks, and draw Mermaid diagrams dynamically.
* **Configuration (`config.c`)**: Manages saving and loading preferences JSON file at `~/.config/scythe/settings.json`, and injects custom GTK CSS on the fly for styling the UI.
* **Data Storage (`db.c` & `conversation.c`)**: Manages local SQLite files (`sessions.db`) and saves JSON-based conversation transcripts.

---

## 🛠️ Technology Stack

Scythe utilizes standard open-source desktop libraries for GNU/Linux environments:

* **GUI Toolkit**: GTK+ 3.0
* **Terminal Shell**: VTE (Virtual Terminal Emulator) 2.91
* **Rich Document Display**: WebKit2GTK 4.1
* **Data Persistence**: SQLite 3 & JSON-GLib 1.0
* **Network Communication**: Libsoup 3.0
* **PDF Report Output**: Libharu (HPDF)
* **Build Tools**: GNU Make & GCC (11+)

---

## 📂 File Structure

An overview of the codebase organization:

```
Scythe/
├── bin/                       # Compiled binaries (e.g., scythe-fixed)
├── docs/                      # Extensive specifications and architecture reports
│   ├── ARCHITECTURE.md        # Detailed diagrams, threads, and databases
│   ├── API_REFERENCE.md       # C-level functions list and structs
│   ├── FEATURES.md            # Comprehensive user features handbook
│   ├── DEVELOPMENT.md         # Contribution, code conventions, and debug logs
│   ├── FILE_STRUCTURE.md      # Expanded filesystem definitions
│   └── README.md              # Legacy documentation summary
├── obj/                       # Intermediate build objects (.o)
├── scripts/                   # System automation and setup scripts
│   └── Dependencies.sh        # Quick dependency installer for Debian/Ubuntu
├── src/                       # Main source code directory
│   ├── main.c                 # Application entry point and GTK window setup
│   ├── terminal.c/.h          # Terminal split panes and terminal spawning
│   ├── ai.c/.h                # LLM request assembly and libsoup streaming
│   ├── webview_ai.c/.h        # Markdown rendering engine, themes, and WebKit integration
│   ├── config.c/.h            # Settings dialog, GTK CSS injection, and file settings.json
│   ├── db.c/.h                # SQLite connection interface and schema creation
│   ├── conversation.c/.h      # AI logs tracker (JSON-backed chats)
│   ├── scripts.c/.h           # Custom bash script automation engine
│   ├── theme_manager.c/.h     # Terminal color scheme controller
│   └── utils.c/.h             # System paths lookup and string escaping
├── CVE-Lookup.py              # CLI Common Vulnerabilities & Exposures search engine
├── Makefile                   # Build definitions and targets
├── Modelfile                  # System instructions and directives for the AI Core
└── test_webview.c             # WebKit Markdown and Mermaid rendering test suite
```

---

## 🚀 Getting Started

### Prerequisites
* Operating System: **GNU/Linux** (Ubuntu 22.04+, Debian 12+, Arch Linux, Fedora)
* Compiler: **GCC 11+** or **Clang 13+**
* Make utility: **GNU Make**

### Dependency Installation

Before building the application, install the developer libraries:

#### Ubuntu / Debian / Kali Linux:
```bash
sudo apt update
sudo apt install libgtk-3-dev libvte-2.91-dev libjson-glib-dev \
  libsoup-3.0-dev libwebkit2gtk-4.1-dev uuid-dev libharu-dev libsqlite3-dev
```

#### Arch Linux:
```bash
sudo pacman -Syu
sudo pacman -S gtk3 vte3 json-glib libsoup3 webkit2gtk-4.1 \
  util-linux-libs libharu sqlite
```

#### Fedora:
```bash
sudo dnf install gtk3-devel vte291-devel json-glib-devel \
  libsoup3-devel webkit2gtk4.1-devel libuuid-devel libharu-devel sqlite-devel
```

### Building Scythe

1. Clone the repository and navigate to the directory:
   ```bash
   git clone https://github.com/kruinfosec/Scythe.git
   cd Scythe
   ```
2. Build the executable using the `Makefile`:
   ```bash
   make
   ```
3. Run the compiled executable:
   ```bash
   ./bin/scythe-fixed
   ```

To clean up intermediate build objects and binary artifacts, run:
```bash
make clean
```

---

## ⚙️ Configuration & Customization

### API Key Configuration
1. Obtain an API key from the [Groq Console](https://console.groq.com).
2. Launch Scythe and click the **Customise** button in the header bar (or use `Ctrl+Shift+P`).
3. Under the **AI Settings** tab, insert your Groq API Key and select your preferred LLM model.
4. Save settings. The settings will be saved locally in `~/.config/scythe/settings.json`.

### Custom Script Automation
Scythe allows loading shell scripts directly into the top bar menu so they can be triggered instantly.
To add custom tools:
1. Create a script directory inside your configuration:
   ```bash
   mkdir -p ~/.config/scythe/scripts
   ```
2. Write any custom shell command script, for example:
   ```bash
   cat << 'EOF' > ~/.config/scythe/scripts/quick_scan.sh
   #!/bin/bash
   echo "Scanning localhost network..."
   nmap -sV localhost
   EOF
   ```
3. Mark it as executable:
   ```bash
   chmod +x ~/.config/scythe/scripts/quick_scan.sh
   ```
4. Access the script in Scythe's top bar under **Automate** to run it automatically in your active terminal.

---

## 🛠️ Built-in Tools

### CVE-Lookup Utility
Scythe includes a powerful vulnerability information search engine: [CVE-Lookup.py](CVE-Lookup.py). 

```bash
python3 CVE-Lookup.py --query CVE-2023-38606
```

#### Features:
* Pulls CVSS scores, metrics, and descriptions from the NVD API 2.0.
* Details weakness mappings from the MITRE CWE API.
* Filters results based on CVSS severity thresholds.
* Generates CSV, JSON, and text-based reports.

---

## ⌨️ Keyboard Shortcuts

Scythe features keybindings that match modern IDE environments:

| Action | Keybinding |
|:---|:---|
| **New Terminal Tab** | `Ctrl + Shift + T` |
| **Split Pane Horizontally** | `Ctrl + Shift + D` |
| **Split Pane Vertically** | `Ctrl + Shift + R` |
| **Toggle AI Side Panel** | `Ctrl + Shift + A` |
| **Open Customization Settings**| `Ctrl + Shift + P` |

> [!NOTE]
> All keyboard shortcuts can be rebound inside the **Customise** dialog interface under the Keyboard Shortcuts settings tab.

---

## 🧪 Development and Testing

Scythe includes isolated testing files to verify subsystems:

* **WebView Rendering Test**: Execute `test_webview.c` to test the Markdown parser, code snippets, syntax highlight, themes, and Mermaid chart drawing:
  ```bash
  gcc test_webview.c src/webview_ai.c -o test_webview `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1`
  ./test_webview
  ```
* **Font Configuration**: Check available terminal fonts by running `demo_fonts.sh`:
  ```bash
  chmod +x demo_fonts.sh
  ./demo_fonts.sh
  ```

---

## 📄 License

This project is licensed under the GPL-3.0 License. See the [LICENSE](docs/LICENSE) file in the `docs` directory for the full license text.
