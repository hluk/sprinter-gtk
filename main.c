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
 * Control is passed to GTK main event loop from which #readStdin is called
 * every time the application is idle and stdin is open. This function parses
 * items from stdin.
 *
 * After main event loop finishes, program prints contents of text entry and
 * exits with exit code 0 if the text was submitted. Otherwise application
 * doesn't print anything on stdout and exits with exit code 1.
 *
 * If wrong arguments are passed to application or other error occurred
 * during execution, the program exits with exit code 2.
 */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "sprinter_icon.h"


/** buffer too small error string */
#define ERR_BUFFER_TOO_SMALL "Line too big (BUFSIZ is %d)!"\
    " Try changing the input separator using option -i.\n"

/**
 * Maximum number of input reads before the control is
 * returned to main event loop.
 * Other values might lead to better or worse responsiveness
 * while reading lot of data from stdin.
 */
#define STDIN_BATCH_SIZE 20

/** Delay list refiltering for #REFILTER_DELAY milliseconds. */
#define REFILTER_DELAY 200

/**\{ \name Default option values */
/** default window title */
#define DEFAULT_TITLE "sprinter"
/** default label */
#define DEFAULT_LABEL "select:"
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
    /** text entry label with custom text */
    GtkLabel *label;
    /** text entry */
    GtkEntry *entry;
    /** item list */
    GtkTreeView *tree_view;
    /** widget for scrolling item list */
    GtkScrolledWindow *scroll_window;
    /** model for items */
    GtkListStore *store;

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
    {'g', "geometry", "window size and position (format: width,height,[-]x,[-]y)"},
    {'h', "help",     "show this help"},
    {'i', "input-separator",  "string which separates items on input"},
    {'l', "label",    "text input label"},
    {'m', "minimal",  "hide list (press TAB key to show the list)"},
    {'o', "output-separator",  "string which separates items on output"},
    /*{'s', "sort",     "sort items alphabetically"},*/
    /*{'S', "strict",   "choose only items from stdin"},*/
    {'t', "title",    "title"}
};

/** header for help (\c --help option)*/
#define HELP_HEADER ("usage: sprinter [options]\noptions:\n")

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

/** Print help and exit. */
void help() {
    int len, i;
    const Argument *arg;

    g_print(HELP_HEADER);

    len = sizeof(arguments)/sizeof(Argument);
    for ( i = 0; i<len; ++i ) {
        arg = &arguments[i];
        g_print( "  -%c, --%-12s %s\n", arg->shopt, arg->opt, arg->help );
    }
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
    int num, num2;
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

    len = sizeof(arguments)/sizeof(Argument);
    i = 1;
    while(i<argc) {
        argp = argv[i];
        ++i;

        if (argp[0] != '-' || argp[1] == '\0') {
            help();
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
            break;
        }

        /* set options */
        arg = arguments[j].shopt;
        j = i;
        if (arg == 'g') {
            if (!argp) {
                help();
                break;
            }
            ++i;

            /** \todo better parsing of geometry values */
            /* width,height,x,y - all optional */
            /* width */
            num2 = sscanf(argp, "%d%c", &num, &c);
            if (num2 > 0 && num > 0)
                options.width = num;

            while( isdigit(*argp) || *argp=='+' ) ++argp;
            if (*argp == ',') {
                ++argp;
            } else if (*argp != '\0') {
                help();
                break;
            }

            /* height */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0 && num > 0)
                options.height = num;

            while( isdigit(*argp) || *argp=='+' ) ++argp;
            if (*argp == ',') {
                ++argp;
            } else if (*argp != '\0') {
                help();
                break;
            }

            /* x */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0)
                options.x = num;

            while( isdigit(*argp) || *argp=='+' || *argp=='-' ) ++argp;
            if (*argp == ',') {
                ++argp;
            } else if (*argp != '\0') {
                help();
                break;
            }

            /* y */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0)
                options.y = num;

            while( isdigit(*argp) || *argp=='+' || *argp=='-' ) ++argp;
            if (*argp != '\0') {
                help();
                break;
            }
        } else if (arg == 'h') {
            options.show_help = TRUE;
            break;
        } else if (arg == 'i') {
            if (!argp) {
                help();
                break;
            }
            ++i;
            options.i_separator = unescape(argp);
        } else if (arg == 'l') {
            if (!argp) help(1);
            ++i;
            options.label = argp;
        } else if (arg == 'm') {
            options.hide_list = TRUE;
        } else if (arg == 'o') {
            if (!argp) help(1);
            ++i;
            options.o_separator = unescape(argp);
        } else if (arg == 's') {
            options.sort_list = TRUE;
        } else if (arg == 'S') {
            options.strict = TRUE;
        } else if (arg == 't') {
            if (!argp) {
                help();
                break;
            }
            ++i;
            options.title = argp;
        } else {
            help();
            break;
        }

        if (force_arg && i == j) {
            help();
            break;
        }
    }

    options.ok = i == argc;

    return options;
}


