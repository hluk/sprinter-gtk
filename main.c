/**
 * \file main.c
 *
 * Function #main is called at start.
 *
 * Program arguments are parsed using #new_options.
 *
 * Main window, widgets are created and state initialized (see #Application)
 * using #new_application.
 *
 * Control is passed to GTK main event loop from which #read_items is called
 * every time the application is idle and stdin is open. This function parses
 * items from stdin. Function is interrupted after reading at most
 * #STDIN_BATCH_SIZE characters (changing value #STDIN_BATCH_SIZE may improve
 * responsiveness).
 *
 * After main event loop finishes, program prints contents of text entry and
 * exits with exit code 0 if the text was submitted. Otherwise application
 * doesn't print anything on stdout and exits with exit code 1.
 *
 * If wrong arguments were passed to application or other error occurred
 * during execution, the program exits with exit code 2.
 */
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/select.h>

#include "sprinter_icon.h"


/** header for help (\c --help option)*/
#define HELP_HEADER ("usage: sprinter [options]\noptions:\n")

/** geometry help */
#define HELP_GEOMETRY \
"Window geometry can be changed with -g option with single argument.\n" \
"Argument format is either W[xH[X[Y]]] or [H]XY where W is width, H is height\n" \
"X is horizontal position and Y is vertical position.\n" \
"\n" \
"If width or height is negative, window width or height will be maximal.\n" \
"\n" \
"If horizontal or vertical position is negative, window is moved from right or\n" \
"bottom screen edge.\n" \
"\n" \
"Use sign (+ or -) to separate numbers immediately next to each other.\n" \
"\n" \
"Examples:\n" \
"   200x600  Window height is 200 pixels, width is 600 pixels and position is\n" \
"            at center of the screen.\n" \
"       0+0  Window is placed at the top left screen corner.\n" \
"      -1-1  Window is placed at the bottom right screen corner.\n" \
"    -1-1-1  Window has maximal height and is placed at the right screen edge.\n" \
"  -1x1+0-1  Window has maximal width and minimal height and is placed at\n" \
"            the bottom screen edge.\n"

/** buffer too small error string */
#define ERR_BUFFER_TOO_SMALL "Item text is too long (BUFSIZ is %d)!"\
    " Try changing the input separator using option -i.\n"

/**
 * Maximum number of characters read from input before the control is
 * returned to main event loop.
 * Other values might lead to better or worse responsiveness
 * while reading lot of data from stdin.
 */
#define STDIN_BATCH_SIZE 250

/** delay (in milliseconds) for list refiltering */
#define REFILTER_DELAY 200

/**\{ \name Default option values */
/** default window title */
#define DEFAULT_TITLE "sprinter"
/** default label */
#define DEFAULT_LABEL "submit"
/** default input separator */
#define DEFAULT_INPUT_SEPARATOR "\n"
/** default output separator */
#define DEFAULT_OUTPUT_SEPARATOR NULL
/** default window width */
#define DEFAULT_WINDOW_WIDTH 230
/** default window height */
#define DEFAULT_WINDOW_HEIGHT 320
/**\}*/

/** columns in list store */
enum
{
    /** visibility toggle */
    COL_VISIBLE,
    /** file icon or empty */
    COL_ICON,
    /** item text */
    COL_TEXT,
    /** number of columns */
    NUM_COLS
};

/** main window, widgets and current state */
typedef struct {
    /** main window */
    GtkWindow *window;
    /** button (with custom label) to submit text */
    GtkButton *button;
    /** text entry */
    GtkEntry *entry;
    /** item list */
    GtkTreeView *tree_view;
    /** widget for scrolling item list */
    GtkScrolledWindow *scroll_window;
    /** list store for items */
    GtkListStore *store;
    /** filtered model */
    GtkTreeModel *filtered_model;
    /** sorted model */
    GtkTreeModel *sorted_model;

    /** Temporarily toggle auto-completion. */
    gboolean complete;
    /** Temporarily toggle list filtering. */
    gboolean filter;
    /** timer for filtering items (for better performance) */
    GSource *filter_timer;

    /** Hide list initially (minimal mode). */
    gboolean hide_list;

    /** window height */
    gint height;

    /** input separator*/
    char *i_separator;
    /** output separator*/
    char *o_separator;

    /** exit code for program */
    int exit_code;

    /** text typed by user */
    gchar *original_text;
} Application;

/** program arguments */
typedef struct {
    /** short option (argument beginning with single dash) */
    const char shopt;
    /** long option (argument beginning with double dash) */
    const char *opt;
    /** option description */
    const char *help;
} Argument;

