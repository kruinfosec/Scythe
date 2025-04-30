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
• Llama3 (local instance or API setup)
• Requests (for HTTP communication with the Llama3 API)

Vulnerability Scanning Tools:
• Nmap
• Nikto
(Ensure these tools are installed on your system or available via Docker)

Reporting & Documentation:
• ReportLab (for generating PDF reports)

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
