/** \file main.c
 */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "sprinter_icon.h"

#define TR(x) (x)

/**
 * Maximum number of input reads until control is
 * returned to main event loop
 */
#define STDIN_BATCH_SIZE 20

/** Default window width */
#define DEFAULT_WINDOW_WIDTH 230
/** Default window height */
#define DEFAULT_WINDOW_HEIGHT 320

/** Columns in list store. */
enum
{
    COL_VISIBLE,
    COL_ICON,
    COL_TEXT,
    NUM_COLS
};

/** Main window, widgets and current state of application */
typedef struct {
    /** Main window */
    GtkWindow *window;
    /** Text entry label with custom text */
    GtkLabel *label;
    /** Text entry */
    GtkEntry *entry;
    /** Item list */
    GtkTreeView *tree_view;
    /** Widget for scrolling item list */
    GtkScrolledWindow *scroll_window;
    /** Model for items */
    GtkListStore *store;

    /** Temporarily toggle auto-completion. */
    gboolean complete;
    /** Sets entry text (if not changed) to first item text. */
    gboolean insert_first;
    /** Timer for filtering items (for better performance). */
    GSource *filter_timer;

    /** Hide list initially (minimal mode) */
    gboolean hide_list;

    /** Window height */
    gint height;
} Application;

/** Program arguments */
typedef struct {
    /** Short option (argument beginning with single dash) */
    const char shopt;
    /** Long option (argument beginning with double dash) */
    const char *opt;
    /** Option description */
    const char *help;
} Argument;

/** Program options (short, long, description) */
const Argument arguments[] = {
    {'g', "geometry", "window size and position (format: width,height,[-]x,[-]y)"},
    {'h', "help",     "show this help"},
    {'l', "label",    "text input label"},
    {'m', "minimal",  "hide list (press TAB key to show the list)"},
    {'o', "sort",     "sort items alphabetically"},
    {'S', "strict",   "choose only items from stdin"},
    {'t', "title",    "title"}
};

/** Undefined value of option */
#define OPTION_UNSET -99999

/** User options (using arguments passed to program at start) */
typedef struct {
    /** Main window title */
    const char *title;
    /**\{ \name Main window geometry */
    /** X position */
    gint x,
    /** Y position */
        y,
    /** width */
        width,
    /** height */
        height;
    /**\}*/
    /** Entry label text */
    const char *label;
    /** Hide list initially (minimal mode) */
    gboolean hide_list;
    /** \b TODO: sort list */
    gboolean sort_list;
    /** \b TODO: if entry text submitted, check if item with same text exists */
    gboolean strict;
} Options;

/** Print help and exit */
void help(int exit_code) {
    int len, i;
    const Argument *arg;

    printf( TR("usage: sprinter [options]\n") );
    printf( TR("options:\n") );

    len = sizeof(arguments)/sizeof(Argument);
    for ( i = 0; i<len; ++i ) {
        arg = &arguments[i];
        printf( "  -%c, --%-12s %s\n", arg->shopt, arg->opt, TR(arg->help) );
    }
    exit(exit_code);
}

/**
 * Sets options for application.
 * Sets \a options accordingly to arguments passed to program.
 */
