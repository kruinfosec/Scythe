#include "notes.h"
#include "db.h"
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <sys/stat.h>
#include <hpdf.h>

static GtkWidget *notes_popover = NULL;
static GtkWidget *notes_textview = NULL;
static GtkWidget *notes_combo = NULL;
static GtkWidget *g_note_toggle_btn = NULL;
static GtkWidget *g_pin_toggle_btn = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *find_replace_bar = NULL;
static GtkWidget *search_entry = NULL;
static GtkWidget *replace_entry = NULL;
static int current_note_id = -1;
static guint render_idle_id = 0;
static guint status_clear_timer_id = 0;
static gboolean is_notes_pinned = FALSE;
/* Tracks currently active image widget for toolbar show/hide */
static gpointer active_image_data_ptr = NULL;

typedef struct {
    GtkWidget *container;
    GtkWidget *gtk_image;
    GtkWidget *handle_eventbox;
    GtkWidget *toolbar;
    GdkPixbuf *original_pixbuf;
    char *filename;
    int current_width;
    char *alignment; // "left", "center", "right"
    gboolean is_dragging;
    double drag_start_x;
    int drag_start_width;
} ImageWidgetData;

// ---- Storage Dir Helper ----
static const char* get_note_images_dir(void) {
    static char img_dir[512] = {0};
    if (img_dir[0] == '\0') {
        const char *home = g_get_home_dir();
        snprintf(img_dir, sizeof(img_dir), "%s/.scythe/note_images", home ? home : "/tmp");
        g_mkdir_with_parents(img_dir, 0755);
    }
    return img_dir;
}

static char* sanitize_filename(const char *input) {
    if (!input) return g_strdup("image.png");
    GString *s = g_string_new("");
    for (const char *p = input; *p; p++) {
        if (g_ascii_isalnum(*p) || *p == '_' || *p == '-' || *p == '.') {
            g_string_append_c(s, *p);
        } else {
            g_string_append_c(s, '_');
        }
    }
    char *res = g_string_free(s, FALSE);
    return res;
}

// ---- Undo / Redo Stack ----
#define MAX_UNDO_STACK 50
static GPtrArray *undo_stack = NULL;
static GPtrArray *redo_stack = NULL;
static gboolean in_undo_redo = FALSE;

static void clear_undo_redo_stacks(void) {
    if (undo_stack) {
        g_ptr_array_free(undo_stack, TRUE);
        undo_stack = NULL;
    }
    if (redo_stack) {
        g_ptr_array_free(redo_stack, TRUE);
        redo_stack = NULL;
    }
    undo_stack = g_ptr_array_new_with_free_func(g_free);
    redo_stack = g_ptr_array_new_with_free_func(g_free);
}

static void push_undo_state(const char *text) {
    if (in_undo_redo || !text) return;
    if (!undo_stack) undo_stack = g_ptr_array_new_with_free_func(g_free);

    // Don't push duplicate states
    if (undo_stack->len > 0) {
        const char *top = g_ptr_array_index(undo_stack, undo_stack->len - 1);
        if (g_strcmp0(top, text) == 0) return;
    }

    if (undo_stack->len >= MAX_UNDO_STACK) {
        g_ptr_array_remove_index(undo_stack, 0);
    }
    g_ptr_array_add(undo_stack, g_strdup(text));

    if (redo_stack) {
        g_ptr_array_free(redo_stack, TRUE);
        redo_stack = g_ptr_array_new_with_free_func(g_free);
    }
}

// Forward declarations
static void on_note_selected(GtkComboBox *combo, gpointer user_data);
static void on_note_toggled(GtkToggleButton *button, gpointer user_data);
static void on_popover_closed(GtkPopover *popover, gpointer user_data);
static void on_new_note_clicked(GtkButton *button, gpointer user_data);
static void on_timestamp_clicked(GtkButton *button, gpointer user_data);
static void apply_markdown_tags(GtkTextBuffer *buffer);
static void save_current_note(void);
static void show_status_message(const char *msg, gboolean is_error);

// ---- Markdown Tag Definitions ----
static int current_edit_line = -1;

static void ensure_tags(GtkTextBuffer *buffer) {
    if (!buffer) return;
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    if (!gtk_text_tag_table_lookup(table, "md-h1")) {
        gtk_text_buffer_create_tag(buffer, "md-h1",
            "weight", PANGO_WEIGHT_BOLD, "scale", 1.8,
            "pixels-below-lines", 6, NULL);
        gtk_text_buffer_create_tag(buffer, "md-h2",
            "weight", PANGO_WEIGHT_BOLD, "scale", 1.5,
            "pixels-below-lines", 4, NULL);
        gtk_text_buffer_create_tag(buffer, "md-h3",
            "weight", PANGO_WEIGHT_BOLD, "scale", 1.3,
            "pixels-below-lines", 2, NULL);
        gtk_text_buffer_create_tag(buffer, "md-bold",
            "weight", PANGO_WEIGHT_BOLD, NULL);
        gtk_text_buffer_create_tag(buffer, "md-italic",
            "style", PANGO_STYLE_ITALIC, NULL);
        gtk_text_buffer_create_tag(buffer, "md-underline",
            "underline", PANGO_UNDERLINE_SINGLE, NULL);
        gtk_text_buffer_create_tag(buffer, "md-code",
            "family", "Monospace", "background", "#2d2d2d",
            "foreground", "#e0e0e0",
            "left-margin", 8, "right-margin", 8, NULL);
        gtk_text_buffer_create_tag(buffer, "md-codeblock",
            "family", "Monospace", "background", "#1e1e1e",
            "foreground", "#d4d4d4", "paragraph-background", "#1e1e1e",
            "left-margin", 12, "right-margin", 12,
            "pixels-above-lines", 4, "pixels-below-lines", 4, NULL);
        gtk_text_buffer_create_tag(buffer, "md-blockquote",
            "left-margin", 20, "foreground", "#888888",
            "style", PANGO_STYLE_ITALIC, NULL);
        gtk_text_buffer_create_tag(buffer, "md-bullet",
            "left-margin", 24, NULL);
        gtk_text_buffer_create_tag(buffer, "md-numlist",
            "left-margin", 28, NULL);
        gtk_text_buffer_create_tag(buffer, "md-hr",
            "justification", GTK_JUSTIFY_CENTER, "foreground", "#666666",
            "scale", 0.8, NULL);
        gtk_text_buffer_create_tag(buffer, "md-link",
            "foreground", "#4a9eff", "underline", PANGO_UNDERLINE_SINGLE, NULL);
        gtk_text_buffer_create_tag(buffer, "md-strikethrough",
            "strikethrough", TRUE, NULL);
        gtk_text_buffer_create_tag(buffer, "md-checkbox-done",
            "foreground", "#50c050", NULL);
        gtk_text_buffer_create_tag(buffer, "md-checkbox-todo",
            "foreground", "#ff6666", NULL);
        gtk_text_buffer_create_tag(buffer, "md-syntax-hidden",
            "invisible", TRUE, NULL);
        gtk_text_buffer_create_tag(buffer, "md-timestamp",
            "foreground", "#888888", "scale", 0.85,
            "style", PANGO_STYLE_ITALIC, NULL);
    }
}

// Render a single line's markdown tags
static void render_line_tags(GtkTextBuffer *buffer, int line_num) {
    if (!buffer) return;
    GtkTextIter line_start, line_end;
    gtk_text_buffer_get_iter_at_line(buffer, &line_start, line_num);
    line_end = line_start;
    if (!gtk_text_iter_ends_line(&line_end))
        gtk_text_iter_forward_to_line_end(&line_end);

    char *text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
    if (!text) return;

    int len = strlen(text);

    if (g_str_has_prefix(text, "### ")) {
        GtkTextIter content_start = line_start;
        gtk_text_iter_forward_chars(&content_start, 4);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-h3", &content_start, &line_end);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &line_start, &content_start);
    } else if (g_str_has_prefix(text, "## ")) {
        GtkTextIter content_start = line_start;
        gtk_text_iter_forward_chars(&content_start, 3);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-h2", &content_start, &line_end);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &line_start, &content_start);
    } else if (g_str_has_prefix(text, "# ")) {
        GtkTextIter content_start = line_start;
        gtk_text_iter_forward_chars(&content_start, 2);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-h1", &content_start, &line_end);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &line_start, &content_start);
    } else if (g_str_has_prefix(text, "> ")) {
        GtkTextIter content_start = line_start;
        gtk_text_iter_forward_chars(&content_start, 2);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-blockquote", &content_start, &line_end);
        gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &line_start, &content_start);
    } else if (g_strcmp0(text, "---") == 0 || g_strcmp0(text, "***") == 0 || g_strcmp0(text, "___") == 0) {
        gtk_text_buffer_apply_tag_by_name(buffer, "md-hr", &line_start, &line_end);
    } else if (g_str_has_prefix(text, "- [x] ") || g_str_has_prefix(text, "- [X] ")) {
        gtk_text_buffer_apply_tag_by_name(buffer, "md-checkbox-done", &line_start, &line_end);
    } else if (g_str_has_prefix(text, "- [ ] ")) {
        gtk_text_buffer_apply_tag_by_name(buffer, "md-checkbox-todo", &line_start, &line_end);
    } else if (g_str_has_prefix(text, "- ") || g_str_has_prefix(text, "* ")) {
        gtk_text_buffer_apply_tag_by_name(buffer, "md-bullet", &line_start, &line_end);
    } else if (len > 2 && g_ascii_isdigit((unsigned char)text[0])) {
        /* Numbered list: match /^\d+\. / */
        int ni = 0;
        while (ni < len && g_ascii_isdigit((unsigned char)text[ni])) ni++;
        if (ni < len && text[ni] == '.' && ni + 1 < len && text[ni + 1] == ' ') {
            gtk_text_buffer_apply_tag_by_name(buffer, "md-numlist", &line_start, &line_end);
        }
    } else if (g_str_has_prefix(text, "Captured: ")) {
        gtk_text_buffer_apply_tag_by_name(buffer, "md-timestamp", &line_start, &line_end);
    }

    /* ---- Inline Link rendering: [text](url) -> apply md-link to visible text ---- */
    {
        const char *lp = text;
        while ((lp = strchr(lp, '[')) != NULL) {
            const char *txt_end = strchr(lp + 1, ']');
            if (!txt_end) break;
            if (txt_end[1] != '(') { lp = txt_end + 1; continue; }
            const char *url_end = strchr(txt_end + 2, ')');
            if (!url_end) break;
            int lts = (int)(lp + 1 - text);
            int lte = (int)(txt_end - text);
            if (lte > lts && url_end > txt_end + 2) {
                GtkTextIter ls2 = line_start, le2 = line_start;
                gtk_text_iter_forward_chars(&ls2, lts);
                gtk_text_iter_forward_chars(&le2, lte);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-link", &ls2, &le2);
                /* Hide [ ... ]( url ) syntax markers */
                GtkTextIter syn1s = line_start, syn1e = line_start;
                gtk_text_iter_forward_chars(&syn1s, (int)(lp - text));
                gtk_text_iter_forward_chars(&syn1e, lts);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &syn1s, &syn1e);
                GtkTextIter syn2s = line_start, syn2e = line_start;
                gtk_text_iter_forward_chars(&syn2s, lte);
                gtk_text_iter_forward_chars(&syn2e, (int)(url_end + 1 - text));
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &syn2s, &syn2e);
            }
            lp = url_end + 1;
        }
    }

    // Inline formatting
    const char *p = text;
    int offset = 0;
    while (offset < len) {
        if (p[offset] == '*' && offset + 1 < len && p[offset + 1] == '*') {
            int start_off = offset;
            int end_off = -1;
            for (int j = offset + 2; j < len - 1; j++) {
                if (p[j] == '*' && p[j + 1] == '*') {
                    end_off = j + 2;
                    break;
                }
            }
            if (end_off > 0) {
                GtkTextIter s = line_start, e = line_start;
                GtkTextIter ss = line_start, se = line_start;
                gtk_text_iter_forward_chars(&s, start_off + 2);
                gtk_text_iter_forward_chars(&e, end_off - 2);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-bold", &s, &e);
                gtk_text_iter_forward_chars(&ss, start_off);
                GtkTextIter ss2 = line_start;
                gtk_text_iter_forward_chars(&ss2, start_off + 2);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &ss, &ss2);
                gtk_text_iter_forward_chars(&se, end_off - 2);
                GtkTextIter se2 = line_start;
                gtk_text_iter_forward_chars(&se2, end_off);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &se, &se2);
                offset = end_off;
                continue;
            }
        }
        if (p[offset] == '*' && (offset == 0 || p[offset - 1] != '*') && (offset + 1 >= len || p[offset + 1] != '*')) {
            int start_off = offset;
            int end_off = -1;
            for (int j = offset + 1; j < len; j++) {
                if (p[j] == '*' && (j + 1 >= len || p[j + 1] != '*')) {
                    end_off = j + 1;
                    break;
                }
            }
            if (end_off > 0) {
                GtkTextIter s = line_start, e = line_start;
                gtk_text_iter_forward_chars(&s, start_off + 1);
                gtk_text_iter_forward_chars(&e, end_off - 1);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-italic", &s, &e);
                GtkTextIter ss = line_start, ss2 = line_start;
                gtk_text_iter_forward_chars(&ss, start_off);
                gtk_text_iter_forward_chars(&ss2, start_off + 1);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &ss, &ss2);
                GtkTextIter se = line_start, se2 = line_start;
                gtk_text_iter_forward_chars(&se, end_off - 1);
                gtk_text_iter_forward_chars(&se2, end_off);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &se, &se2);
                offset = end_off;
                continue;
            }
        }
        if (g_str_has_prefix(p + offset, "<u>")) {
            int start_off = offset;
            int end_off = -1;
            for (int j = offset + 3; j < len - 3; j++) {
                if (g_str_has_prefix(p + j, "</u>")) {
                    end_off = j + 4;
                    break;
                }
            }
            if (end_off > 0) {
                GtkTextIter s = line_start, e = line_start;
                gtk_text_iter_forward_chars(&s, start_off + 3);
                gtk_text_iter_forward_chars(&e, end_off - 4);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-underline", &s, &e);
                /* Hide <u> and </u> syntax */
                GtkTextIter ss = line_start, ss2 = line_start;
                gtk_text_iter_forward_chars(&ss, start_off);
                gtk_text_iter_forward_chars(&ss2, start_off + 3);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &ss, &ss2);
                GtkTextIter se = line_start, se2 = line_start;
                gtk_text_iter_forward_chars(&se, end_off - 4);
                gtk_text_iter_forward_chars(&se2, end_off);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &se, &se2);
                offset = end_off;
                continue;
            }
        }
        if (p[offset] == '`' && (offset + 1 < len && p[offset + 1] != '`')) {
            int start_off = offset;
            int end_off = -1;
            for (int j = offset + 1; j < len; j++) {
                if (p[j] == '`') {
                    end_off = j + 1;
                    break;
                }
            }
            if (end_off > 0) {
                GtkTextIter s = line_start, e = line_start;
                gtk_text_iter_forward_chars(&s, start_off);
                gtk_text_iter_forward_chars(&e, end_off);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-code", &s, &e);
                offset = end_off;
                continue;
            }
        }
        if (p[offset] == '~' && offset + 1 < len && p[offset + 1] == '~') {
            int start_off = offset;
            int end_off = -1;
            for (int j = offset + 2; j < len - 1; j++) {
                if (p[j] == '~' && p[j + 1] == '~') {
                    end_off = j + 2;
                    break;
                }
            }
            if (end_off > 0) {
                GtkTextIter s = line_start, e = line_start;
                gtk_text_iter_forward_chars(&s, start_off + 2);
                gtk_text_iter_forward_chars(&e, end_off - 2);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-strikethrough", &s, &e);
                /* Hide ~~ syntax */
                GtkTextIter ss = line_start, ss2 = line_start;
                gtk_text_iter_forward_chars(&ss, start_off);
                gtk_text_iter_forward_chars(&ss2, start_off + 2);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &ss, &ss2);
                GtkTextIter se = line_start, se2 = line_start;
                gtk_text_iter_forward_chars(&se, end_off - 2);
                gtk_text_iter_forward_chars(&se2, end_off);
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &se, &se2);
                offset = end_off;
                continue;
            }
        }
        offset++;
    }

    g_free(text);
}