/** program options (short, long, description) */
const Argument arguments[] = {
    {'g', "geometry",          "window size and position"},
    {'h', "help",              "show this help"},
    {'i', "input-separator",   "string which separates items on input"},
    {'l', "label",             "text input label"},
    {'m', "minimal",           "hide list (press TAB key to show the list)"},
    {'o', "output-separator",  "string which separates items on output"},
    {'s', "sort",              "sort items naturally"},
    /*{'S', "strict",            "choose only items from stdin"},*/
    {'t', "title",             "title"}
};

/** undefined value for an option */
#define OPTION_UNSET -65535

/** user options (using arguments passed to program at start) */
typedef struct {
    /** main window title */
    const char *title;
    /** entry label text */
    const char *label;

    /**\{ \name Main window geometry */
    gint x,      /**< X position */
         y,      /**< Y position */
         width,  /**< width */
         height; /**< height */
    /**\}*/

    /** help requested */
    gboolean show_help;
    /** Hide list initially (minimal mode). */
    gboolean hide_list;
    /** \todo Sort list. */
    gboolean sort_list;
    /** \todo If entry text submitted, check if item with same text exists. */
    gboolean strict;

    /** input separator*/
    char *i_separator;
    /** output separator*/
    char *o_separator;

    /** options correctly parsed */
    gboolean ok;
} Options;

/** Prints help. */
void help()
{
    int len, i;
    const Argument *arg;

    g_printerr(HELP_HEADER);

    len = sizeof(arguments)/sizeof(Argument);
    for ( i = 0; i<len; ++i ) {
        arg = &arguments[i];
        g_printerr( "  -%c, --%-18s %s\n", arg->shopt, arg->opt, arg->help );
    }
}

/** Prints geometry help. */
void help_geometry()
{
    g_printerr(HELP_GEOMETRY);
}

/**
 * Unescapes string.
 * \returns new unescaped string
 */
gchar *unescape(const gchar *str)
{
    /* new string is not larger than original */
    gchar *result, *r;
    const gchar *s;
    gboolean escape;

    result = malloc( strlen(str) );

    for( s = str, r = result, escape = FALSE; *s; ++s ) {
        if (escape) {
            if (*s == 'n')
                *(r++) = '\n';
            else if (*s == 't')
                *(r++) = '\t';
            else
                *(r++) = *s;
            escape = FALSE;
        } else if (*s == '\\') {
            escape = TRUE;
        } else {
            *(r++) = *s;
        }
    }
    *r = 0;

    return result;
}

/**
 * Creates options for application.
 * Creates \a options and sets it accordingly to arguments passed to program.
 */
Options new_options(int argc, char *argv[])
{
    int w, h, x, y;
    gboolean force_arg;
    char *argp;
    char c, arg;
    int i, j, len;
    Options options;

    /* default options */
    options.title = DEFAULT_TITLE;
    options.label = DEFAULT_LABEL;
    options.show_help = options.hide_list =
        options.sort_list = options.strict = FALSE;
    options.x = options.y = OPTION_UNSET;
    options.width  = DEFAULT_WINDOW_WIDTH;
    options.height = DEFAULT_WINDOW_HEIGHT;
    options.i_separator = DEFAULT_INPUT_SEPARATOR;
    options.o_separator = DEFAULT_OUTPUT_SEPARATOR;
    options.ok = TRUE;

    len = sizeof(arguments)/sizeof(Argument);
    i = 1;
    while(i<argc) {
        argp = argv[i];
        ++i;

        if (argp[0] != '-' || argp[1] == '\0') {
            help();
            options.ok = FALSE;
            break;
        }

        j = 0;
        force_arg = FALSE;

        /* long option */
        if (argp[1] == '-') {
            argp += 2;
            for ( ; j<len; ++j) {
                if ( strcmp(argp, arguments[j].opt) == 0 )
                    break;
            }
            argp = i<argc ? argv[i] : NULL;
        }
        /* short option */
        else {
            argp += 1;
            for ( ; j<len; ++j) {
                if ( *argp == arguments[j].shopt )
                    break;
            }
            argp += 1;
            if (*argp == '\0') {
                argp = i<argc ? argv[i] : NULL;
            } else {
                force_arg = TRUE;
                --i;
            }
        }

        if ( j == len ) {
            help();
            options.ok = FALSE;
            break;
        }

        /* set options */
        arg = arguments[j].shopt;
        j = i;
        if (arg == 'g') {
            if (!argp) {
                help_geometry();
                options.ok = FALSE;
                break;
            }
            ++i;

            if ( sscanf(argp, "%dx%d%d%d%c", &w, &h, &x, &y, &c) == 4 ) {
                /* WxH+X+Y */
                options.width = w;
                options.height = h;
                options.x = x;
                options.y = y;
            } else if ( sscanf(argp, "%dx%d%d%c", &w, &h, &x, &c) == 3 ) {
                /* WxH+X */
                options.width = w;
                options.height = h;
                options.x = x;
            } else if ( sscanf(argp, "%dx%d%c", &w, &h, &c) == 2 ) {
                /* WxH */
                options.width = w;
                options.height = h;
            } else if ( sscanf(argp, "%d%d%d%c", &h, &x, &y, &c) == 3 ) {
                /* H+X+Y */
                options.height = h;
                options.x = x;
                options.y = y;
            } else if ( sscanf(argp, "%d%d%c", &x, &y, &c) == 2 ) {
                /* +X+Y */
                options.x = x;
                options.y = y;
            } else if ( sscanf(argp, "%d%c", &w, &c) == 1 ) {
                /* W */
                options.width = w;
            } else {
                help_geometry();
                options.ok = FALSE;
                break;
            }
        } else if (arg == 'h') {
            options.show_help = TRUE;
            break;
        } else if (arg == 'i') {
            if (!argp) {
                help();
                options.ok = FALSE;
                break;
            }
            ++i;
            options.i_separator = unescape(argp);
        } else if (arg == 'l') {
            if (!argp) {
                help();
                options.ok = FALSE;
                break;
            }
            ++i;
            options.label = argp;
        } else if (arg == 'm') {
            options.hide_list = TRUE;
        } else if (arg == 'o') {
            if (!argp) {
                help();
                options.ok = FALSE;
                break;
            }
            ++i;
            options.o_separator = unescape(argp);
        } else if (arg == 's') {
            options.sort_list = TRUE;
        } else if (arg == 'S') {
            options.strict = TRUE;
        } else if (arg == 't') {
            if (!argp) {
                help();
                options.ok = FALSE;
                break;
            }
            ++i;
            options.title = argp;
        } else {
            help();
            options.ok = FALSE;
            break;
        }

        if (force_arg && i == j) {
            help();
            options.ok = FALSE;
            break;
        }
    }

    options.ok &= i == argc;

    return options;
}


