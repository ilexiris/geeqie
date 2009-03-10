/*
 * Geeqie
 * (C) 2004 John Ellis
 * Copyright (C) 2008 - 2009 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#include "main.h"
#include "layout_util.h"

#include "advanced_exif.h"
#include "bar_sort.h"
#include "bar.h"
#include "cache_maint.h"
#include "collect.h"
#include "collect-dlg.h"
#include "compat.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "history_list.h"
#include "image-overlay.h"
#include "img-view.h"
#include "layout_image.h"
#include "logwindow.h"
#include "misc.h"
#include "pan-view.h"
#include "pixbuf_util.h"
#include "preferences.h"
#include "print.h"
#include "search.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_misc.h"
#include "ui_tabcomp.h"
#include "utilops.h"
#include "view_dir.h"
#include "window.h"
#include "metadata.h"
#include "rcfile.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


#define MENU_EDIT_ACTION_OFFSET 16

static gboolean layout_bar_enabled(LayoutWindow *lw);
static gboolean layout_bar_sort_enabled(LayoutWindow *lw);

/*
 *-----------------------------------------------------------------------------
 * keyboard handler
 *-----------------------------------------------------------------------------
 */

static guint tree_key_overrides[] = {
	GDK_Page_Up,	GDK_KP_Page_Up,
	GDK_Page_Down,	GDK_KP_Page_Down,
	GDK_Home,	GDK_KP_Home,
	GDK_End,	GDK_KP_End
};

static gboolean layout_key_match(guint keyval)
{
	guint i;

	for (i = 0; i < sizeof(tree_key_overrides) / sizeof(guint); i++)
		{
		if (keyval == tree_key_overrides[i]) return TRUE;
		}

	return FALSE;
}

gint layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	LayoutWindow *lw = data;
	gint stop_signal = FALSE;
	gint x = 0;
	gint y = 0;

	if (lw->path_entry && GTK_WIDGET_HAS_FOCUS(lw->path_entry))
		{
		if (event->keyval == GDK_Escape && lw->dir_fd)
			{
			gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			}

		/* the gtkaccelgroup of the window is stealing presses before they get to the entry (and more),
		 * so when the some widgets have focus, give them priority (HACK)
		 */
		if (gtk_widget_event(lw->path_entry, (GdkEvent *)event))
			{
			return TRUE;
			}
		}
	if (lw->vd && lw->options.dir_view_type == DIRVIEW_TREE && GTK_WIDGET_HAS_FOCUS(lw->vd->view) &&
	    !layout_key_match(event->keyval) &&
	    gtk_widget_event(lw->vd->view, (GdkEvent *)event))
		{
		return TRUE;
		}
	if (lw->bar &&
	    bar_event(lw->bar, (GdkEvent *)event))
		{
		return TRUE;
		}

/*
	if (event->type == GDK_KEY_PRESS && lw->full_screen &&
	    gtk_accel_groups_activate(G_OBJECT(lw->window), event->keyval, event->state))
		return TRUE;
*/

	if (lw->image &&
	    (GTK_WIDGET_HAS_FOCUS(lw->image->widget) || (lw->tools && widget == lw->window) || lw->full_screen) )
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_Left: case GDK_KP_Left:
				x -= 1;
				break;
			case GDK_Right: case GDK_KP_Right:
				x += 1;
				break;
			case GDK_Up: case GDK_KP_Up:
				y -= 1;
				break;
			case GDK_Down: case GDK_KP_Down:
				y += 1;
				break;
			default:
				stop_signal = FALSE;
				break;
			}

		if (!stop_signal &&
		    !(event->state & GDK_CONTROL_MASK))
			{
			stop_signal = TRUE;
			switch (event->keyval)
				{
				case GDK_Menu:
					layout_image_menu_popup(lw);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}
		}

	if (x != 0 || y!= 0)
		{
		keyboard_scroll_calc(&x, &y, event);
		layout_image_scroll(lw, x, y, (event->state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(layout_key_press_cb), lw);
}

/*
 *-----------------------------------------------------------------------------
 * menu callbacks
 *-----------------------------------------------------------------------------
 */


static GtkWidget *layout_window(LayoutWindow *lw)
{
	return lw->full_screen ? lw->full_screen->window : lw->window;
}

static void layout_exit_fullscreen(LayoutWindow *lw)
{
	if (!lw->full_screen) return;
	layout_image_full_screen_stop(lw);
}

static void layout_menu_new_window_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	LayoutWindow *nw;

	layout_exit_fullscreen(lw);

	nw = layout_new(NULL, NULL);
	layout_sort_set(nw, options->file_sort.method, options->file_sort.ascending);
	layout_set_fd(nw, lw->dir_fd);
}

static void layout_menu_new_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	collection_window_new(NULL);
}

static void layout_menu_open_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	collection_dialog_load(NULL);
}

static void layout_menu_search_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	dupe_window_new(DUPE_MATCH_NAME);
}

static void layout_menu_pan_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	pan_window_new(lw->dir_fd);
}

static void layout_menu_print_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	print_window_new(layout_image_get_fd(lw), layout_selection_list(lw), layout_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_create_dir(lw->dir_fd, layout_window(lw), NULL, NULL);
}

static void layout_menu_copy_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_copy(NULL, layout_selection_list(lw), NULL, layout_window(lw));
}

static void layout_menu_copy_path_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_copy_path_list_to_clipboard(layout_selection_list(lw));
}

static void layout_menu_move_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_move(NULL, layout_selection_list(lw), NULL, layout_window(lw));
}

static void layout_menu_rename_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_rename(NULL, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_delete_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	file_util_delete(NULL, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_close_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_close(lw);
}

static void layout_menu_exit_cb(GtkAction *action, gpointer data)
{
	exit_program();
}

static void layout_menu_alter_90_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_ROTATE_90);
}

static void layout_menu_alter_90cc_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_ROTATE_90_CC);
}

static void layout_menu_alter_180_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_ROTATE_180);
}

static void layout_menu_alter_mirror_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_MIRROR);
}

static void layout_menu_alter_flip_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_FLIP);
}

static void layout_menu_alter_desaturate_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_DESATURATE);
}

static void layout_menu_alter_none_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter(lw, ALTER_NONE);
}

static void layout_menu_config_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	show_config_window();
}

static void layout_menu_remove_thumb_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

static void layout_menu_wallpaper_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_to_root(lw);
}

/* single window zoom */
static void layout_menu_zoom_in_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_out_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, -get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_1_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 1.0, FALSE);
}

static void layout_menu_zoom_fit_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 0.0, FALSE);
}

static void layout_menu_zoom_fit_hor_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, FALSE, FALSE);
}

static void layout_menu_zoom_fit_vert_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, TRUE, FALSE);
}

static void layout_menu_zoom_2_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 2.0, FALSE);
}

static void layout_menu_zoom_3_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 3.0, FALSE);
}
static void layout_menu_zoom_4_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 4.0, FALSE);
}

static void layout_menu_zoom_1_2_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -2.0, FALSE);
}

static void layout_menu_zoom_1_3_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -3.0, FALSE);
}

static void layout_menu_zoom_1_4_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -4.0, FALSE);
}

