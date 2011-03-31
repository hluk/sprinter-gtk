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
    COL_ICON = 0,
    COL_TEXT,
    NUM_COLS
};

static gchar* g_complete = NULL;
static gulong g_first_item_signal;

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

static gboolean on_key_press(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GtkEntry *entry;

    if (event->type == GDK_KEY_PRESS){
        switch (event->key.keyval)
        {
            case GDK_KEY_Escape:
                gtk_main_quit();
                return TRUE;
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                entry = (GtkEntry *)data;
                g_print( gtk_entry_get_text(GTK_ENTRY(entry)) );
                gtk_main_quit();
                exit(0);
                return TRUE;
        }
    }

    return FALSE;
}

static gboolean tree_view_on_key_press(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    if (event->type == GDK_KEY_PRESS && event->key.keyval == GDK_KEY_Up){
        GtkTreePath *path;
        gtk_tree_view_get_cursor( GTK_TREE_VIEW(widget), &path, NULL );
        if (path) {
            gboolean hasprev = gtk_tree_path_prev(path);
            gtk_tree_path_free(path);

            if (!hasprev) {
                gtk_widget_grab_focus( GTK_WIDGET(data) );
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

void first_item_inserted(GtkTreeModel *tree_model,
                         GtkTreePath  *path,
                         GtkTreeIter  *iter,
                         gpointer      user_data)
{
    gchar *item_text;
    GtkEntry *entry = GTK_ENTRY(user_data);

    if ( gtk_entry_get_text_length(entry) == 0 ) {
        gtk_tree_model_get(tree_model, iter, COL_TEXT, &item_text, -1);
        gtk_entry_set_text(entry, item_text);
        gtk_entry_select_region(entry, 0, -1);
        g_free(item_text);
    }

    g_signal_handler_disconnect(tree_model, g_first_item_signal);
}

gboolean readStdin(gpointer data)
{
    static gchar buf[BUFSIZ];
    static const gchar *const buf_begin = buf;
    static gchar *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    fd_set stdin_fds;
    GtkTreeIter iter;
    GdkPixbuf *pixbuf;
    GtkListStore *list_store = (GtkListStore *)data;

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

                gtk_list_store_append(list_store, &iter);
                gtk_list_store_set( list_store, &iter,
                        /*COL_ICON, GTK_STOCK_EDIT,*/
                        /*COL_ICON, mime_icon,*/
                        COL_ICON, pixbuf,
                        COL_TEXT, buf,
                        -1 );

                if (pixbuf)
                    g_object_unref(pixbuf);
                bufp = buf;
            } else if ( bufp >= &buf[BUFSIZ-1] ) {
                fprintf(stderr, "Line too big (BUFSIZE is %d)!\n", BUFSIZ);
            }
        } else {
            break;
        }
    }

    return !ferror(stdin) && !feof(stdin);

    /*
    while(1) {
        c = getchar();
        if ( c == EOF ) {
            break;
        } else if ( c == '\n' || c == '\0' ) {
            if (i>0) {
                buff[i] = '\0';
                gtk_list_store_append(list_store, &iter);
                gtk_list_store_set(list_store, &iter, 0, buff, -1);
                i = 0;
            }
        } else {
            buff[i] = c;
            i++;
        }
    }
    */
}

static GtkTreeModel *create_model_from_stdin()
{
    GtkListStore *list_store;

    list_store = gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);

    g_idle_add(readStdin, list_store);

    return GTK_TREE_MODEL(list_store);
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

static gboolean filter_func(GtkTreeModel *model,
        GtkTreeIter  *iter,
        gpointer data)
{
    gboolean visible;
    gchar *item_text;
    const gchar *filter_text;
    GtkEntry *entry;
    int sela, selb, len;
    gboolean selected;

    entry = (GtkEntry *)data;
    gtk_tree_model_get(model, iter, COL_TEXT, &item_text, -1);
    if (!item_text)
        return FALSE;
    filter_text = gtk_entry_get_text(entry);

    len = gtk_entry_get_text_length(entry);
    selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &sela, &selb);

    visible = match_tokens(item_text, filter_text, sela) != NULL;

    /*visible = ( item && strcmp(item, gtk_entry_get_text(entry)) == 0 );*/
    g_free(item_text);

    return visible;
}

static GtkTreeModel *create_filter(GtkTreeModel *model, GtkEntry *entry)
{
    GtkTreeModel *filter;

    filter = gtk_tree_model_filter_new(model, NULL);
    gtk_tree_model_filter_set_visible_func( GTK_TREE_MODEL_FILTER(filter),
            filter_func, entry, NULL );

    return filter;
}

gboolean match_func( GtkEntryCompletion *completion,
                     const gchar *key,
                     GtkTreeIter *iter,
                     gpointer user_data)
{
    gchar *item;
    const gchar *a, *b;
    gboolean result = FALSE;
    GtkTreeModel *model;

    model = gtk_entry_completion_get_model(completion);
    gtk_tree_model_get(model, iter, COL_TEXT, &item, -1);
    if (item) {
        if (g_complete) {
            result = strcmp(g_complete, item) == 0;
            g_free(item);
        } else {
            for( a=item, b=key; *a && *b && toupper(*a) == toupper(*b); ++a, ++b);
            if (!*b) {
                result = TRUE;
                g_complete = item;
            } else {
                g_free(item);
            }
        }
    }

    return result;
}