void set_options(int argc, char *argv[], Options *options)
{
    int num, num2;
    gboolean force_arg;
    const char *argp;
    char c, arg;
    int i, j, len;

    len = sizeof(arguments)/sizeof(Argument);
    i = 1;
    while(i<argc) {
        argp = argv[i];
        ++i;

        if (argp[0] != '-' || argp[1] == '\0') {
            help(1);
            return;
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
            help(1);
            return;
        }

        /* do action */
        arg = arguments[j].shopt;
        if (arg == 'g') {
            if (!argp) help(1);
            ++i;

            /* width,height,x,y - all optional */
            /* width */
            num2 = sscanf(argp, "%d%c", &num, &c);
            if (num2 > 0 && num > 0)
                options->width = num;

            while( isdigit(*argp) || *argp=='+' ) ++argp;
            if (*argp == ',')
                ++argp;
            else if (*argp != '\0')
                help(1);

            /* height */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0 && num > 0)
                options->height = num;

            while( isdigit(*argp) || *argp=='+' ) ++argp;
            if (*argp == ',')
                ++argp;
            else if (*argp != '\0')
                help(1);

            /* x */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0)
                options->x = num;

            while( isdigit(*argp) || *argp=='+' || *argp=='-' ) ++argp;
            if (*argp == ',')
                ++argp;
            else if (*argp != '\0')
                help(1);

            /* y */
            num2 = sscanf(argp, "%d", &num);
            if (num2 > 0)
                options->y = num;

            while( isdigit(*argp) || *argp=='+' || *argp=='-' ) ++argp;
            if (*argp != '\0')
                help(1);
        } else if (arg == 'h') {
            if (force_arg) help(1);
            help(0);
        } else if (arg == 'l') {
            if (!argp) help(1);
            ++i;
            options->label = argp;
        } else if (arg == 'm') {
            if (force_arg) help(1);
            options->hide_list = TRUE;
        } else if (arg == 'o') {
            if (force_arg) help(1);
            options->sort_list = TRUE;
        } else if (arg == 'S') {
            if (force_arg) help(1);
            options->strict = TRUE;
        } else if (arg == 't') {
            if (!argp) help(1);
            ++i;
            options->title = argp;
        } else {
            help(1);
        }
    }
}


static void destroy(GtkWidget *w, gpointer data)
{
    gtk_main_quit();
}

/** Hide list (event handler) */
gboolean hide_list(GtkWidget *widget, GdkEvent *event, Application *app)
{
    gint w, h;

    gtk_widget_hide( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, 1 );
    return TRUE;
}

/** Show list */
void show_list(Application *app)
{
    gint w, h;

    gtk_widget_show( GTK_WIDGET(app->scroll_window) );
    gtk_window_get_size( app->window, &w, &h );
    gtk_window_resize( app->window, w, app->height );
}

/**
 * Window key-press-event handler
 */
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
                g_print( gtk_entry_get_text(app->entry) );
                gtk_main_quit();
                exit(0);
                return TRUE;
            case GDK_KEY_Tab:
                if ( gtk_widget_has_focus(GTK_WIDGET(app->entry)) ) {
                    if (app->hide_list)
                        show_list(app);
                    gtk_widget_grab_focus( GTK_WIDGET(app->tree_view) );
                    return TRUE;
                } else {
                    return FALSE;
                }
            case GDK_KEY_Down:
                if ( app->hide_list &&
                        gtk_widget_has_focus(GTK_WIDGET(app->entry)) ) {
                    show_list(app);
                }
                return FALSE;
        }

        /* don't complete on some keys */
        app->complete = key != GDK_KEY_BackSpace && key != GDK_KEY_Delete;
    }

    return FALSE;
}

/**
 * List view key-press-event handler
 */
