/*
 * Geeqie
 * (C) 2004 John Ellis
 * Copyright (C) 2008 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#include "main.h"
#include "image-load.h"

#include "exif.h"
#include "filedata.h"
#include "ui_fileops.h"

#include <fcntl.h>
#include <sys/mman.h>


static void image_loader_sync_pixbuf(ImageLoader *il)
{
	GdkPixbuf *pb;

	if (!il->loader) return;

	pb = gdk_pixbuf_loader_get_pixbuf(il->loader);

	if (pb == il->pixbuf) return;

	if (il->pixbuf) gdk_pixbuf_unref(il->pixbuf);
	il->pixbuf = pb;
	if (il->pixbuf) gdk_pixbuf_ref(il->pixbuf);
}

static void image_loader_area_updated_cb(GdkPixbufLoader *loader,
				 guint x, guint y, guint w, guint h,
				 gpointer data)
{
	ImageLoader *il = data;

	if (il->func_area_ready)
		{
		if (!il->pixbuf)
			{
			image_loader_sync_pixbuf(il);
			if (!il->pixbuf)
				{
				log_printf("critical: area_ready signal with NULL pixbuf (out of mem?)\n");
				}
			}
		il->func_area_ready(il, x, y, w, h, il->data_area_ready);
		}
}

static void image_loader_area_prepared_cb(GdkPixbufLoader *loader, gpointer data)
{
	GdkPixbuf *pb;
	guchar *pix;
	size_t h, rs;
	
	/* a workaround for http://bugzilla.gnome.org/show_bug.cgi?id=547669 */
	gchar *format = gdk_pixbuf_format_get_name(gdk_pixbuf_loader_get_format(loader));
	if (strcmp(format, "svg") == 0)
		{
		g_free(format);
		return;
		}
	
	g_free(format);

	pb = gdk_pixbuf_loader_get_pixbuf(loader);
	
	h = gdk_pixbuf_get_height(pb);
	rs = gdk_pixbuf_get_rowstride(pb);
	pix = gdk_pixbuf_get_pixels(pb);
	
	memset(pix, 0, rs * h); /*this should be faster than pixbuf_fill */

}

static void image_loader_size_cb(GdkPixbufLoader *loader,
				 gint width, gint height, gpointer data)
{
	ImageLoader *il = data;
	GdkPixbufFormat *format;
	gchar **mime_types;
	gint scale = FALSE;
	gint n;

	if (il->requested_width < 1 || il->requested_height < 1) return;

	format = gdk_pixbuf_loader_get_format(loader);
	if (!format) return;

	mime_types = gdk_pixbuf_format_get_mime_types(format);
	n = 0;
	while (mime_types[n])
		{
		if (strstr(mime_types[n], "jpeg")) scale = TRUE;
		n++;
		}
	g_strfreev(mime_types);

	if (!scale) return;

	if (width > il->requested_width || height > il->requested_height)
		{
		gint nw, nh;

		if (((gdouble)il->requested_width / width) < ((gdouble)il->requested_height / height))
			{
			nw = il->requested_width;
			nh = (gdouble)nw / width * height;
			if (nh < 1) nh = 1;
			}
		else
			{
			nh = il->requested_height;
			nw = (gdouble)nh / height * width;
			if (nw < 1) nw = 1;
			}

		gdk_pixbuf_loader_set_size(loader, nw, nh);
		il->shrunk = TRUE;
		}
}

static void image_loader_stop(ImageLoader *il)
{
	if (!il) return;

	if (il->idle_id != -1)
		{
		g_source_remove(il->idle_id);
		il->idle_id = -1;
		}

	if (il->loader)
		{
		/* some loaders do not have a pixbuf till close, order is important here */
		gdk_pixbuf_loader_close(il->loader, NULL);
		image_loader_sync_pixbuf(il);
		g_object_unref(G_OBJECT(il->loader));
		il->loader = NULL;
		}

	if (il->mapped_file)
		{
		if (il->preview)
			{
			exif_free_preview(il->mapped_file);
			}
		else
			{
			munmap(il->mapped_file, il->bytes_total);
			}
		il->mapped_file = NULL;
		}

	il->done = TRUE;
}

static void image_loader_done(ImageLoader *il)
{
	image_loader_stop(il);

	if (il->func_done) il->func_done(il, il->data_done);
}

static gint image_loader_done_delay_cb(gpointer data)
{
	ImageLoader *il = data;

	il->idle_done_id = -1;
	image_loader_done(il);
	return FALSE;
}

static void image_loader_done_delay(ImageLoader *il)
{
	if (il->idle_done_id == -1) il->idle_done_id = g_idle_add_full(il->idle_priority,
								       image_loader_done_delay_cb, il, NULL);
}