static void destroy(GtkWidget *w, gpointer data)
{
    gtk_main_quit();
}

/** Hides list (event handler). */
gboolean hide_list(GtkWidget *widget, GdkEvent *event, Application *app)
{
    gint w, h;

    gtk_widget_hide( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, 1 );
    return TRUE;
}

/** Shows list. */
void show_list(Application *app)
{
    gint w, h;

    gtk_widget_show( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, app->height );
}

/** window key-press-event handler */
gboolean on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
    guint key;
    GtkTreePath *path;

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
                g_print( gtk_entry_get_text(app->entry) );
                app->exit_code = 0;
                gtk_main_quit();
                return TRUE;
            /**
             * If tab or down key pressed and entry widget has focus,
             * show and focus list.
             */
            case GDK_KEY_Tab:
            case GDK_KEY_Down:
            case GDK_KEY_Page_Down:
                if ( gtk_widget_has_focus(GTK_WIDGET(app->entry)) ) {
                    if (app->hide_list)
                        show_list(app);

                    gtk_tree_view_get_cursor( app->tree_view, &path, NULL);
                    gtk_widget_grab_focus( GTK_WIDGET(app->tree_view) );

                    if (!path) {
                        path = gtk_tree_path_new_first();
                        gtk_tree_view_set_cursor( app->tree_view, path, NULL, FALSE );
                    }
                    gtk_tree_path_free(path);

                    return TRUE;
                }
                break;
            /**
             * If left or right arrow pressed and list has focus,
             * focus widget and set cursor position.
             */
            case GDK_KEY_Left:
            case GDK_KEY_Right:
                if ( gtk_widget_has_focus(GTK_WIDGET(app->tree_view)) ) {
                    gtk_widget_grab_focus( GTK_WIDGET(app->entry) );
                    gtk_entry_set_position( app->entry, key == GDK_KEY_Left ? 0 : -1 );
                    return TRUE;
                }
                break;
        }
    }

    return FALSE;
}

