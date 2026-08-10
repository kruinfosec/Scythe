# Scythe

A modern GTK+3 terminal emulator with integrated AI assistance, built for developers and security professionals.

## Features

- **Multi-tab Terminal Emulation**: VTE-based terminal with split-screen support
- **Integrated AI Assistant**: Powered by Groq Cloud API with streaming responses
- **Rich Markdown Rendering**: WebView-based chat interface with syntax highlighting and Mermaid.js diagrams
- **Session Logging**: SQLite-backed command history and session replay
- **Script Automation**: Discover and execute bash scripts from menu
- **Full Customization**: Themes, colors, fonts, opacity, keyboard shortcuts

## Tech Stack

- **UI**: GTK+ 3.0
- **Terminal**: VTE 2.91
- **AI Interface**: WebKitGTK 4.1
- **Database**: SQLite3
- **HTTP**: libsoup 3.0
- **Build**: GNU Make + GCC

## Quick Start

### Prerequisites

- Linux (Ubuntu 22.04+, Arch, Fedora)
- GCC 11+
- GTK+ 3 development libraries

### Install Dependencies

**Ubuntu/Debian**:
```bash
sudo apt install libgtk-3-dev libvte-2.91-dev libjson-glib-dev \
  libsoup-3.0-dev libwebkit2gtk-4.1-dev uuid-dev libharu-dev libsqlite3-dev
```

**Arch Linux**:
```bash
sudo pacman -S gtk3 vte3 json-glib libsoup3 webkit2gtk-4.1 \
  util-linux-libs libharu sqlite
```

**Fedora**:
```bash
sudo dnf install gtk3-devel vte291-devel json-glib-devel \
  libsoup3-devel webkit2gtk4.1-devel libuuid-devel libharu-devel sqlite-devel
```

### Build

```bash
git clone https://github.com/kruinfosec/Scythe.git
cd Scythe
make
./bin/scythe-fixed
```

## Configuration

### API Key Setup
1. Get free API key from [Groq Console](https://console.groq.com)
2. Open Scythe → Click "Customise"
3. Enter API key in settings

### Script Automation
Add bash scripts to `~/.config/scythe/scripts/`:
```bash
mkdir -p ~/.config/scythe/scripts
echo '#!/bin/bash\necho "Hello from Scythe!"' > ~/.config/scythe/scripts/hello.sh
chmod +x ~/.config/scythe/scripts/hello.sh
```
Access via "Automate" menu in Scythe.

## Documentation

Comprehensive documentation available in [`docs/`](docs/):

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)**: System design and component interaction
- **[API_REFERENCE.md](docs/API_REFERENCE.md)**: Complete function reference
- **[FEATURES.md](docs/FEATURES.md)**: User-facing features and usage guide
- **[DEVELOPMENT.md](docs/DEVELOPMENT.md)**: Build system and contribution guide
- **[FILE_STRUCTURE.md](docs/FILE_STRUCTURE.md)**: Code organization

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| New Terminal | `Ctrl+Shift+T` |
| Split Horizontal | `Ctrl+Shift+D` |
| Split Vertical | `Ctrl+Shift+R` |
| Toggle AI Panel | `Ctrl+Shift+A` |
| Open Settings | `Ctrl+Shift+P` |

All shortcuts configurable via Settings dialog.

## System Requirements

**Minimum**:
- CPU: 4-core (Intel i5-8250U / Ryzen 5 2500U)
- RAM: 8 GB
- GPU: 4 GB VRAM or CPU fallback
- Disk: 10 GB free

**Recommended**:
- CPU: 6-core or better
- RAM: 16 GB
- GPU: 6-8 GB VRAM (RTX 3060, RX 6600)
- Disk: 20+ GB free

## Contributing

See [DEVELOPMENT.md](docs/DEVELOPMENT.md) for:
- Build system details
- Coding conventions
- Feature addition workflow
- Debugging guide

## License

See [LICENSE](docs/LICENSE) for details.

## Acknowledgments

- VTE: Terminal widget
- WebKitGTK: Rich text rendering
- Groq: AI backend
- Mermaid.js: Diagram generation

---

**Status**: Active Development  
**Version**: 1.0.0 (Pre-release)  
**Last Updated**: 2025-12-30
