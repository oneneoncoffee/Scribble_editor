/*
 * scrible.c GTK+ Code Editor with Syntax Highlighting
 * Dependencies:
 * GTK+ 3.0
 * GtkSourceView 3.0
 * On Ubuntu/Debian/Penguin cromebook:
 * sudo apt-get install libgtk-3-dev libgtksourceview-3.0-dev
 * Compile: gcc -o scrible scrible.c `pkg-config --cflags --libs gtk+-3.0 gtksourceview-3.0`
 */

#include <gtk/gtk.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop

typedef struct {
    GtkWidget *window;
    GtkWidget *view;
    GtkSourceBuffer *buffer;
    GtkWidget *status_label;
    GtkWidget *tree_view;
    GtkWidget *tree_scroll;
    GtkTreeStore *tree_store;
    gchar *current_file;
    GtkCssProvider *dark_css;
    GtkCssProvider *light_css;
    GtkCssProvider *green_css;
    int theme_mode;
    gboolean dark_mode;
    GtkSourceSearchSettings *search_settings;
    GtkSourceSearchContext *search_context;
    gboolean focus_mode;
    GList *bookmarks; 
    GtkWidget *search_bar;
} EditorApp;

enum {
    COL_NAME = 0,
    COL_LINE,
    NUM_COLS
};

/* Helper struct for build dialog callbacks */
typedef struct {
    GtkWidget *output_entry;
    GtkWidget *flags_entry;
    GtkWidget *linker_entry;
    GtkWidget *preview_text;
    gchar *current_file;
} BuildDialogData;

/* Helper function to compare integers stored as pointers */
static gint compare_bookmarks(gconstpointer a, gconstpointer b) {
    return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}


/* Forward declarations */
static void on_save(GtkWidget *widget, gpointer data); // Add this line
static void on_save_as(GtkWidget *widget, gpointer data);
static void parse_symbols(EditorApp *app);
static void update_status(EditorApp *app);
static void on_toggle_focus_mode(GtkCheckMenuItem *item, gpointer data);
static void on_change_font(GtkWidget *widget, gpointer data);
static void insert_at_cursor(EditorApp *app, const gchar *text); 
static void on_bookmark_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, 
                                          GtkTreeViewColumn *column, gpointer data); 
                                          
/* Toggle bookmark on current line */
static void on_toggle_bookmark(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter iter;
    
    // Get current line number
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    gint line_num = gtk_text_iter_get_line(&iter);
    
    // Check if bookmark already exists on this line
    GList *found = g_list_find(app->bookmarks, GINT_TO_POINTER(line_num));
    
    if (found) {
        // Remove bookmark
        app->bookmarks = g_list_remove(app->bookmarks, GINT_TO_POINTER(line_num));
        
        // Remove visual marker
        GtkTextIter line_start;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &line_start, line_num);
        gtk_text_buffer_remove_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "bookmark", &line_start, &iter);
        
        g_print("Bookmark removed from line %d\n", line_num + 1);
    } else {
        // Add bookmark
        app->bookmarks = g_list_insert_sorted(app->bookmarks, GINT_TO_POINTER(line_num), 
                                     compare_bookmarks);   
        
        // Add visual marker
        GtkTextIter line_start, line_end;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &line_start, line_num);
        line_end = line_start;
        if (!gtk_text_iter_ends_line(&line_end)) {
            gtk_text_iter_forward_to_line_end(&line_end);
        }
        gtk_text_buffer_apply_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "bookmark", &line_start, &line_end);
        
        g_print("Bookmark added to line %d\n", line_num + 1);
    }
    
    update_status(app);
}

/* Go to next bookmark */
static void on_next_bookmark(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->bookmarks) {
        g_print("No bookmarks set\n");
        return;
    }
    
    // Get current line
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    gint current_line = gtk_text_iter_get_line(&iter);
    
    // Find next bookmark after current line
    GList *node = app->bookmarks;
    gint next_line = -1;
    
    while (node) {
        gint bookmark_line = GPOINTER_TO_INT(node->data);
        if (bookmark_line > current_line) {
            next_line = bookmark_line;
            break;
        }
        node = node->next;
    }
    
    // If no bookmark found after current line, wrap to first bookmark
    if (next_line == -1 && app->bookmarks) {
        next_line = GPOINTER_TO_INT(app->bookmarks->data);
    }
    
    // Jump to the bookmark
    if (next_line >= 0) {
        GtkTextIter jump_iter;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &jump_iter, next_line);
        gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(app->buffer), &jump_iter);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &jump_iter, 0.0, TRUE, 0.0, 0.5);
        gtk_widget_grab_focus(app->view);
        update_status(app);
        g_print("Jumped to bookmark at line %d\n", next_line + 1);
    }
}

/* Go to previous bookmark */
static void on_previous_bookmark(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->bookmarks) {
        g_print("No bookmarks set\n");
        return;
    }
    
    // Get current line
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    gint current_line = gtk_text_iter_get_line(&iter);
    
    // Find previous bookmark before current line
    GList *node = g_list_last(app->bookmarks);
    gint prev_line = -1;
    
    while (node) {
        gint bookmark_line = GPOINTER_TO_INT(node->data);
        if (bookmark_line < current_line) {
            prev_line = bookmark_line;
            break;
        }
        node = node->prev;
    }
    
    // If no bookmark found before current line, wrap to last bookmark
    if (prev_line == -1 && app->bookmarks) {
        GList *last = g_list_last(app->bookmarks);
        prev_line = GPOINTER_TO_INT(last->data);
    }
    
    // Jump to the bookmark
    if (prev_line >= 0) {
        GtkTextIter jump_iter;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &jump_iter, prev_line);
        gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(app->buffer), &jump_iter);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &jump_iter, 0.0, TRUE, 0.0, 0.5);
        gtk_widget_grab_focus(app->view);
        update_status(app);
        g_print("Jumped to bookmark at line %d\n", prev_line + 1);
    }
}

/* Clear all bookmarks */
static void on_clear_all_bookmarks(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->bookmarks) {
        g_print("No bookmarks to clear\n");
        return;
    }
    
    // Remove all bookmark visual markers
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gtk_text_buffer_remove_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "bookmark", &start, &end);
    
    // Clear bookmark list
    g_list_free(app->bookmarks);
    app->bookmarks = NULL;
    
    g_print("All bookmarks cleared\n");
    update_status(app);
}

/* List all bookmarks in a dialog */
static void on_list_bookmarks(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->bookmarks) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                            GTK_MESSAGE_INFO,
                            GTK_BUTTONS_OK,
                            "No bookmarks set");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Create dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Bookmarks",
                                         GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         "_Clear All", GTK_RESPONSE_REJECT,
                                         NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // Create tree view for bookmarks
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
    GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    
    // Line number column
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        "Line", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    
    // Preview column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
        "Preview", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    
    // Populate list
    GList *node = app->bookmarks;
    while (node) {
        gint line_num = GPOINTER_TO_INT(node->data);
        
        // Get line preview
        GtkTextIter line_start, line_end;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &line_start, line_num);
        line_end = line_start;
        if (!gtk_text_iter_ends_line(&line_end)) {
            gtk_text_iter_forward_to_line_end(&line_end);
        }
        gchar *line_text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), 
                                                    &line_start, &line_end, FALSE);
        
        // Trim whitespace and limit length
        gchar *trimmed = g_strstrip(g_strdup(line_text));
        if (strlen(trimmed) > 60) {
            trimmed[60] = '\0';
            gchar *with_ellipsis = g_strdup_printf("%s...", trimmed);
            g_free(trimmed);
            trimmed = with_ellipsis;
        }
        
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 
                          0, line_num + 1,  // Display as 1-indexed
                          1, trimmed,
                          -1);
        
        g_free(line_text);
        g_free(trimmed);
        node = node->next;
    }
    
    // Scrolled window
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), tree_view);
    
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);
    gtk_widget_show_all(content_area);
    
    // Handle double-click to jump to bookmark
    g_signal_connect(tree_view, "row-activated", 
                     G_CALLBACK(on_bookmark_list_row_activated), app);
    
    // Run dialog
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_REJECT) {
        on_clear_all_bookmarks(NULL, app);
    }
    
    gtk_widget_destroy(dialog);
    g_object_unref(store);
}

/* Helper: Jump to bookmark when double-clicked in list */
static void on_bookmark_list_row_activated(GtkTreeView *tree_view,
                                          GtkTreePath *path,
                                          GtkTreeViewColumn *column,
                                          gpointer data)
{
    EditorApp *app = (EditorApp *)data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gint line_num;
        gtk_tree_model_get(model, &iter, 0, &line_num, -1);
        
        // Jump to line (convert back to 0-indexed)
        GtkTextIter jump_iter;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &jump_iter, line_num - 1);
        gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(app->buffer), &jump_iter);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &jump_iter, 0.0, TRUE, 0.0, 0.5);
        gtk_widget_grab_focus(app->view);
        update_status(app);
        
        // Close the dialog
        GtkWidget *dialog = gtk_widget_get_toplevel(GTK_WIDGET(tree_view));
        if (GTK_IS_DIALOG(dialog)) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
        }
    }
}

/* Duplicate current line */
static void on_duplicate_line(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    
    // Get cursor position
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &start,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    
    // Get the entire line
    end = start;
    gtk_text_iter_set_line_offset(&start, 0);
    if (!gtk_text_iter_ends_line(&end)) {
        gtk_text_iter_forward_to_line_end(&end);
    }
    
    // Copy the line text
    gchar *line = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
    
    // Insert newline and duplicate line
    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(app->buffer));
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &end, "\n", 1);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &end, line, -1);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(app->buffer));
    
    g_free(line);
}

/* Delete current line */
static void on_delete_line(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    
    // Get cursor position
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &start,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    
    // Select entire line including newline
    end = start;
    gtk_text_iter_set_line_offset(&start, 0);
    if (!gtk_text_iter_ends_line(&end)) {
        gtk_text_iter_forward_to_line_end(&end);
    }
    
    // Include the newline character if not at end of buffer
    if (!gtk_text_iter_is_end(&end)) {
        gtk_text_iter_forward_char(&end);
    }
    
    // Delete the line
    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(app->buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(app->buffer));
    
    update_status(app);
}

/* Go to line number */
static void on_goto_line(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog, *content_area, *grid;
    GtkWidget *entry, *label;
    
    // Create dialog
    dialog = gtk_dialog_new_with_buttons("Go to Line",
                                         GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Go", GTK_RESPONSE_ACCEPT,
                                         NULL);
    
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // Create grid layout
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    
    // Add label and entry
    label = gtk_label_new("Line number:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter line number");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_size_request(entry, 200, -1);
    
    // Get current line for display
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    int current_line = gtk_text_iter_get_line(&iter) + 1;
    int total_lines = gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(app->buffer));
    
    gchar *hint = g_strdup_printf("Current: %d, Total: %d", current_line, total_lines);
    GtkWidget *info_label = gtk_label_new(hint);
    g_free(hint);
    
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), info_label, 0, 1, 2, 1);
    
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(content_area);
    
    // Set default button
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    
    // Run dialog
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        int line = atoi(text) - 1;  // Convert to 0-indexed
        
        if (line >= 0 && line < total_lines) {
            GtkTextIter jump_iter;
            gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &jump_iter, line);
            
            // Move cursor to the line
            gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(app->buffer), &jump_iter);
            
            // Scroll to make the line visible and centered
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &jump_iter, 
                                        0.0, TRUE, 0.0, 0.5);
            
            // Give focus back to editor
            gtk_widget_grab_focus(app->view);
            update_status(app);
        } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                GTK_MESSAGE_WARNING,
                                GTK_BUTTONS_OK,
                                "Line number out of range!\nPlease enter a number between 1 and %d.", 
                                total_lines);
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
    }
    
    gtk_widget_destroy(dialog);
}