static void image_loader_error(ImageLoader *il)
{
	image_loader_stop(il);

	DEBUG_1("pixbuf_loader reported load error for: %s", il->fd->path);

	if (il->func_error) il->func_error(il, il->data_error);
}

static gint image_loader_idle_cb(gpointer data)
{
	ImageLoader *il = data;
	gint b;
	gint c;

	if (!il) return FALSE;

	if (il->idle_id == -1) return FALSE;

	c = il->idle_read_loop_count ? il->idle_read_loop_count : 1;
	while (c > 0)
		{
		b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);

		if (b == 0)
			{
			image_loader_done(il);
			return FALSE;
			}

		if (b < 0 || (b > 0 && !gdk_pixbuf_loader_write(il->loader, il->mapped_file + il->bytes_read, b, NULL)))
			{
			image_loader_error(il);
			return FALSE;
			}

		il->bytes_read += b;

		c--;
		}

	if (il->func_percent && il->bytes_total > 0)
		{
		il->func_percent(il, (gdouble)il->bytes_read / il->bytes_total, il->data_percent);
		}

	return TRUE;
}

static gint image_loader_begin(ImageLoader *il)
{
	gint b;

	if (!il->loader || il->pixbuf) return FALSE;

	b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);

	if (b < 1)
		{
		image_loader_stop(il);
		return FALSE;
		}

	if (!gdk_pixbuf_loader_write(il->loader, il->mapped_file + il->bytes_read, b, NULL))
		{
		image_loader_stop(il);
		return FALSE;
		}

	il->bytes_read += b;

	/* read until size is known */
	while (il->loader && !gdk_pixbuf_loader_get_pixbuf(il->loader) && b > 0)
		{
		b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);
		if (b < 0 || (b > 0 && !gdk_pixbuf_loader_write(il->loader, il->mapped_file + il->bytes_read, b, NULL)))
			{
			image_loader_stop(il);
			return FALSE;
			}
		il->bytes_read += b;
		}
	if (!il->pixbuf) image_loader_sync_pixbuf(il);

	if (il->bytes_read == il->bytes_total || b < 1)
		{
		/* done, handle (broken) loaders that do not have pixbuf till close */
		image_loader_stop(il);

		if (!il->pixbuf) return FALSE;

		image_loader_done_delay(il);
		return TRUE;
		}

	if (!il->pixbuf)
		{
		image_loader_stop(il);
		return FALSE;
		}

	/* finally, progressive loading :) */
	il->idle_id = g_idle_add_full(il->idle_priority, image_loader_idle_cb, il, NULL);

	return TRUE;
}

static gint image_loader_setup(ImageLoader *il)
{
	struct stat st;
	gchar *pathl;

	if (!il || il->loader || il->mapped_file) return FALSE;

	il->mapped_file = NULL;

	if (il->fd)
		{
		ExifData *exif = exif_read_fd(il->fd);

		il->mapped_file = exif_get_preview(exif, &il->bytes_total);
		
		if (il->mapped_file)
			{
			il->preview = TRUE;
			DEBUG_1("Raw file %s contains embedded image", il->fd->path);
			}
		exif_free_fd(il->fd, exif);
		}

	
	if (!il->mapped_file)
		{
		/* normal file */
		gint load_fd;
	
		pathl = path_from_utf8(il->fd->path);
		load_fd = open(pathl, O_RDONLY | O_NONBLOCK);
		g_free(pathl);
		if (load_fd == -1) return FALSE;

		if (fstat(load_fd, &st) == 0)
			{
			il->bytes_total = st.st_size;
			}
		else
			{
			close(load_fd);
			return FALSE;
			}
		
		il->mapped_file = mmap(0, il->bytes_total, PROT_READ|PROT_WRITE, MAP_PRIVATE, load_fd, 0);
		close(load_fd);
		if (il->mapped_file == MAP_FAILED)
			{
			il->mapped_file = 0;
			return FALSE;
			}
		il->preview = FALSE;
		}


	il->loader = gdk_pixbuf_loader_new();
	g_signal_connect(G_OBJECT(il->loader), "area_updated",
			 G_CALLBACK(image_loader_area_updated_cb), il);
	g_signal_connect(G_OBJECT(il->loader), "size_prepared",
			 G_CALLBACK(image_loader_size_cb), il);
	g_signal_connect(G_OBJECT(il->loader), "area_prepared",
			 G_CALLBACK(image_loader_area_prepared_cb), il);

	il->shrunk = FALSE;

	return image_loader_begin(il);
}