/** list view key-press-event handler */
gboolean tree_view_on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
    guint key = event->key.keyval;

    /** If at top of the list and up key pressed, focus entry. */
    if (event->type == GDK_KEY_PRESS &&
            (key == GDK_KEY_Up || key == GDK_KEY_Page_Up) ){
        GtkTreePath *path;
        gtk_tree_view_get_cursor( GTK_TREE_VIEW(widget), &path, NULL );
        if (path) {
            gboolean hasprev = gtk_tree_path_prev(path);
            gtk_tree_path_free(path);

            if (!hasprev) {
                gtk_widget_grab_focus( GTK_WIDGET(app->entry) );
                return TRUE;
            }
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
    GFile *file = g_file_new_for_path (filename);
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

    if (file) {
        GFileInfo *info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (info != NULL)
        {
            GIcon *mime_icon = g_content_type_get_icon (g_file_info_get_content_type (info));
            if (mime_icon != NULL)
            {
                GtkIconInfo *icon_info = gtk_icon_theme_lookup_by_gicon(icon_theme, mime_icon, 16, GTK_ICON_LOOKUP_USE_BUILTIN);
                if (icon_info != NULL)
                {
                    pixbuf = gtk_icon_info_load_icon (icon_info, NULL);
                    gtk_icon_info_free (icon_info);
                }
                g_object_unref (mime_icon);
            }
            g_object_unref (info);
        }
        g_object_unref (file);
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
 * Appends item to store.
 * Creates new row with \a text in \a store at given position (\a iter).
 * Row will be hidden if \a visible is FALSE.
 * If \a text is filename or path to existing file, an icon is created.
 */
void append_item(GtkTreeIter *iter, const gchar *text, gboolean visible, GtkListStore *store)
{
    GdkPixbuf *pixbuf = pixbuf_from_file(text);
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
 * Completes text in entry.
 * \return TRUE if completion was successful, FALSE otherwise
 */
gboolean complete(const gchar *text, Application *app)
{
    const char *filter_text;
    const char *a, *b;
    gint pos = -1;

    filter_text = gtk_entry_get_text(app->entry);

    /* if item starts with filter text */
    for( a = text, b = filter_text;
            *a && *b && toupper(*a) == toupper(*b);
            ++a, ++b );
    if (*a && !*b) {
        app->filter = FALSE;
        gtk_editable_insert_text( GTK_EDITABLE(app->entry), a, -1, &pos );
        gtk_entry_select_region(app->entry, -1, (gint)(b-filter_text));
        app->filter = TRUE;

        return TRUE;
    }

    return FALSE;
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
 * Appends items to list.
 * Appends all items in \a str separated by input separator (Application::i_separator).
 * Last item is added only if \a last is \c TRUE.
 * \callgraph
 */
char *append_items(char *str, gboolean last, Application *app)
{
    char *sep = app->i_separator;
    int sep_len = strlen(sep);
    char *a, *b;
    gboolean visible;
    int from, to;
    GtkTreeIter iter;
    gchar *filter_text;

    filter_text = get_filter_text(&from, &to, app);
    app->complete &= from == to;

    a = str;
    b = NULL;
    while( (*sep && (b = strstr(a, sep))) || last ) {
        if (last && !b) {
            b = str + strlen(str);
            sep_len = 0;
        }
        *b = 0;

        visible = match_tokens(a, filter_text) != NULL;

        /**
         * Does in-line completion only for last output item and only if:
         * - no text in entry is selected and
         * - text cursor is at the end of entry and
         * - Application::complete is \c TRUE.
         */
        if ( app->complete && visible && !app->filter_timer &&
                gtk_entry_get_text_length(app->entry) == to ) {
            complete(a, app);
        }

        /* append new item */
        append_item(&iter, a, visible, app->store);

        /* last item added */
        if (sep_len == 0)
            break;

        a = b+sep_len;
    }

    g_free(filter_text);

    /* move last incomplete item to the begging of string */
    if (a != str) {
        if (*a) {
            b = a-1;
            a = str-1;
            while( (*(++a) = *(++b)) );
        } else {
            a = str;
            *a = 0;
        }
    } else {
        while (*a) ++a;
    }

    return a;
}

/**
 * Read items from standard input.
 * After maximum of #STDIN_BATCH_SIZE passes control back to main event loop.
 * If the whole input is read the application may be unresponsive for some time.
 * \return TRUE if no error occurred and input isn't at end
 * \callgraph
 */
gboolean readStdin(Application *app)
{
    static gchar buf[BUFSIZ] = "";
    static gchar *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    fd_set stdin_fds;
    int i;

    /* interrupt after reading at most N lines */
    for( i = 0; i < STDIN_BATCH_SIZE; ++i ) {
        /* check if data available */
        FD_ZERO(&stdin_fds);
        FD_SET(STDIN_FILENO, &stdin_fds);
        if ( select(STDIN_FILENO+1, &stdin_fds, NULL, NULL, &stdin_tv) <= 0 )
            break;

        /* read data */
        if ( fgets(bufp, BUFSIZ - (bufp - buf), stdin) ) {
            bufp = append_items(buf, FALSE, app);

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
        if (buf[0])
            append_items(buf, TRUE,  app);
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
 * Create filtered model from list store.
 * Items are filtered using #COL_VISIBLE column.
 */
GtkTreeModel *create_filter(GtkListStore *store)
{
    GtkTreeModel *filter;

    filter = gtk_tree_model_filter_new( GTK_TREE_MODEL(store), NULL );
    gtk_tree_model_filter_set_visible_column( GTK_TREE_MODEL_FILTER(filter), COL_VISIBLE );

    return filter;
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
    gint sep_len;
    const gchar *text = gtk_entry_get_text(app->entry);
    gint from, to;

    /* delete selection */
    gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), &from, &to);
    gtk_entry_buffer_delete_text( gtk_entry_get_buffer(app->entry), from, to );

    /* find beginning of last output item */
    b = text;
    if (sep && *sep) {
        sep_len = strlen(sep);
        while( (a = strstr(b, sep)) ) {
            b = a+sep_len;
        }
        if (b != text)
            b -= sep_len;
    }

    /** Changes entry text to item text. */
    gtk_entry_buffer_delete_text( gtk_entry_get_buffer(app->entry), b-text, -1);
    gtk_tree_selection_selected_foreach(selection, append_item_text, app);
    if ( gtk_tree_model_get_iter(model, &iter, path) &&
         !gtk_tree_selection_path_is_selected(selection, path) )
        append_item_text(model, path, &iter, app);

    gtk_entry_select_region(app->entry, b == text ? 0 : b-text+sep_len, -1);

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

    filter_text = get_filter_text(&from, &to, app);
    /* do not auto-complete if text is selected */
    app->complete &= from == to;
    model = GTK_TREE_MODEL(app->store);

    /**
     * If last filtered text starts with \a filter_text,
     * filter only visible items.
     */
    if (last_filter_text) {
        for( a = filter_text, b = last_filter_text;
                *a && *b && toupper(*a) == toupper(*b);
                ++a, ++b );
        /* filter is same, nothing to do */
        if( !*a && !*b )
            return FALSE;
        filter_visible = *a && !*b;
        g_free(last_filter_text);
    } else {
        filter_visible = FALSE;
    }
    last_filter_text = filter_text;

    if ( gtk_tree_model_get_iter_first(model, &iter) ) {
        do {
            if (filter_visible) {
                gtk_tree_model_get(model, &iter, COL_VISIBLE, &visible, -1);
                if (!visible)
                    continue;
            }
            gtk_tree_model_get(model, &iter, COL_TEXT, &item_text, -1);
            if (item_text) {
                /* in-line completion */
                if ( app->complete && gtk_entry_get_text_length(app->entry) == to ) {
                    app->complete = !complete(item_text, app);
                }

                visible = match_tokens(item_text, filter_text) != NULL;
                gtk_list_store_set(app->store, &iter, COL_VISIBLE, visible, -1);

                g_free(item_text);
            }
        } while( gtk_tree_model_iter_next(model, &iter) );
    }

    return FALSE;
}

/** Refilter list after #REFILTER_DELAY milliseconds (sets Application::filter_timer). */
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
    if ( app->filter && gtk_widget_has_focus(GTK_WIDGET(editable)) ) {
        app->complete = TRUE;
        delayed_refilter(app);
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
    if ( gtk_widget_has_focus(GTK_WIDGET(editable)) ) {
        app->complete = FALSE;
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
    gint x ,y;
    GdkGravity gravity;

    /* default position: center of the screen */
    gtk_window_set_position(app->window, GTK_WIN_POS_CENTER);

    /* resizes window */
    gtk_window_resize( app->window, options->width, options->height );
    app->height = options->height;

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

    /** Creates: */
    /** - main window, */
    app->window = GTK_WINDOW( gtk_window_new(GTK_WINDOW_TOPLEVEL) );
    gtk_window_set_title( app->window, options->title );
    /*gtk_window_set_icon_from_file( app->window, "sprinter.svg", NULL );*/
    pixbuf = gdk_pixbuf_new_from_inline(-1, sprinter_icon, FALSE, NULL);
    gtk_window_set_icon( app->window, pixbuf );

    /** - text entry, */
    app->entry = GTK_ENTRY( gtk_entry_new() );
    app->label = GTK_LABEL( gtk_label_new(options->label) );

    /** - list store and filtered model, */
    app->store = gtk_list_store_new( 3,
                    G_TYPE_BOOLEAN,
                    GDK_TYPE_PIXBUF,
                    G_TYPE_STRING );
    model = create_filter(app->store);

    /* appends lines from stdin to list store */
    g_idle_add( (GSourceFunc)readStdin, app );

    /** - list view, */
    app->tree_view = create_list_view(model);
    g_object_unref(model);

    /* multiple selections only if output separator set */
    if (app->o_separator) {
        gtk_tree_selection_set_mode( gtk_tree_view_get_selection(app->tree_view),
                                     GTK_SELECTION_MULTIPLE );
    }

    /** - scrolled window for item list. */
    app->scroll_window = GTK_SCROLLED_WINDOW( gtk_scrolled_window_new(NULL, NULL) );
    gtk_scrolled_window_set_policy( app->scroll_window,
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );

    /* signals */
    g_signal_connect(app->window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);
    g_signal_connect(app->tree_view, "key-press-event", G_CALLBACK(tree_view_on_key_press), app);
    /*g_signal_connect(app->entry, "changed", G_CALLBACK(entry_changed), app);*/
    g_signal_connect(app->entry, "insert-text", G_CALLBACK(insert_text), app);
    g_signal_connect(app->entry, "delete-text", G_CALLBACK(delete_text), app);

    /* on item selected */
    gtk_tree_selection_set_select_function(
            gtk_tree_view_get_selection(app->tree_view),
            item_select, app, NULL
    );

    /* lay out widgets */
    layout = gtk_vbox_new(FALSE, 2);
    hbox = gtk_hbox_new(FALSE, 2);
    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(app->window), layout );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app->label), 0,1,0 );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app->entry), 1,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), hbox, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), GTK_WIDGET(app->scroll_window), 1,1,0 );
    gtk_container_add( GTK_CONTAINER(app->scroll_window), GTK_WIDGET(app->tree_view) );

    /* window position and size */
    set_window_geometry(options, app);

    gtk_widget_show_all( GTK_WIDGET(app->window) );
    if (app->hide_list) {
        hide_list(NULL, NULL, app);
        g_signal_connect( app->tree_view, "focus-out-event",
                          G_CALLBACK(hide_list), app );
    }

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

    gtk_main();

    exit_code = app->exit_code;
    free(app);

    /** \return exit code is 0 only if an item was submitted */
    return exit_code;
}