static void submit(Application *app)
{
    g_print( gtk_entry_get_text(app->entry) );
    app->exit_code = 0;
    gtk_main_quit();
}

/** Hides list. */
void hide_list(Application *app)
{
    gint w, h;

    gtk_widget_hide( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, 1 );
}

/** Shows list. */
void show_list(Application *app)
{
    gint w, h;

    gtk_widget_show( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, app->height );
}

/** Enables list refiltering. */
gboolean enable_filter(Application *app)
{
    app->filter = TRUE;
    return FALSE;
}

/** Disables list refiltering */
gboolean disable_filter(Application *app)
{
    app->filter = FALSE;
    return FALSE;
}

/** window key-press-event handler */
gboolean on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
    guint key;

    if (event->type == GDK_KEY_PRESS){
        key = event->key.keyval;
        switch (key)
        {
            /** If escape key pressed, exit with code 1. */
            case GDK_KEY_Escape:
                gtk_main_quit();
                return TRUE;
            /** If enter key pressed, print entry text and exit with code 0. */
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                submit(app);
                return TRUE;
        }
    }

    return FALSE;
}

/** list view key-press-event handler */
gboolean tree_view_on_key_press( GtkWidget *widget,
                                 GdkEvent *event,
                                 Application *app )
{
    guint key;
    GtkTreePath *path;

    /** If at top of the list and up key pressed, focus entry. */
    if (event->type == GDK_KEY_PRESS) {
        key = event->key.keyval;
        switch(key) {
            /**
             * If up of page up key was pressed and first item is current
             * focus text entry.
             */
            case GDK_KEY_Up:
            case GDK_KEY_Page_Up:
                gtk_tree_view_get_cursor( GTK_TREE_VIEW(widget), &path, NULL );
                if (path) {
                    gboolean hasprev = gtk_tree_path_prev(path);
                    gtk_tree_path_free(path);

                    if (!hasprev) {
                        disable_filter(app);
                        gtk_entry_set_text(app->entry, app->original_text);
                        gtk_widget_grab_focus( GTK_WIDGET(app->entry) );
                        gtk_entry_set_position(app->entry, -1);
                        enable_filter(app);
                        return TRUE;
                    }
                }
                break;
            /**
             * If left or right arrow pressed and list has focus,
             * focus widget and set cursor position.
             */
            case GDK_KEY_Left:
            case GDK_KEY_Right:
                gtk_widget_grab_focus( GTK_WIDGET(app->entry) );
                gtk_entry_set_position( app->entry,
                                        key == GDK_KEY_Left ? 0 : -1 );
                return TRUE;
        }
    }

    return FALSE;
}

