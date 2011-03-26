#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    COL_ICON = 0,
    COL_TEXT,
    NUM_COLS
};

void exit(int status);
void *malloc(size_t size);
void free(void *ptr);

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
                exit(1);
                return TRUE;
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                entry = (GtkEntry *)data;
                g_print( gtk_entry_get_text(GTK_ENTRY(entry)) );
                gtk_main_quit();
                return TRUE;
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

gboolean readStdin(gpointer data)
{
    static char buf[BUFSIZ];
    static const char *const buf_begin = buf;
    static char *bufp = buf;
    static struct timeval stdin_tv = {0,0};
    fd_set stdin_fds;
    GtkTreeIter iter;
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
                /*GIcon *mime_icon = g_content_type_get_icon("text/plain");*/
                GdkPixbuf *pixbuf = pixbuf_from_file(buf);
                /*GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);*/
                /*GError *error = NULL;*/
                /*GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("/usr/share/icons/gnome/16x16/actions/add.png", &error);*/

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
    /*list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);*/

    g_idle_add(readStdin, list_store);

    return GTK_TREE_MODEL(list_store);
}

const gchar *match_tokens(const char *haystack, const char *needle, int max)
{
    if ( !*needle )
        return haystack;

    int i;
    const char *h, *hh, *nn;
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
    const gchar *a, *b, *c;
    int sela, selb, len;
   
    entry = (GtkEntry *)data;
    gtk_tree_model_get(model, iter, COL_TEXT, &item_text, -1);
    if (!item_text)
        return FALSE;
    filter_text = gtk_entry_get_text(entry);

    len = gtk_entry_get_text_length(entry);
    gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &sela, &selb);
    /*fprintf(stderr, "%d %d %d\n", sela, selb, len);*/

    c = match_tokens(item_text, filter_text, len);
    visible = c != NULL;

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

static GtkEntryCompletion *create_completion(GtkTreeModel *model)
{
    GtkEntryCompletion *completion;

    completion = gtk_entry_completion_new();
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

void entry_changed(GtkEntry *entry, GtkTreeView *tree_view)
{
    if ( gtk_widget_has_focus(GTK_WIDGET(entry)) ) {
        gtk_tree_model_filter_refilter(
                GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(tree_view)) );
    }
}

int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *layout;
    GtkWidget *entry;
    GtkWidget *tree_view;
    GtkWidget *scroll_window;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    GtkEntryCompletion *completion;
    GtkTreeModel *model;

    gtk_init(&argc, &argv);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    layout = gtk_vbox_new(FALSE, 2);

    /* entry */
    entry = gtk_entry_new();
    model = create_model_from_stdin();
    model = create_filter( model, GTK_ENTRY(entry) );

    /* entry completion */
    completion = create_completion( GTK_TREE_MODEL(model) );
    gtk_entry_set_completion( GTK_ENTRY(entry), completion );

    /* list */
    tree_view = gtk_tree_view_new_with_model(model);
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(scroll_window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

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

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(tree_view), FALSE );
    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(tree_view), TRUE );
    gtk_tree_view_set_enable_tree_lines( GTK_TREE_VIEW(tree_view), FALSE );

    g_object_unref(model);

    g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(entry_changed), tree_view);

    /*gtk_container_set_border_width( GTK_CONTAINER(window), 2 );*/
    gtk_container_add( GTK_CONTAINER(window), layout );
    gtk_box_pack_start( GTK_BOX(layout), entry, 0,1,0 );
    gtk_box_pack_start( GTK_BOX(layout), scroll_window, 1,1,0 );
    gtk_container_add( GTK_CONTAINER(scroll_window), tree_view );

    gtk_window_resize(GTK_WINDOW(window), 320, 320);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    gtk_widget_show_all(window);
    /*gtk_widget_show(layout);*/
    /*gtk_widget_show(entry);*/
    /*gtk_widget_show(scroll_window);*/
    /*gtk_widget_show(tree_view);*/

    gtk_widget_grab_focus(entry);
    gtk_main();

    return 0;
}

