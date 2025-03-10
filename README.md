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
