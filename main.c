#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "sprinter_icon.h"

#define TR(x) (x)

enum
{
    COL_VISIBLE = 0,
    COL_ICON,
    COL_TEXT,
    NUM_COLS
};

typedef struct {
    GtkWindow *window;
    GtkLabel *label;
    GtkEntry *entry;
    GtkTreeView *tree_view;
    GtkScrolledWindow *scroll_window;
    GtkListStore *store;
    gboolean complete;
    gboolean insert_first;
} Application;

typedef struct {
    const char shopt;
    const char *opt;
    const char *help;
} Argument;

const Argument arguments[] = {
    {'g', "geometry", "window size and position (format: width,height,x,y)"},
    {'h', "help",     "show this help"},
    {'l', "label",    "text input label"},
    {'m', "minimal",  "show popup menu instead of list"},
    {'o', "sort",     "sort items alphabetically"},
    {'S', "strict",   "choose only items from stdin"},
    {'t', "title",    "title"}
};

#define OPTION_UNSET -99999

typedef struct {
    const char *title, *label;
    gboolean hide_list, sort_list, strict;
    int x, y, width, height;
} Options;

/* print help and exit */
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

void parseArguments(int argc, char *argv[], Options *options)
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

static gboolean on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
    guint key;

    if (event->type == GDK_KEY_PRESS){
        key = event->key.keyval;
        switch (key)
        {
            case GDK_KEY_Escape:
                gtk_main_quit();
                return TRUE;
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                g_print( gtk_entry_get_text(app->entry) );
                gtk_main_quit();
                exit(0);
                return TRUE;
        }

        /* don't complete on some keys */
        app->complete = key != GDK_KEY_BackSpace && key != GDK_KEY_Delete;
    }

    return FALSE;
}

static gboolean tree_view_on_key_press(GtkWidget *widget, GdkEvent *event, Application *app)
{
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

gboolean readStdin(gpointer user_data)
{
    static gchar buf[BUFSIZ];
    static const gchar *const buf_begin = buf;
    static gchar *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    const char *filter_text, *a, *b;
    int sela, selb;
    fd_set stdin_fds;
    GtkTreeIter iter;
    GdkPixbuf *pixbuf;
    Application *app;

    app = (Application *)user_data;

    /*
     * interrupt after reading at most N lines and
     * resume after processing pending events in event loop
     */
    int i = 0;
    for( ; i < 20; ++i ) {
        /* set stdin */
        FD_ZERO(&stdin_fds);
        FD_SET(STDIN_FILENO, &stdin_fds);

        /* check if data available */
        if ( select(STDIN_FILENO+1, &stdin_fds, NULL, NULL, &stdin_tv) <= 0 )
            break;

        /* read data */
        if ( fgets(bufp, BUFSIZ - (bufp - buf_begin), stdin) ) {
            while ( *bufp != '\0' ) ++bufp;
            /* each line is one item */
            if ( *(bufp-1) == '\n' ) {
                *(bufp-1) = '\0';
                pixbuf = pixbuf_from_file(buf);

                filter_text = gtk_entry_get_text(app->entry);
                if ( !gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), &sela, &selb) ) {
                    /* inline completion */
                    if (app->complete) {
                        /* if item starts with filter text */
                        for( a = buf, b = filter_text;
                                *a && *b && toupper(*a) == toupper(*b);
                                ++a, ++b );
                        if (*a && !*b) {
                            gtk_editable_insert_text( GTK_EDITABLE(app->entry), a, -1, &sela );
                            gtk_entry_select_region(app->entry, -1, selb);
                        }
                    }
                }
                if ( app->insert_first ) {
                    app->insert_first = TRUE;
                    if ( !*filter_text ) {
                        gtk_entry_set_text(app->entry, buf);
                        gtk_entry_select_region(app->entry, -1, 0);
                    }
                }
                gtk_list_store_append(app->store, &iter);
                gtk_list_store_set( app->store, &iter,
                        COL_VISIBLE, match_tokens(buf, filter_text, sela),
                        COL_ICON, pixbuf,
                        COL_TEXT, buf,
                        -1 );

                if (pixbuf)
                    g_object_unref(pixbuf);
                bufp = buf;
            } else if ( bufp >= &buf[BUFSIZ-1] ) {
                fprintf(stderr, "Line too big (BUFSIZ is %d)!\n", BUFSIZ);
            }
        } else {
            break;
        }
    }

    return !ferror(stdin) && !feof(stdin);
}