gboolean tree_view_on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
    /** If at top of the list and up key pressed, focus entry. */
    if (event->type == GDK_KEY_PRESS && event->key.keyval == GDK_KEY_Up){
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
 * File icon.
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
const gchar *match_tokens(const gchar *haystack, const gchar *needle, int max)
{
    const gchar *h, *hh, *nn;
    int i;

    if ( !*needle )
        return haystack;

    for ( h = haystack; *h; ++h ) {
        for ( i = 0, hh = h, nn = needle; *hh && *nn && i < max ; ++i, ++hh, ++nn ) {
            if ( *nn == ' ' ) {
                if ( match_tokens(hh, nn+1, max-i) )
                    return h;
                else
                    break;
            } else if ( toupper(*hh) != toupper(*nn) ) {
                break;
            }
        }
        if (!*nn || i == max)
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
gboolean complete(const gchar *text, GtkEntry *entry)
{
    const char *filter_text;
    const char *a, *b;
    gint pos = -1;

    filter_text = gtk_entry_get_text(entry);

    /* if item starts with filter text */
    for( a = text, b = filter_text;
            *a && *b && toupper(*a) == toupper(*b);
            ++a, ++b );
    if (*a && !*b) {
        gtk_editable_insert_text( GTK_EDITABLE(entry), a, -1, &pos );
        gtk_entry_select_region(entry, -1, (gint)(b-filter_text));

        return TRUE;
    }

    return FALSE;
}

/**
 * Read standard input.
 * Read few lines from standard input.
 * If the whole input is read the application may be unresponsive for some time.
 * \return TRUE if no error occurred and input isn't at end
 * \callgraph
 */
gboolean readStdin(Application *app)
{
    static gchar buf[BUFSIZ];
    static const gchar *const buf_begin = buf;
    static gchar *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    const char *filter_text;
    gboolean selected, visible;
    int sela, selb;
    fd_set stdin_fds;
    GtkTreeIter iter;

    /* interrupt after reading at most N lines */
    int i = 0;
    for( ; i < STDIN_BATCH_SIZE; ++i ) {
        /* check if data available */
        FD_ZERO(&stdin_fds);
        FD_SET(STDIN_FILENO, &stdin_fds);
        if ( select(STDIN_FILENO+1, &stdin_fds, NULL, NULL, &stdin_tv) <= 0 )
            break;

        /* read data */
        if ( fgets(bufp, BUFSIZ - (bufp - buf_begin), stdin) ) {
            while ( *bufp != '\0' ) ++bufp;
            /* each line is one item */
            if ( *(bufp-1) == '\n' ) {
                *(bufp-1) = '\0';

                filter_text = gtk_entry_get_text(app->entry);
                selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), &sela, &selb);

                /** Does in-line completion only if:
                 * - no text in entry is selected and
                 * - filter_timer is not set and
                 * - completion required.
                 */
                if ( !selected && !app->filter_timer && app->complete) {
                    complete(buf, app->entry);
                }

                /* first item text to entry (if entry is still empty) */
                if ( app->insert_first ) {
                    app->insert_first = TRUE;
                    if ( !*filter_text ) {
                        gtk_entry_set_text(app->entry, buf);
                        gtk_entry_select_region(app->entry, -1, 0);
                    }
                }

                /* append new item */
                visible = match_tokens(buf, filter_text, sela) != NULL;
                append_item(&iter, buf, visible, app->store);

                /* rewind buffer */
                bufp = buf;
            } else if ( bufp >= &buf[BUFSIZ-1] ) {
                /** \b FIXME: handle buffer overflow */
                fprintf(stderr, "Line too big (BUFSIZ is %d)!\n", BUFSIZ);
            }
        } else {
            break;
        }
    }

    return !ferror(stdin) && !feof(stdin);
}

/**
 * Create filtered model from list store.
 */
GtkTreeModel *create_filter(GtkListStore *store)
{
    GtkTreeModel *filter;

    filter = gtk_tree_model_filter_new( GTK_TREE_MODEL(store), NULL );
    gtk_tree_model_filter_set_visible_column( GTK_TREE_MODEL_FILTER(filter), COL_VISIBLE );

    return filter;
}

/**
 * Handler called if item is selected.
 */
gboolean item_select(GtkTreeSelection *selection, GtkTreeModel *model,
        GtkTreePath *path, gboolean path_currently_selected, gpointer data)
{
    gchar *item;
    GtkTreeIter iter;
    GtkEntry *entry;

    if ( !gtk_tree_model_get_iter(model, &iter, path) )
        return TRUE;

    /** Changes entry text to item text. */
    gtk_tree_model_get(model, &iter, COL_TEXT, &item, -1);
    entry = (GtkEntry *)data;
    gtk_entry_set_text(entry, item);
    g_free(item);

    return TRUE;
}

/**
 * Filter items in list.
 * Show item if text in the entry matches the item's text, hide otherwise.
 * \callgraph
 */
gboolean refilter(Application *app)
{
    /* TODO: if text appended to entry - refilter only visible items
     *       - save entry text as static variable and next time
     *         check against new entry text
     */
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *item_text;
    const gchar *filter_text;
    gboolean selected, visible;
    int sela, selb;

    if (app->filter_timer) {
        g_source_destroy(app->filter_timer);
        app->filter_timer = NULL;
    }

    selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), &sela, &selb);
    if (selected)
        return FALSE;

    model = GTK_TREE_MODEL(app->store);
    filter_text = gtk_entry_get_text(app->entry);

    if ( gtk_tree_model_get_iter_first(model, &iter) ) {
        do {
            gtk_tree_model_get(model, &iter, COL_TEXT, &item_text, -1);
            if (item_text) {
                /* in-line completion */
                if (app->complete) {
                    app->complete = !complete(item_text, app->entry);
                }

                visible = match_tokens(item_text, filter_text, selb) != NULL;
                gtk_list_store_set(app->store, &iter, COL_VISIBLE, visible, -1);

                g_free(item_text);
            }
        } while( gtk_tree_model_iter_next(model, &iter) );
    }

    return FALSE;
}

