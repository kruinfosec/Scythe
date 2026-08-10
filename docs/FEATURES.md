# Scythe Features

## Table of Contents
- [Terminal Emulation](#terminal-emulation)
- [AI Assistant](#ai-assistant)
- [Session Management](#session-management)
- [Script Automation](#script-automation)
- [Customization](#customization)
- [Keyboard Shortcuts](#keyboard-shortcuts)

---

## Terminal Emulation

### Multiple Terminals
Run multiple terminal sessions in tabbed interface.

**Features**:
- Unlimited tabs
- Tab labels show current process
- Quick tab switching with keyboard shortcuts
- Close individual tabs or all at once

**Shortcuts**:
- New Terminal: `Ctrl+Shift+T`
- Next Tab: `Alt+Shift+Right`
- Previous Tab: `Alt+Shift+Left`
- Jump to Tab N: `Alt+1` through `Alt+0` (configurable)
- Close Tab: `Ctrl+Shift+W`

### Split Terminals
Split terminal view for multitasking.

**Split Types**:
- **Horizontal Split** (`Ctrl+Shift+D`): Side-by-side terminals
- **Vertical Split** (`Ctrl+Shift+R`): Top/bottom terminals

**Navigation**:
- Move focus: `Alt+Left/Right/Up/Down` (configurable)
- Collapse split: `Ctrl+Shift+E`

**Use Cases**:
- Monitor logs while running commands
- Compare outputs
- Edit and test simultaneously

### Copy/Paste
Standard terminal copy/paste functionality.

**Methods**:
1. **Keyboard**:
   - Copy: `Ctrl+Shift+C`
   - Paste: `Ctrl+Shift+V`
2. **Right-click menu**:
   - Context menu with Copy/Paste options

**Behavior**:
- Selection automatically copied to clipboard
- Middle-click paste supported
- Works across split terminals

---

## AI Assistant

### Integrated AI Chat
Built-in AI assistant powered by Groq Cloud API.

**Supported Models**:
- llama-3.3-70b-versatile (default)
- llama-3.1-8b-instant
- openai/gpt-oss-120b
- openai/gpt-oss-20b
- qwen/qwen3-32b

**Features**:
- Real-time streaming responses
- Rich markdown rendering
- Code syntax highlighting
- Interactive Mermaid.js diagrams
- Collapsible "thinking" sections

### Conversation Management
Organize AI interactions into conversations.

**Operations**:
- **New Conversation** (`+` button): Start fresh chat
- **Rename** (✏️ button): Change conversation title
- **History** (💬 button): View all conversations
- **Switch**: Click conversation to load it

**Storage**: Conversations saved as JSON files in `~/.local/share/scythe/conversations/`

### Visualization
Generate diagrams from text descriptions.

**Usage**:
1. Enter description in AI input
2. Click "Visualize" button (or type prompt manually)
3. AI generates Mermaid diagram code
4. Diagram renders automatically

**Diagram Types**:
- Flowcharts
- Sequence diagrams
- Class diagrams
- State diagrams
- Gantt charts
- Pie charts
- Mindmaps
- Git graphs

**Example**:
```
User: Database schema for user management
AI: [Generates entity-relationship diagram]
```

### Export Conversations
Save conversations for external use.

**Formats**:
- **PDF**: Formatted document with syntax highlighting
- **Markdown**: Plain text export

**Access**: Click ⋮ menu → Select export format

### Model Selection
Choose AI model based on needs.

**How to Change**:
1. Click ⚙️ icon in AI panel
2. Select model from dropdown
3. Model applies to current conversation

**Model Comparison**:
| Model | Speed | Quality | Use Case |
|-------|-------|---------|----------|
| llama-3.1-8b-instant | Fast | Good | Quick questions |
| llama-3.3-70b-versatile | Medium | Excellent | Complex tasks |
| openai/gpt-oss-120b | Slow | Best | Production code |

---

## Session Management

### Recording Sessions
Log terminal activity for replay and analysis.

**Features**:
- Records all commands executed
- Captures timestamps
- Tracks working directories
- Logs terminal relationships (splits)

**Activation**:
1. Click "Log" button in header bar
2. Button turns red when recording
3. Click again to stop

**Storage**: SQLite database at `~/.local/share/scythe/sessions.db`

### Session Persistence
Save sessions for later analysis.

**Save Options**:
- **Manual**: Session menu → Save Session
- **On Exit**: Prompted automatically if logging active

**Session Data**:
- Start/end timestamps
- All commands run
- Terminal hierarchy
- Optional notes

### Session Replay
*Planned feature - not yet implemented*

---

## Script Automation

### Script Discovery
Automatically find and execute bash scripts.

**Location**: `~/.config/scythe/scripts/`

**Requirements**:
- Files ending in `.sh`
- Executable permission (`chmod +x script.sh`)

**Organization**:
- Subdirectories create menu categories
- Example: `~/.config/scythe/scripts/network/scan.sh` → Automate → network → scan

### Running Scripts
Execute scripts with one click.

**Steps**:
1. Click "Automate" button in header
2. Navigate menu to find script
3. Click script name
4. Script runs in current terminal

**Execution**:
- Runs in active terminal's shell
- Has access to terminal's environment
- Output displayed in terminal

### Script Templates
Common automation examples.

**Network Scanner** (`scripts/network/scan.sh`):
```bash
#!/bin/bash
# Quick nmap scan
read -p "Target IP/hostname: " target
nmap -sV -A $target
```

**Git Helper** (`scripts/git/quick-commit.sh`):
```bash
#!/bin/bash
# Stage all, commit, push
git add .
read -p "Commit message: " msg
git commit -m "$msg"
git push
```

**System Update** (`scripts/system/update.sh`):
```bash
#!/bin/bash
# Update system packages
sudo apt update
sudo apt upgrade -y
sudo apt autoremove -y
```

---

## Customization

### Full Theme Control
Scythe offers a robust styling system allowing you to customize every aspect of the interface.

**Preset Themes**:
- **Dark Theme** (Default): Pro-level dark UI optimized for coding.
- **Light Theme**: Clean, high-contrast light mode.

**Custom Color Control**:
Create your own theme by customizing:
- **Terminal**: Background, Text, Cursor (Box/Text)
- **UI Chrome**: Window Background, Text Labels
- **Buttons**: Background, Text, Hover States
- **AI Panel**: Background, Text
- **Chat Bubbles**: Distinct colors for User and AI messages

**Access**: Customise button → Appearance Tab

**Live Preview**: Changes apply immediately across all terminals and UI elements.

### Opacity
Adjust terminal transparency.

**Access**: 
- Customise → Opacity slider
- Keyboard shortcuts: `Ctrl+=` (increase) / `Ctrl+-` (decrease)

**Range**: 0 (transparent) to 100 (opaque)

**Default**: 90

**Adjustment**:
- Slider: Fine-grained control
- Keyboard: 5% increments per keypress
- Changes apply immediately to all terminals

**Use Cases**:
- See through terminal to reference docs
- Create layered workspace
- Aesthetic preference

**Note**: Opacity applies only to terminal area, not UI chrome

### Font
Choose terminal font and size.

**Access**: Customise → Font button

**Format**: Pango font descriptor (e.g., "Monospace 10")

**Recommendations**:
- JetBrains Mono
- Fira Code
- Source Code Pro
- Cascadia Code

**Features to Look For**:
- Monospaced (required)
- Ligature support (optional)
- Powerline glyphs (optional)

### API Key
Configure AI backend access.

**Access**: Customise → API Key field

**Required**: Groq Cloud API key (free tier available)

**Obtain**:
1. Visit https://console.groq.com
2. Create account
3. Generate API key
4. Paste into Scythe

**Security**: Stored in `~/.config/scythe/settings.json` (user-readable only)

### Keyboard Shortcuts
Customize all keybindings.

**Access**: Customise → Shortcuts table

**Configurable Actions**:
- Terminal operations (split, collapse, close)
- Tab management (new, next, previous, jump)
- AI panel (toggle)
- Opacity control (increase, decrease)
- Application (close app, open customise)
- Split navigation (move focus)

**How to Change**:
1. Click shortcut button
2. Press desired key combination
3. Confirm or cancel

**Format**: Modifiers + Key (e.g., "Ctrl+Shift+T")

**Validation**: Checks for conflicts with existing shortcuts

---

## Keyboard Shortcuts

### Default Shortcuts

#### Terminal Management
| Action | Shortcut |
|--------|----------|
| New Terminal | `Ctrl+Shift+T` |
| Close Terminal | `Ctrl+Shift+W` |
| Split Horizontal | `Ctrl+Shift+D` |
| Split Vertical | `Ctrl+Shift+R` |
| Collapse Split | `Ctrl+Shift+E` |
| Copy | `Ctrl+Shift+C` |
| Paste | `Ctrl+Shift+V` |

#### Tab Navigation
| Action | Shortcut |
|--------|----------|
| Next Tab | `Alt+Shift+Right` |
| Previous Tab | `Alt+Shift+Left` |
| Jump to Tab 1-10 | `Alt+1` to `Alt+0` |

#### Split Navigation
| Action | Shortcut |
|--------|----------|
| Focus Left | `Alt+Left` |
| Focus Right | `Alt+Right` |
| Focus Up | `Alt+Up` |
| Focus Down | `Alt+Down` |

#### AI Assistant
| Action | Shortcut |
|--------|----------|
| Toggle AI Panel | `Ctrl+Shift+A` |
| Send Message | `Enter` (in AI input) |

**Panel Behavior**:
- **Opening**: AI input receives focus automatically → Start typing immediately
- **Closing**: Terminal receives focus automatically → Continue where you left off

#### Opacity Control
| Action | Shortcut |
|--------|----------|
| Increase Opacity | `Ctrl+=` |
| Decrease Opacity | `Ctrl+-` |

#### Application
| Action | Shortcut |
|--------|----------|
| Open Customise | `Ctrl+Shift+P` |
| Close Scythe | `Alt+Q` |

### Customization
All shortcuts configurable via Customise dialog. Refer to [Customization](#customization) section.

---

## Tips & Tricks

### Efficient Workflow
1. **Use tab jumps**: `Alt+1` through `Alt+0` for instant tab access
2. **Split terminals**: Monitor long-running tasks while working
3. **Script frequently-used commands**: Access via Automate menu
4. **Name AI conversations**: Easier to find specific discussions later

### AI Best Practices
1. **Be specific**: Clear prompts get better responses
2. **Use Visualize**: Great for understanding complex systems
3. **Export important chats**: Save valuable conversations as PDFs
4. **Switch models**: Use fast model for simple queries, powerful for complex tasks

### Session Management
1. **Enable logging before important work**: Capture commands for documentation
2. **Add session notes**: Describe what you're working on
3. **Review sessions**: Learn from command history

### Terminal Organization
1. **One task per tab**: Keep work contexts separate
2. **Use splits for comparison**: Side-by-side diff, log monitoring
3. **Close tabs when done**: Reduce clutter

---

**Document Version**: 1.0  
**Last Updated**: 2025-12-30