/* HEX Color picker */
static void on_color_picker(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog;
    GdkRGBA color;

    // Create the color chooser dialog
    dialog = gtk_color_chooser_dialog_new("Select Color", GTK_WINDOW(app->window));

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);

        // Convert RGBA to Hex format (#RRGGBB)
        gchar *hex_color = g_strdup_printf("#%02X%02X%02X",
                                           (guint)(color.red * 255),
                                           (guint)(color.green * 255),
                                           (guint)(color.blue * 255));

        // Insert the hex string at the current cursor position
        insert_at_cursor(app, hex_color);
        g_free(hex_color);
    }

    gtk_widget_destroy(dialog);
}
// View: Toggle Sidebar (Tree View)
static void on_toggle_sidebar(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *tree_scroll = gtk_widget_get_parent(app->tree_view);
    gboolean visible = gtk_widget_get_visible(tree_scroll);
    gtk_widget_set_visible(tree_scroll, !visible);
}

// Format: Strip Trailing Whitespace
static void on_strip_trailing(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
    
    // Simple regex-like logic: remove spaces before newlines
    GRegex *regex = g_regex_new("[ \t]+$", G_REGEX_MULTILINE, 0, NULL);
    gchar *result = g_regex_replace(regex, text, -1, 0, "", 0, NULL);
    
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(app->buffer), result, -1);
    
    g_free(text); g_free(result); g_regex_unref(regex);
}


/* Update preview callback */
static void update_build_preview(GtkWidget *widget, gpointer user_data) {
    BuildDialogData *data = (BuildDialogData *)user_data;
    
    const gchar *output = gtk_entry_get_text(GTK_ENTRY(data->output_entry));
    const gchar *flags = gtk_entry_get_text(GTK_ENTRY(data->flags_entry));
    const gchar *linker = gtk_entry_get_text(GTK_ENTRY(data->linker_entry));
    
    gchar *cmd = g_strdup_printf("gcc %s %s %s -o %s && ./%s",
                                 flags && *flags ? flags : "",
                                 data->current_file,
                                 linker && *linker ? linker : "",
                                 output,
                                 output);

    /* FIX: Escape the command string so '&&' becomes '&amp;&amp;' */
    gchar *escaped_cmd = g_markup_escape_text(cmd, -1);
    gchar *markup = g_strdup_printf("<tt>%s</tt>", escaped_cmd);
    
    gtk_label_set_markup(GTK_LABEL(data->preview_text), markup);
    
    g_free(cmd);
    g_free(escaped_cmd); // Clean up the escaped version
    g_free(markup);
}

/* Build: Compile & Run with custom options */
static void on_build_run(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->current_file) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                            GTK_MESSAGE_WARNING,
                            GTK_BUTTONS_OK,
                            "Please save your file before building!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    /* Create build options dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Build Options",
                                         GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Build & Run", GTK_RESPONSE_ACCEPT,
                                         NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    
    /* Source file label (read-only) */
    GtkWidget *source_label = gtk_label_new("Source File:");
    gtk_widget_set_halign(source_label, GTK_ALIGN_END);
    GtkWidget *source_value = gtk_label_new(g_path_get_basename(app->current_file));
    gtk_widget_set_halign(source_value, GTK_ALIGN_START);
    
    /* Output executable name */
    GtkWidget *output_label = gtk_label_new("Output Name:");
    gtk_widget_set_halign(output_label, GTK_ALIGN_END);
    GtkWidget *output_entry = gtk_entry_new();
    
    /* Generate default output name (remove .c extension) */
    gchar *basename = g_path_get_basename(app->current_file);
    gchar *default_output = g_strdup(basename);
    gchar *dot = strrchr(default_output, '.');
    if (dot) *dot = '\0';
    gtk_entry_set_text(GTK_ENTRY(output_entry), default_output);
    g_free(basename);
    g_free(default_output);
    
    /* Compiler flags */
    GtkWidget *flags_label = gtk_label_new("Compiler Flags:");
    gtk_widget_set_halign(flags_label, GTK_ALIGN_END);
    GtkWidget *flags_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(flags_entry), "-Wall -Wextra -g");
    gtk_entry_set_placeholder_text(GTK_ENTRY(flags_entry), "e.g., -Wall -O2 -std=c99");
    
    /* Linker flags */
    GtkWidget *linker_label = gtk_label_new("Linker Flags:");
    gtk_widget_set_halign(linker_label, GTK_ALIGN_END);
    GtkWidget *linker_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(linker_entry), "e.g., -lm -lpthread");
    
    /* Preview label */
    GtkWidget *preview_label = gtk_label_new("Command Preview:");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_END);
    gtk_widget_set_valign(preview_label, GTK_ALIGN_START);
    
    GtkWidget *preview_text = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(preview_text), TRUE);
    gtk_label_set_selectable(GTK_LABEL(preview_text), TRUE);
    gtk_widget_set_halign(preview_text, GTK_ALIGN_START);
    
    /* Setup callback data */
    BuildDialogData *callback_data = g_new(BuildDialogData, 1);
    callback_data->output_entry = output_entry;
    callback_data->flags_entry = flags_entry;
    callback_data->linker_entry = linker_entry;
    callback_data->preview_text = preview_text;
    callback_data->current_file = app->current_file;
    
    /* Connect entry changes to preview update */
    g_signal_connect(output_entry, "changed", G_CALLBACK(update_build_preview), callback_data);
    g_signal_connect(flags_entry, "changed", G_CALLBACK(update_build_preview), callback_data);
    g_signal_connect(linker_entry, "changed", G_CALLBACK(update_build_preview), callback_data);
    
    /* Layout grid */
    gtk_grid_attach(GTK_GRID(grid), source_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), source_value, 1, 0, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), output_entry, 1, 1, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), flags_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), flags_entry, 1, 2, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), linker_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), linker_entry, 1, 3, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 4, 2, 1);
    
    gtk_grid_attach(GTK_GRID(grid), preview_label, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), preview_text, 1, 5, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_widget_show_all(content_area);
    
    /* Initial preview */
    update_build_preview(NULL, callback_data);
    
    /* Run dialog */
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *output = gtk_entry_get_text(GTK_ENTRY(output_entry));
        const gchar *flags = gtk_entry_get_text(GTK_ENTRY(flags_entry));
        const gchar *linker = gtk_entry_get_text(GTK_ENTRY(linker_entry));
        
        /* Build command */
        gchar *compile_cmd = g_strdup_printf("gcc %s %s %s -o %s",
                                            flags && *flags ? flags : "",
                                            app->current_file,
                                            linker && *linker ? linker : "",
                                            output);
        
        gchar *run_cmd = g_strdup_printf("./%s", output);
        
        g_print("\n=== Building ===\n");
        g_print("Command: %s\n", compile_cmd);
        
        int compile_result = system(compile_cmd);
        
        if (compile_result == 0) {
            g_print("\n=== Build Successful ===\n");
            g_print("Running: %s\n\n", run_cmd);
            system(run_cmd);
            g_print("\n=== Program Finished ===\n");
        } else {
            g_print("\n=== Build Failed ===\n");
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                "Compilation failed! Check the terminal for error messages.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
        
        g_free(compile_cmd);
        g_free(run_cmd);
    }
    
    g_free(callback_data);
    gtk_widget_destroy(dialog);
}

/* Quick Build: Uses defaults, no dialog */
static void on_quick_build(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->current_file) {
        g_print("Save the file first before building!\n");
        return;
    }
    
    /* Generate output name */
    gchar *basename = g_path_get_basename(app->current_file);
    gchar *output = g_strdup(basename);
    gchar *dot = strrchr(output, '.');
    if (dot) *dot = '\0';
    g_free(basename);
    
    gchar *cmd = g_strdup_printf("gcc -Wall -Wextra -g %s -o %s && ./%s",
                                 app->current_file, output, output);
    g_print("\n=== Quick Build ===\n%s\n\n", cmd);
    system(cmd);
    
    g_free(cmd);
    g_free(output);
}
// Tool 1: Word & Character Count
static void on_word_count(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
    
    glong chars = g_utf8_strlen(text, -1);
    gchar **words = g_strsplit_set(text, " \n\t\r", -1);
    guint word_count = 0;
    for (int i = 0; words[i] != NULL; i++) {
        if (strlen(words[i]) > 0) word_count++;
    }
    
    gchar *msg = g_strdup_printf("Analysis Complete:\n- Words: %u\n- Characters: %ld", word_count, chars);
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                        GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                        GTK_MESSAGE_INFO,
                        GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);  // Safe to destroy after gtk_dialog_run()
    
    g_free(msg); 
    g_free(text); 
    g_strfreev(words);
}
// Tool 2: Sort Selected Lines Alphabetically
static void on_sort_selection(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
        gchar **lines = g_strsplit(text, "\n", -1);
        
        // Simple bubble sort for lines
        int count = g_strv_length(lines);
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (g_strcmp0(lines[i], lines[j]) > 0) {
                    gchar *temp = lines[i];
                    lines[i] = lines[j];
                    lines[j] = temp;
                }
            }
        }
        
        gchar *sorted_text = g_strjoinv("\n", lines);
        gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(app->buffer));
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(app->buffer), &start, &end);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &start, sorted_text, -1);
        gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(app->buffer));
        
        g_free(text); g_free(sorted_text); g_strfreev(lines);
    }
}
// Convert Selection to Uppercase
static void on_uppercase(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
        gchar *upper = g_utf8_strup(text, -1);
        
        gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(app->buffer));
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(app->buffer), &start, &end);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &start, upper, -1);
        gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(app->buffer));
        
        g_free(text); g_free(upper);
    }
}

// Sort Lines Alphabetically
static void on_sort_lines(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
        gchar **lines = g_strsplit(text, "\n", -1);
        
        // Basic bubble sort for the lines array
        int count = g_strv_length(lines);
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (g_strcmp0(lines[i], lines[j]) > 0) {
                    gchar *tmp = lines[i]; lines[i] = lines[j]; lines[j] = tmp;
                }
            }
        }
        
        gchar *sorted = g_strjoinv("\n", lines);
        gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(app->buffer));
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(app->buffer), &start, &end);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &start, sorted, -1);
        gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(app->buffer));
        
        g_strfreev(lines); g_free(text); g_free(sorted);
    }
}
static void on_toggle_focus_mode(GtkCheckMenuItem *item, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    app->focus_mode = gtk_check_menu_item_get_active(item);
    
    if (!app->focus_mode) {
        /* Remove the yellow highlight tag when turning off Focus Mode */
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
        gtk_text_buffer_remove_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "jump_highlight", &start, &end);
    }
    
    update_status(app);
}

static void on_restart_editor(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    /* 1. Save work first */
    on_save(NULL, app);
    
    /* 2. Prepare arguments for restart */
    /* We assume the executable is named 'scrible' in the current directory */
    char *args[] = {"./scrible", app->current_file, NULL};
    
    /* 3. Restart the process */
    g_print("Restarting editor...\n");
    execvp(args[0], args);
    
    /* If execvp returns, it failed */
    perror("execvp failed");
}