static GtkEntryCompletion *create_completion(GtkTreeModel *model)
{
    GtkEntryCompletion *completion;

    completion = gtk_entry_completion_new();
    gtk_entry_completion_set_match_func(completion, match_func, NULL, NULL);
    gtk_entry_completion_set_model(completion, model);
    gtk_entry_completion_set_text_column(completion, COL_TEXT);
    gtk_entry_completion_set_popup_completion(completion, FALSE);
    gtk_entry_completion_set_inline_selection(completion, TRUE);
    gtk_entry_completion_set_inline_completion(completion, TRUE);

    return completion;
}

gboolean item_select (GtkTreeSelection *selection, GtkTreeModel *model,
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

gboolean refilter(gpointer data)
{
    gtk_tree_model_filter_refilter(
            GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(GTK_TREE_VIEW(data))) );
    return FALSE;
}

void entry_changed(GtkEntry *entry, GtkTreeView *tree_view)
{
    static GSource *source = NULL;

    if ( gtk_widget_has_focus(GTK_WIDGET(entry)) ) {
        if (g_complete) {
            g_free(g_complete);
            g_complete = NULL;
        }

        if (source)
            g_source_destroy(source);
        source = g_timeout_source_new(300);
        g_source_set_callback(source, refilter, tree_view, NULL);
        g_source_attach(source, NULL);
    }
}

int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *layout;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *tree_view;
    GtkWidget *scroll_window;
    GdkPixbuf *pixbuf;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    GtkEntryCompletion *completion;
    GtkTreeModel *model;
    Options options;
    gint w, h, x ,y;
    GdkGravity gravity;

    /* default options */
    options.title = "sprinter";
    options.label = "select:";
    options.hide_list = options.sort_list = options.strict = FALSE;
    options.x = options.y = options.width = options.height = OPTION_UNSET;

    parseArguments(argc, argv, &options);

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title( GTK_WINDOW(window), options.title );
    /*gtk_window_set_icon_from_file( GTK_WINDOW(window), "sprinter.svg", NULL );*/
    pixbuf = gdk_pixbuf_new_from_inline(-1, sprinter_icon, FALSE, NULL);
    gtk_window_set_icon( GTK_WINDOW(window), pixbuf );

    layout = gtk_vbox_new(FALSE, 2);
    hbox = gtk_hbox_new(FALSE, 2);

    /* entry */
    entry = gtk_entry_new();
    label = gtk_label_new(options.label);
    model = create_model_from_stdin();
    model = create_filter( model, GTK_ENTRY(entry) );

    /* entry completion */
    completion = create_completion( GTK_TREE_MODEL(model) );
    gtk_entry_set_completion( GTK_ENTRY(entry), completion );

    /* list */
    tree_view = gtk_tree_view_new_with_model(model);
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll_window),
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

    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    gtk_tree_selection_set_select_function(
            gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view)),
            item_select, GTK_ENTRY(entry), NULL
    );

    gtk_tree_view_set_search_column( GTK_TREE_VIEW(tree_view), COL_TEXT );
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(tree_view), FALSE );
    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(tree_view), TRUE );
    gtk_tree_view_set_enable_tree_lines( GTK_TREE_VIEW(tree_view), FALSE );

    g_object_unref(model);

    g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), entry);
    g_signal_connect(tree_view, "key-press-event", G_CALLBACK(tree_view_on_key_press), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(entry_changed), tree_view);
    g_first_item_signal = g_signal_connect( gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)),
                      "row-inserted", G_CALLBACK(first_item_inserted), entry );

    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(window), layout );
    gtk_box_pack_start( GTK_BOX(hbox), label, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(hbox), entry, 1,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), hbox, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), scroll_window, 1,1,0 );
    gtk_container_add( GTK_CONTAINER(scroll_window), tree_view );

    /* default position: center of the screen */
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    /* resize window */
    if (options.height != OPTION_UNSET || options.width != OPTION_UNSET) {
        gtk_window_get_size( GTK_WINDOW(window), &w, &h );
        if (options.width == OPTION_UNSET)
            options.width = w;
        else if (options.height == OPTION_UNSET)
            options.height = h;
        gtk_window_resize( GTK_WINDOW(window), options.width, options.height );
    } else {
        gtk_window_resize(GTK_WINDOW(window), 230, 300);
    }
    /* move window */
    if (options.x != OPTION_UNSET || options.y != OPTION_UNSET) {
        gtk_window_get_position( GTK_WINDOW(window), &x, &y );
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
        gtk_window_set_gravity( GTK_WINDOW(window), gravity );
        gtk_window_move( GTK_WINDOW(window), options.x, options.y );
    }

    gtk_widget_show_all(window);
    /*gtk_widget_show(layout);*/
    /*gtk_widget_show(entry);*/
    /*gtk_widget_show(scroll_window);*/
    /*gtk_widget_show(tree_view);*/

    gtk_widget_grab_focus(entry);
    gtk_main();

    /* exit code is 0 only if an item was submitted - i.e. ENTER pressed*/
    return 1;
}