/** text entry key-press-event handler */
gboolean entry_on_key_press( GtkWidget *widget,
                             GdkEvent *event,
                             Application *app )
{
    guint key;
    GtkTreePath *path;

    /** If at top of the list and up key pressed, focus entry. */
    if (event->type == GDK_KEY_PRESS) {
        key = event->key.keyval;
        switch(key) {
            /**
             * If tab or down key pressed and entry widget has focus,
             * show and focus list.
             */
            case GDK_KEY_Tab:
            case GDK_KEY_Down:
            case GDK_KEY_Page_Down:
                if (app->hide_list)
                    show_list(app);

                gtk_tree_view_get_cursor( app->tree_view, &path, NULL);
                gtk_widget_grab_focus( GTK_WIDGET(app->tree_view) );

                if (!path) {
                    path = gtk_tree_path_new_first();
                    gtk_tree_view_set_cursor( app->tree_view, path,
                                              NULL, FALSE );
                }
                gtk_tree_path_free(path);

                return TRUE;
        }
    }

    return FALSE;
}

/**
 * file icon
 * \return file icon if file with path \a filename exists, NULL otherwise
 */
GdkPixbuf *pixbuf_from_file(const gchar *filename)
{
    GdkPixbuf *pixbuf = NULL;
    GFile *file = g_file_new_for_path(filename);
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

    if (file) {
        GFileInfo *info =
            g_file_query_info( file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                               G_FILE_QUERY_INFO_NONE, NULL, NULL );
        if (info) {
            GIcon *mime_icon =
                g_content_type_get_icon( g_file_info_get_content_type(info) );
            if (mime_icon) {
                GtkIconInfo *icon_info =
                    gtk_icon_theme_lookup_by_gicon( icon_theme, mime_icon, 16,
                                                    GTK_ICON_LOOKUP_USE_BUILTIN );
                if (icon_info) {
                    pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
                    gtk_icon_info_free(icon_info);
                }
                g_object_unref(mime_icon);
            }
            g_object_unref(info);
        }
        g_object_unref(file);
    }

    return pixbuf;
}

/**
 * Match tokens.
 * Find all tokens (space separated strings in \a needle) in given order in
 * \a haystack cropped to \a max characters.
 * Search is case insensitive.
 * \return pointer to first matched substring in \a haystack, NULL if not found
 */
const gchar *match_tokens(const gchar *haystack, const gchar *needle)
{
    const gchar *h, *hh, *nn;

    if ( !*needle )
        return haystack;

    for ( h = haystack; *h; ++h ) {
        for ( hh = h, nn = needle; *hh && *nn; ++hh, ++nn ) {
            if ( *nn == ' ' ) {
                if ( match_tokens(hh, nn+1) )
                    return h;
                else
                    break;
            } else if ( toupper(*hh) != toupper(*nn) ) {
                break;
            }
        }
        if (!*nn)
            return h;
    }
    return NULL;
}

/**
 * Insert item to store.
 * Creates new row with \a text and icon (\a pixbuf) in \a store
 * at position given by \a iter.
 * Row will be hidden if \a visible is FALSE.
 * If \a text is filename or path to existing file, an icon is created.
 */
void insert_item( GtkTreeIter *iter,
                  const gchar *text,
                  GdkPixbuf *pixbuf,
                  gboolean visible,
                  GtkListStore *store )
{
    gtk_list_store_append(store, iter);
    gtk_list_store_set( store, iter,
            COL_VISIBLE, visible,
            COL_ICON, pixbuf,
            COL_TEXT, text,
            -1 );

    if (pixbuf)
        g_object_unref(pixbuf);
}

/**
 * Returns filter text and selection bounds.
 * Filter text is last output item without characters after
 * text cursor or within and after selected text region.
 * \returns filter text
 */
gchar *get_filter_text(gint *from, gint *to, Application *app)
{
    const gchar *filter_text;
    gchar *sep = app->o_separator;
    gchar *a;
    gint sep_len;

    gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), from, to);
    if (*from == *to)
        *to = gtk_entry_get_text_length(app->entry);

    filter_text = gtk_entry_get_text(app->entry);

    if (sep && *sep) {
        sep_len = strlen(sep);
        while( (a = strstr(filter_text, sep)) ) {
            filter_text = a+sep_len;
        }
    }

    return g_strndup(filter_text, *from);
}

/**
 * Appends item to list.
 * \callgraph
 */
void append_item(char *text, Application *app)
{
    gboolean visible;
    int from, to;
    GtkTreeIter iter;
    gchar *filter_text;
    GtkTreePath *path;

    filter_text = get_filter_text(&from, &to, app);
    app->complete &= from == to;

    visible = match_tokens(text, filter_text) != NULL;

    /* append new item */
    insert_item(&iter, text, pixbuf_from_file(text), visible, app->store);

    /**
     * Does in-line completion only for last output item and only if:
     * - no text in entry is selected and
     * - text cursor is at the end of entry and
     * - Application::complete is \c TRUE.
     */
    if ( app->complete && visible && !app->filter_timer &&
            gtk_entry_get_text_length(app->entry) == to ) {
        gtk_tree_view_get_cursor( app->tree_view, &path, NULL);
        if (path) {
            gtk_tree_path_free(path);
        } else {
            path = gtk_tree_path_new_first();
            gtk_tree_view_set_cursor( app->tree_view, path,
                    NULL, FALSE );
        }
    }

    g_free(filter_text);
}