/* connected zoom */
static void layout_menu_connect_zoom_in_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_out_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, -get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_1_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 1.0, TRUE);
}

static void layout_menu_connect_zoom_fit_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 0.0, TRUE);
}

static void layout_menu_connect_zoom_fit_hor_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, FALSE, TRUE);
}

static void layout_menu_connect_zoom_fit_vert_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, TRUE, TRUE);
}

static void layout_menu_connect_zoom_2_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 2.0, TRUE);
}

static void layout_menu_connect_zoom_3_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 3.0, TRUE);
}
static void layout_menu_connect_zoom_4_1_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 4.0, TRUE);
}

static void layout_menu_connect_zoom_1_2_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -2.0, TRUE);
}

static void layout_menu_connect_zoom_1_3_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -3.0, TRUE);
}

static void layout_menu_connect_zoom_1_4_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -4.0, TRUE);
}


static void layout_menu_split_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer data)
{
	LayoutWindow *lw = data;
	ImageSplitMode mode;

	layout_exit_fullscreen(lw);

	mode = gtk_radio_action_get_current_value(action);
	if (mode == lw->split_mode) mode = 0; /* toggle back */

	layout_split_change(lw, mode);
}


static void layout_menu_thumb_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_thumb_set(lw, gtk_toggle_action_get_active(action));
}


static void layout_menu_list_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer data)
{
	LayoutWindow *lw = data;
	
	layout_exit_fullscreen(lw);
	layout_views_set(lw, lw->options.dir_view_type, (gtk_radio_action_get_current_value(action) == 1) ? FILEVIEW_ICON : FILEVIEW_LIST);
}

static void layout_menu_view_dir_as_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_views_set(lw, (DirViewType) gtk_radio_action_get_current_value(action), lw->file_view_type);
}

static void layout_menu_view_in_new_window_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	view_window_new(layout_image_get_fd(lw));
}

static void layout_menu_fullscreen_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);

	/* FIXME:interrupting thumbs no longer allowed */
#if 0
	interrupt_thumbs();
#endif
}

static void layout_menu_overlay_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_toggle(lw->image);
}

static void layout_menu_histogram_chan_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_histogram_chan_toggle(lw->image);
}

static void layout_menu_histogram_log_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_histogram_log_toggle(lw->image);
}

static void layout_menu_refresh_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_refresh(lw);
}

static void layout_menu_bar_exif_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	
	layout_exit_fullscreen(lw);
	layout_exif_window_new(lw);
}

static void layout_menu_float_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.tools_float == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);
}

static void layout_menu_hide_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_toolbar_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.toolbar_hidden == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_toolbar_toggle(lw);
}

static void layout_menu_info_pixel_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.info_pixel_hidden == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_toggle(lw);
}

/* NOTE: these callbacks are called also from layout_util_sync_views */
static void layout_menu_bar_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_bar_enabled(lw) == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_toggle(lw);
}

static void layout_menu_bar_sort_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_bar_sort_enabled(lw) == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_sort_toggle(lw);
}

static void layout_menu_slideshow_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_slideshow_toggle(lw);
}

static void layout_menu_slideshow_pause_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_slideshow_pause_toggle(lw);
}

static void layout_menu_help_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("html_contents");
}

static void layout_menu_help_keys_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("documentation");
}

static void layout_menu_notes_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	
	layout_exit_fullscreen(lw);
	help_window_show("release_notes");
}

static void layout_menu_about_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	show_about_window();
}

static void layout_menu_log_window_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	log_window_new();
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_invert(lw);
}

static void layout_menu_marks_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_marks_set(lw, gtk_toggle_action_get_active(action));
}


static void layout_menu_set_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_SET);
}

static void layout_menu_res_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_RESET);
}

static void layout_menu_toggle_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_TOGGLE);
}

static void layout_menu_sel_mark_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_SET);
}

static void layout_menu_sel_mark_or_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_OR);
}

static void layout_menu_sel_mark_and_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_AND);
}

static void layout_menu_sel_mark_minus_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_MINUS);
}


/*
 *-----------------------------------------------------------------------------
 * go menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_image_first_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_prev(lw);
}

static void layout_menu_image_next_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_next(lw);
}

static void layout_menu_image_last_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_last(lw);
}

static void layout_menu_back_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	FileData *dir_fd;
	gchar *path = NULL;
	GList *list = history_list_get_by_key("path_list");
	gint n = 0;

	while (list)
		{
		if (n == 1) {
			/* Previous path from history */
			path = (gchar *)list->data;
			break;
		}
		list = list->next;
		n++;
		}

	if (!path) return;
	
	/* Open previous path */
	dir_fd = file_data_new_simple(path);
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GtkAction *action, gpointer data)
{
	const gchar *path;
	
	if (options->layout.home_path && *options->layout.home_path)
		path = options->layout.home_path;
	else
		path = homedir();

	if (path)
		{
		LayoutWindow *lw = data;
		FileData *dir_fd = file_data_new_simple(path);
		layout_set_fd(lw, dir_fd);
		file_data_unref(dir_fd);
		}
}


/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_edit_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	GList *list;
	const gchar *key = gtk_action_get_name(action);
	
	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	list = layout_selection_list(lw);
	file_util_start_editor_from_filelist(key, list, lw->window);
	filelist_free(list);
}

#if 0
static void layout_menu_edit_update(LayoutWindow *lw)
{
	gint i;

	/* main edit menu */

	if (!lw->action_group) return;

	for (i = 0; i < GQ_EDITOR_GENERIC_SLOTS; i++)
		{
		gchar *key;
		GtkAction *action;
		const gchar *name;
	
		key = g_strdup_printf("Editor%d", i);

		action = gtk_action_group_get_action(lw->action_group, key);
		g_object_set_data(G_OBJECT(action), "edit_index", GINT_TO_POINTER(i));

		name = editor_get_name(i);
		if (name)
			{
			gchar *text = g_strdup_printf(_("_%d %s..."), i, name);

			g_object_set(action, "label", text,
					     "sensitive", TRUE, NULL);
			g_free(text);
			}
		else
			{
			gchar *text;

			text = g_strdup_printf(_("_%d empty"), i);
			g_object_set(action, "label", text, "sensitive", FALSE, NULL);
			g_free(text);
			}

		g_free(key);
		}
}

void layout_edit_update_all(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_menu_edit_update(lw);
		}
}

#endif

/*
 *-----------------------------------------------------------------------------
 * recent menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_recent_cb(GtkWidget *widget, gpointer data)
{
	gint n;
	gchar *path;

	n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "recent_index"));

	path = g_list_nth_data(history_list_get_by_key("recent"), n);

	if (!path) return;

	/* make a copy of it */
	path = g_strdup(path);
	collection_window_new(path);
	g_free(path);
}