static GtkTreeModel *create_filter(GtkListStore *store)
{
    GtkTreeModel *filter;

    filter = gtk_tree_model_filter_new( GTK_TREE_MODEL(store), NULL );
    gtk_tree_model_filter_set_visible_column( GTK_TREE_MODEL_FILTER(filter), COL_VISIBLE );

    return filter;
}

gboolean item_select(GtkTreeSelection *selection, GtkTreeModel *model,
        GtkTreePath *path, gboolean path_currently_selected, gpointer data)
{
    gchar *item;
    GtkTreeIter iter;
    GtkEntry *entry;

    if ( !gtk_tree_model_get_iter(model, &iter, path) )
        return TRUE;

    gtk_tree_model_get(model, &iter, COL_TEXT, &item, -1);
    entry = (GtkEntry *)data;
    gtk_entry_set_text(entry, item);
    g_free(item);

    return TRUE;
}

gboolean refilter(gpointer user_data)
{
    /* TODO: if text appended to entry - refilter only visible items
     *       - save entry text as static variable and next time
     *         check against new entry text
     */
    Application *app;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *item_text;
    const gchar *filter_text, *a, *b;
    gboolean selected, visible;
    int sela, selb;

    app = (Application *)user_data;

    selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(app->entry), &sela, &selb);
    if (selected)
        return FALSE;

    model = GTK_TREE_MODEL(app->store);
    filter_text = gtk_entry_get_text(app->entry);

    if ( gtk_tree_model_get_iter_first(model, &iter) ) {
        do {
            gtk_tree_model_get(model, &iter, COL_TEXT, &item_text, -1);
            if (item_text) {
                /* inline completion */
                if (app->complete) {
                    /* if item starts with filter text */
                    for( a = item_text, b = filter_text;
                            *a && *b && toupper(*a) == toupper(*b);
                            ++a, ++b );
                    if (*a && !*b) {
                        gtk_editable_insert_text( GTK_EDITABLE(app->entry), a, -1, &sela );
                        gtk_entry_select_region(app->entry, -1, selb);
                    }
                }

                visible = match_tokens(item_text, filter_text, selb) != NULL;
                fprintf(stderr, "%c %s:%d %s\n", visible ? '1' : '0', filter_text, selb, item_text);
                gtk_list_store_set(app->store, &iter, COL_VISIBLE, visible, -1);

                g_free(item_text);
            }
        } while( gtk_tree_model_iter_next(model, &iter) );
    }

    return FALSE;
}

void entry_changed(GtkEntry *entry, Application *app)
{
    static GSource *source = NULL;

    if ( gtk_widget_has_focus(GTK_WIDGET(entry)) ) {
        app->insert_first = FALSE;
        if (source)
            g_source_destroy(source);
        source = g_timeout_source_new(200);
        g_source_set_callback(source, refilter, app, NULL);
        g_source_attach(source, NULL);
    }
}