/**
 * Read items from standard input.
 * After at most #STDIN_BATCH_SIZE characters read from stdin passes control
 * back to main event loop.
 * If the whole input is read at once the application may be unresponsive for
 * some time.
 * \return TRUE if no error occurred and input isn't at end
 * \callgraph
 */
gboolean read_items(Application *app)
{
    static gchar buf[BUFSIZ] = "";
    static gchar *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    fd_set stdin_fds;
    int i, c;
    const gchar *a, *b;
    const gchar *sep = app->i_separator;
    const int sep_len = strlen(sep);

    /* disable stdin buffering (otherwise select waits on new line) */
    setbuf(stdin, NULL);

    /* interrupt after reading at most N characters */
    for( i = 0; i < STDIN_BATCH_SIZE; ++i ) {
        /* check if data available */
        FD_ZERO(&stdin_fds);
        FD_SET(STDIN_FILENO, &stdin_fds);
        if ( select(STDIN_FILENO+1, &stdin_fds, NULL, NULL, &stdin_tv) <= 0 )
            break;

        /* read data */
        if ( (c = getchar()) != EOF ) {
            *bufp = c;
            ++bufp;
            /* is separator? */
            if (sep_len) {
                for( a = bufp-sep_len, b = sep;
                        *b && a < bufp && *a == *b; ++a, ++b );
                if (!*b) {
                    *(bufp-sep_len) = 0;
                    bufp = buf;

                    /* append new item if not empty */
                    if (buf[0])
                        append_item(buf, app);
                }
            }

            if ( bufp >= &buf[BUFSIZ-1] ) {
                /** \bug Doesn't handle buffer overflow. */
                g_printerr(ERR_BUFFER_TOO_SMALL, BUFSIZ);
                app->exit_code = 2;
                gtk_main_quit();
                return FALSE;
            }
        } else {
            break;
        }
    }

    if ( ferror(stdin) || feof(stdin) ) {
        /* insert last item */
        *bufp = 0;
        if (buf[0])
            append_item(buf, app);
        return FALSE;
    } else {
        return TRUE;
    }
}

/** Compare two items in model. */
gint natural_compare( GtkTreeModel *model,
                      GtkTreeIter *a,
                      GtkTreeIter *b,
                      gpointer user_data )
{
    gchar *item1, *item2, *aa, *bb;
    int num1, num2;
    gint result = 0;

    gtk_tree_model_get(model, a, COL_TEXT, &item1, -1);
    gtk_tree_model_get(model, b, COL_TEXT, &item2, -1);

    for( aa = item1, bb = item2; *aa && *bb; ++aa, ++bb ) {
        if ( isdigit(*aa) && isdigit(*bb) ) {
            sscanf(aa, "%d%s", &num1, aa);
            sscanf(bb, "%d%s", &num2, bb);
            if (num1 != num2) {
                result = num1 - num2;
                break;
            }
        } else if (*aa != *bb) {
            result = *aa - *bb;
            break;
        }
    }

    g_free(item1);
    g_free(item2);

    return result;
}

/**
 * Create filtered model from \a model.
 * Items are filtered using #COL_VISIBLE column.
 */
GtkTreeModel *create_filtered_model(GtkTreeModel *model)
{
    GtkTreeModel *filtered;

    filtered = gtk_tree_model_filter_new(model, NULL);
    gtk_tree_model_filter_set_visible_column( GTK_TREE_MODEL_FILTER(filtered),
                                              COL_VISIBLE );

    return filtered;
}

/**
 * Create sorteded model from \a model.
 * Items are sorted using #COL_TEXT column.
 */
GtkTreeModel *create_sorted_model(GtkTreeModel *model)
{
    GtkTreeModel *sorted;

    sorted = gtk_tree_model_sort_new_with_model(model);
    gtk_tree_sortable_set_sort_func( GTK_TREE_SORTABLE(sorted), COL_TEXT,
                                     natural_compare, NULL, NULL );
    gtk_tree_sortable_set_sort_column_id( GTK_TREE_SORTABLE(sorted), COL_TEXT,
                                          GTK_SORT_ASCENDING );

    return sorted;
}

/**
 * Appends item to entry.
 * Items are separated by output separator (Application::o_separator).
 */
void append_item_text( GtkTreeModel *model,
                       GtkTreePath *path,
                       GtkTreeIter *iter,
                       gpointer user_data )
{
    Application *app = (Application *)user_data;
    GtkEntry *entry = app->entry;
    gchar *item;

    gtk_tree_model_get(model, iter, COL_TEXT, &item, -1);
    /**
     * \bug Separator with new line character (\\n)
     * doesn't show correctly in entry.
     */
    if ( gtk_entry_get_text_length(entry) )
        gtk_entry_append_text(entry, app->o_separator ? app->o_separator : "");
    gtk_entry_append_text(entry, item);
    g_free(item);
}