/* Find/Replace Callback classic function */
static void on_find_replace(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog, *content_area, *grid;
    GtkWidget *find_entry, *replace_entry;
    GtkWidget *check_case, *check_word, *check_regex;

    dialog = gtk_dialog_new_with_buttons("Advanced Search & Replace", GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Find Next", 1,
                                         "Replace All", 4, // Added Replace All
                                         "Replace", 2,
                                         "Close", GTK_RESPONSE_CLOSE, NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);

    // Entry Fields
    find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(find_entry), "Search term...");
    replace_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(replace_entry), "Replace with...");

    // Option Toggles
    check_case = gtk_check_button_new_with_label("Match Case");
    check_word = gtk_check_button_new_with_label("Whole Words Only");
    check_regex = gtk_check_button_new_with_label("Use Regular Expressions");

    // Layout
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Find:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), find_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Replace:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), replace_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_case, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_word, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_regex, 1, 4, 1, 1);

    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (!app->search_settings) {
        app->search_settings = gtk_source_search_settings_new();
        app->search_context = gtk_source_search_context_new(app->buffer, app->search_settings);
        gtk_source_search_context_set_highlight(app->search_context, TRUE);
    }

    gint result;
    while ((result = gtk_dialog_run(GTK_DIALOG(dialog))) > 0) {
        const gchar *find_text = gtk_entry_get_text(GTK_ENTRY(find_entry));
        const gchar *replace_text = gtk_entry_get_text(GTK_ENTRY(replace_entry));
        
        // Apply Settings
        gtk_source_search_settings_set_search_text(app->search_settings, find_text);
        gtk_source_search_settings_set_case_sensitive(app->search_settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_case)));
        gtk_source_search_settings_set_at_word_boundaries(app->search_settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_word)));
        gtk_source_search_settings_set_regex_enabled(app->search_settings, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_regex)));

        GtkTextIter start, end, iter;
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter, 
                                         gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));

        if (result == 1) { // Find Next
            if (gtk_source_search_context_forward2(app->search_context, &iter, &start, &end, NULL)) {
                gtk_text_buffer_select_range(GTK_TEXT_BUFFER(app->buffer), &start, &end);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &start, 0.0, FALSE, 0.0, 0.0);
            }
        } else if (result == 2) { // Replace
            if (gtk_source_search_context_forward2(app->search_context, &iter, &start, &end, NULL)) {
                gtk_source_search_context_replace2(app->search_context, &start, &end, replace_text, -1, NULL);
            }
        } else if (result == 4) { // Replace All
            gtk_source_search_context_replace_all(app->search_context, replace_text, -1, NULL);
        }
    }
    gtk_widget_destroy(dialog);
}

/* Clear Highlighting */
static void on_clear_search(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    if (app->search_settings) {
        gtk_source_search_settings_set_search_text(app->search_settings, NULL);
    }
}

/* Fine selection */
static void on_find_selection(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;

    if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        gchar *selected_text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
        
        if (!app->search_settings) {
            app->search_settings = gtk_source_search_settings_new();
            app->search_context = gtk_source_search_context_new(app->buffer, app->search_settings);
            gtk_source_search_context_set_highlight(app->search_context, TRUE);
        }

        gtk_source_search_settings_set_search_text(app->search_settings, selected_text);
        g_free(selected_text);
    }
}

static void on_search_entry_changed(GtkSearchEntry *entry, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    
    if (!app->search_settings) {
        app->search_settings = gtk_source_search_settings_new();
        app->search_context = gtk_source_search_context_new(app->buffer, app->search_settings);
        gtk_source_search_context_set_highlight(app->search_context, TRUE);
    }
    
    gtk_source_search_settings_set_search_text(app->search_settings, text);
}

/* find all */
static void on_find_all(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    if (!app->search_settings || !app->search_context) return;

    const gchar *text = gtk_source_search_settings_get_search_text(app->search_settings);
    if (!text || strlen(text) == 0) return;

    int count = gtk_source_search_context_get_occurrences_count(app->search_context);
    
    gchar *msg = g_strdup_printf("Found %d occurrences of '%s'", count, text);
    gtk_label_set_text(GTK_LABEL(app->status_label), msg);
    g_free(msg);

    // Ensure highlighting is forced on
    gtk_source_search_context_set_highlight(app->search_context, TRUE);
}
/* Quick find */
static void on_toggle_search_bar(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    // Note: This assumes you added GtkWidget *search_bar to your EditorApp struct
    gboolean revealed = gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(app->search_bar));
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(app->search_bar), !revealed);
}


/* Apply dark theme using GTK CSS */
static GtkCssProvider *create_light_theme(void) {
    GtkCssProvider *css = gtk_css_provider_new();

    const gchar *data =
        "window { background-color: #ffffff; }"

        "GtkSourceView {"
        "  background-color: #ffffff;"
        "  color: #000000;"
        "  caret-color: #000000;"
        "}"

        "GtkSourceView text:selected {"
        "  background-color: #cce4ff;"
        "  color: #000000;"
        "}"

        "GtkSourceView gutter {"
        "  background-color: #f0f0f0;"
        "}"

        "GtkSourceView gutter text {"
        "  color: #555555;"
        "}"

        "GtkSourceView gutter text.current-line {"
        "  color: #000000;"
        "  font-weight: bold;"
        "}"

        "GtkSourceView text.current-line {"
        "  background-color: #e6e6e6;"
        "}";

    gtk_css_provider_load_from_data(css, data, -1, NULL);
    return css;
}

static void apply_theme(EditorApp *app) {
    GdkScreen *screen = gdk_screen_get_default();

    /* Safety Check: Only remove if the pointer is actually a valid provider */
    if (app->dark_css && GTK_IS_STYLE_PROVIDER(app->dark_css))
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(app->dark_css));
        
    if (app->light_css && GTK_IS_STYLE_PROVIDER(app->light_css))
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(app->light_css));
        
    if (app->green_css && GTK_IS_STYLE_PROVIDER(app->green_css))
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(app->green_css));

    GtkCssProvider *active = NULL;
    if (app->theme_mode == 1) active = app->dark_css;
    else if (app->theme_mode == 2) active = app->green_css;
    else active = app->light_css;

    /* Final safety check before adding */
    if (active && GTK_IS_STYLE_PROVIDER(active)) {
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(active),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
}

static GtkCssProvider *create_dark_theme(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    const gchar *data =
        /* 1. Base Window & Panels - Using deep charcoal instead of pure black */
        "window, .background { background-color: #1e1e1e; color: #eeeeec; }"
        
        /* TITLE BAR STYLING */
        "headerbar { background-color: #2d2d2d; border-bottom: 1px solid #3f3f3f; box-shadow: none; color:#eeeeec;}"
        "headerbar .title { color: #97a69b; font-weight: bold; border: 3px double #FFFFFF; }"
        "headerbar button { color: #eeeeec; background: #383838; border: 1px solid #484848;  }"
        "headerbar button:hover { background: #454545; }"
        "headerbar text { color: #4948FF; background: #3584e4; }"
        /* Buttons in the header */
        /* Window control buttons (minimize, maximize, close) */
        "headerbar button { "
        "  background-color: #2d2d2d; "
        "  border: none; "
        "  color: #686b69; " 
        "  min-width: 16px; "
        "  min-height: 16px; "
        "}"
        
        /* Button icon color */
        "headerbar button image { "
        "  color: #686b69; "  
        "}"
        /* Hover state for buttons and their icons */
        "headerbar button:hover { "
        "  background-color: #162118; "
        "}"
     
        "box, grid, paned { border: 1px solid #333333; }" 

        /* 2. THE MENUBAR */
        "menubar { background-color: #68767d; color: #eeeeec; border-bottom: 1px solid #3f3f3f; }"
        "menubar > menuitem { padding: 4px 8px; }" 
        "menubar > menuitem:hover { background-color: #3584e4; color: #ffffff; }"

        /* 3. THE DROPDOWN MENUS (Floating Boxes) */
        "menu { "
        "  background-color: #2d2d2d; "
        "  border: 1px dotted #454545; "
        "  margin: 5px; "
        "}"
        "menu menuitem { background-color: #2d2d2d; color: #eeeeec; padding: 5px; }"
        "menu menuitem:hover { background-color: #3584e4; color: #ffffff; }"

        /* 4. THE EDITOR & GUTTER (GtkSourceView) */
        "GtkSourceView, text { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Monospace'; }"
        "gutter, GtkSourceView gutter text { background-color: #1e1e1e; color: #858585; border-right: 1px solid #333333; }"
        "GtkSourceView gutter text.current-line { color: #ffffff; font-weight: bold; }"
        "GtkSourceView text.current-line { background-color: #2a2a2a; }"
        "GtkSourceView { caret-color: #ffffff; }"
        "GtkSourceView text:selected { background-color: #264f78; color: #ffffff; }"

        /* 5. THE SIDEBAR (Treeview) */
        "treeview { background-color: #252526; color: #cccccc; }"
        "treeview:selected { background-color: #37373d; color: #ffffff; }"
        "treeview:hover { background-color: #2a2d2e; }";

    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(css),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_data(css, data, -1, NULL);
    return css;
}

static GtkCssProvider *create_green_theme(void) {
    GtkCssProvider *css = gtk_css_provider_new();
const gchar *data =
    /* 1. Base Window & Panels */
    "window, .background { background-color: #000000; }"
    /* TITLE BAR STYLING */
    "headerbar { background-color: #000000; border-bottom: 2px solid #004400; }"
    "headerbar .title { color: #00FF41 ; font-family: 'Monospace'; }"
    "headerbar button { color: #00FF41; background: #000000; border: 1px solid #004400; }"
    "headerbar button:hover { background: #00FF41; color: #000000; }"
    "headerbar text { color: #00FF41; background: #004400; }"
    
    "box, grid, paned { border: 1px solid #004400; }" /* Subtler cyan/green border */

    /* 2. THE MENUBAR (The Top Bar) */
    "menubar { background-color: #000000; color: #00FF41; border-bottom: 4px solid #004400; }"
    "menubar > menuitem { color: #00FF41; }" 
    "menubar > menuitem label { color: #00FF41; }"
    "menubar > menuitem:hover { background-color: #004400; }"
    "menubar > menuitem label:hover { color: #004100; }"
    /* Buttons in the header */
    "headerbar button { "
    "  background-color: #000000; "
    "  border: 1px solid #004400; "
    "  color: #FFFFFF; " /* This usually changes the icon color */
    "}"
    "button { "
    "color: #008800; background-image: none; "
    "background-color: #0d0d0d;"
    "border: 1px double #004400;"
    "transition: all 0.2s ease;"
    "}" 
    /* Hover state for buttons and their icons */
    "headerbar button:hover { "
    "  background-color: #00FF41; "
    "}"
    "headerbar button:hover image { "
    "  color: #000000; "
    "}" 
    "button image { color: inherit; }"    
    /* 3. THE DROPDOWN MENUS (The Floating Boxes) */
    "menu { "
    "  background-color: #000000; "
    "  border: 1px solid #00FF41; "
    "  box-shadow: 1px 5px 15px rgba(0, 255, 65, 0.3); " 
    "}"
    "menu menuitem { "
    " background-color: #000000;" 
    " color: #00FF41; transition: all 0.1s ease; "
    " box-shadow: 1px 5px 15px rgba(0, 255, 65, 0.3); "
    " border: 1px solid #004400;" 
    " margin: 0px; padding: 5px;"
    "}"
    "menu menuitem:hover { background-color: #004400; color: #000fff; }"
    "menu menuitem label { color: #00FF41; }" /* Inherit green/black from item */

    /* 4. THE EDITOR & GUTTER */
    "GtkSourceView, text { background-color: #000000; color: #00FF41; }"
    "gutter, GtkSourceView gutter text { background-color: #000000; color: #003B00; border: none; }"
    "GtkSourceView gutter text.current-line { color: #00FF41; font-weight: bold; }"
    "GtkSourceView text.current-line { background-color: #001100; }"
    "GtkSourceView { caret-color: #FFFFFF; outline: none; }"
    "GtkSourceView text:selected { background-color: #00FF41; color: #000000; }"

    /* 5. THE SIDEBAR (Treeview) */
    "treeview { background-color: #000000; color: #008800; }"
    "treeview:selected { background-color: #00ff41; color: #000000; }";
        /* The Active Line (The "Yellow" equivalent in Green Mode) */
        "GtkSourceView gutter text.current-line {"
        "  color: #00FF41;"
        "  font-weight: bold;"
        "  border: 1px solid #00ffff;"
        "}"   
        /* Specifically show boxes with a different color */
        "box, grid, paned {"
        "  border: 1px solid #00ffff;" /* Cyan for containers */
        "}"
        "*:treeview {"
        "  background-color: #000000;"
        "  color: #008800;" /* Muted green for sidebar names */
        "}"
        "treeview:selected {"
        "  background-color: #00ff41;"
        "  color: #000000;"
        "}"
        "GtkSourceView text.current-line {"
        "  background-color: #001A00;"
        "}";

    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(css),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_data(css, data, -1, NULL);
    return css;
}

