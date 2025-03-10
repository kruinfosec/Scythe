#include <gtk/gtk.h>
#include <vte/vte.h>

static void spawn_shell(VteTerminal *terminal) {
    const char *shell = g_getenv("SHELL") ? g_getenv("SHELL") : "/bin/bash";
    char *argv[] = { (char *)shell, NULL };

    vte_terminal_spawn_async(
        terminal,
        VTE_PTY_DEFAULT,
        NULL,             // Working directory
        argv,             // Command to run
        NULL,             // Environment variables
        0,                // Spawn flags
        NULL,             // Child setup function
        NULL,             // Child setup data
        NULL,             // Cleanup for child setup data
        -1,               // Timeout (-1 for default)
        NULL,             // Async callback
        NULL,             // User data for async callback
        NULL              // GError**
    );
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Scythe");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    VteTerminal *terminal = VTE_TERMINAL(vte_terminal_new());

    vte_terminal_set_color_background(terminal, &(GdkRGBA){0, 0, 0, 1});
    vte_terminal_set_color_foreground(terminal, &(GdkRGBA){1, 1, 1, 1});
    vte_terminal_set_font(terminal, pango_font_description_from_string("Monospace 12"));

    spawn_shell(terminal);

    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(terminal));
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);

    gtk_main();
    return 0;
}