/**
 * Handler called if item is selected.
 */
gboolean item_select( GtkTreeSelection *selection,
                      GtkTreeModel *model,
                      GtkTreePath *path,
                      gboolean path_currently_selected,
                      gpointer user_data )
{
    Application *app = (Application *)user_data;
    GtkTreeIter iter;
    gchar *sep = app->o_separator;
    const gchar *a, *b;
    gint sep_len = 0;
    const gchar *text;

    if (path_currently_selected) {
        return TRUE;
    }

    disable_filter(app);

    /* delete selection */
    gtk_editable_delete_selection( GTK_EDITABLE(app->entry) );
    text = app->original_text;

    /* find beginning of last output item */
    b = text;
    if (sep && *sep) {
        sep_len = strlen(sep);
        while( (a = strstr(b, sep)) ) {
            b = a+sep_len;
        }
        if (b != text) {
            b -= sep_len;
        }
    }

    /** Changes entry text to item text. */
    gtk_entry_buffer_delete_text( gtk_entry_get_buffer(app->entry), b-text, -1);
    gtk_tree_selection_selected_foreach(selection, append_item_text, app);
    if ( gtk_tree_model_get_iter(model, &iter, path) &&
         !gtk_tree_selection_path_is_selected(selection, path) )
        append_item_text(model, path, &iter, app);

    /** select text */
    gtk_editable_select_region( GTK_EDITABLE(app->entry),
                                b == text ? 0 : b-text+sep_len, -1 );
    for( a = gtk_entry_get_text(app->entry), b = text;
            *a && *b && *a == *b;
            ++a, ++b );
    if (!*b) {
        gtk_editable_select_region( GTK_EDITABLE(app->entry),
                                    b-text, -1 );
    }

    enable_filter(app);

    return TRUE;
}

/**
 * Filter items in list.
 * Show item if text in the entry matches the item's text, hide otherwise.
 * \callgraph
 */
gboolean refilter(Application *app)
{
    static gchar *last_filter_text = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *item_text;
    gchar *filter_text, *a, *b;
    gboolean visible, filter_visible;
    int from, to;

    if (app->filter_timer) {
        g_source_destroy(app->filter_timer);
        app->filter_timer = NULL;
    }

    if(!last_filter_text) {
        last_filter_text = g_strdup("");
    }

    filter_text = get_filter_text(&from, &to, app);

    /**
     * If last filtered text starts with \a filter_text,
     * filter only visible items.
     */
    for( a = filter_text, b = last_filter_text;
            *a && *b && toupper(*a) == toupper(*b);
            ++a, ++b );
    /* filter only if previous filter differs */
    if( *a || *b ) {
        gtk_tree_selection_unselect_all(
                gtk_tree_view_get_selection(app->tree_view) );

        filter_visible = !*b;
        model = GTK_TREE_MODEL(app->store);
        if ( gtk_tree_model_get_iter_first(model, &iter) ) {
            do {
                if (filter_visible) {
                    gtk_tree_model_get( model, &iter,
                            COL_VISIBLE, &visible, -1 );
                    if (!visible)
                        continue;
                }

                gtk_tree_model_get(model, &iter, COL_TEXT, &item_text, -1);
                if (item_text) {
                    visible = match_tokens(item_text, filter_text) != NULL;
                    gtk_list_store_set(app->store, &iter, COL_VISIBLE, visible, -1);
                    g_free(item_text);
                }
            } while( gtk_tree_model_iter_next(model, &iter) );
        }
    }
    g_free(last_filter_text);
    last_filter_text = filter_text;

    /* in-line auto-completion */
    /* complete only if text cursor is at the end of entry */
    /* and no entry text is selected */
    app->complete = app->complete && from == to &&
        gtk_entry_get_text_length(app->entry) == to;

    model = gtk_tree_view_get_model(app->tree_view);
    if ( app->complete && gtk_tree_model_get_iter_first(model, &iter) ) {
        do {
            gtk_tree_model_get(model, &iter, COL_TEXT, &item_text, -1);
            if (item_text) {
                for( a = item_text, b = filter_text;
                        *a && *b && *a == *b;
                        ++a, ++b );
                if (*a && !*b) {
                    app->complete = FALSE;
                    GtkTreePath *path =
                        gtk_tree_model_get_path(model, &iter);
                    gtk_tree_view_set_cursor( app->tree_view, path,
                            NULL, FALSE);
                    g_free(item_text);
                    break;
                }

                g_free(item_text);
            }
        } while( gtk_tree_model_iter_next(model, &iter) );
    }

    return FALSE;
}