// Apply tags to buffer
static void apply_markdown_tags(GtkTextBuffer *buffer) {
    if (!buffer) return;
    ensure_tags(buffer);

    GtkTextIter buf_start, buf_end;
    gtk_text_buffer_get_bounds(buffer, &buf_start, &buf_end);

    const char *tag_names[] = {
        "md-h1", "md-h2", "md-h3", "md-bold", "md-italic", "md-underline", "md-code",
        "md-codeblock", "md-blockquote", "md-bullet", "md-numlist", "md-hr", "md-link",
        "md-strikethrough", "md-checkbox-done", "md-checkbox-todo",
        "md-syntax-hidden", "md-timestamp", NULL
    };
    for (int i = 0; tag_names[i]; i++) {
        gtk_text_buffer_remove_tag_by_name(buffer, tag_names[i], &buf_start, &buf_end);
    }

    int line_count = gtk_text_buffer_get_line_count(buffer);
    gboolean in_code_block = FALSE;

    for (int i = 0; i < line_count; i++) {
        GtkTextIter ls, le;
        gtk_text_buffer_get_iter_at_line(buffer, &ls, i);
        le = ls;
        if (!gtk_text_iter_ends_line(&le))
            gtk_text_iter_forward_to_line_end(&le);
        char *line_text = gtk_text_buffer_get_text(buffer, &ls, &le, FALSE);

        if (line_text && g_str_has_prefix(line_text, "```")) {
            if (in_code_block) {
                gtk_text_buffer_apply_tag_by_name(buffer, "md-codeblock", &ls, &le);
                in_code_block = FALSE;
                g_free(line_text);
                continue;
            } else {
                in_code_block = TRUE;
                gtk_text_buffer_apply_tag_by_name(buffer, "md-codeblock", &ls, &le);
                g_free(line_text);
                continue;
            }
        }
        if (in_code_block) {
            gtk_text_buffer_apply_tag_by_name(buffer, "md-codeblock", &ls, &le);
            g_free(line_text);
            continue;
        }

        /* Image line visibility: visible ONLY when this image's toolbar is active/selected */
        gboolean is_img_line = line_text && g_str_has_prefix(line_text, "<img src=\"scythe://note_image/");
        gboolean is_captured_line = line_text && g_str_has_prefix(line_text, "Captured: ");

        if (is_img_line || is_captured_line) {
            gboolean is_active = FALSE;
            if (active_image_data_ptr) {
                ImageWidgetData *act = (ImageWidgetData *)active_image_data_ptr;
                if (act->filename && line_text && strstr(line_text, act->filename)) {
                    is_active = TRUE;
                } else if (is_captured_line && i > 0) {
                    GtkTextIter prev_s, prev_e;
                    gtk_text_buffer_get_iter_at_line(buffer, &prev_s, i - 1);
                    prev_e = prev_s;
                    if (!gtk_text_iter_ends_line(&prev_e))
                        gtk_text_iter_forward_to_line_end(&prev_e);
                    char *prev_txt = gtk_text_buffer_get_text(buffer, &prev_s, &prev_e, FALSE);
                    if (prev_txt && act->filename && strstr(prev_txt, act->filename)) {
                        is_active = TRUE;
                    }
                    g_free(prev_txt);
                }
            }

            if (is_active) {
                if (is_captured_line) {
                    gtk_text_buffer_apply_tag_by_name(buffer, "md-timestamp", &ls, &le);
                }
            } else {
                gtk_text_buffer_apply_tag_by_name(buffer, "md-syntax-hidden", &ls, &le);
            }
            g_free(line_text);
            continue;
        }
        g_free(line_text);

        /* Obsidian-style: skip current edit line so raw syntax stays visible while editing */
        if (i == current_edit_line) continue;

        render_line_tags(buffer, i);
    }
}

static gboolean render_idle_cb(gpointer user_data) {
    render_idle_id = 0;
    if (user_data) {
        GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
        apply_markdown_tags(buffer);
        g_object_unref(buffer); /* release strong ref taken in schedule_render */
    }
    return G_SOURCE_REMOVE;
}

static void schedule_render(GtkTextBuffer *buffer) {
    if (!buffer) return;
    if (render_idle_id != 0) {
        g_source_remove(render_idle_id);
        render_idle_id = 0;
    }
    g_object_ref(buffer); /* keep buffer alive until idle fires */
    render_idle_id = g_idle_add(render_idle_cb, buffer);
}

// Status feedback helper
static gboolean clear_status_cb(gpointer user_data) {
    if (status_label) {
        gtk_label_set_text(GTK_LABEL(status_label), "");
    }
    status_clear_timer_id = 0;
    return G_SOURCE_REMOVE;
}

static void show_status_message(const char *msg, gboolean is_error) {
    if (!status_label || !msg) return;
    if (status_clear_timer_id != 0) {
        g_source_remove(status_clear_timer_id);
        status_clear_timer_id = 0;
    }

    if (is_error) {
        char *markup = g_strdup_printf("<span color='#ff6666'>%s</span>", msg);
        gtk_label_set_markup(GTK_LABEL(status_label), markup);
        g_free(markup);
    } else {
        char *markup = g_strdup_printf("<span color='#50c050'>%s</span>", msg);
        gtk_label_set_markup(GTK_LABEL(status_label), markup);
        g_free(markup);
    }

    status_clear_timer_id = g_timeout_add(3000, clear_status_cb, NULL);
}

// Helper to scale GdkPixbuf to max width
static GdkPixbuf* scale_pixbuf_responsive(GdkPixbuf *pixbuf, int max_width) {
    if (!pixbuf) return NULL;
    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    if (w <= max_width) {
        g_object_ref(pixbuf);
        return pixbuf;
    }
    int new_w = max_width;
    int new_h = (int)((double)h * (double)max_width / (double)w);
    if (new_h < 1) new_h = 1;
    return gdk_pixbuf_scale_simple(pixbuf, new_w, new_h, GDK_INTERP_BILINEAR);
}

// ---- Interactive Resizable Image Widget with Drag Handle & Alignment Toolbar ----

static void update_image_tag_in_markdown(const char *filename, int new_width, const char *alignment) {
    if (!notes_textview || !filename) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    const char *align_str = alignment ? alignment : "center";
    int valid_w = (new_width >= 80 && new_width <= 800) ? new_width : 450;

    char clean_fn[128];
    g_strlcpy(clean_fn, filename, sizeof(clean_fn));
    char *hash = strchr(clean_fn, '#');
    if (hash) *hash = '\0';

    /* Build the new tag string */
    char new_img_tag[384];
    snprintf(new_img_tag, sizeof(new_img_tag),
             "<img src=\"scythe://note_image/%s\" width=\"%d\" align=\"%s\" />",
             clean_fn, valid_w, align_str);

    /* Build the search key — search for <img ... src="scythe://note_image/FILENAME" */
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "scythe://note_image/%s", clean_fn);

    /* Use iterator-based search to avoid offset mismatch caused by child anchors */
    GtkTextIter search_start;
    gtk_text_buffer_get_start_iter(buffer, &search_start);

    GtkTextIter match_start, match_end;
    if (!gtk_text_iter_forward_search(&search_start, search_key,
                                      GTK_TEXT_SEARCH_TEXT_ONLY,
                                      &match_start, &match_end, NULL)) {
        save_current_note();
        return; /* filename not found in buffer */
    }

    /* Walk backward from match_start to find the opening '<' of the tag */
    GtkTextIter tag_open = match_start;
    while (!gtk_text_iter_is_start(&tag_open)) {
        gtk_text_iter_backward_char(&tag_open);
        gunichar ch = gtk_text_iter_get_char(&tag_open);
        if (ch == '<') break;
        if (ch == '\n') {
            /* Overshot — no opening '<' found on this line; abort to avoid corruption */
            save_current_note();
            return;
        }
    }

    /* Walk forward from match_end to find the closing '>' */
    GtkTextIter tag_close = match_end;
    while (!gtk_text_iter_is_end(&tag_close)) {
        gunichar ch = gtk_text_iter_get_char(&tag_close);
        gtk_text_iter_forward_char(&tag_close);
        if (ch == '>') break;
        if (ch == '\n') {
            save_current_note();
            return;
        }
    }

    /* Safety: ensure tag_open < tag_close and both are valid */
    if (gtk_text_iter_compare(&tag_open, &tag_close) >= 0) {
        save_current_note();
        return;
    }

    /* Replace the old tag text with the new one */
    gtk_text_buffer_delete(buffer, &tag_open, &tag_close);
    gtk_text_buffer_insert(buffer, &tag_open, new_img_tag, -1);

    save_current_note();
}