ImageLoader *image_loader_new(FileData *fd)
{
	ImageLoader *il;

	if (!fd) return NULL;

	il = g_new0(ImageLoader, 1);
	il->fd = file_data_ref(fd);
	il->pixbuf = NULL;
	il->idle_id = -1;
	il->idle_priority = G_PRIORITY_DEFAULT_IDLE;
	il->done = FALSE;
	il->loader = NULL;

	il->bytes_read = 0;
	il->bytes_total = 0;

	il->idle_done_id = -1;

	il->idle_read_loop_count = options->image.idle_read_loop_count;
	il->read_buffer_size = options->image.read_buffer_size;
	il->mapped_file = NULL;

	il->requested_width = 0;
	il->requested_height = 0;
	il->shrunk = FALSE;
	DEBUG_1("new image loader %p, bufsize=%u idle_loop=%u", il, il->read_buffer_size, il->idle_read_loop_count);
	return il;
}

void image_loader_free(ImageLoader *il)
{
	if (!il) return;

	image_loader_stop(il);
	if (il->idle_done_id != -1) g_source_remove(il->idle_done_id);
	if (il->pixbuf) gdk_pixbuf_unref(il->pixbuf);
	file_data_unref(il->fd);
	DEBUG_1("freeing image loader %p bytes_read=%d", il, il->bytes_read);
	g_free(il);
}

/* don't forget to gdk_pixbuf_ref() it if you want to use it after image_loader_free() */
GdkPixbuf *image_loader_get_pixbuf(ImageLoader *il)
{
	if (!il) return NULL;

	return il->pixbuf;
}

gchar *image_loader_get_format(ImageLoader *il)
{
	GdkPixbufFormat *format;
	gchar **mimev;
	gchar *mime;

	if (!il || !il->loader) return NULL;

	format = gdk_pixbuf_loader_get_format(il->loader);
	if (!format) return NULL;

	mimev = gdk_pixbuf_format_get_mime_types(format);
	if (!mimev) return NULL;

	/* return first member of mimev, as GdkPixbufLoader has no way to tell us which exact one ? */
	mime = g_strdup(mimev[0]);
	g_strfreev(mimev);

	return mime;
}

void image_loader_set_area_ready_func(ImageLoader *il,
				      void (*func_area_ready)(ImageLoader *, guint, guint, guint, guint, gpointer),
				      gpointer data_area_ready)
{
	if (!il) return;

	il->func_area_ready = func_area_ready;
	il->data_area_ready = data_area_ready;
}

void image_loader_set_error_func(ImageLoader *il,
				 void (*func_error)(ImageLoader *, gpointer),
				 gpointer data_error)
{
	if (!il) return;

	il->func_error = func_error;
	il->data_error = data_error;
}

void image_loader_set_done_func(ImageLoader *il,
				void (*func_done)(ImageLoader *, gpointer),
				gpointer data_done)
{
	if (!il) return;

	il->func_done = func_done;
	il->data_done = data_done;
}

void image_loader_set_percent_func(ImageLoader *il,
				   void (*func_percent)(ImageLoader *, gdouble, gpointer),
				   gpointer data_percent)
{
	if (!il) return;

	il->func_percent = func_percent;
	il->data_percent = data_percent;
}

void image_loader_set_requested_size(ImageLoader *il, gint width, gint height)
{
	if (!il) return;

	il->requested_width = width;
	il->requested_height = height;
}

void image_loader_set_buffer_size(ImageLoader *il, guint count)
{
	if (!il) return;

	il->idle_read_loop_count = count ? count : 1;
}

void image_loader_set_priority(ImageLoader *il, gint priority)
{
	if (!il) return;

	il->idle_priority = priority;
}

gint image_loader_start(ImageLoader *il, void (*func_done)(ImageLoader *, gpointer), gpointer data_done)
{
	if (!il) return FALSE;

	if (!il->fd) return FALSE;

	image_loader_set_done_func(il, func_done, data_done);

	return image_loader_setup(il);
}

gdouble image_loader_get_percent(ImageLoader *il)
{
	if (!il || il->bytes_total == 0) return 0.0;

	return (gdouble)il->bytes_read / il->bytes_total;
}

gint image_loader_get_is_done(ImageLoader *il)
{
	if (!il) return FALSE;

	return il->done;
}

FileData *image_loader_get_fd(ImageLoader *il)
{
	if (!il) return NULL;

	return il->fd;
	
}

gint image_loader_get_shrunk(ImageLoader *il)
{
	if (!il) return FALSE;

	return il->shrunk;
	
}


gint image_load_dimensions(FileData *fd, gint *width, gint *height)
{
	ImageLoader *il;
	gint success;

	il = image_loader_new(fd);

	success = image_loader_start(il, NULL, NULL);

	if (success && il->pixbuf)
		{
		if (width) *width = gdk_pixbuf_get_width(il->pixbuf);
		if (height) *height = gdk_pixbuf_get_height(il->pixbuf);;
		}
	else
		{
		if (width) *width = -1;
		if (height) *height = -1;
		}

	image_loader_free(il);

	return success;
}