static void layout_menu_recent_update(LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *recent;
	GtkWidget *item;
	GList *list;
	gint n;

	if (!lw->ui_manager) return;

	list = history_list_get_by_key("recent");
	n = 0;

	menu = gtk_menu_new();

	while (list)
		{
		const gchar *filename = filename_from_path((gchar *)list->data);
		gchar *name;
		gboolean free_name = FALSE;

		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			name = remove_extension_from_path(filename);
			free_name = TRUE;
			}
		else
			{
			name = (gchar *) filename;
			}

		item = menu_item_add_simple(menu, name, G_CALLBACK(layout_menu_recent_cb), lw);
		if (free_name) g_free(name);
		g_object_set_data(G_OBJECT(item), "recent_index", GINT_TO_POINTER(n));
		list = list->next;
		n++;
		}

	if (n == 0)
		{
		menu_item_add(menu, _("Empty"), NULL, NULL);
		}

	recent = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/FileMenu/OpenRecent");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), menu);
	gtk_widget_set_sensitive(recent, (n != 0));
}

void layout_recent_update_all(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_menu_recent_update(lw);
		}
}

void layout_recent_add_path(const gchar *path)
{
	if (!path) return;

	history_list_add_to_key("recent", path, options->open_recent_list_maxsize);

	layout_recent_update_all();
}

/*
 *-----------------------------------------------------------------------------
 * copy path
 *-----------------------------------------------------------------------------
 */

static void layout_copy_path_update(LayoutWindow *lw)
{
	GtkWidget *item = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/FileMenu/CopyPath");

	if (!item) return;
	
	if (options->show_copy_path)
		gtk_widget_show(item);
	else
		gtk_widget_hide(item);
}

void layout_copy_path_update_all(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_copy_path_update(lw);
		}
}

/*
 *-----------------------------------------------------------------------------
 * menu
 *-----------------------------------------------------------------------------
 */

#define CB G_CALLBACK

