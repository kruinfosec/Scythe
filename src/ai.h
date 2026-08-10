#ifndef AI_H
#define AI_H

#include "common.h"

// Define a struct to hold pointers to GtkEntry and GtkTextView widgets
typedef struct {
    GtkEntry *entry;
    GtkWidget *webview; // Changed from GtkTextView* to GtkWidget* (WebView)
} AiWidgets;

// Struct to hold data for streaming AI response
typedef struct {
    gboolean in_think_block;
    gboolean in_bold_block;
    gboolean in_italic_block;
    gboolean in_code_block;
    gboolean in_table;
    gboolean json_detected;
    GString *partial_buffer; // For handling split tags
    GString *json_buffer; // For buffering complete JSON response
    GtkTextView *think_text_view; // The text view inside the expander
    GtkTextView *code_text_view; // The text view inside the code block
    GPtrArray *table_buffer; // Array of strings for table rows
} AiParserState;

typedef struct {
    GtkWidget *webview;          // Changed from GtkTextView* to GtkWidget* (WebView)
    GString *buffer;             // Raw JSON chunk buffer
    GString *response_text;      // Accumulated response text for saving
    // AiParserState removed as WebView handles markdown
} AiStreamData;

void send_ai_message(GtkWidget *widget, gpointer user_data);
void send_visualization_request(GtkWidget *widget, gpointer user_data);
void cleanup_ai_widgets(GtkWidget *widget, gpointer user_data);
void toggle_ai_panel_common(GtkPaned *hpaned);  // Common toggle logic
void on_toggle_ai_panel(GtkWidget *widget, gpointer user_data);  // Button callback

#endif // AI_H