static void apply_image_alignment(ImageWidgetData *data) {
    if (!data || !data->container) return;

    if (g_strcmp0(data->alignment, "left") == 0) {
        gtk_widget_set_margin_start(data->container, 0);
        gtk_widget_set_margin_end(data->container, 40);
        gtk_widget_set_halign(data->container, GTK_ALIGN_START);
    } else if (g_strcmp0(data->alignment, "right") == 0) {
        gtk_widget_set_margin_start(data->container, 40);
        gtk_widget_set_margin_end(data->container, 0);
        gtk_widget_set_halign(data->container, GTK_ALIGN_END);
    } else { /* "center" */
        gtk_widget_set_margin_start(data->container, 20);
        gtk_widget_set_margin_end(data->container, 20);
        gtk_widget_set_halign(data->container, GTK_ALIGN_CENTER);
    }
}

static void save_image_metadata_canonical(ImageWidgetData *data) {
    if (!data || !data->filename) return;

    // 1. SQLite is Canonical Source of Truth
    if (current_note_id != -1) {
        note_update_image_metadata(current_note_id, data->filename, data->current_width, data->alignment);
    }

    // 2. Portable Fallback in Markdown
    update_image_tag_in_markdown(data->filename, data->current_width, data->alignment);
}

// Drag-to-Resize Handlers
static gboolean on_handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (data && event->button == 1) {
        data->is_dragging = TRUE;
        data->drag_start_x = event->x_root;
        data->drag_start_width = data->current_width;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_handle_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (data && data->is_dragging && data->original_pixbuf && data->gtk_image) {
        double dx = event->x_root - data->drag_start_x;
        int new_w = (int)(data->drag_start_width + dx);

        if (new_w < 80) new_w = 80;
        if (new_w > 750) new_w = 750;

        data->current_width = new_w;

        GdkPixbuf *scaled = scale_pixbuf_responsive(data->original_pixbuf, new_w);
        if (scaled) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(data->gtk_image), scaled);
            g_object_unref(scaled);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_handle_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (data && data->is_dragging && event->button == 1) {
        data->is_dragging = FALSE;
        save_image_metadata_canonical(data);
        return TRUE;
    }
    return FALSE;
}

// Preset Size Button Handler
static void on_preset_btn_clicked(GtkButton *btn, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (!data || !data->original_pixbuf || !data->gtk_image) return;

    int target_w = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "target-width"));
    if (target_w <= 0) target_w = 350;

    data->current_width = target_w;
    GdkPixbuf *scaled = scale_pixbuf_responsive(data->original_pixbuf, target_w);
    if (scaled) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(data->gtk_image), scaled);
        g_object_unref(scaled);
    }

    save_image_metadata_canonical(data);
}

// Alignment Button Handler
static void on_align_btn_clicked(GtkButton *btn, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (!data) return;

    const char *align_choice = (const char *)g_object_get_data(G_OBJECT(btn), "align-val");
    if (align_choice) {
        if (data->alignment) g_free(data->alignment);
        data->alignment = g_strdup(align_choice);

        apply_image_alignment(data);
        save_image_metadata_canonical(data);
    }
}

static void free_image_widget_data(gpointer data_ptr) {
    ImageWidgetData *data = (ImageWidgetData *)data_ptr;
    if (data) {
        if (active_image_data_ptr == data) active_image_data_ptr = NULL;
        data->container = NULL;
        data->gtk_image = NULL;
        data->toolbar = NULL;
        data->handle_eventbox = NULL;
        if (data->original_pixbuf) { g_object_unref(data->original_pixbuf); data->original_pixbuf = NULL; }
        if (data->filename) { g_free(data->filename); data->filename = NULL; }
        if (data->alignment) { g_free(data->alignment); data->alignment = NULL; }
        g_free(data);
    }
}

/* Deactivate whichever image toolbar is currently showing */
static void deactivate_current_image(void) {
    if (!active_image_data_ptr) return;
    ImageWidgetData *prev = (ImageWidgetData *)active_image_data_ptr;
    if (prev->toolbar) gtk_widget_hide(prev->toolbar);
    if (prev->handle_eventbox) gtk_widget_hide(prev->handle_eventbox);
    active_image_data_ptr = NULL;
    if (notes_textview) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
        if (buf) apply_markdown_tags(buf);
    }
}

/* Open image in default system image viewer */
static void open_image_in_default_viewer(const char *filename) {
    if (!filename) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", get_note_images_dir(), filename);
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri) {
        GError *err = NULL;
        g_app_info_launch_default_for_uri(uri, NULL, &err);
        if (err) g_error_free(err);
        g_free(uri);
    }
}

/* Called when user clicks on the image area: activate this image (show toolbar + handle) */
/* Double-click opens default system image viewer */
static gboolean on_image_area_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    if (!data) return FALSE;

    /* Double-click: open image in default viewer */
    if (event->type == GDK_2BUTTON_PRESS && data->filename) {
        open_image_in_default_viewer(data->filename);
        return TRUE;
    }

    if (active_image_data_ptr != data) {
        deactivate_current_image();
        active_image_data_ptr = data;
        if (notes_textview) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
            if (buf) apply_markdown_tags(buf);
        }
    }
    if (data->toolbar) gtk_widget_show(data->toolbar);
    if (data->handle_eventbox) gtk_widget_show(data->handle_eventbox);
    return FALSE; /* don't consume event so drag handle still works */
}

static GtkWidget* create_resizable_image_widget(GdkPixbuf *original_pixbuf, const char *filename, int initial_width, const char *initial_align) {
    if (!original_pixbuf) return NULL;
    if (initial_width <= 0) initial_width = 450;

    ImageWidgetData *data = g_new0(ImageWidgetData, 1);
    data->original_pixbuf = g_object_ref(original_pixbuf);
    data->filename = g_strdup(filename);
    data->current_width = initial_width;
    data->alignment = g_strdup(initial_align ? initial_align : "center");

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    data->container = container;
    gtk_widget_set_margin_top(container, 4);
    gtk_widget_set_margin_bottom(container, 4);
    apply_image_alignment(data);

    /* ---- Toolbar (HIDDEN by default, shown when image clicked) ---- */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    data->toolbar = toolbar;

    GtkWidget *lbl_size = gtk_label_new("Size:");
    gtk_box_pack_start(GTK_BOX(toolbar), lbl_size, FALSE, FALSE, 2);

    struct { const char *label; int width; } presets[] = {
        {"S", 200}, {"M", 350}, {"L", 500}, {"Full", 650}
    };
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_button_new_with_label(presets[i].label);
        g_object_set_data(G_OBJECT(b), "target-width", GINT_TO_POINTER(presets[i].width));
        g_signal_connect(b, "clicked", G_CALLBACK(on_preset_btn_clicked), data);
        gtk_box_pack_start(GTK_BOX(toolbar), b, FALSE, FALSE, 0);
    }

    GtkWidget *lbl_align = gtk_label_new(" Align:");
    gtk_box_pack_start(GTK_BOX(toolbar), lbl_align, FALSE, FALSE, 2);

    struct { const char *label; const char *val; } align_opts[] = {
        {"Left", "left"}, {"Center", "center"}, {"Right", "right"}
    };
    for (int i = 0; i < 3; i++) {
        GtkWidget *ab = gtk_button_new_with_label(align_opts[i].label);
        g_object_set_data(G_OBJECT(ab), "align-val", (gpointer)align_opts[i].val);
        g_signal_connect(ab, "clicked", G_CALLBACK(on_align_btn_clicked), data);
        gtk_box_pack_start(GTK_BOX(toolbar), ab, FALSE, FALSE, 0);
    }

    /* ---- Image + Drag Handle wrapped in EventBox for click detection ---- */
    GtkWidget *img_eventbox = gtk_event_box_new();
    gtk_widget_add_events(img_eventbox, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(img_eventbox, "button-press-event", G_CALLBACK(on_image_area_clicked), data);

    GtkWidget *overlay = gtk_overlay_new();

    GdkPixbuf *scaled = scale_pixbuf_responsive(original_pixbuf, initial_width);
    GtkWidget *img = gtk_image_new_from_pixbuf(scaled ? scaled : original_pixbuf);
    if (scaled) g_object_unref(scaled);

    data->gtk_image = img;
    gtk_container_add(GTK_CONTAINER(overlay), img);

    /* Drag Handle (hidden by default, shown on image activate) */
    GtkWidget *handle_eventbox = gtk_event_box_new();
    data->handle_eventbox = handle_eventbox;
    gtk_widget_set_valign(handle_eventbox, GTK_ALIGN_END);
    gtk_widget_set_halign(handle_eventbox, GTK_ALIGN_END);
    gtk_widget_set_margin_end(handle_eventbox, 4);
    gtk_widget_set_margin_bottom(handle_eventbox, 4);

    GtkWidget *handle_label = gtk_label_new(" \342\206\230 ");
    gtk_widget_set_tooltip_text(handle_eventbox, "Drag to resize image");
    gtk_container_add(GTK_CONTAINER(handle_eventbox), handle_label);

    gtk_widget_add_events(handle_eventbox, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(handle_eventbox, "button-press-event", G_CALLBACK(on_handle_button_press), data);
    g_signal_connect(handle_eventbox, "motion-notify-event", G_CALLBACK(on_handle_motion_notify), data);
    g_signal_connect(handle_eventbox, "button-release-event", G_CALLBACK(on_handle_button_release), data);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), handle_eventbox);
    gtk_container_add(GTK_CONTAINER(img_eventbox), overlay);

    g_object_set_data_full(G_OBJECT(container), "img-data", data, free_image_widget_data);

    gtk_box_pack_start(GTK_BOX(container), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), img_eventbox, FALSE, FALSE, 0);

    gtk_widget_show_all(container);
    /* Toolbar and handle hidden until image is clicked */
    gtk_widget_hide(toolbar);
    gtk_widget_hide(handle_eventbox);
    return container;
}

// Helper to insert image inline at cursor/end
static void insert_image_into_buffer(GdkPixbuf *pixbuf, const char *filename, const char *timestamp_str) {
    if (!notes_textview || !pixbuf) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);

    gtk_text_buffer_insert(buffer, &iter, "\n", -1);

    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, &iter);
    GtkWidget *img_widget = create_resizable_image_widget(pixbuf, filename, 450, "center");
    if (img_widget && anchor) {
        gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(notes_textview), img_widget, anchor);
    }

    char *img_tag = g_strdup_printf("\n<img src=\"scythe://note_image/%s\" width=\"450\" align=\"center\" />\nCaptured: %s\n",
                                    filename ? filename : "snip.png",
                                    timestamp_str ? timestamp_str : "");
    gtk_text_buffer_insert(buffer, &iter, img_tag, -1);
    g_free(img_tag);

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *full_text = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
    push_undo_state(full_text);
    g_free(full_text);
}