static GtkActionEntry menu_entries[] = {
  { "FileMenu",		NULL,		N_("_File"),			NULL, 		NULL, 	NULL },
  { "GoMenu",		NULL,		N_("_Go"),			NULL, 		NULL, 	NULL },
  { "EditMenu",		NULL,		N_("_Edit"),			NULL, 		NULL, 	NULL },
  { "SelectMenu",	NULL,		N_("_Select"),			NULL, 		NULL, 	NULL },
  { "AdjustMenu",	NULL,		N_("_Adjust"),			NULL, 		NULL, 	NULL },
  { "ExternalMenu",	NULL,		N_("E_xternal Editors"),	NULL, 		NULL, 	NULL },
  { "ViewMenu",		NULL,		N_("_View"),			NULL, 		NULL, 	NULL },
  { "DirMenu",          NULL,           N_("_View Directory as"),	NULL, 		NULL, 	NULL },
  { "ZoomMenu",		NULL,		N_("_Zoom"),			NULL, 		NULL, 	NULL },
  { "ConnectZoomMenu",	NULL,		N_("_Connected Zoom"),		NULL, 		NULL, 	NULL },
  { "SplitMenu",	NULL,		N_("_Split"),			NULL, 		NULL, 	NULL },
  { "HelpMenu",		NULL,		N_("_Help"),			NULL, 		NULL, 	NULL },

  { "FirstImage",	GTK_STOCK_GOTO_TOP,	N_("_First Image"),	"Home",		NULL,	CB(layout_menu_image_first_cb) },
  { "PrevImage",	GTK_STOCK_GO_UP,   	N_("_Previous Image"),	"BackSpace",	NULL,	CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt1",	GTK_STOCK_GO_UP,   	N_("_Previous Image"),	"Page_Up",	NULL,	CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt2",	GTK_STOCK_GO_UP,   	N_("_Previous Image"),	"KP_Page_Up",	NULL,	CB(layout_menu_image_prev_cb) },
  { "NextImage",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),	"space",	NULL,	CB(layout_menu_image_next_cb) },
  { "NextImageAlt1",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),	"Page_Down",	NULL,	CB(layout_menu_image_next_cb) },
  { "NextImageAlt2",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),	"KP_Page_Down",	NULL,	CB(layout_menu_image_next_cb) },
  { "LastImage",	GTK_STOCK_GOTO_BOTTOM,	N_("_Last Image"),	"End",		NULL,	CB(layout_menu_image_last_cb) },
  { "Back",		GTK_STOCK_GO_BACK,	N_("_Back"),		NULL,		N_("Back"),	CB(layout_menu_back_cb) },
  { "Home",		GTK_STOCK_HOME,		N_("_Home"),		NULL,		N_("Home"),	CB(layout_menu_home_cb) },


  { "NewWindow",	GTK_STOCK_NEW,	N_("New _window"),	NULL,		NULL,	CB(layout_menu_new_window_cb) },
  { "NewCollection",	GTK_STOCK_INDEX,N_("_New collection"),	"C",		NULL,	CB(layout_menu_new_cb) },
  { "OpenCollection",	GTK_STOCK_OPEN,	N_("_Open collection..."),"O",		NULL,	CB(layout_menu_open_cb) },
  { "OpenRecent",	NULL,		N_("Open _recent"),	NULL,		NULL,	NULL },
  { "Search",		GTK_STOCK_FIND,	N_("_Search..."),	"F3",		NULL,	CB(layout_menu_search_cb) },
  { "FindDupes",	GTK_STOCK_FIND,	N_("_Find duplicates..."),"D",		NULL,	CB(layout_menu_dupes_cb) },
  { "PanView",		NULL,		N_("Pan _view"),	"<control>J",	NULL,	CB(layout_menu_pan_cb) },
  { "Print",		GTK_STOCK_PRINT,N_("_Print..."),	"<shift>P",	NULL,	CB(layout_menu_print_cb) },
  { "NewFolder",	NULL,		N_("N_ew folder..."),	"<control>F",	NULL,	CB(layout_menu_dir_cb) },
  { "Copy",		NULL,		N_("_Copy..."),		"<control>C",	NULL,	CB(layout_menu_copy_cb) },
  { "Move",		NULL,		N_("_Move..."),		"<control>M",	NULL,	CB(layout_menu_move_cb) },
  { "Rename",		NULL,		N_("_Rename..."),	"<control>R",	NULL,	CB(layout_menu_rename_cb) },
  { "Delete",	GTK_STOCK_DELETE,	N_("_Delete..."),	"<control>D",	NULL,	CB(layout_menu_delete_cb) },
  { "DeleteAlt1",GTK_STOCK_DELETE,	N_("_Delete..."),	"Delete",	NULL,	CB(layout_menu_delete_cb) },
  { "DeleteAlt2",GTK_STOCK_DELETE,	N_("_Delete..."),	"KP_Delete",	NULL,	CB(layout_menu_delete_cb) },
  { "CopyPath",		NULL,		N_("_Copy path to clipboard"),	NULL,		NULL,	CB(layout_menu_copy_path_cb) },
  { "CloseWindow",	GTK_STOCK_CLOSE,N_("C_lose window"),	"<control>W",	NULL,	CB(layout_menu_close_cb) },
  { "Quit",		GTK_STOCK_QUIT, N_("_Quit"),		"<control>Q",	NULL,	CB(layout_menu_exit_cb) },

  { "RotateCW",		NULL,	N_("_Rotate clockwise"),	"bracketright",	NULL,	CB(layout_menu_alter_90_cb) },
  { "RotateCCW",	NULL,	N_("Rotate _counterclockwise"),	"bracketleft",	NULL,	CB(layout_menu_alter_90cc_cb) },
  { "Rotate180",	NULL,		N_("Rotate 1_80"),	"<shift>R",	NULL,	CB(layout_menu_alter_180_cb) },
  { "Mirror",		NULL,		N_("_Mirror"),		"<shift>M",	NULL,	CB(layout_menu_alter_mirror_cb) },
  { "Flip",		NULL,		N_("_Flip"),		"<shift>F",	NULL,	CB(layout_menu_alter_flip_cb) },
  { "Grayscale",	NULL,		N_("Toggle _grayscale"),"<shift>G",	NULL,	CB(layout_menu_alter_desaturate_cb) },
  { "AlterNone",	NULL,		N_("_Original state"),  "<shift>O",	NULL,	CB(layout_menu_alter_none_cb) },

  { "SelectAll",	NULL,		N_("Select _all"),	"<control>A",	NULL,	CB(layout_menu_select_all_cb) },
  { "SelectNone",	NULL,		N_("Select _none"), "<control><shift>A",NULL,	CB(layout_menu_unselect_all_cb) },
  { "SelectInvert",	NULL,		N_("_Invert Selection"), "<control><shift>I",	NULL,	CB(layout_menu_invert_selection_cb) },

  { "Preferences",GTK_STOCK_PREFERENCES,N_("P_references..."),	"<control>O",	NULL,	CB(layout_menu_config_cb) },
  { "Maintenance",	NULL,		N_("_Thumbnail maintenance..."),NULL,	NULL,	CB(layout_menu_remove_thumb_cb) },
  { "Wallpaper",	NULL,		N_("Set as _wallpaper"),NULL,		NULL,	CB(layout_menu_wallpaper_cb) },

  { "ZoomIn",	GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),		"equal",	N_("Zoom in"),	CB(layout_menu_zoom_in_cb) },
  { "ZoomInAlt1",GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),		"KP_Add",	N_("Zoom in"),	CB(layout_menu_zoom_in_cb) },
  { "ZoomOut",	GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),	"minus",	N_("Zoom out"),	CB(layout_menu_zoom_out_cb) },
  { "ZoomOutAlt1",GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),	"KP_Subtract",	N_("Zoom out"),	CB(layout_menu_zoom_out_cb) },
  { "Zoom100",	GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),	"Z",		N_("Zoom 1:1"),	CB(layout_menu_zoom_1_1_cb) },
  { "Zoom100Alt1",GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),	"KP_Divide",	N_("Zoom 1:1"),	CB(layout_menu_zoom_1_1_cb) },
  { "ZoomFit",	GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),	"X",		N_("Zoom to fit"),	CB(layout_menu_zoom_fit_cb) },
  { "ZoomFitAlt1",GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),	"KP_Multiply",	N_("Zoom to fit"),	CB(layout_menu_zoom_fit_cb) },
  { "ZoomFillHor",	NULL,		N_("Fit _Horizontally"),"H",		NULL,	CB(layout_menu_zoom_fit_hor_cb) },
  { "ZoomFillVert",	NULL,		N_("Fit _Vertically"),	"W",		NULL,	CB(layout_menu_zoom_fit_vert_cb) },
  { "Zoom200",	        NULL,		N_("Zoom _2:1"),	NULL,		NULL,	CB(layout_menu_zoom_2_1_cb) },
  { "Zoom300",	        NULL,		N_("Zoom _3:1"),	NULL,		NULL,	CB(layout_menu_zoom_3_1_cb) },
  { "Zoom400",		NULL,		N_("Zoom _4:1"),	NULL,		NULL,	CB(layout_menu_zoom_4_1_cb) },
  { "Zoom50",		NULL,		N_("Zoom 1:2"),		NULL,		NULL,	CB(layout_menu_zoom_1_2_cb) },
  { "Zoom33",		NULL,		N_("Zoom 1:3"),		NULL,		NULL,	CB(layout_menu_zoom_1_3_cb) },
  { "Zoom25",		NULL,		N_("Zoom 1:4"),		NULL,		NULL,	CB(layout_menu_zoom_1_4_cb) },

  { "ConnectZoomIn",	GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),		"plus",			NULL,	CB(layout_menu_connect_zoom_in_cb) },
  { "ConnectZoomInAlt1",GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),		"<shift>KP_Add",	NULL,	CB(layout_menu_connect_zoom_in_cb) },
  { "ConnectZoomOut",	GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),	"underscore",		NULL,	CB(layout_menu_connect_zoom_out_cb) },
  { "ConnectZoomOutAlt1",GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),	"<shift>KP_Subtract",	NULL,	CB(layout_menu_connect_zoom_out_cb) },
  { "ConnectZoom100",	GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),	"<shift>Z",		NULL,	CB(layout_menu_connect_zoom_1_1_cb) },
  { "ConnectZoom100Alt1",GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),	"<shift>KP_Divide",	NULL,	CB(layout_menu_connect_zoom_1_1_cb) },
  { "ConnectZoomFit",	GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),	"<shift>X",		NULL,	CB(layout_menu_connect_zoom_fit_cb) },
  { "ConnectZoomFitAlt1",GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),	"<shift>KP_Multiply",	NULL,	CB(layout_menu_connect_zoom_fit_cb) },
  { "ConnectZoomFillHor",	NULL,		N_("Fit _Horizontally"),"<shift>H",		NULL,	CB(layout_menu_connect_zoom_fit_hor_cb) },
  { "ConnectZoomFillVert",	NULL,		N_("Fit _Vertically"),	"<shift>W",		NULL,	CB(layout_menu_connect_zoom_fit_vert_cb) },
  { "ConnectZoom200",	        NULL,		N_("Zoom _2:1"),	NULL,			NULL,	CB(layout_menu_connect_zoom_2_1_cb) },
  { "ConnectZoom300",	        NULL,		N_("Zoom _3:1"),	NULL,			NULL,	CB(layout_menu_connect_zoom_3_1_cb) },
  { "ConnectZoom400",		NULL,		N_("Zoom _4:1"),	NULL,			NULL,	CB(layout_menu_connect_zoom_4_1_cb) },
  { "ConnectZoom50",		NULL,		N_("Zoom 1:2"),		NULL,			NULL,	CB(layout_menu_connect_zoom_1_2_cb) },
  { "ConnectZoom33",		NULL,		N_("Zoom 1:3"),		NULL,			NULL,	CB(layout_menu_connect_zoom_1_3_cb) },
  { "ConnectZoom25",		NULL,		N_("Zoom 1:4"),		NULL,			NULL,	CB(layout_menu_connect_zoom_1_4_cb) },


  { "ViewInNewWindow",	NULL,		N_("_View in new window"),	"<control>V",		NULL,	CB(layout_menu_view_in_new_window_cb) },

  { "FullScreen",	NULL,		N_("F_ull screen"),	"F",		NULL,	CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt1",	NULL,		N_("F_ull screen"),	"V",		NULL,	CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",	NULL,		N_("F_ull screen"),	"F11",		NULL,	CB(layout_menu_fullscreen_cb) },
  { "Escape",		NULL,		N_("Escape"),		"Escape",	NULL,	CB(layout_menu_escape_cb) },
  { "EscapeAlt1",	NULL,		N_("Escape"),		"Q",		NULL,	CB(layout_menu_escape_cb) },
  { "ImageOverlay",	NULL,		N_("_Image Overlay"),	"I",		NULL,	CB(layout_menu_overlay_cb) },
  { "HistogramChan",	NULL,	N_("Histogram _channels"),	"K",		NULL,	CB(layout_menu_histogram_chan_cb) },
  { "HistogramLog",	NULL,	N_("Histogram _log mode"),	"J",		NULL,	CB(layout_menu_histogram_log_cb) },
  { "HideTools",	NULL,		N_("_Hide file list"),	"<control>H",	NULL,	CB(layout_menu_hide_cb) },
  { "SlideShowPause",	NULL,		N_("_Pause slideshow"), "P",		NULL,	CB(layout_menu_slideshow_pause_cb) },
  { "Refresh",	GTK_STOCK_REFRESH,	N_("_Refresh"),		"R",		NULL,	CB(layout_menu_refresh_cb) },

  { "HelpContents",	GTK_STOCK_HELP,	N_("_Contents"),	"F1",		NULL,	CB(layout_menu_help_cb) },
  { "HelpShortcuts",	NULL,		N_("_Keyboard shortcuts"),NULL,		NULL,	CB(layout_menu_help_keys_cb) },
  { "HelpNotes",	NULL,		N_("_Release notes"),	NULL,		NULL,	CB(layout_menu_notes_cb) },
  { "About",		NULL,		N_("_About"),		NULL,		NULL,	CB(layout_menu_about_cb) },
  { "LogWindow",	NULL,		N_("_Log Window"),	NULL,		NULL,	CB(layout_menu_log_window_cb) },
  
  { "ExifWin",		NULL,		N_("E_xif window"),	"<control>E",	NULL,	CB(layout_menu_bar_exif_cb),	 FALSE  },

};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Thumbnails",	PIXBUF_INLINE_ICON_THUMB,		N_("Show _Thumbnails"),	"T",		N_("Show Thumbnails"),	CB(layout_menu_thumb_cb),	 FALSE },
  { "ShowMarks",        NULL,		N_("Show _Marks"),	"M",		NULL,	CB(layout_menu_marks_cb),	 FALSE  },
  { "FloatTools",	PIXBUF_INLINE_ICON_FLOAT,		N_("_Float file list"),	"L",		NULL,	CB(layout_menu_float_cb),	 FALSE  },
  { "HideToolbar",	NULL,		N_("Hide tool_bar"),	NULL,		NULL,	CB(layout_menu_toolbar_cb),	 FALSE  },
  { "HideInfoPixel",	NULL,		N_("Hide Pi_xel Info"),	NULL,		NULL,	CB(layout_menu_info_pixel_cb),	 FALSE  },
  { "SBar",		NULL,		N_("_Info"),		"<control>K",	NULL,	CB(layout_menu_bar_cb),		 FALSE  },
  { "SBarSort",		NULL,		N_("Sort _manager"),	"<control>S",	NULL,	CB(layout_menu_bar_sort_cb),	 FALSE  },
  { "SlideShow",	NULL,		N_("Toggle _slideshow"),"S",		NULL,	CB(layout_menu_slideshow_cb),	 FALSE  },
};