/**
 * Refilter list after delay.
 * Refiltering operation is delayed #REFILTER_DELAY milliseconds.
 */
void delayed_refilter(Application *app)
{
    if (app->filter_timer)
        g_source_destroy(app->filter_timer);
    app->filter_timer = g_timeout_source_new(REFILTER_DELAY);
    g_source_set_callback( app->filter_timer,
                           (GSourceFunc)refilter, app, NULL );
    g_source_attach(app->filter_timer, NULL);
}

/**
 * Handler called if text was inserted to entry.
 * \callgraph
 */
void insert_text( GtkEditable *editable,
                  gchar       *new_text,
                  gint         new_text_length,
                  gpointer     position,
                  Application *app )
{
    if (app->filter) {
        app->complete = TRUE;
    }
}

/**
 * Handler called if text was deleted from entry.
 * \callgraph
 */
void delete_text( GtkEditable *editable,
                  gint         start_pos,
                  gint         end_pos,
                  Application *app )
{
    if (app->filter) {
        app->complete = FALSE;
    }
}

/**
 * Handler called if entry text was changed.
 * \callgraph
 */
void text_changed( GtkEditable *editable,
                   Application *app )
{
    if (app->filter) {
        g_free(app->original_text);
        app->original_text = g_strdup( gtk_entry_get_text(app->entry) );

        delayed_refilter(app);
    }
}

/**
 * Create list view from \a model.
 */
GtkTreeView *create_list_view(GtkTreeModel *model)
{
    GtkTreeView *tree_view;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;

    tree_view = GTK_TREE_VIEW( gtk_tree_view_new_with_model(model) );

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(GTK_TREE_VIEW_COLUMN(col), TRUE);

    /* icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes( col, renderer,
                                         "pixbuf", COL_ICON,
                                         NULL );

    /* text column */
    renderer = gtk_cell_renderer_text_new();
    /** If text is too long, display dots in middle. */
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes( col, renderer,
                                         "text", COL_TEXT,
                                         NULL );

    gtk_tree_view_append_column(tree_view, col);

    gtk_tree_view_set_search_column(tree_view, COL_TEXT);
    gtk_tree_view_set_headers_visible(tree_view, FALSE);

    /**
     * For performance reasons rows have fixed height and
     * tree lines are hidden.
     */
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_set_fixed_height_mode(tree_view, TRUE);
    gtk_tree_view_set_enable_tree_lines(tree_view, FALSE);

    return tree_view;
}

/** Sets window geometry using \a options */
void set_window_geometry(const Options *options, Application *app)
{
    gint x ,y, w, h;
    GdkGravity gravity;
    GdkScreen *screen = gtk_window_get_screen(app->window);

    /* default position: center of the screen */
    gtk_window_set_position(app->window, GTK_WIN_POS_CENTER);

    /* resizes window */
    w = options->width;
    h = options->height;
    if (w < 0)
        w = gdk_screen_get_width(screen) + w + 1;
    else if (w == 0)
        w = DEFAULT_WINDOW_WIDTH;
    if (h < 0)
        h = gdk_screen_get_height(screen) + h + 1;
    else if (h == 0)
        h = DEFAULT_WINDOW_HEIGHT;
    gtk_window_resize(app->window, w, h);
    app->height = h;

    /* moves window */
    if (options->x != OPTION_UNSET || options->y != OPTION_UNSET) {
        gtk_window_get_position( app->window, &x, &y );
        if (options->x != OPTION_UNSET)
            x = options->x;
        if (options->y != OPTION_UNSET)
            y = options->y;

        if (x < 0) {
            if (y < 0) {
                gravity = GDK_GRAVITY_SOUTH_EAST;
                y = gdk_screen_height() + y + 1;
            } else {
                gravity = GDK_GRAVITY_NORTH_EAST;
            }
            x = gdk_screen_width() + x;
        } else if (y < 0) {
            gravity = GDK_GRAVITY_SOUTH_WEST;
            y = gdk_screen_height() + y + 1;
        } else {
            gravity = GDK_GRAVITY_NORTH_WEST;
        }

        gtk_window_set_gravity( app->window, gravity );
        gtk_window_move( app->window, x, y );
    }
}

/**
 * Creates main window with widgets.
 * Uses \a options to pass user options to application.
 * \callgraph
 */