static void insert_at_cursor(EditorApp *app, const gchar *text) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &iter, text, -1);
}

static void on_theme_change(GtkCheckMenuItem *item, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    if (gtk_check_menu_item_get_active(item)) {
        const gchar *label = gtk_menu_item_get_label(GTK_MENU_ITEM(item));
        
        if (g_strcmp0(label, "Light") == 0) app->theme_mode = 0;
        else if (g_strcmp0(label, "Dark") == 0) app->theme_mode = 1;
        else if (g_strcmp0(label, "Matrix Green") == 0) app->theme_mode = 2;
        
        apply_theme(app);
    }
}

static void on_insert_time(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M:%S", t);
    insert_at_cursor(app, buf);
}

static void on_insert_date(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    insert_at_cursor(app, buf);
}

static void on_toggle_dark_mode(GtkCheckMenuItem *item, gpointer data) {
    EditorApp *app = data;
    app->dark_mode = gtk_check_menu_item_get_active(item);
    apply_theme(app);
    // Refresh the gutter explicitly (helps stop line bug in on_tree_selection_changed) 
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(app->view), TRUE);
}

static void on_insert_datetime(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    insert_at_cursor(app, buf);
}

static void on_insert_header_comment(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", t);
    
    char buf[512];
    snprintf(buf, sizeof(buf),
        "/*\n"
        " * File: %s\n"
        " * Author: \n"
        " * Date: %s\n"
        " * Description: \n"
        " */\n\n",
        app->current_file ? g_path_get_basename(app->current_file) : "untitled.c",
        date_buf);
    
    insert_at_cursor(app, buf);
}

static void on_insert_function_comment(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *comment = 
        "/*\n"
        " * Function: \n"
        " * Description: \n"
        " * Parameters: \n"
        " * Returns: \n"
        " */\n";
    insert_at_cursor(app, comment);
}

static void on_insert_main(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "int main(int argc, char *argv[]) {\n"
        "    \n"
        "    return 0;\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_for_loop(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "for (int i = 0; i < n; i++) {\n    \n}\n";
    insert_at_cursor(app, code);
}

static void on_insert_while_loop(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "while (condition) {\n    \n}\n";
    insert_at_cursor(app, code);
}

static void on_insert_if_else(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "if (condition) {\n    \n} else {\n    \n}\n";
    insert_at_cursor(app, code);
}

static void on_insert_switch(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "switch (variable) {\n"
        "    case value1:\n"
        "        break;\n"
        "    case value2:\n"
        "        break;\n"
        "    default:\n"
        "        break;\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_struct(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "typedef struct {\n"
        "    \n"
        "} name_t;\n";
    insert_at_cursor(app, code);
}

static void on_comment_selection(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    
    if (!gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        /* No selection, comment current line */
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &start,
                                         gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
        end = start;
        gtk_text_iter_set_line_offset(&start, 0);
        if (!gtk_text_iter_ends_line(&end))
            gtk_text_iter_forward_to_line_end(&end);
    }
    
    gint start_line = gtk_text_iter_get_line(&start);
    gint end_line = gtk_text_iter_get_line(&end);
    
    /* Comment each line */
    for (gint line = start_line; line <= end_line; line++) {
        GtkTextIter line_start;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &line_start, line);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(app->buffer), &line_start, "// ", -1);
    }
}

static void on_uncomment_selection(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    
    if (!gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end)) {
        /* No selection, uncomment current line */
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &start,
                                         gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));
        end = start;
        gtk_text_iter_set_line_offset(&start, 0);
        if (!gtk_text_iter_ends_line(&end))
            gtk_text_iter_forward_to_line_end(&end);
    }
    
    gint start_line = gtk_text_iter_get_line(&start);
    gint end_line = gtk_text_iter_get_line(&end);
    
    /* Uncomment each line */
    for (gint line = start_line; line <= end_line; line++) {
        GtkTextIter line_start, line_end;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &line_start, line);
        line_end = line_start;
        
        /* Skip whitespace */
        while (!gtk_text_iter_ends_line(&line_end) && 
               g_unichar_isspace(gtk_text_iter_get_char(&line_end))) {
            gtk_text_iter_forward_char(&line_end);
        }
        
        /* Check for // comment */
        if (gtk_text_iter_get_char(&line_end) == '/' && 
            gtk_text_iter_forward_char(&line_end) &&
            gtk_text_iter_get_char(&line_end) == '/') {
            
            GtkTextIter delete_start = line_end;
            gtk_text_iter_backward_char(&delete_start);
            gtk_text_iter_forward_char(&line_end);
            
            /* Also remove space after // if present */
            if (gtk_text_iter_get_char(&line_end) == ' ') {
                gtk_text_iter_forward_char(&line_end);
            }
            
            gtk_text_buffer_delete(GTK_TEXT_BUFFER(app->buffer), &delete_start, &line_end);
        }
    }
}

static void on_block_comment(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;

    if (gtk_text_buffer_get_selection_bounds(
            GTK_TEXT_BUFFER(app->buffer), &start, &end)) {

        gtk_text_buffer_insert(
            GTK_TEXT_BUFFER(app->buffer), &start, "/*\n", -1);

        /* Re-fetch end iterator AFTER insertion */
        gtk_text_buffer_get_iter_at_mark(
            GTK_TEXT_BUFFER(app->buffer),
            &end,
            gtk_text_buffer_get_selection_bound(GTK_TEXT_BUFFER(app->buffer))
        );

        gtk_text_buffer_insert(
            GTK_TEXT_BUFFER(app->buffer), &end, "\n*/", -1);
    } else {
        insert_at_cursor(app, "/* */");
    }
}

static void update_status(EditorApp *app) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(app->buffer), &iter, 
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(app->buffer)));

    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;
    int total_lines = gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(app->buffer));

    gchar *status;
    if (app->focus_mode) {
        status = g_strdup_printf("Line: %d, Col: %d | Total Lines: %d | [FOCUS MODE]", 
                                 line, col, total_lines);
    } else {
        status = g_strdup_printf("Line: %d, Col: %d | Total Lines: %d", 
                                 line, col, total_lines);
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), status);
    g_free(status);
}
/*major bug fix: Missing line numbers due to app repaint */ 
static void on_tree_selection_changed(GtkTreeSelection *selection, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint line_num;
        gtk_tree_model_get(model, &iter, COL_LINE, &line_num, -1);
        
        if (line_num > 0) {
            app->focus_mode = TRUE;

            GtkTextIter start, end;
            /* GTK uses 0-indexed line numbers */
            gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(app->buffer), &start, line_num - 1);
            
            /* 1. Clear ALL previous yellow highlights from the buffer */
            GtkTextIter buf_start, buf_end;
            gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &buf_start, &buf_end);
            gtk_text_buffer_remove_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "jump_highlight", &buf_start, &buf_end);

            /* 2. Find the end of the line to highlight the whole row */
            end = start;
            if (!gtk_text_iter_ends_line(&end)) {
                gtk_text_iter_forward_to_line_end(&end);
            }

            /* 3. Apply the 'jump_highlight' tag (defined as yellow in main) */
            gtk_text_buffer_apply_tag_by_name(GTK_TEXT_BUFFER(app->buffer), "jump_highlight", &start, &end);

            /* 4. Move cursor and scroll to center (0.5 yalign) */
            gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(app->buffer), &start);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->view), &start, 0.0, TRUE, 0.0, 0.5);

            update_status(app);
            gtk_widget_grab_focus(app->view);
            gtk_widget_queue_draw(app->view);
        }
    }
}

static gboolean is_identifier_char(char c) {
    return isalnum(c) || c == '_';
}

static gboolean skip_whitespace(const char **ptr) {
    while (**ptr && isspace(**ptr)) {
        (*ptr)++;
    }
    return **ptr != '\0';
}