static GtkRadioActionEntry menu_radio_entries[] = {
  { "ViewList",		NULL,		N_("View Images as _List"),		"<control>L",	NULL,	0 },
  { "ViewIcons",	NULL,		N_("View Images as I_cons"),		"<control>I",	NULL,	1 }
};

static GtkRadioActionEntry menu_split_radio_entries[] = {
  { "SplitHorizontal",	NULL,		N_("Horizontal"),	"E",		NULL,	SPLIT_HOR },
  { "SplitVertical",	NULL,		N_("Vertical"),		"U",		NULL,	SPLIT_VERT },
  { "SplitQuad",	NULL,		N_("Quad"),		NULL,		NULL,	SPLIT_QUAD },
  { "SplitSingle",	NULL,		N_("Single"),		"Y",		NULL,	SPLIT_NONE }
};


#undef CB

static const gchar *menu_ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='NewWindow'/>"
"      <menuitem action='NewCollection'/>"
"      <menuitem action='OpenCollection'/>"
"      <menuitem action='OpenRecent'/>"
"      <placeholder name='OpenSection'/>"
"      <separator/>"
"      <menuitem action='Search'/>"
"      <menuitem action='FindDupes'/>"
"      <placeholder name='SearchSection'/>"
"      <separator/>"
"      <menuitem action='Print'/>"
"      <placeholder name='PrintSection'/>"
"      <separator/>"
"      <menuitem action='NewFolder'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Move'/>"
"      <menuitem action='Rename'/>"
"      <menuitem action='Delete'/>"
"      <placeholder name='FileOpsSection'/>"
"      <separator/>"
"      <placeholder name='QuitSection'/>"
"      <menuitem action='CloseWindow'/>"
"      <menuitem action='Quit'/>"
"      <separator/>"
"    </menu>"
"    <menu action='GoMenu'>"
"      <menuitem action='FirstImage'/>"
"      <menuitem action='PrevImage'/>"
"      <menuitem action='NextImage'/>"
"      <menuitem action='LastImage'/>"
"      <separator/>"
"      <menuitem action='Back'/>"
"      <menuitem action='Home'/>"
"      <separator/>"
"    </menu>"
"    <menu action='SelectMenu'>"
"      <menuitem action='SelectAll'/>"
"      <menuitem action='SelectNone'/>"
"      <menuitem action='SelectInvert'/>"
"      <placeholder name='SelectSection'/>"
"      <separator/>"
"      <menuitem action='CopyPath'/>"
"      <placeholder name='ClipboardSection'/>"
"      <separator/>"
"      <menuitem action='ShowMarks'/>"
"      <placeholder name='MarksSection'/>"
"      <separator/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menu action='ExternalMenu'>"
"      </menu>"
"      <placeholder name='EditSection'/>"
"      <separator/>"
"      <menu action='AdjustMenu'>"
"        <menuitem action='RotateCW'/>"
"        <menuitem action='RotateCCW'/>"
"        <menuitem action='Rotate180'/>"
"        <menuitem action='Mirror'/>"
"        <menuitem action='Flip'/>"
"        <menuitem action='Grayscale'/>"
"        <menuitem action='AlterNone'/>"
"      </menu>"
"      <placeholder name='PropertiesSection'/>"
"      <separator/>"
"      <menuitem action='Preferences'/>"
"      <menuitem action='Maintenance'/>"
"      <placeholder name='PreferencesSection'/>"
"      <separator/>"
"      <menuitem action='Wallpaper'/>"
"      <separator/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='ViewInNewWindow'/>"
"      <menuitem action='PanView'/>"
"      <placeholder name='WindowSection'/>"
"      <separator/>"
"      <menu action='ZoomMenu'>"
"        <menuitem action='ZoomIn'/>"
"        <menuitem action='ZoomOut'/>"
"        <menuitem action='ZoomFit'/>"
"        <menuitem action='ZoomFillHor'/>"
"        <menuitem action='ZoomFillVert'/>"
"        <menuitem action='Zoom100'/>"
"        <menuitem action='Zoom200'/>"
"        <menuitem action='Zoom300'/>"
"        <menuitem action='Zoom400'/>"
"        <menuitem action='Zoom50'/>"
"        <menuitem action='Zoom33'/>"
"        <menuitem action='Zoom25'/>"
"      </menu>"
"      <menu action='ConnectZoomMenu'>"
"        <menuitem action='ConnectZoomIn'/>"
"        <menuitem action='ConnectZoomOut'/>"
"        <menuitem action='ConnectZoomFit'/>"
"        <menuitem action='ConnectZoomFillHor'/>"
"        <menuitem action='ConnectZoomFillVert'/>"
"        <menuitem action='ConnectZoom100'/>"
"        <menuitem action='ConnectZoom200'/>"
"        <menuitem action='ConnectZoom300'/>"
"        <menuitem action='ConnectZoom400'/>"
"        <menuitem action='ConnectZoom50'/>"
"        <menuitem action='ConnectZoom33'/>"
"        <menuitem action='ConnectZoom25'/>"
"      </menu>"
"      <placeholder name='ZoomSection'/>"
"      <separator/>"
"      <menu action='SplitMenu'>"
"        <menuitem action='SplitHorizontal'/>"
"        <menuitem action='SplitVertical'/>"
"        <menuitem action='SplitQuad'/>"
"        <menuitem action='SplitSingle'/>"
"      </menu>"
"      <separator/>"
"      <menuitem action='ViewList'/>"
"      <menuitem action='ViewIcons'/>"
"      <menuitem action='Thumbnails'/>"
"      <placeholder name='ListSection'/>"
"      <separator/>"
"      <menu action='DirMenu'>"
"        <menuitem action='FolderList'/>"
"        <menuitem action='FolderTree'/>"
"      </menu>"
"      <placeholder name='DirSection'/>"
"      <separator/>"
"      <menuitem action='ImageOverlay'/>"
"      <menuitem action='HistogramChan'/>"
"      <menuitem action='HistogramLog'/>"
"      <menuitem action='FullScreen'/>"
"      <placeholder name='OverlaySection'/>"
"      <separator/>"
"      <menuitem action='FloatTools'/>"
"      <menuitem action='HideTools'/>"
"      <menuitem action='HideToolbar'/>"
"      <menuitem action='HideInfoPixel'/>"
"      <placeholder name='ToolsSection'/>"
"      <separator/>"
"      <menuitem action='SBar'/>"
"      <menuitem action='ExifWin'/>"
"      <menuitem action='SBarSort'/>"
"      <placeholder name='SideBarSection'/>"
"      <separator/>"
"      <menuitem action='SlideShow'/>"
"      <menuitem action='SlideShowPause'/>"
"      <menuitem action='Refresh'/>"
"      <placeholder name='SlideShowSection'/>"
"      <separator/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <separator/>"
"      <menuitem action='HelpContents'/>"
"      <menuitem action='HelpShortcuts'/>"
"      <menuitem action='HelpNotes'/>"
"      <placeholder name='HelpSection'/>"
"      <separator/>"
"      <menuitem action='About'/>"
"      <separator/>"
"      <menuitem action='LogWindow'/>"
"      <separator/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='ToolBar'>"
"  </toolbar>"
"<accelerator action='PrevImageAlt1'/>"
"<accelerator action='PrevImageAlt2'/>"
"<accelerator action='NextImageAlt1'/>"
"<accelerator action='NextImageAlt2'/>"
"<accelerator action='DeleteAlt1'/>"
"<accelerator action='DeleteAlt2'/>"
"<accelerator action='FullScreenAlt1'/>"
"<accelerator action='FullScreenAlt2'/>"
"<accelerator action='Escape'/>"
"<accelerator action='EscapeAlt1'/>"

