#ifndef WEBVIEW_AI_H
#define WEBVIEW_AI_H

#include <webkit2/webkit2.h>
#include <gtk/gtk.h>

// Create WebView widget for AI responses
GtkWidget* create_ai_webview(void);

// Append a message to the chat (user or ai)
void webview_append_message(GtkWidget *webview, const char *role, const char *content);

// Start a new AI message stream (creates an empty bubble)
void webview_start_ai_message(GtkWidget *webview);

// Update the current AI message content (for streaming)
void webview_update_ai_message(GtkWidget *webview, const char *content);

// Clear the conversation
void webview_clear(GtkWidget *webview);

// Load conversation history into WebView
void webview_set_theme(GtkWidget *webview, const char *theme);
void webview_set_custom_theme(GtkWidget *webview, const char *bg_color, const char *fg_color);
void webview_load_conversation(GtkWidget *webview, GPtrArray *messages);

// Apply current theme settings to a live webview (real-time update)
void webview_apply_settings(GtkWidget *webview);

// Markdown preview helpers
char* markdown_to_html(const char *text);
GtkWidget* create_markdown_webview(void);
void update_markdown_webview(GtkWidget *webview, const char *markdown);

#endif // WEBVIEW_AI_H