Application *new_application(const Options *options)
{
    Application *app;
    GtkWidget *layout;
    GtkWidget *hbox;
    GdkPixbuf *pixbuf;
    GtkTreeModel *model;

    app = malloc( sizeof(Application) );

    app->complete = TRUE;
    app->filter_timer = NULL;
    app->hide_list = options->hide_list;
    app->i_separator = options->i_separator;
    app->o_separator = options->o_separator;
    app->exit_code = 1;
    app->original_text = g_strdup("");
    app->sorted_model = NULL;

    /** Creates: */
    /** - main window, */
    app->window = GTK_WINDOW( gtk_window_new(GTK_WINDOW_TOPLEVEL) );
    gtk_window_set_title( app->window, options->title );
    pixbuf = gdk_pixbuf_new_from_inline(-1, sprinter_icon, FALSE, NULL);
    gtk_window_set_icon( app->window, pixbuf );

    /** - text entry, */
    app->entry = GTK_ENTRY( gtk_entry_new() );
    app->button = GTK_BUTTON( gtk_button_new_with_label(options->label) );
    g_object_set(app->button, "can-focus", FALSE, NULL);

    /** - list store and filtered model, */
    app->store = gtk_list_store_new( 3,
                    G_TYPE_BOOLEAN,
                    GDK_TYPE_PIXBUF,
                    G_TYPE_STRING );
    model = app->filtered_model = create_filtered_model( GTK_TREE_MODEL(app->store) );
    if (options->sort_list) {
        model = app->sorted_model = create_sorted_model(model);
    }

    /** - list view, */
    app->tree_view = create_list_view(model);
    g_object_unref(model);

    /* multiple selections only if output separator set */
    if (app->o_separator) {
        gtk_tree_selection_set_mode( gtk_tree_view_get_selection(app->tree_view),
                                     GTK_SELECTION_MULTIPLE );
        gtk_tree_view_set_rubber_banding(app->tree_view, TRUE);
    }

    /** - scrolled window for item list. */
    app->scroll_window =
        GTK_SCROLLED_WINDOW( gtk_scrolled_window_new(NULL, NULL) );
    gtk_scrolled_window_set_policy( app->scroll_window,
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );

    /* signals */
    g_signal_connect( app->window, "destroy",
                      G_CALLBACK(gtk_main_quit), NULL );
    g_signal_connect( app->window, "key-press-event",
                      G_CALLBACK(on_key_press), app );
    g_signal_connect( app->tree_view, "key-press-event",
                      G_CALLBACK(tree_view_on_key_press), app );
    g_signal_connect( app->entry, "key-press-event",
                      G_CALLBACK(entry_on_key_press), app );
    g_signal_connect( app->entry, "insert-text",
                      G_CALLBACK(insert_text), app );
    g_signal_connect( app->entry, "delete-text",
                      G_CALLBACK(delete_text), app );
    g_signal_connect( app->entry, "changed",
                      G_CALLBACK(text_changed), app );
    g_signal_connect_swapped( app->button, "clicked",
                              G_CALLBACK(submit), app);
    g_signal_connect_swapped( app->entry, "focus-in-event",
                              G_CALLBACK(enable_filter), app );
    g_signal_connect_swapped( app->entry, "focus-out-event",
                              G_CALLBACK(disable_filter), app );

    /* on item selected */
    gtk_tree_selection_set_select_function(
            gtk_tree_view_get_selection(app->tree_view),
            item_select, app, NULL );

    /* lay out widgets */
    layout = gtk_vbox_new(FALSE, 2);
    hbox = gtk_hbox_new(FALSE, 2);
    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(app->window), layout );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app->entry), 1,1,0 );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app->button), 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), hbox, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), GTK_WIDGET(app->scroll_window),
                        1,1,0 );
    gtk_container_add( GTK_CONTAINER(app->scroll_window),
                       GTK_WIDGET(app->tree_view) );

    /* show widgets (from inner-most) */
    gtk_widget_show( GTK_WIDGET(app->tree_view) );
    gtk_widget_show( GTK_WIDGET(app->scroll_window) );
    gtk_widget_show_all( GTK_WIDGET(hbox) );
    gtk_widget_show( GTK_WIDGET(layout) );
    set_window_geometry(options, app);
    if (app->hide_list) {
        hide_list(app);
        g_signal_connect_swapped( app->tree_view, "focus-out-event",
                          G_CALLBACK(hide_list), app );
    }
    gtk_widget_show( GTK_WIDGET(app->window) );

    gtk_widget_grab_focus( GTK_WIDGET(app->entry) );

    return app;
}

/**
 * \callgraph
 */
int main(int argc, char *argv[])
{
    Options options;
    Application *app;
    int exit_code;

    /** Parses options from program arguments. */
    options = new_options(argc, argv);
    if (options.show_help) {
        help();
        return 0;
    } else if (!options.ok) {
        return 2;
    }

    /** Initializes application and shows main window. */
    gtk_init(&argc, &argv);

    app = new_application(&options);

    /** Starts appending lines from stdin to list store. */
    g_idle_add( (GSourceFunc)read_items, app );

    gtk_main();

    exit_code = app->exit_code;
    free(app);

    /** \return exit code is 0 only if an item was submitted */
    return exit_code;
}