/**
 * Handler called if entry text changes.
 * \callgraph
 */
void entry_changed(GtkEntry *entry, Application *app)
{
    if ( gtk_widget_has_focus(GTK_WIDGET(entry)) ) {
        app->insert_first = FALSE;

        if (app->filter_timer)
            g_source_destroy(app->filter_timer);
        app->filter_timer = g_timeout_source_new(200);
        g_source_set_callback( app->filter_timer,
                               (GSourceFunc)refilter, app, NULL );
        g_source_attach(app->filter_timer, NULL);
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
    gtk_tree_view_column_set_attributes(col, renderer,
                                        "pixbuf", COL_ICON,
                                        NULL);

    /* text column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer,
                                        "text", COL_TEXT,
                                        NULL);

    gtk_tree_view_append_column(tree_view, col);

    gtk_tree_view_set_search_column(tree_view, COL_TEXT);
    gtk_tree_view_set_headers_visible(tree_view, FALSE);
    /**
     * For performance reasons each row has fixed height
     * (\e gtk_tree_view_set_fixed_height_mode()) and
     * tree lines are hidden (\e gtk_tree_view_set_enable_tree_lines()).
     */
    gtk_tree_view_set_fixed_height_mode(tree_view, TRUE);
    gtk_tree_view_set_enable_tree_lines(tree_view, FALSE);

    return tree_view;
}

/** Sets window geometry using \a options */
void set_window_geometry(Options *options, Application *app)
{
    gint w, h, x ,y;
    GdkGravity gravity;

    /* default position: center of the screen */
    gtk_window_set_position(app->window, GTK_WIN_POS_CENTER);

    /* resizes window */
    w = DEFAULT_WINDOW_WIDTH;
    h = DEFAULT_WINDOW_HEIGHT;
    if (options->height != OPTION_UNSET || options->width != OPTION_UNSET) {
        if (options->width != OPTION_UNSET)
            w = options->width;
        if (options->height != OPTION_UNSET)
            h = options->height;
        gtk_window_resize( app->window, options->width, options->height );
        app->height = options->height;
    }
    gtk_window_resize(app->window, w, h);
    app->height = h;

    /* moves window */
    if (options->x != OPTION_UNSET || options->y != OPTION_UNSET) {
        gtk_window_get_position( app->window, &x, &y );
        if (options->x == OPTION_UNSET)
            options->x = x;
        else if (options->y == OPTION_UNSET)
            options->y = y;
        if (options->x < 0) {
            if (options->y < 0) {
                gravity = GDK_GRAVITY_SOUTH_EAST;
                options->y = gdk_screen_height() + options->y + 1;
            } else {
                gravity = GDK_GRAVITY_NORTH_EAST;
            }
            options->x = gdk_screen_width() + options->x;
        } else if (options->y < 0) {
            gravity = GDK_GRAVITY_SOUTH_WEST;
            options->y = gdk_screen_height() + options->y + 1;
        } else {
            gravity = GDK_GRAVITY_NORTH_WEST;
        }
        gtk_window_set_gravity( app->window, gravity );
        gtk_window_move( app->window, options->x, options->y );
    }
}

/**
 * \callgraph
 */
int main(int argc, char *argv[])
{
    GtkWidget *layout;
    GtkWidget *hbox;
    GdkPixbuf *pixbuf;
    GtkTreeModel *model;
    Options options;
    Application app;

    /* options */
    options.title = "sprinter";
    options.label = "select:";
    options.hide_list = options.sort_list = options.strict = FALSE;
    options.x = options.y = options.width = options.height = OPTION_UNSET;
    set_options(argc, argv, &options);

    app.complete = TRUE;
    app.insert_first = TRUE;
    app.filter_timer = NULL;
    app.hide_list = options.hide_list;

    gtk_init(&argc, &argv);

    /** Creates main window and widgets. */
    /* main window */
    app.window = GTK_WINDOW( gtk_window_new(GTK_WINDOW_TOPLEVEL) );
    gtk_window_set_title( app.window, options.title );
    /*gtk_window_set_icon_from_file( app.window, "sprinter.svg", NULL );*/
    pixbuf = gdk_pixbuf_new_from_inline(-1, sprinter_icon, FALSE, NULL);
    gtk_window_set_icon( app.window, pixbuf );

    /* text entry */
    app.entry = GTK_ENTRY( gtk_entry_new() );
    app.label = GTK_LABEL( gtk_label_new(options.label) );

    /* list store and filtered model */
    app.store = gtk_list_store_new( 3,
                    G_TYPE_BOOLEAN,
                    GDK_TYPE_PIXBUF,
                    G_TYPE_STRING );
    model = create_filter(app.store);

    /* appends lines from stdin to list store */
    g_idle_add( (GSourceFunc)readStdin, &app );

    /* list view */
    app.tree_view = create_list_view(model);

    /* scrolled window for item list */
    app.scroll_window = GTK_SCROLLED_WINDOW( gtk_scrolled_window_new(NULL, NULL) );
    gtk_scrolled_window_set_policy( app.scroll_window,
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );

    g_object_unref(model);

    /* signals */
    g_signal_connect(app.window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);
    g_signal_connect(app.tree_view, "key-press-event", G_CALLBACK(tree_view_on_key_press), &app);
    g_signal_connect(app.entry, "changed", G_CALLBACK(entry_changed), &app);

    /* on item selected */
    gtk_tree_selection_set_select_function(
            gtk_tree_view_get_selection(app.tree_view),
            item_select, app.entry, NULL
    );

    /* lay out widgets */
    layout = gtk_vbox_new(FALSE, 2);
    hbox = gtk_hbox_new(FALSE, 2);
    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(app.window), layout );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app.label), 0,1,0 );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app.entry), 1,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), hbox, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), GTK_WIDGET(app.scroll_window), 1,1,0 );
    gtk_container_add( GTK_CONTAINER(app.scroll_window), GTK_WIDGET(app.tree_view) );

    /* window position and size*/
    set_window_geometry(&options, &app);

    gtk_widget_show_all( GTK_WIDGET(app.window) );
    if (app.hide_list) {
        hide_list(NULL, NULL, &app);
        g_signal_connect( app.tree_view, "focus-out-event",
                          G_CALLBACK(hide_list), &app );
    }

    gtk_main();

    /** \return exit code is 0 only if an item was submitted */
    return 1;
}