// ---- Formatting Helpers ----
static void insert_formatting(const char *prefix, const char *suffix) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    int pre_len = prefix ? (int)strlen(prefix) : 0;
    int suf_len = suffix ? (int)strlen(suffix) : 0;

    GtkTextIter sel_start, sel_end;
    gboolean has_selection = gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end);

    if (!has_selection) {
        /* No selection — insert markers and place cursor between them */
        if (prefix) gtk_text_buffer_insert_at_cursor(buffer, prefix, -1);
        if (suffix) {
            GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
            GtkTextIter ins;
            gtk_text_buffer_get_iter_at_mark(buffer, &ins, mark);
            gtk_text_buffer_insert(buffer, &ins, suffix, -1);
            gtk_text_iter_backward_chars(&ins, (gint)suf_len);
            gtk_text_buffer_place_cursor(buffer, &ins);
        }
        goto push_undo;
    }

    {
        /*
         * Get text WITH hidden chars (TRUE) so we see the actual buffer content
         * including invisible syntax markers like ** ~~ <u> etc.
         */
        char *sel_with_hidden = gtk_text_buffer_get_text(buffer, &sel_start, &sel_end, TRUE);
        if (!sel_with_hidden) goto do_wrap;
        int sel_len = (int)strlen(sel_with_hidden);

        /* ---- Check: text includes markers (visible selection may hide them) ---- */
        if (pre_len > 0 && suf_len > 0 &&
            sel_len >= pre_len + suf_len &&
            strncmp(sel_with_hidden, prefix, pre_len) == 0 &&
            strncmp(sel_with_hidden + sel_len - suf_len, suffix, suf_len) == 0) {
            /* Already wrapped — unwrap by removing prefix/suffix from buffer */
            char *inner = g_strndup(sel_with_hidden + pre_len, sel_len - pre_len - suf_len);
            gtk_text_buffer_delete(buffer, &sel_start, &sel_end);
            gtk_text_buffer_insert(buffer, &sel_start, inner, -1);
            g_free(inner);
            g_free(sel_with_hidden);
            goto push_undo;
        }

        /* ---- Check: markers are OUTSIDE the selection ---- */
        if (pre_len > 0 && suf_len > 0) {
            GtkTextIter before = sel_start;
            GtkTextIter after  = sel_end;
            gboolean can_back = gtk_text_iter_backward_chars(&before, pre_len);
            gboolean can_fwd  = gtk_text_iter_forward_chars(&after, suf_len);
            if (can_back && can_fwd) {
                char *before_txt = gtk_text_buffer_get_text(buffer, &before, &sel_start, TRUE);
                char *after_txt  = gtk_text_buffer_get_text(buffer, &sel_end, &after, TRUE);
                if (before_txt && after_txt &&
                    strcmp(before_txt, prefix) == 0 &&
                    strcmp(after_txt, suffix) == 0) {
                    /* Delete suffix first (preserves before offsets) */
                    gtk_text_buffer_delete(buffer, &sel_end, &after);
                    /* Re-get before iter since buffer mutated */
                    GtkTextIter bs2 = sel_start;
                    gtk_text_iter_backward_chars(&bs2, pre_len);
                    GtkTextIter be2 = bs2;
                    gtk_text_iter_forward_chars(&be2, pre_len);
                    gtk_text_buffer_delete(buffer, &bs2, &be2);
                    g_free(before_txt);
                    g_free(after_txt);
                    g_free(sel_with_hidden);
                    goto push_undo;
                }
                g_free(before_txt);
                g_free(after_txt);
            }
        }
        g_free(sel_with_hidden);
    }

do_wrap:
    {
        /* ---- Not wrapped — apply formatting ---- */
        char *selected = gtk_text_buffer_get_text(buffer, &sel_start, &sel_end, FALSE);
        char *formatted = g_strdup_printf("%s%s%s",
            prefix ? prefix : "",
            selected ? selected : "",
            suffix ? suffix : "");
        gtk_text_buffer_delete(buffer, &sel_start, &sel_end);
        gtk_text_buffer_insert(buffer, &sel_start, formatted, -1);
        g_free(selected);
        g_free(formatted);
    }

push_undo: ;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
    push_undo_state(text);
    g_free(text);
}

static void insert_line_prefix(const char *prefix) {
    if (!notes_textview || !prefix) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);

    GtkTextIter line_start = iter;
    gtk_text_iter_set_line_offset(&line_start, 0);

    gtk_text_buffer_insert(buffer, &line_start, prefix, -1);

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
    push_undo_state(text);
    g_free(text);
}

// ---- Link Insertion Dialog ----
static void show_link_dialog(void) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkWidget *toplevel = gtk_widget_get_toplevel(notes_textview);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Insert Link",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
        GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Insert", GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget *title_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Link Title...");

    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        char *sel = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (sel) {
            gtk_entry_set_text(GTK_ENTRY(title_entry), sel);
            g_free(sel);
        }
    }

    GtkWidget *url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "https://example.com");

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Link Title:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), title_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("URL:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), vbox);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *t = gtk_entry_get_text(GTK_ENTRY(title_entry));
        const char *u = gtk_entry_get_text(GTK_ENTRY(url_entry));

        const char *link_title = (t && strlen(t) > 0) ? t : "Link";
        const char *link_url = (u && strlen(u) > 0) ? u : "https://";

        // URL Sanitization
        char *sanitized_url = g_strdup(link_url);
        g_strstrip(sanitized_url);

        char *md_link = g_strdup_printf("[%s](%s)", link_title, sanitized_url);
        insert_formatting("", md_link);

        g_free(md_link);
        g_free(sanitized_url);
    }
    gtk_widget_destroy(dialog);
}

// ---- Image File Chooser ----
static void show_image_chooser(void) {
    if (!notes_textview) return;
    GtkWidget *toplevel = gtk_widget_get_toplevel(notes_textview);
    GtkWindow *parent_win = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Select Image",
        parent_win,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open",
        "_Cancel"
    );

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/png");
    gtk_file_filter_add_mime_type(filter, "image/jpeg");
    gtk_file_filter_add_mime_type(filter, "image/gif");
    gtk_file_filter_add_mime_type(filter, "image/webp");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (filepath && g_file_test(filepath, G_FILE_TEST_EXISTS)) {
            GError *err = NULL;
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filepath, &err);
            if (pixbuf) {
                char *basename = g_path_get_basename(filepath);
                char *clean_name = sanitize_filename(basename);
                g_free(basename);

                // Save to note_images dir and DB
                char dst_path[1024];
                snprintf(dst_path, sizeof(dst_path), "%s/%s", get_note_images_dir(), clean_name);
                gdk_pixbuf_save(pixbuf, dst_path, "png", NULL, NULL);

                gsize len = 0;
                char *data = NULL;
                if (g_file_get_contents(dst_path, &data, &len, NULL)) {
                    if (current_note_id == -1) save_current_note();
                    if (current_note_id != -1) {
                        note_save_image(current_note_id, clean_name, data, len);
                    }
                    g_free(data);
                }

                GDateTime *now = g_date_time_new_now_local();
                char *time_str = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");

                insert_image_into_buffer(pixbuf, clean_name, time_str);

                g_free(time_str);
                g_date_time_unref(now);
                g_free(clean_name);
                g_object_unref(pixbuf);
                show_status_message("Image inserted", FALSE);
            } else {
                show_status_message("Failed to load image", TRUE);
                if (err) g_error_free(err);
            }
            g_free(filepath);
        }
    }
    g_object_unref(native);
}

// ---- Clipboard Image Paste Handler ----
static void check_and_paste_image_from_clipboard(void) {
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!clipboard) return;

    if (gtk_clipboard_wait_is_image_available(clipboard)) {
        GdkPixbuf *pixbuf = gtk_clipboard_wait_for_image(clipboard);
        if (pixbuf) {
            GDateTime *now = g_date_time_new_now_local();
            char *file_timestamp = g_date_time_format(now, "%Y%m%d_%H%M%S");
            char *display_timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");

            char filename[128];
            snprintf(filename, sizeof(filename), "paste_%s.png", file_timestamp);

            char dest_path[512];
            snprintf(dest_path, sizeof(dest_path), "%s/%s", get_note_images_dir(), filename);

            GError *err = NULL;
            gdk_pixbuf_save(pixbuf, dest_path, "png", &err, NULL);

            if (!err) {
                gsize len = 0;
                char *data = NULL;
                if (g_file_get_contents(dest_path, &data, &len, NULL)) {
                    if (current_note_id == -1) save_current_note();
                    if (current_note_id != -1) {
                        note_save_image(current_note_id, filename, data, len);
                    }
                    g_free(data);
                }

                insert_image_into_buffer(pixbuf, filename, display_timestamp);
                show_status_message("Pasted image attached!", FALSE);
            }
            g_free(file_timestamp);
            g_free(display_timestamp);
            g_date_time_unref(now);
            g_object_unref(pixbuf);
        }
    }
}

// ---- Undo / Redo Actions ----
static void do_undo(void) {
    if (!undo_stack || undo_stack->len <= 1 || !notes_textview) return;

    in_undo_redo = TRUE;

    char *curr = g_ptr_array_remove_index(undo_stack, undo_stack->len - 1);
    if (!redo_stack) redo_stack = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(redo_stack, curr);

    const char *prev = g_ptr_array_index(undo_stack, undo_stack->len - 1);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (buffer) {
        gtk_text_buffer_set_text(buffer, prev ? prev : "", -1);
        apply_markdown_tags(buffer);
    }

    show_status_message("Undo", FALSE);
    in_undo_redo = FALSE;
}

static void do_redo(void) {
    if (!redo_stack || redo_stack->len == 0 || !notes_textview) return;

    in_undo_redo = TRUE;

    char *next = g_ptr_array_remove_index(redo_stack, redo_stack->len - 1);
    if (!undo_stack) undo_stack = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(undo_stack, g_strdup(next));

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (buffer) {
        gtk_text_buffer_set_text(buffer, next ? next : "", -1);
        apply_markdown_tags(buffer);
    }

    g_free(next);
    show_status_message("Redo", FALSE);
    in_undo_redo = FALSE;
}

// ---- Note Persistence ----
static void parse_and_render_images_in_buffer(GtkTextBuffer *buffer) {
    if (!buffer) return;

    GtkTextIter search_iter;
    gtk_text_buffer_get_start_iter(buffer, &search_iter);

    GtkTextIter match_start, match_end;
    while (gtk_text_iter_forward_search(&search_iter, "scythe://note_image/",
                                        GTK_TEXT_SEARCH_TEXT_ONLY,
                                        &match_start, &match_end, NULL)) {
        /* Find filename right after match_end */
        GtkTextIter fn_end = match_end;
        while (!gtk_text_iter_is_end(&fn_end)) {
            gunichar ch = gtk_text_iter_get_char(&fn_end);
            if (ch == '"' || ch == ')' || ch == '>' || ch == ' ' || ch == '\n' || ch == '#') break;
            gtk_text_iter_forward_char(&fn_end);
        }

        char *filename = gtk_text_buffer_get_text(buffer, &match_end, &fn_end, FALSE);
        if (filename && strlen(filename) > 0 && strlen(filename) < 128) {
            char *clean_fn = g_strdup(filename);
            char *hash = strchr(clean_fn, '#');
            if (hash) *hash = '\0';
            char *quote = strchr(clean_fn, '"');
            if (quote) *quote = '\0';

            int req_w = 450;
            char *req_align = NULL;

            if (current_note_id != -1) {
                note_get_image_metadata(current_note_id, clean_fn, &req_w, &req_align);
            }
            if (req_w <= 0) req_w = 450;
            if (!req_align) req_align = g_strdup("center");

            char disk_path[512];
            snprintf(disk_path, sizeof(disk_path), "%s/%s", get_note_images_dir(), clean_fn);

            GdkPixbuf *pixbuf = NULL;
            if (g_file_test(disk_path, G_FILE_TEST_EXISTS)) {
                pixbuf = gdk_pixbuf_new_from_file(disk_path, NULL);
            } else if (current_note_id != -1) {
                void *db_data = NULL;
                int db_size = 0;
                if (note_get_image(current_note_id, clean_fn, &db_data, &db_size)) {
                    GInputStream *stream = g_memory_input_stream_new_from_data(db_data, db_size, g_free);
                    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, NULL);
                    g_object_unref(stream);
                    gdk_pixbuf_save(pixbuf, disk_path, "png", NULL, NULL);
                }
            }

            if (pixbuf) {
                /* Walk back to tag opening '<' or use match_start */
                GtkTextIter tag_anchor_pos = match_start;
                while (!gtk_text_iter_is_start(&tag_anchor_pos)) {
                    gtk_text_iter_backward_char(&tag_anchor_pos);
                    if (gtk_text_iter_get_char(&tag_anchor_pos) == '<') break;
                }

                /* Check if an anchor already exists at this iter */
                if (!gtk_text_iter_get_child_anchor(&tag_anchor_pos)) {
                    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, &tag_anchor_pos);
                    GtkWidget *widget = create_resizable_image_widget(pixbuf, clean_fn, req_w, req_align);
                    if (widget && anchor && notes_textview) {
                        gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(notes_textview), widget, anchor);
                    }
                }
                g_object_unref(pixbuf);
            }

            g_free(clean_fn);
            if (req_align) g_free(req_align);
            g_free(filename);
        }
        search_iter = fn_end;
    }
}