static void extract_identifier(const char **ptr, char *buf, size_t size) {
    size_t i = 0;
    while (**ptr && is_identifier_char(**ptr) && i < size - 1) {
        buf[i++] = **ptr;
        (*ptr)++;
    }
    buf[i] = '\0';
}
/* 
 * The current logic increments line_num every time strtok finds a token, 
 * but if your code has multiple consecutive newlines (empty lines), strtok treats them 
 * as a single delimiter and "jumps" over them without incrementing your counter.
 *

static void parse_symbols(EditorApp *app) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
    
    gtk_tree_store_clear(app->tree_store);
    
    GtkTreeIter func_iter, struct_iter, typedef_iter, enum_iter;
    gboolean has_funcs = FALSE, has_structs = FALSE, has_typedefs = FALSE, has_enums = FALSE;
    
    char *line = strtok(text, "\n");
    int line_num = 1;
    
    while (line != NULL) {
        const char *ptr = line;
        
        // Skip leading whitespace and comments 
        while (*ptr && isspace(*ptr)) ptr++;
        
        if (strncmp(ptr, "//", 2) == 0 || *ptr == '#') {
            line = strtok(NULL, "\n");
            line_num++;
            continue;
        }
        
        // Check for typedef 
        if (strncmp(ptr, "typedef", 7) == 0 && !is_identifier_char(ptr[7])) {
            ptr += 7;
            if (!skip_whitespace(&ptr)) goto next_line;
            
            char type_buf[256];
            
            // Handle typedef struct 
            if (strncmp(ptr, "struct", 6) == 0 && !is_identifier_char(ptr[6])) {
                ptr += 6;
                if (!skip_whitespace(&ptr)) goto next_line;
                
                // Skip struct name if present 
                if (is_identifier_char(*ptr)) {
                    while (*ptr && is_identifier_char(*ptr)) ptr++;
                    if (!skip_whitespace(&ptr)) goto next_line;
                }
                
                // Skip to closing brace and typedef name 
                char *brace = strchr(ptr, '}');
                if (brace) {
                    ptr = brace + 1;
                    if (!skip_whitespace(&ptr)) goto next_line;
                    extract_identifier(&ptr, type_buf, sizeof(type_buf));
                    
                    if (type_buf[0]) {
                        if (!has_typedefs) {
                            gtk_tree_store_append(app->tree_store, &typedef_iter, NULL);
                            gtk_tree_store_set(app->tree_store, &typedef_iter, 
                                             COL_NAME, "Typedefs", COL_LINE, 0, -1);
                            has_typedefs = TRUE;
                        }
                        GtkTreeIter child;
                        gtk_tree_store_append(app->tree_store, &child, &typedef_iter);
                        gtk_tree_store_set(app->tree_store, &child, 
                                         COL_NAME, type_buf, COL_LINE, line_num, -1);
                    }
                }
            }
            // Handle typedef enum 
            else if (strncmp(ptr, "enum", 4) == 0 && !is_identifier_char(ptr[4])) {
                ptr += 4;
                if (!skip_whitespace(&ptr)) goto next_line;
                
                char *brace = strchr(ptr, '}');
                if (brace) {
                    ptr = brace + 1;
                    if (!skip_whitespace(&ptr)) goto next_line;
                    extract_identifier(&ptr, type_buf, sizeof(type_buf));
                    
                    if (type_buf[0]) {
                        if (!has_typedefs) {
                            gtk_tree_store_append(app->tree_store, &typedef_iter, NULL);
                            gtk_tree_store_set(app->tree_store, &typedef_iter, 
                                             COL_NAME, "Typedefs", COL_LINE, 0, -1);
                            has_typedefs = TRUE;
                        }
                        GtkTreeIter child;
                        gtk_tree_store_append(app->tree_store, &child, &typedef_iter);
                        gtk_tree_store_set(app->tree_store, &child, 
                                         COL_NAME, type_buf, COL_LINE, line_num, -1);
                    }
                }
            }
        }
        // Check for struct 
        else if (strncmp(ptr, "struct", 6) == 0 && !is_identifier_char(ptr[6])) {
            ptr += 6;
            if (!skip_whitespace(&ptr)) goto next_line;
            
            char struct_name[256];
            extract_identifier(&ptr, struct_name, sizeof(struct_name));
            
            if (struct_name[0]) {
                if (!has_structs) {
                    gtk_tree_store_append(app->tree_store, &struct_iter, NULL);
                    gtk_tree_store_set(app->tree_store, &struct_iter, 
                                     COL_NAME, "Structures", COL_LINE, 0, -1);
                    has_structs = TRUE;
                }
                GtkTreeIter child;
                gtk_tree_store_append(app->tree_store, &child, &struct_iter);
                gtk_tree_store_set(app->tree_store, &child, 
                                 COL_NAME, struct_name, COL_LINE, line_num, -1);
            }
        }
        // Check for enum 
        else if (strncmp(ptr, "enum", 4) == 0 && !is_identifier_char(ptr[4])) {
            ptr += 4;
            if (!skip_whitespace(&ptr)) goto next_line;
            
            char enum_name[256];
            extract_identifier(&ptr, enum_name, sizeof(enum_name));
            
            if (enum_name[0]) {
                if (!has_enums) {
                    gtk_tree_store_append(app->tree_store, &enum_iter, NULL);
                    gtk_tree_store_set(app->tree_store, &enum_iter, 
                                     COL_NAME, "Enums", COL_LINE, 0, -1);
                    has_enums = TRUE;
                }
                GtkTreeIter child;
                gtk_tree_store_append(app->tree_store, &child, &enum_iter);
                gtk_tree_store_set(app->tree_store, &child, 
                                 COL_NAME, enum_name, COL_LINE, line_num, -1);
            }
        }
        // Check for functions - look for pattern: type name(...) 
        else {
            const char *scan = ptr;
            
            // Skip static keyword if present 
            if (strncmp(scan, "static", 6) == 0 && isspace(scan[6])) {
                scan += 6;
                while (*scan && isspace(*scan)) scan++;
            }
            
            // Look for a potential return type followed by identifier and ( 
            const char *maybe_func = scan;
            int paren_pos = -1;
            
            // Scan the line for identifier followed by ( 
            for (int i = 0; maybe_func[i] && i < 200; i++) {
                if (maybe_func[i] == '(') {
                    paren_pos = i;
                    break;
                }
            }
            
            if (paren_pos > 0) {
                // Work backwards from ( to find function name 
                int name_end = paren_pos - 1;
                while (name_end >= 0 && isspace(maybe_func[name_end])) name_end--;
                
                if (name_end >= 0 && is_identifier_char(maybe_func[name_end])) {
                    int name_start = name_end;
                    while (name_start > 0 && is_identifier_char(maybe_func[name_start - 1])) {
                        name_start--;
                    }
                    
                    char func_name[256];
                    int len = name_end - name_start + 1;
                    if (len > 0 && len < (int)sizeof(func_name)) {
                        strncpy(func_name, &maybe_func[name_start], len);
                        func_name[len] = '\0';
                        
                        // Filter out keywords 
                        if (strcmp(func_name, "if") != 0 && 
                            strcmp(func_name, "while") != 0 && 
                            strcmp(func_name, "for") != 0 && 
                            strcmp(func_name, "switch") != 0 &&
                            strcmp(func_name, "sizeof") != 0 &&
                            strcmp(func_name, "return") != 0) {
                            
                            // Check if there's a return type before the name 
                            int has_return_type = 0;
                            if (name_start > 0) {
                                int check = name_start - 1;
                                while (check >= 0 && isspace(maybe_func[check])) check--;
                                if (check >= 0 && (is_identifier_char(maybe_func[check]) || 
                                                   maybe_func[check] == '*')) {
                                    has_return_type = 1;
                                }
                            }
                            
                            if (has_return_type) {
                                if (!has_funcs) {
                                    gtk_tree_store_append(app->tree_store, &func_iter, NULL);
                                    gtk_tree_store_set(app->tree_store, &func_iter, 
                                                     COL_NAME, "Functions", COL_LINE, 0, -1);
                                    has_funcs = TRUE;
                                }
                                GtkTreeIter child;
                                gtk_tree_store_append(app->tree_store, &child, &func_iter);
                                gtk_tree_store_set(app->tree_store, &child, 
                                                 COL_NAME, func_name, COL_LINE, line_num, -1);
                            }
                        }
                    }
                }
            }
        }
        
next_line:
        line = strtok(NULL, "\n");
        line_num++;
    }
    
    g_free(text);
    
    // Expand all categories 
    gtk_tree_view_expand_all(GTK_TREE_VIEW(app->tree_view));
}
* 
* Next Version update: Key Improvements
* Direct Buffer Access: Using gtk_text_iter_get_line ensures the line number exactly matches what is displayed in the gutter.
* No more strtok: By using gtk_text_iter_forward_line, empty lines are accounted for correctly, keeping your "Symbols" tree in 
* sync with the source code.
*/
static void parse_symbols(EditorApp *app) {
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(app->buffer), &iter);
    
    gtk_tree_store_clear(app->tree_store);
    GtkTreeIter func_iter;
    gboolean has_funcs = FALSE;
    
    while (TRUE) {
        // Get the actual line number from the editor (0-indexed, so add 1)
        int line_num = gtk_text_iter_get_line(&iter) + 1;
        
        GtkTextIter line_end = iter;
        if (!gtk_text_iter_ends_line(&line_end)) gtk_text_iter_forward_to_line_end(&line_end);
        
        // Use the buffer to get text, avoiding the "too many arguments" error
        gchar *line_text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &iter, &line_end, FALSE);
        const char *ptr = line_text;
        while (*ptr && isspace(*ptr)) ptr++;
        
        // Simple C function detection
        if (*ptr != '\0' && strstr(ptr, "(") && !strstr(ptr, ";") && 
            !strstr(ptr, "if") && !strstr(ptr, "while") && !strstr(ptr, "for")) {
            
            if (!has_funcs) {
                gtk_tree_store_append(app->tree_store, &func_iter, NULL);
                gtk_tree_store_set(app->tree_store, &func_iter, COL_NAME, "Functions", COL_LINE, 0, -1);
                has_funcs = TRUE;
            }
            GtkTreeIter child;
            gtk_tree_store_append(app->tree_store, &child, &func_iter);
            gtk_tree_store_set(app->tree_store, &child, COL_NAME, ptr, COL_LINE, line_num, -1);
        }
        g_free(line_text);
        if (!gtk_text_iter_forward_line(&iter)) break;
    }
    gtk_tree_view_expand_all(GTK_TREE_VIEW(app->tree_view));
}

static void on_new(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(app->buffer), "", -1);
    if (app->current_file) {
        g_free(app->current_file);
        app->current_file = NULL;
    }
    gtk_tree_store_clear(app->tree_store);
    update_status(app);
}

static void on_open(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;
    
    dialog = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW(app->window),
                                         action, "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT, NULL);
    
    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        gchar *filename = gtk_file_chooser_get_filename(chooser);
        gchar *contents;
        gsize length;
        
        if (g_file_get_contents(filename, &contents, &length, NULL)) {
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(app->buffer), contents, length);
            g_free(contents);
            
            if (app->current_file)
                g_free(app->current_file);
            app->current_file = g_strdup(filename);
            
            /* Auto-detect language */
            GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
            GtkSourceLanguage *lang = gtk_source_language_manager_guess_language(lm, filename, NULL);
            gtk_source_buffer_set_language(app->buffer, lang);
            
            parse_symbols(app);
            update_status(app);
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_save(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    
    if (!app->current_file) {
        on_save_as(widget, data);
        return;
    }
    
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
    
    g_file_set_contents(app->current_file, text, -1, NULL);
    g_free(text);
    
    parse_symbols(app);
}

static void on_save_as(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;
    
    dialog = gtk_file_chooser_dialog_new("Save File", GTK_WINDOW(app->window),
                                         action, "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT, NULL);
    chooser = GTK_FILE_CHOOSER(dialog);
    
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    
    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(chooser);
        
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
        gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->buffer), &start, &end, FALSE);
        
        g_file_set_contents(filename, text, -1, NULL);
        g_free(text);
        
        if (app->current_file)
            g_free(app->current_file);
        app->current_file = g_strdup(filename);
        
        parse_symbols(app);
        update_status(app);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_quit(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

static void on_refresh_symbols(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    parse_symbols(app);
}

/* Function to show the updates in a popup */
static void show_update_notes(GtkWidget *widget, gpointer data) {
    GtkWidget *parent = (GtkWidget *)data;
    GtkWidget *msg_dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Scrible Sketch Book - v2.9 Updates");

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(msg_dialog),
        "• Added GCC Quick Build & Run system.\n"
        "• Integrated ANSI C syntax highlighting.\n"
        "• Font size and safe font dialog update.\n"
        "• Improved symbol browser navigation.\n\n"
        "Note: fixed library header bug in this version "
        "of Scrible. \n"
        " ");

    gtk_dialog_run(GTK_DIALOG(msg_dialog));
    gtk_widget_destroy(msg_dialog);
}

static void on_about(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog;
    GtkCssProvider *provider;
    GtkStyleContext *context;
    
    dialog = gtk_about_dialog_new();
    
    /*  Add the "Update Notes" Button at the top */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *update_btn = gtk_button_new_with_label("✨ View What's New");
    
    // Pack it at the top (index 0)
    gtk_box_pack_start(GTK_BOX(content_area), update_btn, FALSE, FALSE, 10);
    gtk_box_reorder_child(GTK_BOX(content_area), update_btn, 0);
    
    // Connect the click signal
    g_signal_connect(update_btn, "clicked", G_CALLBACK(show_update_notes), dialog);

    /*  Standard Info */
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Scrible Sketch Book");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "Version 2.9");
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), "accessories-text-editor");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), "https://github.com/oneneoncoffee/");
   
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), 
        "A GTK+ 3 code editor with syntax highlighting.\n"
        "This software is 100% free and open source.");

    const gchar *authors[] = {"Johnny Buckallew Stroud", "Contributor Name", "Special thanks to Jesus!", NULL};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

    /* Apply Black & Gold Theme ONLY to this dialog */
    provider = gtk_css_provider_new();
    const gchar *css = 
        "dialog, grid, box, stack, scrolledwindow, viewport, list, row { background-color: #000000; }"
        "label { color: #ffffff; font-weight: bold; }"
        "label.title { color: #ffa600; font-size: 1.2em; }"
        "button { background-color: #222222; color: #ffa600; border: 1px solid #ffa600; }";
    
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    
    context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), 
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    /* Show and Run */
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_widget_show_all(dialog); // Necessary to make the manually added button appear
    gtk_dialog_run(GTK_DIALOG(dialog));
    
    /* Cleanup */
    g_object_unref(provider);
    gtk_widget_destroy(dialog);
}


/* Editor menu call backs */
static void on_undo(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    /* Use GTK_SOURCE_IS_BUFFER instead of GTK_IS_SOURCE_BUFFER */
    if (GTK_SOURCE_IS_BUFFER(app->buffer) && gtk_source_buffer_can_undo(app->buffer)) {
        gtk_source_buffer_undo(app->buffer);
    }
}

static void on_redo(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    /* Use GTK_SOURCE_IS_BUFFER instead of GTK_IS_SOURCE_BUFFER */
    if (GTK_SOURCE_IS_BUFFER(app->buffer) && gtk_source_buffer_can_redo(app->buffer)) {
        gtk_source_buffer_redo(app->buffer);
    }
}
static void on_cut(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    gtk_text_buffer_cut_clipboard(GTK_TEXT_BUFFER(app->buffer), gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), TRUE);
}