"<accelerator action='ZoomInAlt1'/>"
"<accelerator action='ZoomOutAlt1'/>"
"<accelerator action='Zoom100Alt1'/>"
"<accelerator action='ZoomFitAlt1'/>"

"<accelerator action='ConnectZoomInAlt1'/>"
"<accelerator action='ConnectZoomOutAlt1'/>"
"<accelerator action='ConnectZoom100Alt1'/>"
"<accelerator action='ConnectZoomFitAlt1'/>"
"</ui>";

static gchar *menu_translate(const gchar *path, gpointer data)
{
	return (gchar *)(_(path));
}

static void layout_actions_setup_mark(LayoutWindow *lw, gint mark, gchar *name_tmpl, gchar *label_tmpl, gchar *accel_tmpl,  GCallback cb)
{
	gchar name[50];
	gchar label[100];
	gchar accel[50];
	GtkActionEntry entry = { name, NULL, label, accel, NULL, cb };
	GtkAction *action;

	g_snprintf(name, sizeof(name), name_tmpl, mark);
	g_snprintf(label, sizeof(label), label_tmpl, mark);
	if (accel_tmpl)
		g_snprintf(accel, sizeof(accel), accel_tmpl, mark % 10);
	else
		accel[0] = 0;
	gtk_action_group_add_actions(lw->action_group, &entry, 1, lw);
	action = gtk_action_group_get_action(lw->action_group, name);
	g_object_set_data(G_OBJECT(action), "mark_num", GINT_TO_POINTER(mark));
}

static void layout_actions_setup_marks(LayoutWindow *lw)
{
	gint mark;
	GError *error;
	GString *desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>"
				"    <menu action='SelectMenu'>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		layout_actions_setup_mark(lw, mark, "Mark%d", 		_("Mark _%d"), NULL, NULL);
		layout_actions_setup_mark(lw, mark, "SetMark%d", 	_("_Set mark %d"), 			NULL, G_CALLBACK(layout_menu_set_mark_sel_cb));
		layout_actions_setup_mark(lw, mark, "ResetMark%d", 	_("_Reset mark %d"), 			NULL, G_CALLBACK(layout_menu_res_mark_sel_cb));
		layout_actions_setup_mark(lw, mark, "ToggleMark%d", 	_("_Toggle mark %d"), 			"%d", G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, mark, "ToggleMark%dAlt1",	_("_Toggle mark %d"), 			"KP_%d", G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, mark, "SelectMark%d", 	_("_Select mark %d"), 			"<control>%d", G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, mark, "SelectMark%dAlt1",	_("_Select mark %d"), 			"<control>KP_%d", G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, mark, "AddMark%d", 	_("_Add mark %d"), 			NULL, G_CALLBACK(layout_menu_sel_mark_or_cb));
		layout_actions_setup_mark(lw, mark, "IntMark%d", 	_("_Intersection with mark %d"), 	NULL, G_CALLBACK(layout_menu_sel_mark_and_cb));
		layout_actions_setup_mark(lw, mark, "UnselMark%d", 	_("_Unselect mark %d"), 		NULL, G_CALLBACK(layout_menu_sel_mark_minus_cb));

		g_string_append_printf(desc,
				"      <menu action='Mark%d'>"
				"        <menuitem action='ToggleMark%d'/>"
				"        <menuitem action='SetMark%d'/>"
				"        <menuitem action='ResetMark%d'/>"
				"        <separator/>"
				"        <menuitem action='SelectMark%d'/>"
				"        <menuitem action='AddMark%d'/>"
				"        <menuitem action='IntMark%d'/>"
				"        <menuitem action='UnselMark%d'/>"
				"      </menu>",
				mark, mark, mark, mark, mark, mark, mark, mark);
		}

	g_string_append(desc,
				"    </menu>"
				"  </menubar>");
	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		g_string_append_printf(desc,
				"<accelerator action='ToggleMark%dAlt1'/>"
				"<accelerator action='SelectMark%dAlt1'/>",
				mark, mark);
		}
	g_string_append(desc,   "</ui>" );

	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
}