static void load_note_into_editor(int note_id) {
    if (note_id == -1 || !notes_textview) return;

    char *content = note_get_content(note_id);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    g_signal_handlers_block_matched(buffer, G_SIGNAL_MATCH_FUNC,
        0, 0, NULL, G_CALLBACK(schedule_render), NULL);

    if (content) {
        gtk_text_buffer_set_text(buffer, content, -1);
        g_free(content);
    } else {
        gtk_text_buffer_set_text(buffer, "", -1);
    }

    current_note_id = note_id;
    current_edit_line = -1;
    clear_undo_redo_stacks();

    parse_and_render_images_in_buffer(buffer);

    g_signal_handlers_unblock_matched(buffer, G_SIGNAL_MATCH_FUNC,
        0, 0, NULL, G_CALLBACK(schedule_render), NULL);

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
    push_undo_state(text);
    g_free(text);

    apply_markdown_tags(buffer);
}

static void save_current_note(void) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    if (current_note_id != -1) {
        note_update_content(current_note_id, text);
        show_status_message("Autosaved", FALSE);
    } else if (text && strlen(text) > 0) {
        int session_id = session_get_current();
        if (session_id != -1) {
            char title[64];
            GDateTime *now = g_date_time_new_now_local();
            char *time_str = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
            snprintf(title, sizeof(title), "Note %s", time_str ? time_str : "");
            if (time_str) g_free(time_str);
            if (now) g_date_time_unref(now);
            current_note_id = note_create(session_id, title);
            if (current_note_id != -1) {
                note_update_content(current_note_id, text);
                show_status_message("Note created & saved", FALSE);
            }
        }
    }
    g_free(text);
}

static void refresh_notes_list(void) {
    int session_id = session_get_current();
    if (session_id == -1) return;

    if (notes_combo) {
        g_signal_handlers_block_by_func(notes_combo, G_CALLBACK(on_note_selected), NULL);
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(notes_combo));
    }

    GPtrArray *notes = note_list_by_session(session_id);
    if (notes && notes->len > 0) {
        for (guint i = 0; i < notes->len; i++) {
            NoteInfo *info = g_ptr_array_index(notes, i);
            if (info) {
                char id_str[32];
                snprintf(id_str, sizeof(id_str), "%d", info->id);
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(notes_combo), id_str, info->title ? info->title : "Untitled Note");
            }
        }
        if (current_note_id == -1) {
            NoteInfo *first = g_ptr_array_index(notes, 0);
            if (first) {
                char id_str[32];
                snprintf(id_str, sizeof(id_str), "%d", first->id);
                gtk_combo_box_set_active_id(GTK_COMBO_BOX(notes_combo), id_str);
                load_note_into_editor(first->id);
            }
        } else {
            char id_str[32];
            snprintf(id_str, sizeof(id_str), "%d", current_note_id);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(notes_combo), id_str);
        }
        g_ptr_array_free(notes, TRUE);
    } else {
        current_note_id = -1;
        if (notes_textview) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
            if (buffer) gtk_text_buffer_set_text(buffer, "", -1);
        }
    }

    if (notes_combo) {
        g_signal_handlers_unblock_by_func(notes_combo, G_CALLBACK(on_note_selected), NULL);
    }
}

void notes_refresh(void) {
    if (notes_combo && notes_textview) {
        save_current_note();
        current_note_id = -1;
        refresh_notes_list();
    }
}

static void on_snip_clicked(GtkButton *button, gpointer user_data);

void notes_trigger_snip(void) {
    on_snip_clicked(NULL, NULL);
}

void notes_toggle_pin(void) {
    if (!g_pin_toggle_btn) return;
    gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_pin_toggle_btn));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_pin_toggle_btn), !active);
}

static void on_popover_map(GtkWidget *widget, gpointer user_data) {
    if (notes_textview) {
        gtk_widget_grab_focus(notes_textview);
    }
}

void notes_toggle(void) {
    if (!notes_popover) return;
    if (gtk_widget_get_visible(notes_popover)) {
        if (is_notes_pinned) {
            notes_toggle_pin();
        }
        gtk_popover_popdown(GTK_POPOVER(notes_popover));
    } else {
        refresh_notes_list();
        gtk_popover_popup(GTK_POPOVER(notes_popover));
        if (notes_textview) gtk_widget_grab_focus(notes_textview);
    }
}

static void on_note_toggled(GtkToggleButton *button, gpointer user_data) {
    if (gtk_toggle_button_get_active(button)) {
        if (!gtk_widget_get_visible(notes_popover)) {
            refresh_notes_list();
            gtk_popover_popup(GTK_POPOVER(notes_popover));
        }
        if (notes_textview) gtk_widget_grab_focus(notes_textview);
    } else {
        if (is_notes_pinned) {
            notes_toggle_pin();
        }
        gtk_popover_popdown(GTK_POPOVER(notes_popover));
    }
}

void close_notes_if_unpinned(void) {
    if (notes_popover && gtk_widget_get_visible(notes_popover) && !is_notes_pinned) {
        gtk_popover_popdown(GTK_POPOVER(notes_popover));
    }
}

gboolean notes_handle_window_click(GdkEventButton *event) {
    if (!event || !notes_popover) return FALSE;
    if (gtk_widget_get_visible(notes_popover) && !is_notes_pinned) {
        GtkWidget *target = gtk_get_event_widget((GdkEvent *)event);
        if (target && target != notes_popover && !gtk_widget_is_ancestor(target, notes_popover) && target != g_note_toggle_btn) {
            close_notes_if_unpinned();
        }
    }
    return FALSE;
}

static void on_pin_toggled(GtkToggleButton *button, gpointer user_data) {
    is_notes_pinned = gtk_toggle_button_get_active(button);
    if (notes_popover) {
        gtk_popover_set_modal(GTK_POPOVER(notes_popover), FALSE);
    }
    /* Color-only state indicator: red fill = locked, default = unlocked */
    if (g_pin_toggle_btn) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(g_pin_toggle_btn));
        if (is_notes_pinned) {
            gtk_style_context_add_class(ctx, "pin-active");
        } else {
            gtk_style_context_remove_class(ctx, "pin-active");
        }
    }
}

static void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    save_current_note();
    if (!is_notes_pinned && g_note_toggle_btn) {
        g_signal_handlers_block_by_func(g_note_toggle_btn, G_CALLBACK(on_note_toggled), NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_note_toggle_btn), FALSE);
        g_signal_handlers_unblock_by_func(g_note_toggle_btn, G_CALLBACK(on_note_toggled), NULL);
    }
}

static void on_note_selected(GtkComboBox *combo, gpointer user_data) {
    const char *id_str = gtk_combo_box_get_active_id(combo);
    if (id_str) {
        int selected_id = atoi(id_str);
        if (selected_id != current_note_id) {
            save_current_note();
            load_note_into_editor(selected_id);
        }
    }
}

static void on_new_note_clicked(GtkButton *button, gpointer user_data) {
    int session_id = session_get_current();
    if (session_id == -1) return;

    save_current_note();

    char title[64];
    GDateTime *now = g_date_time_new_now_local();
    char *time_str = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    snprintf(title, sizeof(title), "Note %s", time_str ? time_str : "");
    if (time_str) g_free(time_str);
    if (now) g_date_time_unref(now);

    int new_id = note_create(session_id, title);
    if (new_id != -1) {
        current_note_id = new_id;
        refresh_notes_list();
        load_note_into_editor(new_id);
    }
}

static void on_timestamp_clicked(GtkButton *button, gpointer user_data) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GDateTime *now = g_date_time_new_now_local();
    char *time_str = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    char *insert_text = g_strdup_printf("\n---\n**[%s]**\n", time_str ? time_str : "");
    gtk_text_buffer_insert_at_cursor(buffer, insert_text, -1);

    g_free(insert_text);
    if (time_str) g_free(time_str);
    if (now) g_date_time_unref(now);
}

// ---- Find & Replace ----
static void on_find_next_clicked(GtkButton *button, gpointer user_data) {
    if (!search_entry || !notes_textview) return;
    const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (!query || strlen(query) == 0) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter start;
    gtk_text_buffer_get_iter_at_mark(buffer, &start, mark);

    GtkTextIter match_start, match_end;
    gboolean found = gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &match_start, &match_end, NULL);
    if (!found) {
        gtk_text_buffer_get_start_iter(buffer, &start);
        found = gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &match_start, &match_end, NULL);
    }

    if (found) {
        gtk_text_buffer_select_range(buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(notes_textview), &match_start, 0.1, FALSE, 0, 0);
        show_status_message("Match found", FALSE);
    } else {
        show_status_message("No match found", TRUE);
    }
}

static void on_replace_clicked(GtkButton *button, gpointer user_data) {
    if (!search_entry || !replace_entry || !notes_textview) return;
    const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));
    const char *replacement = gtk_entry_get_text(GTK_ENTRY(replace_entry));
    if (!query || strlen(query) == 0) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end)) {
        char *sel_text = gtk_text_buffer_get_text(buffer, &sel_start, &sel_end, FALSE);
        if (sel_text && g_ascii_strcasecmp(sel_text, query) == 0) {
            gtk_text_buffer_delete(buffer, &sel_start, &sel_end);
            gtk_text_buffer_insert(buffer, &sel_start, replacement ? replacement : "", -1);
            show_status_message("Replaced 1 instance", FALSE);
        }
        if (sel_text) g_free(sel_text);
    }
    on_find_next_clicked(NULL, NULL);
}

static void on_replace_all_clicked(GtkButton *button, gpointer user_data) {
    if (!search_entry || !replace_entry || !notes_textview) return;
    const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));
    const char *replacement = gtk_entry_get_text(GTK_ENTRY(replace_entry));
    if (!query || strlen(query) == 0) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);

    int count = 0;
    GtkTextIter m_start, m_end;
    while (gtk_text_iter_forward_search(&iter, query, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &m_start, &m_end, NULL)) {
        gtk_text_buffer_delete(buffer, &m_start, &m_end);
        gtk_text_buffer_insert(buffer, &m_start, replacement ? replacement : "", -1);
        count++;
        iter = m_start;
    }

    char status[64];
    snprintf(status, sizeof(status), "Replaced %d instances", count);
    show_status_message(status, count == 0);
}

static void toggle_find_replace_bar(void) {
    if (!find_replace_bar) return;
    gboolean visible = gtk_widget_get_visible(find_replace_bar);
    gtk_widget_set_visible(find_replace_bar, !visible);
    if (!visible && search_entry) {
        gtk_widget_grab_focus(search_entry);
    }
}