static void on_copy(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    gtk_text_buffer_copy_clipboard(GTK_TEXT_BUFFER(app->buffer), gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
}

static void on_paste(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    gtk_text_buffer_paste_clipboard(GTK_TEXT_BUFFER(app->buffer), gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), NULL, TRUE);
}

static void on_delete(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    gtk_text_buffer_delete_selection(GTK_TEXT_BUFFER(app->buffer), TRUE, TRUE);
}

static void on_select_all(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(app->buffer), &start, &end);
    gtk_text_buffer_select_range(GTK_TEXT_BUFFER(app->buffer), &start, &end);
}
/* Additional Control Structure Snippets */
static void on_insert_do_while(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "do {\n    \n} while (condition);\n";
    insert_at_cursor(app, code);
}

static void on_insert_if(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "if (condition) {\n    \n}\n";
    insert_at_cursor(app, code);
}

/* Function Snippets */
static void on_insert_void_function(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "void function_name(void) {\n"
        "    \n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_int_function(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "int function_name(int param) {\n"
        "    \n"
        "    return 0;\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_ptr_function(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "void *function_name(void *param) {\n"
        "    \n"
        "    return NULL;\n"
        "}\n";
    insert_at_cursor(app, code);
}

/* Data Structure Snippets */
static void on_insert_typedef_struct(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "typedef struct {\n"
        "    int member1;\n"
        "    char member2;\n"
        "} struct_name_t;\n";
    insert_at_cursor(app, code);
}

static void on_insert_enum(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "enum enum_name {\n"
        "    VALUE1,\n"
        "    VALUE2,\n"
        "    VALUE3\n"
        "};\n";
    insert_at_cursor(app, code);
}

static void on_insert_union(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "union union_name {\n"
        "    int as_int;\n"
        "    float as_float;\n"
        "    char as_bytes[4];\n"
        "};\n";
    insert_at_cursor(app, code);
}

static void on_insert_array(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "int array[SIZE];\n";
    insert_at_cursor(app, code);
}

/* Memory Management Snippets */
static void on_insert_malloc(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "ptr = (type *)malloc(n * sizeof(type));\n"
        "if (ptr == NULL) {\n"
        "    fprintf(stderr, \"Memory allocation failed\\n\");\n"
        "    exit(EXIT_FAILURE);\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_calloc(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "ptr = (type *)calloc(n, sizeof(type));\n"
        "if (ptr == NULL) {\n"
        "    fprintf(stderr, \"Memory allocation failed\\n\");\n"
        "    exit(EXIT_FAILURE);\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_realloc(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "temp = realloc(ptr, new_size * sizeof(type));\n"
        "if (temp == NULL) {\n"
        "    fprintf(stderr, \"Reallocation failed\\n\");\n"
        "    free(ptr);\n"
        "    exit(EXIT_FAILURE);\n"
        "}\n"
        "ptr = temp;\n";
    insert_at_cursor(app, code);
}

static void on_insert_free(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "if (ptr != NULL) {\n"
        "    free(ptr);\n"
        "    ptr = NULL;\n"
        "}\n";
    insert_at_cursor(app, code);
}

/* File I/O Snippets */
static void on_insert_fopen_read(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "FILE *fp = fopen(\"filename.txt\", \"r\");\n"
        "if (fp == NULL) {\n"
        "    perror(\"Error opening file\");\n"
        "    return -1;\n"
        "}\n"
        "// Read operations here\n"
        "fclose(fp);\n";
    insert_at_cursor(app, code);
}

static void on_insert_fopen_write(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "FILE *fp = fopen(\"filename.txt\", \"w\");\n"
        "if (fp == NULL) {\n"
        "    perror(\"Error opening file\");\n"
        "    return -1;\n"
        "}\n"
        "// Write operations here\n"
        "fclose(fp);\n";
    insert_at_cursor(app, code);
}

static void on_insert_fread(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "size_t items_read = fread(buffer, sizeof(type), count, fp);\n"
        "if (items_read != count) {\n"
        "    if (feof(fp)) {\n"
        "        printf(\"End of file reached\\n\");\n"
        "    } else if (ferror(fp)) {\n"
        "        perror(\"Error reading file\");\n"
        "    }\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_fwrite(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "size_t items_written = fwrite(buffer, sizeof(type), count, fp);\n"
        "if (items_written != count) {\n"
        "    perror(\"Error writing file\");\n"
        "}\n";
    insert_at_cursor(app, code);
}

static void on_insert_fprintf(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = "fprintf(fp, \"format string\\n\", args);\n";
    insert_at_cursor(app, code);
}

static void on_insert_fscanf(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *code = 
        "if (fscanf(fp, \"format\", &var) != 1) {\n"
        "    fprintf(stderr, \"Error reading input\\n\");\n"
        "}\n";
    insert_at_cursor(app, code);
}

/* Section Comment Snippet */
static void on_insert_section_comment(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    const char *comment = 
        "/******************************************************************************\n"
        " * Section: \n"
        " * Description: \n"
        " *****************************************************************************/\n\n";
    insert_at_cursor(app, comment);
}

/* Insert add ANSI C outline */ 
static void on_insert_ansi_c_program(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", t);
    
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "/*\n"
        " * File: %s\n"
        " * Author: \n"
        " * Date: %s\n"
        " * Description: \n"
        " */\n\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n\n"
        "int main(int argc, char *argv[]) {\n"
        "    \n"
        "    return 0;\n"
        "}\n",
        app->current_file ? g_path_get_basename(app->current_file) : "program.c",
        date_buf);
    
    insert_at_cursor(app, buf);
}


static void create_menubar(EditorApp *app, GtkWidget *vbox) {
    GtkWidget *menubar = gtk_menu_bar_new();
    
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app->window), accel_group);
    
    /* --- FILE MENU --- */
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_mnemonic("_File");
    
    GtkWidget *new_item = gtk_menu_item_new_with_label("New");
    GtkWidget *open_item = gtk_menu_item_new_with_label("Open");
    GtkWidget *save_item = gtk_menu_item_new_with_label("Save");
    GtkWidget *save_as_item = gtk_menu_item_new_with_label("Save As");
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    
    // File Shortcuts
    gtk_widget_add_accelerator(new_item, "activate", accel_group, GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(open_item, "activate", accel_group, GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(save_item, "activate", accel_group, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(quit_item, "activate", accel_group, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_as_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
    
    /* --- EDIT MENU --- */
    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *edit_item = gtk_menu_item_new_with_mnemonic("_Edit");
    
    GtkWidget *undo_item = gtk_menu_item_new_with_label("Undo");
    GtkWidget *redo_item = gtk_menu_item_new_with_label("Redo");
    GtkWidget *cut_item = gtk_menu_item_new_with_label("Cut");
    GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy");
    GtkWidget *paste_item = gtk_menu_item_new_with_label("Paste");
    GtkWidget *find_item = gtk_menu_item_new_with_label("Find and Replace");
    GtkWidget *comment_item = gtk_menu_item_new_with_label("Comment Lines");
    GtkWidget *uncomment_item = gtk_menu_item_new_with_label("Uncomment Lines");
    GtkWidget *block_comment_item = gtk_menu_item_new_with_label("Block Comment");
    GtkWidget *duplicate_line_item = gtk_menu_item_new_with_label("Duplicate Line");
    GtkWidget *delete_line_item = gtk_menu_item_new_with_label("Delete Line");
    GtkWidget *goto_line_item = gtk_menu_item_new_with_label("Go to Line...");
    
    // Edit Shortcuts
    gtk_widget_add_accelerator(undo_item, "activate", accel_group, GDK_KEY_z, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(redo_item, "activate", accel_group, GDK_KEY_y, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(find_item, "activate", accel_group, GDK_KEY_f, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(duplicate_line_item, "activate", accel_group, GDK_KEY_d, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(delete_line_item, "activate", accel_group, GDK_KEY_k, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(goto_line_item, "activate", accel_group, GDK_KEY_g, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), duplicate_line_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), delete_line_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), goto_line_item);
    g_signal_connect(duplicate_line_item, "activate", G_CALLBACK(on_duplicate_line), app);
    g_signal_connect(delete_line_item, "activate", G_CALLBACK(on_delete_line), app);
    g_signal_connect(goto_line_item, "activate", G_CALLBACK(on_goto_line), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), undo_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), redo_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), cut_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), find_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), comment_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), uncomment_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), block_comment_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_item), edit_menu);

    /* --- INSERT MENU (Snippets) --- */
    GtkWidget *insert_menu = gtk_menu_new();
    GtkWidget *insert_item = gtk_menu_item_new_with_mnemonic("_Insert");

GtkWidget *ansi_c_program_item = gtk_menu_item_new_with_label("ANSI C Program Template");
g_signal_connect(ansi_c_program_item, "activate", G_CALLBACK(on_insert_ansi_c_program), app);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), ansi_c_program_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), gtk_separator_menu_item_new());
   
/* Control Structures Submenu */
GtkWidget *control_menu = gtk_menu_new();
GtkWidget *control_item = gtk_menu_item_new_with_label("Control Structures");
GtkWidget *for_snip = gtk_menu_item_new_with_label("for loop");
GtkWidget *while_snip = gtk_menu_item_new_with_label("while loop");
GtkWidget *do_while_snip = gtk_menu_item_new_with_label("do-while loop");
GtkWidget *if_snip = gtk_menu_item_new_with_label("if statement");
GtkWidget *if_else_snip = gtk_menu_item_new_with_label("if-else statement");
GtkWidget *switch_snip = gtk_menu_item_new_with_label("switch statement");
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), for_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), while_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), do_while_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), gtk_separator_menu_item_new());
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), if_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), if_else_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(control_menu), switch_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(control_item), control_menu);

/* Functions Submenu */
GtkWidget *func_menu = gtk_menu_new();
GtkWidget *func_item = gtk_menu_item_new_with_label("Functions");
GtkWidget *main_snip = gtk_menu_item_new_with_label("main() function");
GtkWidget *void_func_snip = gtk_menu_item_new_with_label("void function");
GtkWidget *int_func_snip = gtk_menu_item_new_with_label("int function");
GtkWidget *ptr_func_snip = gtk_menu_item_new_with_label("pointer function");
gtk_menu_shell_append(GTK_MENU_SHELL(func_menu), main_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(func_menu), void_func_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(func_menu), int_func_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(func_menu), ptr_func_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(func_item), func_menu);

/* Data Structures Submenu */
GtkWidget *data_menu = gtk_menu_new();
GtkWidget *data_item = gtk_menu_item_new_with_label("Data Structures");
GtkWidget *struct_snip = gtk_menu_item_new_with_label("struct definition");
GtkWidget *typedef_struct_snip = gtk_menu_item_new_with_label("typedef struct");
GtkWidget *enum_snip = gtk_menu_item_new_with_label("enum definition");
GtkWidget *union_snip = gtk_menu_item_new_with_label("union definition");
GtkWidget *array_snip = gtk_menu_item_new_with_label("array declaration");
gtk_menu_shell_append(GTK_MENU_SHELL(data_menu), struct_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(data_menu), typedef_struct_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(data_menu), enum_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(data_menu), union_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(data_menu), array_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(data_item), data_menu);

/* Memory Management Submenu */
GtkWidget *mem_menu = gtk_menu_new();
GtkWidget *mem_item = gtk_menu_item_new_with_label("Memory Management");
GtkWidget *malloc_snip = gtk_menu_item_new_with_label("malloc");
GtkWidget *calloc_snip = gtk_menu_item_new_with_label("calloc");
GtkWidget *realloc_snip = gtk_menu_item_new_with_label("realloc");
GtkWidget *free_snip = gtk_menu_item_new_with_label("free with NULL check");
gtk_menu_shell_append(GTK_MENU_SHELL(mem_menu), malloc_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(mem_menu), calloc_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(mem_menu), realloc_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(mem_menu), free_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(mem_item), mem_menu);