static GList *layout_actions_editor_menu_path(EditorDescription *editor)
{
	gchar **split = g_strsplit(editor->menu_path, "/", 0);
	gint i = 0;
	GList *ret = NULL;
	
	if (split[0] == NULL) 
		{
		g_strfreev(split);
		return NULL;
		}
	
	while (split[i])
		{
		ret = g_list_prepend(ret, g_strdup(split[i]));
		i++;
		}
	
	g_strfreev(split);
	
	ret = g_list_prepend(ret, g_strdup(editor->key));
	
	return g_list_reverse(ret);
}

static void layout_actions_editor_add(GString *desc, GList *path, GList *old_path)
{
	gint to_open, to_close, i;
	while (path && old_path && strcmp((gchar *)path->data, (gchar *)old_path->data) == 0)
		{
		path = path->next;
		old_path = old_path->next;
		}
	to_open = g_list_length(path) - 1;
	to_close = g_list_length(old_path) - 1;
	
	if (to_close > 0)
		{
		old_path = g_list_last(old_path);
		old_path = old_path->prev;
		}
	
	for (i =  0; i < to_close; i++)
		{
		gchar *name = old_path->data;
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append(desc,	"      </placeholder>");
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append(desc,	"    </menu>");
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		old_path = old_path->prev;
		}

	for (i =  0; i < to_open; i++)
		{
		gchar *name = path->data;
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append_printf(desc,	"      <placeholder name='%s'>", name);
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append_printf(desc,	"    <menu action='%s'>", name);
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		path = path->next;
		}
	
	if (path)
		g_string_append_printf(desc, "      <menuitem action='%s'/>", (gchar *)path->data);
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GError *error;
	GList *editors_list;
	GList *work;
	GList *old_path;
	GString *desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	editors_list = editor_list_get();
	
	old_path = NULL;
	work = editors_list;
	while (work)
		{
		GList *path;
		EditorDescription *editor = work->data;
		GtkActionEntry entry = { editor->key, NULL, editor->name, editor->hotkey, NULL, G_CALLBACK(layout_menu_edit_cb) };
		
		if (editor->icon && register_theme_icon_as_stock(editor->key, editor->icon))
			{
			entry.stock_id = editor->key;
			}
		gtk_action_group_add_actions(lw->action_group_external, &entry, 1, lw);
		
		path = layout_actions_editor_menu_path(editor);
		layout_actions_editor_add(desc, path, old_path);
		
		string_list_free(old_path);
		old_path = path;
		work = work->next;
		}

	layout_actions_editor_add(desc, NULL, old_path);
	string_list_free(old_path);

	g_string_append(desc,   "  </menubar>"
				"</ui>" );

	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
	g_list_free(editors_list);
}

void layout_actions_setup(LayoutWindow *lw)
{
	GError *error;

	if (lw->ui_manager) return;

	lw->action_group = gtk_action_group_new("MenuActions");
	gtk_action_group_set_translate_func(lw->action_group, menu_translate, NULL, NULL);
	lw->action_group_external = gtk_action_group_new("MenuActionsExternal");
	/* lw->action_group_external contains translated entries, no translate func is required */

	gtk_action_group_add_actions(lw->action_group,
				     menu_entries, G_N_ELEMENTS(menu_entries), lw);
	gtk_action_group_add_toggle_actions(lw->action_group,
					    menu_toggle_entries, G_N_ELEMENTS(menu_toggle_entries), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_radio_entries, G_N_ELEMENTS(menu_radio_entries),
					   0, G_CALLBACK(layout_menu_list_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_split_radio_entries, G_N_ELEMENTS(menu_split_radio_entries),
					   0, G_CALLBACK(layout_menu_split_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_view_dir_radio_entries, VIEW_DIR_TYPES_COUNT,
					   0, G_CALLBACK(layout_menu_view_dir_as_cb), lw);

	lw->ui_manager = gtk_ui_manager_new();
	gtk_ui_manager_set_add_tearoffs(lw->ui_manager, TRUE);
	gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group, 0);
	gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_external, 1);

	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string(lw->ui_manager, menu_ui_description, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	
	layout_toolbar_clear(lw);
	layout_toolbar_add_default(lw);
	
	layout_actions_setup_marks(lw);
	layout_actions_setup_editors(lw);
	layout_copy_path_update(lw);
}

void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window)
{
	GtkAccelGroup *group;

	if (!lw->ui_manager) return;

	group = gtk_ui_manager_get_accel_group(lw->ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), group);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	return gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu");
}

GtkWidget *layout_actions_toolbar(LayoutWindow *lw)
{
	GtkWidget *bar = gtk_ui_manager_get_widget(lw->ui_manager, "/ToolBar");
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(bar), GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_ICONS);
	return bar;
}

void layout_toolbar_clear(LayoutWindow *lw)
{
	if (lw->toolbar_merge_id) 
		{
		gtk_ui_manager_remove_ui(lw->ui_manager, lw->toolbar_merge_id);
		gtk_ui_manager_ensure_update(lw->ui_manager);
		}
	string_list_free(lw->toolbar_actions);
	lw->toolbar_actions = NULL;
	
	lw->toolbar_merge_id = gtk_ui_manager_new_merge_id(lw->ui_manager);
}
	

void layout_toolbar_add(LayoutWindow *lw, const gchar *action)
{
	if (!action || !lw->ui_manager) return;
	gtk_ui_manager_add_ui(lw->ui_manager, lw->toolbar_merge_id, "/ToolBar", action, action, GTK_UI_MANAGER_TOOLITEM, FALSE); 
	lw->toolbar_actions = g_list_append(lw->toolbar_actions, g_strdup(action));
}


void layout_toolbar_add_default(LayoutWindow *lw)
{
	layout_toolbar_add(lw, "Thumbnails");
	layout_toolbar_add(lw, "Back");
	layout_toolbar_add(lw, "Home");
	layout_toolbar_add(lw, "Refresh");
	layout_toolbar_add(lw, "ZoomIn");
	layout_toolbar_add(lw, "ZoomOut");
	layout_toolbar_add(lw, "ZoomFit");
	layout_toolbar_add(lw, "Zoom100");
	layout_toolbar_add(lw, "Preferences");
	layout_toolbar_add(lw, "FloatTools");
}

void layout_toolbar_write_config(LayoutWindow *lw, GString *outstr, gint indent)
{
	GList *work = lw->toolbar_actions;
	WRITE_STRING("<toolbar>\n");
	indent++;
	while (work)
		{
		gchar *action = work->data;
		work = work->next;
		WRITE_STRING("<toolitem\n");
		write_char_option(outstr, indent + 1, "action", action);
		WRITE_STRING("/>\n");
		}
	indent--;
	WRITE_STRING("</toolbar>\n");
}