// ---- List Auto-Continue (Obsidian-style Enter key) ----
static gboolean handle_list_enter(GtkTextBuffer *buffer) {
    if (!buffer) return FALSE;

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);

    GtkTextIter line_start = iter;
    gtk_text_iter_set_line_offset(&line_start, 0);
    GtkTextIter line_end = line_start;
    if (!gtk_text_iter_ends_line(&line_end))
        gtk_text_iter_forward_to_line_end(&line_end);

    char *line = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
    if (!line) return FALSE;

    const char *prefix = NULL;
    char num_prefix[32] = {0};
    gboolean is_empty_item = FALSE;

    if (g_str_has_prefix(line, "- [x] ") || g_str_has_prefix(line, "- [X] ") ||
        g_str_has_prefix(line, "- [ ] ")) {
        is_empty_item = (line[6] == '\0');
        prefix = "- [ ] ";
    } else if (g_str_has_prefix(line, "- ") || g_str_has_prefix(line, "* ")) {
        char pfx[3] = { line[0], ' ', '\0' };
        is_empty_item = (line[2] == '\0');
        snprintf(num_prefix, sizeof(num_prefix), "%s", pfx);
        prefix = num_prefix;
    } else {
        // Check numbered list: ^\d+\. 
        int li = 0;
        while (line[li] && g_ascii_isdigit((unsigned char)line[li])) li++;
        if (li > 0 && line[li] == '.' && line[li + 1] == ' ') {
            int num = atoi(line);
            is_empty_item = (line[li + 2] == '\0');
            snprintf(num_prefix, sizeof(num_prefix), "%d. ", num + 1);
            prefix = num_prefix;
        }
    }

    if (!prefix) { g_free(line); return FALSE; }

    if (is_empty_item) {
        // Exit list: delete empty bullet and insert bare newline
        gtk_text_buffer_delete(buffer, &line_start, &line_end);
        gtk_text_buffer_insert_at_cursor(buffer, "\n", -1);
    } else {
        char *new_line = g_strdup_printf("\n%s", prefix);
        gtk_text_buffer_insert_at_cursor(buffer, new_line, -1);
        g_free(new_line);
    }

    g_free(line);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *full = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
    push_undo_state(full);
    g_free(full);
    return TRUE;
}

// ---- Clickable Link Handler ----
static gboolean on_textview_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (!widget || event->button != 1) return FALSE;
    if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) return FALSE;

    GtkTextView *tv = GTK_TEXT_VIEW(widget);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tv);
    if (!buffer) return FALSE;

    if (gtk_text_buffer_get_has_selection(buffer)) return FALSE;

    int bx, by;
    gtk_text_view_window_to_buffer_coords(tv, GTK_TEXT_WINDOW_TEXT,
        (int)event->x, (int)event->y, &bx, &by);

    GtkTextIter click_iter;
    gtk_text_view_get_iter_at_location(tv, &click_iter, bx, by);

    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *link_tag = gtk_text_tag_table_lookup(table, "md-link");
    if (!link_tag || !gtk_text_iter_has_tag(&click_iter, link_tag)) return FALSE;

    /*
     * Extract URL from the full line text (including hidden chars) using
     * string matching. This avoids issues with md-syntax-hidden making
     * ](url) invisible at the iterator level.
     */
    GtkTextIter line_s = click_iter, line_e = click_iter;
    gtk_text_iter_set_line_offset(&line_s, 0);
    if (!gtk_text_iter_ends_line(&line_e))
        gtk_text_iter_forward_to_line_end(&line_e);
    /* include_hidden_chars=TRUE to get full raw text */
    char *full_line = gtk_text_buffer_get_text(buffer, &line_s, &line_e, TRUE);
    if (!full_line) return FALSE;

    /* Find [text](url) pattern in line */
    const char *lp = full_line;
    /* Get link text from visible selection to match the right link */
    GtkTextIter tag_start = click_iter, tag_end = click_iter;
    gtk_text_iter_backward_to_tag_toggle(&tag_start, link_tag);
    gtk_text_iter_forward_to_tag_toggle(&tag_end, link_tag);
    char *link_text = gtk_text_buffer_get_text(buffer, &tag_start, &tag_end, FALSE);

    gboolean launched = FALSE;
    if (link_text) {
        char *search_pat = g_strdup_printf("[%s](", link_text);
        char *found = strstr(lp, search_pat);
        if (found) {
            const char *url_s = found + strlen(search_pat);
            const char *url_e = strchr(url_s, ')');
            if (url_e && url_e > url_s && (url_e - url_s) < 2048) {
                char *url = g_strndup(url_s, (gsize)(url_e - url_s));
                g_strstrip(url);
                char *full_url = url;
                if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://") &&
                    !g_str_has_prefix(url, "ftp://") && !g_str_has_prefix(url, "mailto:")) {
                    full_url = g_strdup_printf("https://%s", url);
                }
                GError *err = NULL;
                g_app_info_launch_default_for_uri(full_url, NULL, &err);
                if (err) {
                    g_error_free(err);
                } else {
                    launched = TRUE;
                }
                if (full_url != url) g_free(full_url);
                g_free(url);
            }
        }
        g_free(search_pat);
        g_free(link_text);
    }
    g_free(full_line);
    return launched;
}

// ---- Keyboard Shortcuts Handler ----
static gboolean on_editor_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    if (!event) return FALSE;
    guint state = event->state & gtk_accelerator_get_default_mod_mask();

    // Plain Enter: Obsidian-style list auto-continue
    if (state == 0 && (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        if (handle_list_enter(buffer)) return TRUE;
        deactivate_current_image();
        return FALSE;
    }

    // Escape key closes unpinned notes popover
    if (state == 0 && event->keyval == GDK_KEY_Escape) {
        if (!is_notes_pinned && notes_popover) {
            gtk_popover_popdown(GTK_POPOVER(notes_popover));
            return TRUE;
        }
    }

    // Any plain keypress deactivates active image toolbar
    if (state == 0) deactivate_current_image();

    if (state == GDK_CONTROL_MASK) {
        switch (event->keyval) {
            case GDK_KEY_b:
            case GDK_KEY_B:
                insert_formatting("**", "**");
                return TRUE;
            case GDK_KEY_i:
            case GDK_KEY_I:
                insert_formatting("*", "*");
                return TRUE;
            case GDK_KEY_u:
            case GDK_KEY_U:
                insert_formatting("<u>", "</u>");
                return TRUE;
            case GDK_KEY_k:
            case GDK_KEY_K:
                show_link_dialog();
                return TRUE;
            case GDK_KEY_f:
            case GDK_KEY_F:
                toggle_find_replace_bar();
                return TRUE;
            case GDK_KEY_s:
            case GDK_KEY_S:
                save_current_note();
                return TRUE;
            case GDK_KEY_z:
            case GDK_KEY_Z:
                do_undo();
                return TRUE;
            case GDK_KEY_y:
            case GDK_KEY_Y:
                do_redo();
                return TRUE;
            case GDK_KEY_v:
            case GDK_KEY_V:
                check_and_paste_image_from_clipboard();
                return FALSE;
        }
    } else if (state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_Z:
            case GDK_KEY_z:
                do_redo();
                return TRUE;
            case GDK_KEY_X:
            case GDK_KEY_x:
                insert_formatting("~~", "~~");
                return TRUE;
        }
    }

    return FALSE;
}

// ---- Formatting Toolbar Callbacks (Bold/Italic/Underline/Strike/H1/H2/H3 are keyboard-only) ----
static void on_btn_bullet(GtkButton *btn, gpointer data) { insert_line_prefix("- "); }
static void on_btn_numlist(GtkButton *btn, gpointer data) { insert_line_prefix("1. "); }
static void on_btn_checklist(GtkButton *btn, gpointer data) { insert_line_prefix("- [ ] "); }
static void on_btn_quote(GtkButton *btn, gpointer data) { insert_line_prefix("> "); }
static void on_btn_codeblock(GtkButton *btn, gpointer data) { insert_formatting("\n```\n", "\n```\n"); }
static void on_btn_table(GtkButton *btn, gpointer data) {
    insert_formatting("", "\n| Column 1 | Column 2 |\n| --- | --- |\n| Item 1 | Item 2 |\n");
}
static void on_btn_link(GtkButton *btn, gpointer data) { show_link_dialog(); }
static void on_btn_image(GtkButton *btn, gpointer data) { show_image_chooser(); }
static void on_btn_undo(GtkButton *btn, gpointer data) { do_undo(); }
static void on_btn_redo(GtkButton *btn, gpointer data) { do_redo(); }
static void on_btn_find(GtkButton *btn, gpointer data) { toggle_find_replace_bar(); }

// ---- Native GDK Rubber-Band Screenshot ----
typedef struct {
    GtkWidget *overlay_window;
    gboolean selecting;
    int start_x, start_y;
    int end_x, end_y;
} SnipData;

static gboolean on_snip_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    SnipData *data = (SnipData *)user_data;
    if (!data || !cr) return TRUE;

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_paint(cr);

    if (data->selecting) {
        int x = MIN(data->start_x, data->end_x);
        int y = MIN(data->start_y, data->end_y);
        int w = abs(data->end_x - data->start_x);
        int h = abs(data->end_y - data->start_y);

        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);

        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr, 0.15, 0.39, 0.92, 0.9);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
    }

    return TRUE;
}

static gboolean on_snip_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    SnipData *data = (SnipData *)user_data;
    if (data && event && event->button == 1) {
        data->selecting = TRUE;
        data->start_x = (int)event->x;
        data->start_y = (int)event->y;
        data->end_x = data->start_x;
        data->end_y = data->start_y;
    }
    return TRUE;
}

static gboolean on_snip_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    SnipData *data = (SnipData *)user_data;
    if (data && data->selecting && event) {
        data->end_x = (int)event->x;
        data->end_y = (int)event->y;
        if (widget) gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean on_snip_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    SnipData *data = (SnipData *)user_data;
    if (data && event && event->button == 1 && data->selecting) {
        data->selecting = FALSE;
        data->end_x = (int)event->x;
        data->end_y = (int)event->y;
        if (data->overlay_window) gtk_widget_destroy(data->overlay_window);
        gtk_main_quit();
    }
    return TRUE;
}

static gboolean on_snip_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    SnipData *data = (SnipData *)user_data;
    if (data && event && event->keyval == GDK_KEY_Escape) {
        data->start_x = data->start_y = data->end_x = data->end_y = 0;
        data->selecting = FALSE;
        if (data->overlay_window) gtk_widget_destroy(data->overlay_window);
        gtk_main_quit();
    }
    return TRUE;
}