int main(int argc, char *argv[])
{
    GtkWidget *layout;
    GtkWidget *hbox;
    GdkPixbuf *pixbuf;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    GtkTreeModel *model;
    Options options;
    gint w, h, x ,y;
    GdkGravity gravity;
    Application app;

    /* default options */
    options.title = "sprinter";
    options.label = "select:";
    options.hide_list = options.sort_list = options.strict = FALSE;
    options.x = options.y = options.width = options.height = OPTION_UNSET;

    parseArguments(argc, argv, &options);

    gtk_init(&argc, &argv);

    app.complete = TRUE;
    app.insert_first = TRUE;

    app.window = GTK_WINDOW( gtk_window_new(GTK_WINDOW_TOPLEVEL) );
    gtk_window_set_title( app.window, options.title );
    /*gtk_window_set_icon_from_file( app.window, "sprinter.svg", NULL );*/
    pixbuf = gdk_pixbuf_new_from_inline(-1, sprinter_icon, FALSE, NULL);
    gtk_window_set_icon( app.window, pixbuf );

    layout = gtk_vbox_new(FALSE, 2);
    hbox = gtk_hbox_new(FALSE, 2);

    /* entry */
    app.entry = GTK_ENTRY( gtk_entry_new() );
    app.label = GTK_LABEL( gtk_label_new(options.label) );

    /* list store and filtered model */
    app.store = gtk_list_store_new( 3,
                    G_TYPE_BOOLEAN,
                    GDK_TYPE_PIXBUF,
                    G_TYPE_STRING );
    model = create_filter(app.store);

    /* append lines from stdin to list store */
    g_idle_add(readStdin, &app);

    /* list */
    app.tree_view = GTK_TREE_VIEW( gtk_tree_view_new_with_model(model) );
    app.scroll_window = GTK_SCROLLED_WINDOW( gtk_scrolled_window_new(NULL, NULL) );
    gtk_scrolled_window_set_policy( app.scroll_window,
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );

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

    gtk_tree_view_append_column(app.tree_view, col);

    gtk_tree_selection_set_select_function(
            gtk_tree_view_get_selection(app.tree_view),
            item_select, app.entry, NULL
    );

    gtk_tree_view_set_search_column( app.tree_view, COL_TEXT );
    gtk_tree_view_set_headers_visible( app.tree_view, FALSE );
    gtk_tree_view_set_fixed_height_mode( app.tree_view, TRUE );
    gtk_tree_view_set_enable_tree_lines( app.tree_view, FALSE );

    g_object_unref(model);

    g_signal_connect(app.window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);
    g_signal_connect(app.tree_view, "key-press-event", G_CALLBACK(tree_view_on_key_press), &app);
    g_signal_connect(app.entry, "changed", G_CALLBACK(entry_changed), &app);

    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(app.window), layout );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app.label), 0,1,0 );
    gtk_box_pack_start( GTK_BOX(hbox), GTK_WIDGET(app.entry), 1,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), hbox, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), GTK_WIDGET(app.scroll_window), 1,1,0 );
    gtk_container_add( GTK_CONTAINER(app.scroll_window), GTK_WIDGET(app.tree_view) );

    /* default position: center of the screen */
    gtk_window_set_position(app.window, GTK_WIN_POS_CENTER);

    /* resize window */
    if (options.height != OPTION_UNSET || options.width != OPTION_UNSET) {
        gtk_window_get_size( app.window, &w, &h );
        if (options.width == OPTION_UNSET)
            options.width = w;
        else if (options.height == OPTION_UNSET)
            options.height = h;
        gtk_window_resize( app.window, options.width, options.height );
    } else {
        gtk_window_resize(app.window, 230, 300);
    }
    /* move window */
    if (options.x != OPTION_UNSET || options.y != OPTION_UNSET) {
        gtk_window_get_position( app.window, &x, &y );
        if (options.x == OPTION_UNSET)
            options.x = x;
        else if (options.y == OPTION_UNSET)
            options.y = y;
        if (options.x < 0) {
            if (options.y < 0) {
                gravity = GDK_GRAVITY_SOUTH_EAST;
                options.y = gdk_screen_height() + options.y + 1;
            } else {
                gravity = GDK_GRAVITY_NORTH_EAST;
            }
            options.x = gdk_screen_width() + options.x;
        } else if (options.y < 0) {
            gravity = GDK_GRAVITY_SOUTH_WEST;
            options.y = gdk_screen_height() + options.y + 1;
        } else {
            gravity = GDK_GRAVITY_NORTH_WEST;
        }
        gtk_window_set_gravity( app.window, gravity );
        gtk_window_move( app.window, options.x, options.y );
    }

    gtk_widget_show_all( GTK_WIDGET(app.window) );

    gtk_main();

    /* exit code is 0 only if an item was submitted - i.e. ENTER pressed*/
    return 1;
}