/* File I/O Submenu */
GtkWidget *fileio_menu = gtk_menu_new();
GtkWidget *fileio_item = gtk_menu_item_new_with_label("File I/O");
GtkWidget *fopen_snip = gtk_menu_item_new_with_label("fopen read");
GtkWidget *fopen_write_snip = gtk_menu_item_new_with_label("fopen write");
GtkWidget *fread_snip = gtk_menu_item_new_with_label("fread");
GtkWidget *fwrite_snip = gtk_menu_item_new_with_label("fwrite");
GtkWidget *fprintf_snip = gtk_menu_item_new_with_label("fprintf");
GtkWidget *fscanf_snip = gtk_menu_item_new_with_label("fscanf");
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fopen_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fopen_write_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fread_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fwrite_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fprintf_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(fileio_menu), fscanf_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileio_item), fileio_menu);

/* Comments Submenu */
GtkWidget *comment_menu = gtk_menu_new();
GtkWidget *comment_snip_item = gtk_menu_item_new_with_label("Comments");
GtkWidget *header_comment_snip = gtk_menu_item_new_with_label("Header Comment");
GtkWidget *func_comment_snip = gtk_menu_item_new_with_label("Function Comment");
GtkWidget *section_comment_snip = gtk_menu_item_new_with_label("Section Comment");
gtk_menu_shell_append(GTK_MENU_SHELL(comment_menu), header_comment_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(comment_menu), func_comment_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(comment_menu), section_comment_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(comment_snip_item), comment_menu);

/* DateTime Submenu */
GtkWidget *dt_menu = gtk_menu_new();
GtkWidget *dt_item = gtk_menu_item_new_with_label("Date/Time");
GtkWidget *time_snip = gtk_menu_item_new_with_label("Current Time");
GtkWidget *date_snip = gtk_menu_item_new_with_label("Current Date");
GtkWidget *datetime_snip = gtk_menu_item_new_with_label("Date and Time");
gtk_menu_shell_append(GTK_MENU_SHELL(dt_menu), time_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(dt_menu), date_snip);
gtk_menu_shell_append(GTK_MENU_SHELL(dt_menu), datetime_snip);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(dt_item), dt_menu);

/* Assemble Insert Menu */
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), control_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), func_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), data_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), mem_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), fileio_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), gtk_separator_menu_item_new());
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), comment_snip_item);
gtk_menu_shell_append(GTK_MENU_SHELL(insert_menu), dt_item);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(insert_item), insert_menu);

/* Control Structures */
g_signal_connect(for_snip, "activate", G_CALLBACK(on_insert_for_loop), app);
g_signal_connect(while_snip, "activate", G_CALLBACK(on_insert_while_loop), app);
g_signal_connect(do_while_snip, "activate", G_CALLBACK(on_insert_do_while), app);
g_signal_connect(if_snip, "activate", G_CALLBACK(on_insert_if), app);
g_signal_connect(if_else_snip, "activate", G_CALLBACK(on_insert_if_else), app);
g_signal_connect(switch_snip, "activate", G_CALLBACK(on_insert_switch), app);

/* Functions */
g_signal_connect(main_snip, "activate", G_CALLBACK(on_insert_main), app);
g_signal_connect(void_func_snip, "activate", G_CALLBACK(on_insert_void_function), app);
g_signal_connect(int_func_snip, "activate", G_CALLBACK(on_insert_int_function), app);
g_signal_connect(ptr_func_snip, "activate", G_CALLBACK(on_insert_ptr_function), app);

/* Data Structures */
g_signal_connect(struct_snip, "activate", G_CALLBACK(on_insert_struct), app);
g_signal_connect(typedef_struct_snip, "activate", G_CALLBACK(on_insert_typedef_struct), app);
g_signal_connect(enum_snip, "activate", G_CALLBACK(on_insert_enum), app);
g_signal_connect(union_snip, "activate", G_CALLBACK(on_insert_union), app);
g_signal_connect(array_snip, "activate", G_CALLBACK(on_insert_array), app);

/* Memory Management */
g_signal_connect(malloc_snip, "activate", G_CALLBACK(on_insert_malloc), app);
g_signal_connect(calloc_snip, "activate", G_CALLBACK(on_insert_calloc), app);
g_signal_connect(realloc_snip, "activate", G_CALLBACK(on_insert_realloc), app);
g_signal_connect(free_snip, "activate", G_CALLBACK(on_insert_free), app);

/* File I/O */
g_signal_connect(fopen_snip, "activate", G_CALLBACK(on_insert_fopen_read), app);
g_signal_connect(fopen_write_snip, "activate", G_CALLBACK(on_insert_fopen_write), app);
g_signal_connect(fread_snip, "activate", G_CALLBACK(on_insert_fread), app);
g_signal_connect(fwrite_snip, "activate", G_CALLBACK(on_insert_fwrite), app);
g_signal_connect(fprintf_snip, "activate", G_CALLBACK(on_insert_fprintf), app);
g_signal_connect(fscanf_snip, "activate", G_CALLBACK(on_insert_fscanf), app);

/* Comments */
g_signal_connect(header_comment_snip, "activate", G_CALLBACK(on_insert_header_comment), app);
g_signal_connect(func_comment_snip, "activate", G_CALLBACK(on_insert_function_comment), app);
g_signal_connect(section_comment_snip, "activate", G_CALLBACK(on_insert_section_comment), app);

/* Date/Time */
g_signal_connect(time_snip, "activate", G_CALLBACK(on_insert_time), app);
g_signal_connect(date_snip, "activate", G_CALLBACK(on_insert_date), app);
g_signal_connect(datetime_snip, "activate", G_CALLBACK(on_insert_datetime), app);
    
   
    /* --- VIEW MENU (With Integrated Themes) --- */
    GtkWidget *view_menu = gtk_menu_new();
    GtkWidget *view_item = gtk_menu_item_new_with_mnemonic("_View");

    GtkWidget *font_item = gtk_menu_item_new_with_label("Change Font...");
    g_signal_connect(font_item, "activate", G_CALLBACK(on_change_font), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), font_item);

    GtkWidget *toggle_side = gtk_menu_item_new_with_label("Toggle Sidebar");
    g_signal_connect(toggle_side, "activate", G_CALLBACK(on_toggle_sidebar), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), toggle_side);
    GtkWidget *focus_item = gtk_menu_item_new_with_label("Focus Mode");
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), focus_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_item), view_menu);
   

    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh Symbols");
    
    // Theme Submenu
    GtkWidget *theme_menu = gtk_menu_new();
    GtkWidget *theme_root = gtk_menu_item_new_with_label("Themes");
    GSList *group = NULL;
    GtkWidget *light_opt = gtk_radio_menu_item_new_with_label(group, "Light");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(light_opt));
    GtkWidget *dark_opt = gtk_radio_menu_item_new_with_label(group, "Dark");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(dark_opt));
    GtkWidget *green_opt = gtk_radio_menu_item_new_with_label(group, "Matrix Green");

    gtk_menu_shell_append(GTK_MENU_SHELL(theme_menu), light_opt);
    gtk_menu_shell_append(GTK_MENU_SHELL(theme_menu), dark_opt);
    gtk_menu_shell_append(GTK_MENU_SHELL(theme_menu), green_opt);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(theme_root), theme_menu);

    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), refresh_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), theme_root);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_item), view_menu);

    /* --- TOOLS & HELP --- */
    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_mnemonic("_Tools");
    GtkWidget *restart_item = gtk_menu_item_new_with_label("Save & Restart");
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), restart_item);
   
    GtkWidget *color_picker_item = gtk_menu_item_new_with_label("Hex Color Picker");
    g_signal_connect(color_picker_item, "activate", G_CALLBACK(on_color_picker), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), color_picker_item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_item), tools_menu);

/* --- TRANSFORM MENU --- */
    GtkWidget *trans_menu = gtk_menu_new();
    GtkWidget *trans_item = gtk_menu_item_new_with_mnemonic("_Transform");

    // Uppercase
    GtkWidget *up_item = gtk_menu_item_new_with_label("Selection to Uppercase");
    g_signal_connect(up_item, "activate", G_CALLBACK(on_uppercase), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(trans_menu), up_item);

    // Sort
    GtkWidget *sort_item = gtk_menu_item_new_with_label("Sort Selected Lines");
    g_signal_connect(sort_item, "activate", G_CALLBACK(on_sort_selection), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(trans_menu), sort_item);

    // Document Statistics (Fixing the separator warning here)
    gtk_menu_shell_append(GTK_MENU_SHELL(trans_menu), gtk_separator_menu_item_new());

    GtkWidget *stat_item = gtk_menu_item_new_with_label("Word Count & Stats");
    g_signal_connect(stat_item, "activate", G_CALLBACK(on_word_count), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(trans_menu), stat_item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(trans_item), trans_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), trans_item);
   
    /* Format menu */
    GtkWidget *format_menu = gtk_menu_new();
    GtkWidget *format_item = gtk_menu_item_new_with_mnemonic("F_ormat");

    GtkWidget *strip_item = gtk_menu_item_new_with_label("Strip Trailing Spaces");
    g_signal_connect(strip_item, "activate", G_CALLBACK(on_strip_trailing), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(format_menu), strip_item);

    // Re-using the Sort and Uppercase tools here fits well too
    gtk_menu_shell_append(GTK_MENU_SHELL(format_menu), gtk_separator_menu_item_new());

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(format_item), format_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), format_item);

    /* BUILD GCC MENU */
    GtkWidget *build_menu = gtk_menu_new();
    GtkWidget *build_item = gtk_menu_item_new_with_mnemonic("_Build");

    GtkWidget *run_item = gtk_menu_item_new_with_label("Build & Run... (Custom)");
    g_signal_connect(run_item, "activate", G_CALLBACK(on_build_run), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(build_menu), run_item);

    GtkWidget *quick_build_item = gtk_menu_item_new_with_label("Quick Build & Run");
    // Add shortcut
    gtk_widget_add_accelerator(quick_build_item, "activate", accel_group, 
                           GDK_KEY_F5, 0, GTK_ACCEL_VISIBLE);
    g_signal_connect(quick_build_item, "activate", G_CALLBACK(on_quick_build), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(build_menu), quick_build_item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(build_item), build_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), build_item);

/* --- BOOKMARKS MENU --- */
GtkWidget *bookmarks_menu = gtk_menu_new();
GtkWidget *bookmarks_item = gtk_menu_item_new_with_mnemonic("_Bookmarks");

GtkWidget *toggle_bookmark_item = gtk_menu_item_new_with_label("Toggle Bookmark");
GtkWidget *next_bookmark_item = gtk_menu_item_new_with_label("Next Bookmark");
GtkWidget *prev_bookmark_item = gtk_menu_item_new_with_label("Previous Bookmark");
GtkWidget *list_bookmarks_item = gtk_menu_item_new_with_label("List All Bookmarks");
GtkWidget *clear_bookmarks_item = gtk_menu_item_new_with_label("Clear All Bookmarks");

// Bookmark Shortcuts
gtk_widget_add_accelerator(toggle_bookmark_item, "activate", accel_group, 
                          GDK_KEY_b, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
gtk_widget_add_accelerator(next_bookmark_item, "activate", accel_group, 
                          GDK_KEY_F2, 0, GTK_ACCEL_VISIBLE);
gtk_widget_add_accelerator(prev_bookmark_item, "activate", accel_group, 
                          GDK_KEY_F2, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
gtk_widget_add_accelerator(list_bookmarks_item, "activate", accel_group, 
                          GDK_KEY_b, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), toggle_bookmark_item);
gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), next_bookmark_item);
gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), prev_bookmark_item);
gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), gtk_separator_menu_item_new());
gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), list_bookmarks_item);
gtk_menu_shell_append(GTK_MENU_SHELL(bookmarks_menu), clear_bookmarks_item);
gtk_menu_item_set_submenu(GTK_MENU_ITEM(bookmarks_item), bookmarks_menu);