static void on_snip_clicked(GtkButton *button, gpointer user_data) {
    if (notes_popover) gtk_widget_hide(notes_popover);

    while (gtk_events_pending()) gtk_main_iteration();
    g_usleep(200000); // 200ms pause for popover unmap

    SnipData data = { NULL, FALSE, 0, 0, 0, 0 };

    GdkScreen *screen = gdk_screen_get_default();
    if (!screen) {
        show_status_message("No display screen found", TRUE);
        return;
    }

    GdkDisplay *display = gdk_screen_get_display(screen);
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (display && !monitor) monitor = gdk_display_get_monitor(display, 0);

    GdkRectangle geom = {0, 0, 1920, 1080};
    if (monitor) gdk_monitor_get_geometry(monitor, &geom);

    GtkWidget *overlay = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    data.overlay_window = overlay;
    gtk_window_set_decorated(GTK_WINDOW(overlay), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(overlay), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(overlay), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(overlay), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(overlay), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_widget_set_app_paintable(overlay, TRUE);

    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(overlay, visual);
    }

    gtk_window_move(GTK_WINDOW(overlay), geom.x, geom.y);
    gtk_window_resize(GTK_WINDOW(overlay), geom.width, geom.height);
    gtk_window_fullscreen(GTK_WINDOW(overlay));

    gtk_widget_add_events(overlay, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);

    GdkCursor *crosshair = display ? gdk_cursor_new_for_display(display, GDK_CROSSHAIR) : NULL;

    g_signal_connect(overlay, "draw", G_CALLBACK(on_snip_draw), &data);
    g_signal_connect(overlay, "button-press-event", G_CALLBACK(on_snip_button_press), &data);
    g_signal_connect(overlay, "motion-notify-event", G_CALLBACK(on_snip_motion), &data);
    g_signal_connect(overlay, "button-release-event", G_CALLBACK(on_snip_button_release), &data);
    g_signal_connect(overlay, "key-press-event", G_CALLBACK(on_snip_key_press), &data);

    gtk_widget_show(overlay);

    GdkWindow *gdk_win = gtk_widget_get_window(overlay);
    if (gdk_win && crosshair) {
        gdk_window_set_cursor(gdk_win, crosshair);
    }

    gtk_main();
    if (crosshair) g_object_unref(crosshair);

    int x = MIN(data.start_x, data.end_x);
    int y = MIN(data.start_y, data.end_y);
    int w = abs(data.end_x - data.start_x);
    int h = abs(data.end_y - data.start_y);

    if (w > 5 && h > 5) {
        GdkWindow *root = gdk_screen_get_root_window(screen);
        GdkPixbuf *pixbuf = root ? gdk_pixbuf_get_from_window(root, x + geom.x, y + geom.y, w, h) : NULL;

        if (pixbuf) {
            GDateTime *now = g_date_time_new_now_local();
            char *file_timestamp = g_date_time_format(now, "%Y%m%d_%H%M%S");
            char *display_timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");

            char filename[128];
            snprintf(filename, sizeof(filename), "snip_%s.png", file_timestamp ? file_timestamp : "shot");

            char dest_path[512];
            snprintf(dest_path, sizeof(dest_path), "%s/%s", get_note_images_dir(), filename);

            GError *error = NULL;
            gdk_pixbuf_save(pixbuf, dest_path, "png", &error, NULL);

            if (!error) {
                gsize length = 0;
                char *contents = NULL;
                if (g_file_get_contents(dest_path, &contents, &length, NULL)) {
                    if (current_note_id == -1) save_current_note();
                    if (current_note_id != -1) {
                        note_save_image(current_note_id, filename, contents, length);
                    }
                    if (contents) g_free(contents);
                }

                insert_image_into_buffer(pixbuf, filename, display_timestamp);
                show_status_message("Screenshot captured & attached!", FALSE);
            } else {
                show_status_message("Snip save failed", TRUE);
                g_error_free(error);
            }

            if (file_timestamp) g_free(file_timestamp);
            if (display_timestamp) g_free(display_timestamp);
            if (now) g_date_time_unref(now);
            g_object_unref(pixbuf);
        } else {
            show_status_message("Capture failed", TRUE);
        }
    } else {
        show_status_message("Snip cancelled", TRUE);
    }

    if (notes_popover) gtk_popover_popup(GTK_POPOVER(notes_popover));
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer user_data) {
    if (!buffer) return;
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);
    int new_line = gtk_text_iter_get_line(&iter);
    if (new_line != current_edit_line) {
        current_edit_line = new_line;
        apply_markdown_tags(buffer);
    }
}

static void on_buffer_changed_render(GtkTextBuffer *buffer, gpointer user_data) {
    if (!buffer) return;
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);
    current_edit_line = gtk_text_iter_get_line(&iter);
    schedule_render(buffer);
}

/* ---- Resize Grip State ---- */
static GtkWidget *g_scrolled_window = NULL;
static gboolean resize_dragging = FALSE;
static double resize_start_x = 0, resize_start_y = 0;
static int resize_start_w = 0, resize_start_h = 0;

static gboolean on_resize_grip_press(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    if (ev->button == 1) {
        resize_dragging = TRUE;
        resize_start_x = ev->x_root;
        resize_start_y = ev->y_root;
        if (g_scrolled_window) {
            gtk_widget_get_size_request(g_scrolled_window, &resize_start_w, &resize_start_h);
            if (resize_start_w < 0) resize_start_w = 670;
            if (resize_start_h < 0) resize_start_h = 370;
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_resize_grip_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data) {
    if (!resize_dragging || !g_scrolled_window) return FALSE;
    /* Drag handle at bottom-left: dragging left expands width, dragging down expands height */
    double dx = resize_start_x - ev->x_root; /* inverted: drag left = wider */
    double dy = ev->y_root - resize_start_y;
    int new_w = (int)(resize_start_w + dx);
    int new_h = (int)(resize_start_h + dy);
    if (new_w < 400) new_w = 400;
    if (new_w > 1200) new_w = 1200;
    if (new_h < 200) new_h = 200;
    if (new_h > 900) new_h = 900;
    gtk_widget_set_size_request(g_scrolled_window, new_w, new_h);
    return TRUE;
}

static gboolean on_resize_grip_release(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    if (ev->button == 1) {
        resize_dragging = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* ---- Export Functions ---- */
static void copy_file_simple(const char *src, const char *dst) {
    char *contents = NULL;
    gsize length = 0;
    if (g_file_get_contents(src, &contents, &length, NULL)) {
        g_file_set_contents(dst, contents, (gssize)length, NULL);
        g_free(contents);
    }
}

static void export_as_markdown(void) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(buffer, &s, &e, TRUE);
    if (!text || strlen(text) == 0) {
        g_free(text);
        return;
    }

    GtkWidget *toplevel = gtk_widget_get_toplevel(notes_textview);
    GtkWindow *parent_win = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Export as Markdown",
        parent_win,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Save",
        "_Cancel"
    );
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), "note.md");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Markdown files (*.md)");
    gtk_file_filter_add_pattern(filter, "*.md");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (path) {
            char *target_dir = g_path_get_dirname(path);
            GString *out = g_string_new("");
            char **lines = g_strsplit(text, "\n", -1);

            for (int i = 0; lines && lines[i]; i++) {
                const char *line = lines[i];
                if (g_str_has_prefix(line, "<img src=\"scythe://note_image/")) {
                    const char *fn_start = line + strlen("<img src=\"scythe://note_image/");
                    const char *fn_end = strchr(fn_start, '"');
                    if (fn_end && fn_end > fn_start) {
                        char *fn = g_strndup(fn_start, fn_end - fn_start);
                        char src_p[512], dst_p[512];
                        snprintf(src_p, sizeof(src_p), "%s/%s", get_note_images_dir(), fn);
                        snprintf(dst_p, sizeof(dst_p), "%s/%s", target_dir, fn);
                        copy_file_simple(src_p, dst_p);

                        g_string_append_printf(out, "![image](%s)\n", fn);
                        g_free(fn);
                    } else {
                        g_string_append_printf(out, "%s\n", line);
                    }
                } else {
                    g_string_append_printf(out, "%s\n", line);
                }
            }
            g_strfreev(lines);
            g_free(target_dir);

            GError *err = NULL;
            g_file_set_contents(path, out->str, -1, &err);
            if (err) g_error_free(err);
            g_string_free(out, TRUE);
            g_free(path);
        }
    }
    g_object_unref(native);
    g_free(text);
}

static void export_as_pdf(void) {
    if (!notes_textview) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    if (!buffer) return;

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(buffer, &s, &e, TRUE);
    if (!text || strlen(text) == 0) {
        g_free(text);
        return;
    }

    GtkWidget *toplevel = gtk_widget_get_toplevel(notes_textview);
    GtkWindow *parent_win = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Export as PDF",
        parent_win,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Save",
        "_Cancel"
    );
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), "note.pdf");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PDF files (*.pdf)");
    gtk_file_filter_add_pattern(filter, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (path) {
            HPDF_Doc pdf = HPDF_New(NULL, NULL);
            if (pdf) {
                HPDF_Page page = HPDF_AddPage(pdf);
                HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                HPDF_Font font = HPDF_GetFont(pdf, "Helvetica", NULL);
                HPDF_Page_SetFontAndSize(page, font, 11);

                float page_w = HPDF_Page_GetWidth(page);
                float page_h = HPDF_Page_GetHeight(page);
                float margin = 50.0f;
                float y = page_h - margin;
                float line_height = 14.0f;
                float max_w = page_w - 2 * margin;

                char **lines = g_strsplit(text, "\n", -1);
                for (int i = 0; lines && lines[i]; i++) {
                    const char *line = lines[i];

                    /* ---- Image tag handling ---- */
                    if (g_str_has_prefix(line, "<img src=\"scythe://note_image/")) {
                        const char *fn_start = line + strlen("<img src=\"scythe://note_image/");
                        const char *fn_end = strchr(fn_start, '"');
                        if (fn_end && fn_end > fn_start) {
                            char *fn = g_strndup(fn_start, fn_end - fn_start);
                            char img_path[512];
                            snprintf(img_path, sizeof(img_path), "%s/%s", get_note_images_dir(), fn);
                            g_free(fn);

                            if (g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                                HPDF_Image img = HPDF_LoadPngImageFromFile(pdf, img_path);
                                if (img) {
                                    float iw = HPDF_Image_GetWidth(img);
                                    float ih = HPDF_Image_GetHeight(img);
                                    float target_w = iw > max_w ? max_w : iw;
                                    float target_h = ih * (target_w / iw);

                                    if (y - target_h < margin) {
                                        page = HPDF_AddPage(pdf);
                                        HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                                        HPDF_Page_SetFontAndSize(page, font, 11);
                                        y = page_h - margin;
                                    }

                                    y -= target_h;
                                    HPDF_Page_DrawImage(page, img, margin, y, target_w, target_h);
                                    y -= 10.0f;
                                    continue;
                                }
                            }
                        }
                        continue;
                    }

                    if (g_str_has_prefix(line, "Captured: ")) {
                        HPDF_Font italic_font = HPDF_GetFont(pdf, "Helvetica-Oblique", NULL);
                        HPDF_Page_SetFontAndSize(page, italic_font, 9);
                        y -= 12.0f;
                        if (y < margin) {
                            page = HPDF_AddPage(pdf);
                            HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                            y = page_h - margin;
                        }
                        HPDF_Page_BeginText(page);
                        HPDF_Page_MoveTextPos(page, margin, y);
                        HPDF_Page_ShowText(page, line);
                        HPDF_Page_EndText(page);
                        y -= 6.0f;
                        continue;
                    }

                    /* Check if heading */
                    gboolean is_heading = FALSE;
                    float font_size = 11.0f;
                    if (g_str_has_prefix(line, "### ")) { font_size = 14.0f; line += 4; is_heading = TRUE; }
                    else if (g_str_has_prefix(line, "## ")) { font_size = 16.0f; line += 3; is_heading = TRUE; }
                    else if (g_str_has_prefix(line, "# ")) { font_size = 20.0f; line += 2; is_heading = TRUE; }

                    if (is_heading) {
                        HPDF_Font bold_font = HPDF_GetFont(pdf, "Helvetica-Bold", NULL);
                        HPDF_Page_SetFontAndSize(page, bold_font, font_size);
                        line_height = font_size + 4.0f;
                    } else {
                        HPDF_Page_SetFontAndSize(page, font, 11);
                        line_height = 14.0f;
                    }

                    y -= line_height;
                    if (y < margin) {
                        page = HPDF_AddPage(pdf);
                        HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                        HPDF_Page_SetFontAndSize(page, font, 11);
                        y = page_h - margin;
                    }

                    /* Strip markdown formatting markers for PDF output */
                    char *clean = g_strdup(line);
                    GString *out = g_string_new("");
                    const char *p = clean;
                    while (*p) {
                        if (p[0] == '*' && p[1] == '*') { p += 2; continue; }
                        if (p[0] == '*') { p += 1; continue; }
                        if (p[0] == '~' && p[1] == '~') { p += 2; continue; }
                        if (g_str_has_prefix(p, "<u>")) { p += 3; continue; }
                        if (g_str_has_prefix(p, "</u>")) { p += 4; continue; }
                        g_string_append_c(out, *p);
                        p++;
                    }

                    /* Truncate line to fit page width */
                    HPDF_REAL tw = HPDF_Page_TextWidth(page, out->str);
                    if (tw > max_w && out->len > 0) {
                        while (out->len > 0 && HPDF_Page_TextWidth(page, out->str) > max_w) {
                            g_string_truncate(out, out->len - 1);
                        }
                    }

                    HPDF_Page_BeginText(page);
                    HPDF_Page_MoveTextPos(page, margin, y);
                    HPDF_Page_ShowText(page, out->str);
                    HPDF_Page_EndText(page);

                    g_string_free(out, TRUE);
                    g_free(clean);
                }
                g_strfreev(lines);

                HPDF_SaveToFile(pdf, path);
                HPDF_Free(pdf);
            }
            g_free(path);
        }
    }
    g_object_unref(native);
    g_free(text);
}