void layout_toolbar_add_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *action = NULL;
	
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("action", action)) continue;

		DEBUG_1("unknown attribute %s = %s", option, value);
		}

	layout_toolbar_add(lw, action);
	g_free(action);	
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static void layout_util_sync_views(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "FolderTree");
	radio_action_set_current_value(GTK_RADIO_ACTION(action), lw->options.dir_view_type);

	action = gtk_action_group_get_action(lw->action_group, "ViewIcons");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->file_view_type);

	action = gtk_action_group_get_action(lw->action_group, "FloatTools");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.tools_float);

	action = gtk_action_group_get_action(lw->action_group, "SBar");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_enabled(lw));

	action = gtk_action_group_get_action(lw->action_group, "SBarSort");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_sort_enabled(lw));

	action = gtk_action_group_get_action(lw->action_group, "HideToolbar");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.toolbar_hidden);
	
	action = gtk_action_group_get_action(lw->action_group, "HideInfoPixel");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.info_pixel_hidden);

	action = gtk_action_group_get_action(lw->action_group, "ShowMarks");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_marks);

	action = gtk_action_group_get_action(lw->action_group, "SlideShow");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_slideshow_active(lw));

}

void layout_util_sync_thumb(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "Thumbnails");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_thumbnails);
	g_object_set(action, "sensitive", (lw->file_view_type == FILEVIEW_LIST), NULL);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
	layout_util_sync_thumb(lw);
	layout_menu_recent_update(lw);
//	layout_menu_edit_update(lw);
}

/*
 *-----------------------------------------------------------------------------
 * icons (since all the toolbar icons are included here, best place as any)
 *-----------------------------------------------------------------------------
 */

PixmapFolders *folder_icons_new(void)
{
	PixmapFolders *pf;

	pf = g_new0(PixmapFolders, 1);

	pf->close = pixbuf_inline(PIXBUF_INLINE_FOLDER_CLOSED);
	pf->open = pixbuf_inline(PIXBUF_INLINE_FOLDER_OPEN);
	pf->deny = pixbuf_inline(PIXBUF_INLINE_FOLDER_LOCKED);
	pf->parent = pixbuf_inline(PIXBUF_INLINE_FOLDER_UP);

	return pf;
}

void folder_icons_free(PixmapFolders *pf)
{
	if (!pf) return;

	g_object_unref(pf->close);
	g_object_unref(pf->open);
	g_object_unref(pf->deny);
	g_object_unref(pf->parent);

	g_free(pf);
}

/*
 *-----------------------------------------------------------------------------
 * sidebars
 *-----------------------------------------------------------------------------
 */

static gboolean layout_bar_enabled(LayoutWindow *lw)
{
	return lw->bar && GTK_WIDGET_VISIBLE(lw->bar);
}

static void layout_bar_destroyed(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;

	lw->bar = NULL;
/* 
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;
	
	if (!lw->utility_box) return;

	bar = bar_new_default(lw);
	
	layout_bar_set(lw, bar);
}

static void layout_bar_close(LayoutWindow *lw)
{
	if (lw->bar)
		{
		bar_close(lw->bar);
		lw->bar = NULL;
		}
}


void layout_bar_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_close(lw); /* if any */

	if (!bar) return;
	lw->bar = bar;

	g_signal_connect(G_OBJECT(lw->bar), "destroy",
			 G_CALLBACK(layout_bar_destroyed), lw);


//	gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->bar, FALSE, FALSE, 0);
	gtk_paned_pack2(GTK_PANED(lw->utility_paned), lw->bar, FALSE, TRUE); 

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}


void layout_bar_toggle(LayoutWindow *lw)
{
	if (layout_bar_enabled(lw))
		{
		gtk_widget_hide(lw->bar);
		}
	else
		{
		if (!lw->bar)
			{
			layout_bar_set_default(lw);
			}
		gtk_widget_show(lw->bar);
		}
	layout_util_sync_views(lw);
}

static void layout_bar_new_image(LayoutWindow *lw)
{
	if (!layout_bar_enabled(lw)) return;

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}

static void layout_bar_new_selection(LayoutWindow *lw, gint count)
{
	if (!layout_bar_enabled(lw)) return;

//	bar_info_selection(lw->bar_info, count - 1);
}

static gboolean layout_bar_sort_enabled(LayoutWindow *lw)
{
	return lw->bar_sort && GTK_WIDGET_VISIBLE(lw->bar_sort);
}


static void layout_bar_sort_destroyed(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;

	lw->bar_sort = NULL;

/* 
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_sort_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;
	
	if (!lw->utility_box) return;

	bar = bar_sort_new_default(lw);
	
	layout_bar_sort_set(lw, bar);
}

static void layout_bar_sort_close(LayoutWindow *lw)
{
	if (lw->bar_sort)
		{
		bar_sort_close(lw->bar_sort);
		lw->bar_sort = NULL;
		}
}

void layout_bar_sort_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_sort_close(lw); /* if any */

	if (!bar) return;
	lw->bar_sort = bar;

	g_signal_connect(G_OBJECT(lw->bar_sort), "destroy",
			 G_CALLBACK(layout_bar_sort_destroyed), lw);

	gtk_box_pack_end(GTK_BOX(lw->utility_box), lw->bar_sort, FALSE, FALSE, 0);
}

void layout_bar_sort_toggle(LayoutWindow *lw)
{
	if (layout_bar_sort_enabled(lw))
		{
		gtk_widget_hide(lw->bar_sort);
		}
	else
		{
		if (!lw->bar_sort)
			{
			layout_bar_sort_set_default(lw);
			}
		gtk_widget_show(lw->bar_sort);
		}
	layout_util_sync_views(lw);
}

void layout_bars_new_image(LayoutWindow *lw)
{
	layout_bar_new_image(lw);

	if (lw->exif_window) advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));

	/* this should be called here to handle the metadata edited in bars */
	if (options->metadata.confirm_on_image_change)
		metadata_write_queue_confirm(NULL, NULL);
}

void layout_bars_new_selection(LayoutWindow *lw, gint count)
{
	layout_bar_new_selection(lw, count);
}

GtkWidget *layout_bars_prepare(LayoutWindow *lw, GtkWidget *image)
{
	lw->utility_box = gtk_hbox_new(FALSE, PREF_PAD_GAP);
	lw->utility_paned = gtk_hpaned_new();
	gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->utility_paned, TRUE, TRUE, 0);

	gtk_paned_pack1(GTK_PANED(lw->utility_paned), image, TRUE, FALSE); 
	gtk_widget_show(lw->utility_paned);
	
	gtk_widget_show(image);

	return lw->utility_box;
}

void layout_bars_close(LayoutWindow *lw)
{
	layout_bar_sort_close(lw);
	layout_bar_close(lw);
}

static void layout_exif_window_destroy(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	lw->exif_window = NULL;
}

void layout_exif_window_new(LayoutWindow *lw)
{
	if (!lw->exif_window) 
		{
		lw->exif_window = advanced_exif_new();
		if (!lw->exif_window) return;
		g_signal_connect(G_OBJECT(lw->exif_window), "destroy",
				 G_CALLBACK(layout_exif_window_destroy), lw);
		advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
