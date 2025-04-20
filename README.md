# Scythe
An open-source command-line tool designed to simplify vulnerability detection and security testing. It combines powerful scanning capabilities with an intuitive interface, making it accessible for both beginners and experts. Features include automated vulnerability checks, detailed reporting, real-time collaboration, and an AI assistant.

# Requirements
Below is a list of all the dependencies needed to run the project on your PC:

Programming Language & Environment:
• Python 3.8+
• Git (for version control)

Desktop & UI Libraries:
• PyQt5 (or PySide6) for building the desktop application
• QTermWidget (with its Python bindings) for terminal emulation

Database:
• PostgreSQL (installed locally)
• SQLAlchemy (for ORM integration)
• psycopg2-binary (PostgreSQL adapter for Python)

AI Integration:
• DeepSeek (local instance or API setup)
• Requests (for HTTP communication with the DeepSeek API)

Vulnerability Scanning Tools:
• Nmap
• OpenVAS
• OWASP ZAP
(Ensure these tools are installed on your system or available via Docker)

Reporting & Documentation:
• ReportLab (for generating PDF reports)

# Structure

/project-root
├── README.md                # Project overview & usage instructions
├── package.json             # Node.js/Electron configuration
├── main.js                  # Electron main process
├── index.html               # Main HTML file for the Electron app
├── renderer.js              # Frontend JavaScript (xterm.js integration)
├── /src
│   ├── /database            # PostgreSQL integration code
│   │     └── db_setup.py    # SQLAlchemy schema & connection
│   ├── /reports             # Report generation module
│   │     └── report_generator.py  # ReportLab-based PDF generator
│   ├── /ai                  # AI integration module
│   │     └── ai_module.py   # DeepSeek API integration using requests
│   ├── /scanners            # Vulnerability scanning integration
│   │     └── scanners.js    # (Sample) Node child_process calls to Nmap/OpenVAS/ZAP
│   └── /collaboration       # (Future) Collaboration features
├── /tests                   # Unit and integration tests
└── /docs                    # Design documents and additional documentation

# Minimum Specs (runs all features, but slower AI inference):
 - CPU: 4-core (e.g., Intel i5-8250U / Ryzen 5 2500U)
 - RAM: 8 GB
 - GPU: 4 GB VRAM (e.g., GTX 1050 Ti / MX450) or CPU fallback
 - Disk: 10 GB free (for tools, AI models, session logs)
 - OS: 64-bit Linux with GTK+ 3, VTE, Python 3.8+, bash
# Recommended Specs (for smooth multiterminal use + fast AI):
 - CPU: 6-core or better (e.g., i5-11400H / Ryzen 5 5600)
 - RAM: 16 GB
 - GPU: 6–8 GB VRAM (e.g., RTX 3060, RX 6600)
 - Disk: 20+ GB free
 - OS: Latest Linux (Ubuntu 22.04+ / Arch / Fedora)

# To set OpenAI API key as an environment variable:
 - export OPENAI_API_KEY="your-api-key"