static void on_export_md_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    export_as_markdown();
}

static void on_export_pdf_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    export_as_pdf();
}

// ---- Initialization ----
void notes_init(GtkWidget *header_bar) {
    if (!header_bar) return;

    GtkWidget *note_toggle_btn = gtk_toggle_button_new_with_label("Note");
    gtk_widget_set_name(note_toggle_btn, "note-toggle-button");
    g_note_toggle_btn = note_toggle_btn;

    notes_popover = gtk_popover_new(note_toggle_btn);
    gtk_popover_set_position(GTK_POPOVER(notes_popover), GTK_POS_BOTTOM);
    gtk_popover_set_modal(GTK_POPOVER(notes_popover), FALSE);
    g_signal_connect(notes_popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    g_signal_connect(notes_popover, "map", G_CALLBACK(on_popover_map), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    // ---- Pin Button CSS: bright solid red when locked ----
    GtkCssProvider *pin_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(pin_css,
        "#pin-toggle-button:checked,\n"
        "#pin-toggle-button.pin-active,\n"
        "#pin-toggle-button:checked:hover,\n"
        "#pin-toggle-button:checked:active {\n"
        "    background-color: #dc2626;\n"
        "    background-image: none;\n"
        "    color: #ffffff;\n"
        "    border-color: #991b1b;\n"
        "    box-shadow: none;\n"
        "    border-radius: 4px;\n"
        "    min-width: 24px;\n"
        "    min-height: 24px;\n"
        "}\n"
        "#pin-toggle-button {\n"
        "    min-width: 24px;\n"
        "    min-height: 24px;\n"
        "    padding: 2px 6px;\n"
        "}",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(pin_css), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(pin_css);

    // --- Format Toolbar (Scrollable) — B/I/U/S/H1-H3 are keyboard-only shortcuts ---
    GtkWidget *fmt_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fmt_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(fmt_scroll, 660, 36);

    GtkWidget *fmt_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *btn_bullet    = gtk_button_new_with_label("\342\200\242 List");
    GtkWidget *btn_numlist   = gtk_button_new_with_label("1. List");
    GtkWidget *btn_checklist = gtk_button_new_with_label("\342\230\221 Task");
    GtkWidget *btn_quote     = gtk_button_new_with_label("\342\200\234 Quote");
    GtkWidget *btn_code      = gtk_button_new_with_label("</> Code");
    GtkWidget *btn_table     = gtk_button_new_with_label("\342\212\236 Table");
    GtkWidget *btn_link      = gtk_button_new_with_label("\360\237\224\227 Link");
    GtkWidget *btn_img       = gtk_button_new_with_label("\360\237\226\274 Image");
    GtkWidget *btn_undo      = gtk_button_new_with_label("\342\206\266");
    GtkWidget *btn_redo      = gtk_button_new_with_label("\342\206\267");
    GtkWidget *btn_find      = gtk_button_new_with_label("\360\237\224\215");

    gtk_widget_set_tooltip_text(btn_bullet,    "Bullet List  ( - item )");
    gtk_widget_set_tooltip_text(btn_numlist,   "Numbered List  ( 1. item )");
    gtk_widget_set_tooltip_text(btn_checklist, "Task / Checkbox  ( - [ ] )");
    gtk_widget_set_tooltip_text(btn_quote,     "Blockquote  ( > )");
    gtk_widget_set_tooltip_text(btn_code,      "Code Block  ( ``` )");
    gtk_widget_set_tooltip_text(btn_table,     "Insert Table");
    gtk_widget_set_tooltip_text(btn_link,      "Insert Link  (Ctrl+K)");
    gtk_widget_set_tooltip_text(btn_img,       "Insert Image");
    gtk_widget_set_tooltip_text(btn_undo,      "Undo  (Ctrl+Z)");
    gtk_widget_set_tooltip_text(btn_redo,      "Redo  (Ctrl+Y / Ctrl+Shift+Z)");
    gtk_widget_set_tooltip_text(btn_find,      "Find & Replace  (Ctrl+F)");

    g_signal_connect(btn_bullet,    "clicked", G_CALLBACK(on_btn_bullet),    NULL);
    g_signal_connect(btn_numlist,   "clicked", G_CALLBACK(on_btn_numlist),   NULL);
    g_signal_connect(btn_checklist, "clicked", G_CALLBACK(on_btn_checklist), NULL);
    g_signal_connect(btn_quote,     "clicked", G_CALLBACK(on_btn_quote),     NULL);
    g_signal_connect(btn_code,      "clicked", G_CALLBACK(on_btn_codeblock), NULL);
    g_signal_connect(btn_table,     "clicked", G_CALLBACK(on_btn_table),     NULL);
    g_signal_connect(btn_link,      "clicked", G_CALLBACK(on_btn_link),      NULL);
    g_signal_connect(btn_img,       "clicked", G_CALLBACK(on_btn_image),     NULL);
    g_signal_connect(btn_undo,      "clicked", G_CALLBACK(on_btn_undo),      NULL);
    g_signal_connect(btn_redo,      "clicked", G_CALLBACK(on_btn_redo),      NULL);
    g_signal_connect(btn_find,      "clicked", G_CALLBACK(on_btn_find),      NULL);

    gtk_box_pack_start(GTK_BOX(fmt_box), btn_bullet,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_numlist,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_checklist, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_quote,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_code,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_table,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_link,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_img,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_undo,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_redo,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt_box), btn_find,      FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(fmt_scroll), fmt_box);
    gtk_box_pack_start(GTK_BOX(vbox), fmt_scroll, FALSE, FALSE, 0);

    // --- Find & Replace Bar ---
    find_replace_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Find...");
    replace_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(replace_entry), "Replace with...");

    GtkWidget *btn_find_next = gtk_button_new_with_label("Find");
    GtkWidget *btn_replace = gtk_button_new_with_label("Replace");
    GtkWidget *btn_replace_all = gtk_button_new_with_label("All");
    GtkWidget *btn_close_find = gtk_button_new_with_label("\342\234\225");

    g_signal_connect(btn_find_next, "clicked", G_CALLBACK(on_find_next_clicked), NULL);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_find_next_clicked), NULL);
    g_signal_connect(btn_replace, "clicked", G_CALLBACK(on_replace_clicked), NULL);
    g_signal_connect(btn_replace_all, "clicked", G_CALLBACK(on_replace_all_clicked), NULL);
    g_signal_connect_swapped(btn_close_find, "clicked", G_CALLBACK(toggle_find_replace_bar), NULL);

    gtk_box_pack_start(GTK_BOX(find_replace_bar), search_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(find_replace_bar), replace_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(find_replace_bar), btn_find_next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(find_replace_bar), btn_replace, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(find_replace_bar), btn_replace_all, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(find_replace_bar), btn_close_find, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), find_replace_bar, FALSE, FALSE, 0);

    // --- Single Pane Editor ---
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, 670, 370);
    g_scrolled_window = scrolled_window;

    notes_textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(notes_textview), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(notes_textview), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(notes_textview), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(notes_textview), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(notes_textview), 8);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes_textview));
    ensure_tags(buffer);

    g_signal_connect(buffer, "changed", G_CALLBACK(on_buffer_changed_render), NULL);
    g_signal_connect(buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), NULL);
    g_signal_connect(notes_textview, "key-press-event", G_CALLBACK(on_editor_key_press), NULL);
    // Clickable links + image deactivation on text area click
    gtk_widget_add_events(notes_textview, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(notes_textview, "button-release-event", G_CALLBACK(on_textview_button_release), NULL);

    gtk_container_add(GTK_CONTAINER(scrolled_window), notes_textview);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // --- Bottom Toolbar ---
    GtkWidget *toolbar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *new_btn = gtk_button_new_with_label("+ New Note");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_new_note_clicked), NULL);

    notes_combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(notes_combo, 150, -1);
    g_signal_connect(notes_combo, "changed", G_CALLBACK(on_note_selected), NULL);

    GtkWidget *snip_btn = gtk_button_new_with_label("\360\237\223\267 Snip");
    g_signal_connect(snip_btn, "clicked", G_CALLBACK(on_snip_clicked), NULL);

    GtkWidget *timestamp_btn = gtk_button_new_with_label("\342\217\261 Timestamp");
    g_signal_connect(timestamp_btn, "clicked", G_CALLBACK(on_timestamp_clicked), NULL);

    /* Pin toggle: dot (●) — red = pinned, grey = unpinned. Color managed via CSS class. */
    GtkWidget *pin_toggle_btn = gtk_toggle_button_new_with_label("\342\227\217");
    gtk_widget_set_name(pin_toggle_btn, "pin-toggle-button");
    g_pin_toggle_btn = pin_toggle_btn;
    gtk_widget_set_tooltip_text(pin_toggle_btn, "Pin: Keep Notes panel open (red = pinned)");
    g_signal_connect(pin_toggle_btn, "toggled", G_CALLBACK(on_pin_toggled), NULL);

    /* Export button with popover menu */
    GtkWidget *export_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(export_btn), "\360\237\223\244 Export");
    gtk_widget_set_tooltip_text(export_btn, "Export note as PDF or Markdown");
    GtkWidget *export_popover = gtk_popover_new(export_btn);
    GtkWidget *export_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(export_box, 6);
    gtk_widget_set_margin_end(export_box, 6);
    gtk_widget_set_margin_top(export_box, 6);
    gtk_widget_set_margin_bottom(export_box, 6);
    GtkWidget *exp_md = gtk_button_new_with_label("Export as .md");
    GtkWidget *exp_pdf = gtk_button_new_with_label("Export as PDF");
    g_signal_connect(exp_md, "clicked", G_CALLBACK(on_export_md_clicked), NULL);
    g_signal_connect(exp_pdf, "clicked", G_CALLBACK(on_export_pdf_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(export_box), exp_md, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(export_box), exp_pdf, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(export_popover), export_box);
    gtk_widget_show_all(export_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(export_btn), export_popover);

    /* Resize grip at bottom-left: dragging expands the notes popover */
    GtkWidget *resize_grip = gtk_event_box_new();
    GtkWidget *grip_label = gtk_label_new("\342\207\231");
    gtk_container_add(GTK_CONTAINER(resize_grip), grip_label);
    gtk_widget_set_tooltip_text(resize_grip, "Drag to resize (bottom-left)");
    gtk_widget_add_events(resize_grip, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(resize_grip, "button-press-event", G_CALLBACK(on_resize_grip_press), NULL);
    g_signal_connect(resize_grip, "motion-notify-event", G_CALLBACK(on_resize_grip_motion), NULL);
    g_signal_connect(resize_grip, "button-release-event", G_CALLBACK(on_resize_grip_release), NULL);

    gtk_box_pack_start(GTK_BOX(toolbar_box), resize_grip,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), new_btn,        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), notes_combo,    TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), snip_btn,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), timestamp_btn,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), export_btn,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), pin_toggle_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(notes_popover), vbox);
    gtk_widget_show_all(vbox);
    gtk_widget_hide(find_replace_bar);

    g_signal_connect(note_toggle_btn, "toggled", G_CALLBACK(on_note_toggled), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), note_toggle_btn);

    refresh_notes_list();
}