// Connect signals
g_signal_connect(toggle_bookmark_item, "activate", G_CALLBACK(on_toggle_bookmark), app);
g_signal_connect(next_bookmark_item, "activate", G_CALLBACK(on_next_bookmark), app);
g_signal_connect(prev_bookmark_item, "activate", G_CALLBACK(on_previous_bookmark), app);
g_signal_connect(list_bookmarks_item, "activate", G_CALLBACK(on_list_bookmarks), app);
g_signal_connect(clear_bookmarks_item, "activate", G_CALLBACK(on_clear_all_bookmarks), app);



    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_mnemonic("_Help");
    GtkWidget *about_item = gtk_menu_item_new_with_label("About");
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

/* --- Search Menu --- */
GtkWidget *search_menu = gtk_menu_new();
GtkWidget *search_root = gtk_menu_item_new_with_label("Search");
gtk_menu_item_set_submenu(GTK_MENU_ITEM(search_root), search_menu);

GtkWidget *mi_show_search = gtk_menu_item_new_with_label("Quick Find (Search Bar)");
g_signal_connect(mi_show_search, "activate", G_CALLBACK(on_toggle_search_bar), app);
gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), mi_show_search);

GtkWidget *mi_adv_find = gtk_menu_item_new_with_label("Advanced Find/Replace...");
g_signal_connect(mi_adv_find, "activate", G_CALLBACK(on_find_replace), app);
gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), mi_adv_find);

gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), gtk_separator_menu_item_new());

GtkWidget *mi_jump_line = gtk_menu_item_new_with_label("Go to Line...");
g_signal_connect(mi_jump_line, "activate", G_CALLBACK(on_goto_line), app);
gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), mi_jump_line);

GtkWidget *mi_jump_bookmark = gtk_menu_item_new_with_label("Next Bookmark");
g_signal_connect(mi_jump_bookmark, "activate", G_CALLBACK(on_next_bookmark), app);
gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), mi_jump_bookmark);

gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), gtk_separator_menu_item_new());

GtkWidget *mi_clear_highlights = gtk_menu_item_new_with_label("Clear Highlights");
g_signal_connect(mi_clear_highlights, "activate", G_CALLBACK(on_clear_search), app);
gtk_menu_shell_append(GTK_MENU_SHELL(search_menu), mi_clear_highlights);

gtk_menu_shell_append(GTK_MENU_SHELL(menubar), search_root);

    /* --- ASSEMBLE BAR --- */
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), insert_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), bookmarks_item); 
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), tools_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_item);

    /* --- CONNECT SIGNALS --- */
    g_signal_connect(new_item, "activate", G_CALLBACK(on_new), app);
    g_signal_connect(open_item, "activate", G_CALLBACK(on_open), app);
    g_signal_connect(save_item, "activate", G_CALLBACK(on_save), app);
    g_signal_connect(save_as_item, "activate", G_CALLBACK(on_save_as), app);
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), app);
    
    g_signal_connect(undo_item, "activate", G_CALLBACK(on_undo), app);
    g_signal_connect(redo_item, "activate", G_CALLBACK(on_redo), app);
    g_signal_connect(cut_item, "activate", G_CALLBACK(on_cut), app);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), app);
    g_signal_connect(paste_item, "activate", G_CALLBACK(on_paste), app);
    g_signal_connect(find_item, "activate", G_CALLBACK(on_find_replace), app);
    g_signal_connect(comment_item, "activate", G_CALLBACK(on_comment_selection), app);
    g_signal_connect(uncomment_item, "activate", G_CALLBACK(on_uncomment_selection), app);
    g_signal_connect(block_comment_item, "activate", G_CALLBACK(on_block_comment), app);

    g_signal_connect(main_snip, "activate", G_CALLBACK(on_insert_main), app);
    g_signal_connect(for_snip, "activate", G_CALLBACK(on_insert_for_loop), app);
    g_signal_connect(time_snip, "activate", G_CALLBACK(on_insert_time), app);
    
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_symbols), app);
    g_signal_connect(light_opt, "toggled", G_CALLBACK(on_theme_change), app);
    g_signal_connect(dark_opt, "toggled", G_CALLBACK(on_theme_change), app);
    g_signal_connect(green_opt, "toggled", G_CALLBACK(on_theme_change), app);
    
    g_signal_connect(restart_item, "activate", G_CALLBACK(on_restart_editor), app);
    g_signal_connect(about_item, "activate", G_CALLBACK(on_about), app);

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
}
/* Cursor moved callback */
static void on_cursor_moved(GtkTextBuffer *buffer,
                            GtkTextIter   *location,
                            GtkTextMark   *mark,
                            gpointer       data)
{
    EditorApp *app = (EditorApp *)data;
  
    // Optional: Turn off focus mode when user moves cursor manually
    // app->focus_mode = FALSE; 
    update_status(app);
    if (!app || !app->status_label) return;

    /* Only update when the INSERT cursor moves */
    if (mark == gtk_text_buffer_get_insert(buffer)) {
        update_status(app);
    }
}

static void on_change_font(GtkWidget *widget, gpointer data) {
    EditorApp *app = (EditorApp *)data;
    GtkWidget *dialog;
    
    dialog = gtk_font_chooser_dialog_new("Choose Editor Font", GTK_WINDOW(app->window));

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gchar *font_desc_str = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));
        PangoFontDescription *desc = pango_font_description_from_string(font_desc_str);
        
        const char *family = pango_font_description_get_family(desc);
        int size = pango_font_description_get_size(desc);
        
        // Pango sizes are in points * PANGO_SCALE or absolute pixels
        // Convert to a standard point size for CSS
        double size_pt = (double)size / PANGO_SCALE;

        GtkCssProvider *provider = gtk_css_provider_new();
        
        /* * We construct a clean CSS string. 
         * Using 'font-family' and 'font-size' separately is safer than the shorthand.
         */
        gchar *css = g_strdup_printf(
            "textview { font-family: '%s'; font-size: %.1fpt; }", 
            family, size_pt
        );
        
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        
        GtkStyleContext *context = gtk_widget_get_style_context(app->view);
        gtk_style_context_add_provider(context, 
                                       GTK_STYLE_PROVIDER(provider), 
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        
        g_print("Font updated to: %s (Parsed: %s at %.1fpt)\n", font_desc_str, family, size_pt);
        
        pango_font_description_free(desc);
        g_free(font_desc_str);
        g_free(css);
        g_object_unref(provider);
    }

    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    EditorApp *app = g_malloc0(sizeof(EditorApp));

    /* Initialize all pointers and state FIRST */
    app->dark_css = NULL;
    app->light_css = NULL;
    app->green_css = NULL;
    app->theme_mode = 1;  // Default to dark theme
    app->focus_mode = FALSE;
    app->current_file = NULL;
    app->search_settings = NULL;
    app->search_context = NULL;
    app->bookmarks = NULL;
    
    /* Window Setup */
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Scrible Code Editor");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 600);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Main Layout */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    /* Create menubar */
    create_menubar(app, vbox);

    /* Horizontal Paned container for Sidebar and Editor */
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

    /* --- Tree View (Left Pane) --- */
    app->tree_store = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_INT);
    app->tree_view  = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->tree_store));
    
    /* Enable built-in symbol search */
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(app->tree_view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(app->tree_view), COL_NAME);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        "Symbols", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree_view), column);
    
    /* Wrap TreeView in ScrolledWindow and set minimum width */
    GtkWidget *tree_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tree_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(tree_scroll, 150, 100); 
    gtk_container_add(GTK_CONTAINER(tree_scroll), app->tree_view);
    app->tree_scroll = tree_scroll; 
    
    /* Pack sidebar into the first pane */
    gtk_paned_pack1(GTK_PANED(hpaned), tree_scroll, FALSE, FALSE);

    /* --- Editor (Right Pane) --- */
    app->buffer = GTK_SOURCE_BUFFER(gtk_source_buffer_new(NULL)); 
    // Enable undo/redo history (100 steps)
    gtk_source_buffer_set_max_undo_levels(app->buffer, 100);
    app->view   = gtk_source_view_new_with_buffer(app->buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(app->view), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(app->view), TRUE);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(app->view), 4);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), app->view);
    
    /* Create the yellow highlight tag */
    gtk_text_buffer_create_tag(GTK_TEXT_BUFFER(app->buffer), "jump_highlight",
                           "background", "#ffff00", /* Bright Yellow */
                           "foreground", "#0000EE", /* blue text for contrast */
                           NULL);
    /* Create bookmark tag */ 
    gtk_text_buffer_create_tag(GTK_TEXT_BUFFER(app->buffer), "bookmark",
                       "background", "#3584e4", /* Blue background */
                       "foreground", "#ffffff", /* White text */
                       NULL);
    /* Pack editor into the second pane */
    gtk_paned_pack2(GTK_PANED(hpaned), scrolled, TRUE, FALSE);

    /* Set initial sidebar width (divider position) */
    gtk_paned_set_position(GTK_PANED(hpaned), 250);

    /* --- Bottom Status Bar --- */
    app->status_label = gtk_label_new("Ready");
    gtk_widget_set_halign(app->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(app->status_label, 5);
    gtk_widget_set_margin_end(app->status_label, 5);
    gtk_widget_set_margin_top(app->status_label, 2);
    gtk_widget_set_margin_bottom(app->status_label, 2);
    gtk_box_pack_start(GTK_BOX(vbox), app->status_label, FALSE, FALSE, 0);

    /* Signals */
    g_signal_connect(GTK_TEXT_BUFFER(app->buffer), "mark-set", 
                     G_CALLBACK(on_cursor_moved), app);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
    g_signal_connect(selection, "changed", 
                     G_CALLBACK(on_tree_selection_changed), app);

    /* Show all widgets FIRST */
    gtk_widget_show_all(app->window);
    
    /* Create theme providers AFTER widgets are visible */
    app->dark_css  = create_dark_theme();
    app->light_css = create_light_theme();
    app->green_css = create_green_theme();
    
    /* Apply the selected theme */
    apply_theme(app);
    
    /* If a filename was provided as argument, open it */
    if (argc > 1) {
        gchar *contents;
        gsize length;
        
        if (g_file_get_contents(argv[1], &contents, &length, NULL)) {
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(app->buffer), contents, length);
            g_free(contents);
            
            app->current_file = g_strdup(argv[1]);
            
            /* Auto-detect language */
            GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
            GtkSourceLanguage *lang = gtk_source_language_manager_guess_language(lm, argv[1], NULL);
            gtk_source_buffer_set_language(app->buffer, lang);
            
            parse_symbols(app);
            update_status(app);
        }
    }
    
  
  // 1. Create the Search Bar widget
app->search_bar = gtk_search_bar_new();
GtkWidget *search_entry = gtk_search_entry_new();
gtk_container_add(GTK_CONTAINER(app->search_bar), search_entry);
gtk_search_bar_connect_entry(GTK_SEARCH_BAR(app->search_bar), GTK_ENTRY(search_entry));

// 2. Pack it into your main vertical box (vbox)
// Put it above the text view scroll window
gtk_box_pack_start(GTK_BOX(vbox), app->search_bar, FALSE, FALSE, 0);

// 3. Optional: Connect the search entry to the search context
g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_entry_changed), app);

    g_print("Scrible editor initialized successfully\n"); 
    
    gtk_main();
    
    /* Cleanup */
    if (app->current_file) g_free(app->current_file);
    if (app->bookmarks) g_list_free(app->bookmarks);
    if (app->dark_css) g_object_unref(app->dark_css);
    if (app->light_css) g_object_unref(app->light_css);
    if (app->green_css) g_object_unref(app->green_css);
    g_free(app);
    
    g_print("Exiting scrible editor cleanly\n"); 
    return 0;
}
