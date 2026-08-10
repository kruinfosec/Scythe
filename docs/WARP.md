# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

Scythe is an open-source security testing and vulnerability assessment tool that combines multiple approaches:
- A GTK-based GUI terminal application (C/GTK+)
- Python-based security scanning engines
- AI assistant integration via Ollama/Llama3
- Comprehensive vulnerability reporting

## Core Architecture

The project has a multi-component architecture:

### 1. GUI Terminal Application (`embedded_terminal.c`)
- Built with GTK+3 and VTE (Virtual Terminal Emulator)
- Provides split-pane terminal interface with tabs
- Integrates AI assistant with streaming responses via Ollama API
- Uses libsoup for HTTP communication and json-glib for JSON processing

### 2. Security Scanning Engine (`server.py`)
- Comprehensive external security assessment tool
- Modular scanning approach with threaded execution
- Integrates multiple security tools (Nmap, Nikto)
- Generates detailed security reports in text format

### 3. CVE Lookup Tool (`CVE-Lookup.py`)
- Standalone CVE/CWE vulnerability database search tool
- Interfaces with NIST NVD API and CWE MITRE API
- Supports batch processing from files
- Colorized CLI output with progress indicators

## Development Commands

### Building the GUI Application
```bash
# Compile the basic GUI terminal application (no settings UI)
gcc -o scythe embedded_terminal.c `pkg-config --cflags --libs gtk+-3.0 vte-2.91 json-glib-1.0 libsoup-2.4` -Wall

# Compile the enhanced GUI terminal with customization dialog (recommended)
make
```

### Installing Dependencies
```bash
# Run the dependency installation script
bash Dependencies.sh
```

This script installs:
- Build tools and compilers
- GTK+3, VTE, libsoup, json-glib development libraries  
- Security tools (Nikto, Nmap)
- Python 3 runtime
- Ollama AI backend (if not on WSL)

### Running the Security Tools

#### External Security Assessment
```bash
# Run web security scan on a domain
python3 server.py example.com

# Interactive mode (prompts for domain)
python3 server.py
```

#### CVE Lookup
```bash
# Search specific CVE
python3 CVE-Lookup.py CVE-2021-44228

# Search from file
python3 CVE-Lookup.py --file cve_list.txt

# Search with CVSS filtering
python3 CVE-Lookup.py --keyword "apache" --min-cvss 7.0
```

#### GUI Terminal
```bash
# Launch the basic GUI terminal
./scythe

# Launch the enhanced GUI terminal with settings dialog (recommended)
./scythe-fixed

# Check available hacker fonts on your system
./demo_fonts.sh
```

## Key Components & Data Flow

### Terminal Application Structure
- `TerminalWidget`: Manages individual terminal instances with split capability
- `AiWidgets`: Handles AI chat interface components
- `AiStreamData`: Manages streaming responses from Ollama API
- Multi-threaded terminal spawning with VTE integration

### Security Scanner Modules
- **Domain Analysis**: WHOIS, DNS records, subdomain enumeration
- **SSL/TLS Assessment**: Certificate validation, cipher analysis
- **Web Security**: HTTP headers, security misconfigurations
- **Vulnerability Scanning**: Nmap port scanning, Nikto web testing
- **Email Security**: SPF/DMARC/DKIM record validation
- **Threat Intelligence**: Basic blacklist checking, CVE mapping

### AI Integration
- Communicates with local Ollama instance on `http://127.0.0.1:11434`
- Uses "dama" model by default
- Streams responses via JSON chunked transfer
- Integrated into GUI terminal for contextual security assistance

## File Structure & Patterns

### Configuration Files
- `.vscode/`: VSCode debugging configuration for C development
- `Dependencies.sh`: Automated dependency installation
- `Modelfile`: Ollama model configuration (if needed)

### Executable Components
- `embedded_terminal.c`: Basic GUI terminal application (no settings UI)
- `embedded_terminal_fixed.c`: Enhanced GUI terminal with customization dialog
- `scythe`: Compiled basic terminal executable
- `scythe-fixed`: Compiled enhanced terminal executable (recommended)
- Python scripts are executed directly

### Generated Reports
- Scan results saved as `scan_results_{domain}_{timestamp}.txt`
- Include comprehensive security assessment data
- Formatted for both terminal display and file storage

## Development Environment

### Required System Libraries
- GTK+3 development headers (`libgtk-3-dev`)
- VTE terminal widget (`libvte-2.91-dev`)
- HTTP client library (`libsoup2.4-dev`)
- JSON processing (`libjson-glib-dev`)

### Python Dependencies
Security scanning tools require:
- `requests`, `dnspython`, `python-whois`, `colorama`
- External tools: `nmap`, `nikto`

### AI Backend
- Ollama must be running locally for AI features
- Default model: Llama3 (can be customized in Modelfile)
- Streaming API integration requires persistent HTTP connections

## Security Considerations

When working with this codebase:
- Security tools may trigger false positives in security software
- Network scanning capabilities require appropriate permissions
- AI integration sends data to local Ollama instance only
- Generated reports may contain sensitive information
- External tool dependencies (Nmap, Nikto) have their own security implications

## Testing & Debugging

### C Application Debugging
Use VSCode with the provided launch configuration:
- Builds to `/home/kruinfosec/Scythe/build/Debug/outDebug`
- GDB integration with pretty-printing enabled

### Security Tool Testing
- Test against controlled environments only
- Verify tool installations with `--version` flags
- Check network connectivity for API-dependent features (CVE lookup)

## Integration Points

The various components integrate through:
- GUI terminal can execute Python security scripts
- AI assistant can provide context for security findings
- All tools generate reports that can be cross-referenced
- Common data formats (JSON, text reports) enable automation

## Known Issues & Fixes

### Settings Dialog Displaying Current Values
The enhanced terminal (`embedded_terminal_fixed.c`) now properly displays current settings values in the customization dialog instead of always showing defaults. This fixes an issue where:
- Theme selection would always show "Default" regardless of current theme
- Opacity slider would always show 100% instead of actual opacity
- Font selection would show system default instead of configured font
- Background color would show black instead of configured color
- Checkbox states would show defaults instead of current settings

The fix ensures all UI elements in the settings overlay reflect the actual current configuration.

### Curated Hacker Font Selection
The font selection has been replaced with a curated list of techy/hacker-style monospace fonts instead of the system font picker. Available fonts include:
- **Hack** (Regular & Bold) - Clean, legible programming font
- **Fira Code** (Regular, Medium, Bold) - Popular coding font with ligatures
- **DejaVu Sans Mono** - Widely available, excellent Unicode support
- **Liberation Mono** - Open-source alternative to Courier New
- **Courier** & **Courier 10 Pitch** - Classic console-style fonts
- **FreeMono** - GNU FreeFont monospace variant
- **Noto Sans Mono** - Google's universal monospace font

This provides a focused selection of fonts that are actually available on your system and visually distinct, enhancing the hacker aesthetic while maintaining excellent readability in terminal environments.

### Unlimited Scrollback History Fix
The terminal now properly supports unlimited scrollback history:
- **Default Setting**: Unlimited scrollback is enabled by default
- **All Terminals**: Settings apply to all terminal instances (including split terminals)
- **Persistent**: Settings are saved and restored between sessions
- **Dynamic Updates**: Changes in settings dialog affect all open terminals immediately

This resolves the issue where users couldn't scroll up to see their previous work after a certain number of lines, even with "unlimited history" enabled.
