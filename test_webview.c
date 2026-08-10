// Simple test program for WebView AI component
#include <gtk/gtk.h>
#include "webview_ai.h"

static gboolean send_second_message(GtkWidget *webview);
static gboolean switch_to_light_theme(GtkWidget *webview);

static gboolean test_messages(GtkWidget *webview) {
    // Test markdown rendering
    webview_append_message(webview, "user", 
        "Can you show me a **Python** example with a table?");
    
    webview_append_message(webview, "assistant",
        "# Complete Markdown Test\n\n"
        "## Headings Work\n"
        "### This is H3\n\n"
        "**Lists:**\n"
        "- First item\n"
        "- Second item with **bold**\n"
        "- Third item with *italic*\n\n"
        "**Code Example:**\n\n"
        "```python\n"
        "def hello_world():\n"
        "    print('Hello, World!')\n"
        "    return 42\n"
        "```\n\n"
        "**Table Example:**\n\n"
        "| Language | Year | Type |\n"
        "|----------|------|------|\n"
        "| Python   | 1991 | Interpreted |\n"
        "| C        | 1972 | Compiled |\n"
        "| Rust     | 2015 | Compiled |\n\n"
        "**Text Formatting:**\n"
        "- **Bold text**\n"
        "- *Italic text*\n"
        "- `Inline code`\n"
        "- **Bold with *italic* inside**\n"
        "- Combined: **bold** and *italic* and `code`\n\n"
        "**Flowchart:**\n"
        "```mermaid\n"
        "graph TD\n"
        "    A[Attacker] -->|Exploit| B(Target System)\n"
        "    B -->|Access| C{Database}\n"
        "    C -->|Yes| D[Data Exfiltration]\n"
        "    C -->|No| E[Access Denied]\n"
        "    style A fill:#f96,stroke:#333,stroke-width:2px\n"
        "    style D fill:#f66,stroke:#333,stroke-width:2px\n"
        "```\n\n"
        "**Sequence Diagram:**\n"
        "```mermaid\n"
        "sequenceDiagram\n"
        "    participant User\n"
        "    participant System\n"
        "    participant DB\n"
        "    User->>System: Login Request\n"
        "    System->>DB: Verify Credentials\n"
        "    DB-->>System: Valid User\n"
        "    System-->>User: Access Granted\n"
        "```\n\n"
        "**Pie Chart:**\n"
        "```mermaid\n"
        "pie title Vulnerability Types\n"
        "    \"SQL Injection\" : 40\n"
        "    \"XSS\" : 30\n"
        "    \"CSRF\" : 20\n"
        "    \"Other\" : 10\n"
        "```\n\n"
        "**State Diagram:**\n"
        "```mermaid\n"
        "stateDiagram-v2\n"
        "    [*] --> Idle\n"
        "    Idle --> Scanning : Start Scan\n"
        "    Scanning --> Vulnerable : Vuln Found\n"
        "    Scanning --> Secure : No Vuln\n"
        "    Vulnerable --> [*]\n"
        "    Secure --> [*]\n"
        "```\n\n"
        "**Gantt Chart (Incident Response):**\n"
        "```mermaid\n"
        "gantt\n"
        "    title Incident Response Timeline\n"
        "    dateFormat  HH:mm\n"
        "    axisFormat  %H:%M\n"
        "    section Detection\n"
        "    Alert Received       :a1, 08:00, 30m\n"
        "    Triage              :after a1, 1h\n"
        "    section Containment\n"
        "    Isolate Host        :crit, 09:30, 15m\n"
        "    Block IP            :15m\n"
        "    section Eradication\n"
        "    Remove Malware      :10:00, 2h\n"
        "    Patch Vulnerability :2h\n"
        "```\n\n"
        "**Mindmap (Attack Surface):**\n"
        "```mermaid\n"
        "mindmap\n"
        "  root((Attack Surface))\n"
        "    External\n"
        "      Web Apps\n"
        "      VPN Gateways\n"
        "      Email\n"
        "    Internal\n"
        "      Workstations\n"
        "      Servers\n"
        "      IoT Devices\n"
        "    Cloud\n"
        "      S3 Buckets\n"
        "      IAM Keys\n"
        "```");
    
    return FALSE;  // Don't repeat
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "WebView AI Test");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    GtkWidget *webview = create_ai_webview();
    gtk_container_add(GTK_CONTAINER(window), webview);
    
    gtk_widget_show_all(window);
    
    // Test after window is visible
    g_timeout_add(1000, (GSourceFunc)test_messages, webview);
    
    // Switch to light theme after 5 seconds to test dynamic theming
    g_timeout_add_seconds(5, (GSourceFunc)switch_to_light_theme, webview);
    
    gtk_main();
    
    return 0;
}

static gboolean switch_to_light_theme(GtkWidget *webview) {
    g_print("Switching to LIGHT theme...\n");
    webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(webview), "setTheme('light');", NULL, NULL, NULL);
    return FALSE;
}
