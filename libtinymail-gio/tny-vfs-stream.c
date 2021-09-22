/* libtinymail-gio - The Tiny Mail base library for GnomeVFS
 * Copyright (C) 2006-2007 Philip Van Hoof <pvanhoof@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with self library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * A GnomeVFS to CamelStream mapper.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <tny-vfs-stream.h>

typedef struct _TnyVfsStreamPrivate TnyVfsStreamPrivate;

struct _TnyVfsStreamPrivate
{
	GFile *file;
	gboolean eos;
	off_t position;		/* current postion in the stream */
	off_t bound_start;	/* first valid position */
	off_t bound_end;	/* first invalid position */
};

#define TNY_VFS_STREAM_GET_PRIVATE(stream) \
	((TnyVfsStreamPrivate *)tny_vfs_stream_get_instance_private( \
						(TnyVfsStream *)(stream)))

static void
tny_stream_init (gpointer g, gpointer iface_data);

static void
tny_seekable_init (gpointer g, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(
		TnyVfsStream,
		tny_vfs_stream,
		G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TNY_TYPE_STREAM, tny_stream_init);
		G_IMPLEMENT_INTERFACE(TNY_TYPE_SEEKABLE, tny_seekable_init);
		G_ADD_PRIVATE(TnyVfsStream);
);

static void
tny_vfs_stream_set_errno (GError *error)
{
	if (!error || error->domain != G_IO_ERROR) {
		errno = EIO;
		return;
	}

	switch(error->code) {
	case G_IO_ERROR_NOT_FOUND:
	case G_IO_ERROR_HOST_NOT_FOUND:
		errno = ENOENT;
		break;
	case G_IO_ERROR_CANT_CREATE_BACKUP:
	case G_IO_ERROR_WRONG_ETAG:
	case G_IO_ERROR_FAILED:
		errno = EIO;
		break;
	case G_IO_ERROR_FAILED_HANDLED:
		errno = 0;
		break;
	case G_IO_ERROR_MESSAGE_TOO_LARGE:
		errno = EMSGSIZE;
		break;
	case G_IO_ERROR_NOT_CONNECTED:
		errno = ENOTCONN;
		break;
	case G_IO_ERROR_PROXY_FAILED:
		errno = EHOSTDOWN;
		break;
	case G_IO_ERROR_PROXY_AUTH_FAILED:
		errno = EKEYREJECTED;
		break;
	case G_IO_ERROR_PROXY_NEED_AUTH:
		errno = ENOKEY;
		break;
	case G_IO_ERROR_PROXY_NOT_ALLOWED:
		errno = EACCES;
		break;
	case G_IO_ERROR_CLOSED:
	case G_IO_ERROR_CONNECTION_CLOSED:
#if G_IO_ERROR_CONNECTION_CLOSED != G_IO_ERROR_BROKEN_PIPE
	case G_IO_ERROR_BROKEN_PIPE:
#endif
		errno = EPIPE;
		break;
	case G_IO_ERROR_CONNECTION_REFUSED:
		errno = ECONNREFUSED;
		break;
	case G_IO_ERROR_NETWORK_UNREACHABLE:
		errno = ENETUNREACH;
		break;
	case G_IO_ERROR_HOST_UNREACHABLE:
		errno = EHOSTUNREACH;
		break;
	case G_IO_ERROR_DBUS_ERROR:
		errno = ENOANO;
		break;
	case G_IO_ERROR_PARTIAL_INPUT:
		errno = ENODATA;
		break;
	case G_IO_ERROR_INVALID_DATA:
		errno = EBADMSG;
		break;
	case G_IO_ERROR_NOT_SUPPORTED:
		errno = EOPNOTSUPP;
		break;
	case G_IO_ERROR_NOT_REGULAR_FILE:
	case G_IO_ERROR_NOT_SYMBOLIC_LINK:
	case G_IO_ERROR_NOT_MOUNTABLE_FILE:
		errno = EMEDIUMTYPE;
		break;
	case G_IO_ERROR_NOT_MOUNTED:
		errno = ENOMEDIUM;
		break;
	case G_IO_ERROR_ALREADY_MOUNTED:
		errno = EBUSY;
		break;
	case G_IO_ERROR_INVALID_ARGUMENT:
	case G_IO_ERROR_INVALID_FILENAME:
		errno = EINVAL;
		break;
	case G_IO_ERROR_ADDRESS_IN_USE:
		errno = EADDRINUSE;
		break;
	case G_IO_ERROR_NO_SPACE:
		errno = ENOSPC;
		break;
	case G_IO_ERROR_READ_ONLY:
		errno = EROFS;
		break;
	case G_IO_ERROR_TOO_MANY_OPEN_FILES:
		errno = EMFILE;
		break;
	case G_IO_ERROR_NOT_DIRECTORY:
		errno = ENOTDIR;
		break;
	case G_IO_ERROR_TIMED_OUT:
		errno = ETIMEDOUT;
		break;
	case G_IO_ERROR_NOT_INITIALIZED:
		errno = EUNATCH;
		break;
	case G_IO_ERROR_CANCELLED:
		errno = ECANCELED;
		break;
	case G_IO_ERROR_WOULD_MERGE:
		errno = ENOTUNIQ;
		break;
	case G_IO_ERROR_EXISTS:
		errno = EEXIST;
		break;
	case G_IO_ERROR_WOULD_BLOCK:
		errno = EWOULDBLOCK;
		break;
	case G_IO_ERROR_WOULD_RECURSE:
		errno = ELOOP;
		break;
	case G_IO_ERROR_PERMISSION_DENIED:
		errno = EPERM;
		break;
	case G_IO_ERROR_IS_DIRECTORY:
		errno = EISDIR;
		break;
	case G_IO_ERROR_NOT_EMPTY:
		errno = ENOTEMPTY;
		break;
	case G_IO_ERROR_PENDING:
	case G_IO_ERROR_BUSY:
		errno = EBUSY;
		break;
	case G_IO_ERROR_TOO_MANY_LINKS:
		errno = EMLINK;
		break;
	case G_IO_ERROR_FILENAME_TOO_LONG:
		errno = ENAMETOOLONG;
		break;
	}
}

static gssize
tny_vfs_stream_write_to_stream (TnyStream *self, TnyStream *output)
{
	char tmp_buf[4096];
	gssize total = 0;
	gssize nb_read;
	gssize nb_written;

	g_assert (TNY_IS_STREAM (output));

	while (G_UNLIKELY (!tny_stream_is_eos (self))) {
		nb_read = tny_stream_read (self, tmp_buf, sizeof (tmp_buf));

		if (G_UNLIKELY (nb_read < 0))
			return -1;

		else if (G_LIKELY (nb_read > 0)) {
			nb_written = 0;
	
			while (G_LIKELY (nb_written < nb_read))
			{
				gssize len = tny_stream_write (output, tmp_buf + nb_written,
								  nb_read - nb_written);
				if (G_UNLIKELY (len < 0))
					return -1;

				nb_written += len;
			}
			total += nb_written;
		}
	}

	return total;
}

static gssize
tny_vfs_stream_read  (TnyStream *self, char *buffer, gsize n)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize nread = 0;
	GError *error = NULL;

	if (priv->bound_end != (~0))
		n = MIN (priv->bound_end - priv->position, n);

	nread = g_input_stream_read ((GInputStream*)priv->file, buffer, n,
				     NULL, &error);

	if (nread > 0)
		priv->position += nread;
	else {
		if (nread < 0)
			nread = -1;
		else
			priv->eos = TRUE;

		tny_vfs_stream_set_errno (error);
	}

	return nread;
}

static gssize
tny_vfs_stream_write (TnyStream *self, const char *buffer, gsize n)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize nwritten = 0;
	GError *error = NULL;

	if (priv->bound_end != (~0))
		n = MIN (priv->bound_end - priv->position, n);

	nwritten = g_output_stream_write ((GOutputStream *)priv->file, buffer,
					  n, NULL, &error);

	if (nwritten > 0) {
		priv->position += nwritten;
	} else {
		if (nwritten < 0)
			nwritten = -1;
		else
			priv->eos = TRUE;

		tny_vfs_stream_set_errno (error);
	}

	return nwritten;
}

static gint
tny_vfs_stream_close (TnyStream *self)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	GError *error = NULL;
	gboolean success;

	if (priv->file == NULL)  {
		errno = EINVAL;
		return -1;
	}

	success = g_io_stream_close ((GIOStream *)priv->file, NULL, &error);

	priv->file = NULL;
	priv->eos = TRUE;

	if (success)
		return 0;

	tny_vfs_stream_set_errno (error);

	return -1;
}


/**
 * tny_vfs_stream_set_file:
 * @self: A #TnyVfsStream instance
 * @file: The #GFile to write to or read from
 *
 * Set the #GFile to play adaptor for
 *
 **/
void
tny_vfs_stream_set_file (TnyVfsStream *self, GFile *file)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	if (priv->file) {
		g_io_stream_close ((GIOStream *)priv->file, NULL, NULL);
		priv->file = NULL;
	}

	if (!file)
		return;

	priv->file = file;
	priv->eos = FALSE;
	priv->position = 0;
	g_seekable_seek ((GSeekable *)file, priv->position, G_SEEK_SET, NULL,
			 NULL);
}

/**
 * tny_vfs_stream_new:
 * @file: The #GFile to write to or read from
 *
 * Create an adaptor instance between #TnyStream and #GFile
 *
 * Return value: a new #TnyStream instance
 **/
TnyStream*
tny_vfs_stream_new (GFile *file)
{
	TnyVfsStream *self = g_object_new (TNY_TYPE_VFS_STREAM, NULL);

	tny_vfs_stream_set_file (self, file);

	return TNY_STREAM (self);
}

static void
tny_vfs_stream_init (TnyVfsStream *self)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	priv->eos = FALSE;
	priv->file = NULL;
	priv->bound_start = 0;
	priv->bound_end = (~0);
	priv->position = 0;
}

static void
tny_vfs_stream_finalize (GObject *object)
{
	TnyVfsStream *self = (TnyVfsStream *)object;	
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	if (G_LIKELY (priv->file))
		g_io_stream_close ((GIOStream *)priv->file, NULL, NULL);

	G_OBJECT_CLASS(tny_vfs_stream_parent_class)->finalize (object);

	return;
}

static gint 
tny_vfs_flush (TnyStream *self)
{
	return 0;
}

static gboolean 
tny_vfs_is_eos (TnyStream *self)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	return priv->eos;
}

static gint 
tny_vfs_reset (TnyStream *self)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gint retval = 0;
	GError *error = NULL;

	if (priv->file == NULL) {
		errno = EINVAL;
		return -1;
	}

	g_seekable_seek ((GSeekable *)priv->file, 0, G_SEEK_SET, NULL,
			 &error);

	if (error == NULL) {
		priv->position = 0;
	} else {
		tny_vfs_stream_set_errno (error);
		retval = -1;
	}
	priv->eos = FALSE;

	return retval;
}



static off_t 
tny_vfs_seek (TnySeekable *self, off_t offset, int policy)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize real = 0;
	GError *error = NULL;
	GFile *file = priv->file;

	switch (policy) {
	case SEEK_SET:
		real = offset;
		break;
	case SEEK_CUR:
		real = priv->position + offset;
		break;
	case SEEK_END:
		if (priv->bound_end == (~0)) {
			g_seekable_seek ((GSeekable *)file, offset,
					 G_SEEK_END, NULL, &error);

			if (error != NULL) {
				tny_vfs_stream_set_errno (error);
				return -1;
			}

			real = g_seekable_tell ((GSeekable *)file);

			if (real != -1) {
				if (real<priv->bound_start)
					real = priv->bound_start;

				priv->position = real;
			}

			return real;
		}

		real = priv->bound_end + offset;
		break;
	}

	if (priv->bound_end != (~0))
		real = MIN (real, priv->bound_end);

	real = MAX (real, priv->bound_start);

	g_seekable_seek ((GSeekable *)file, real, G_SEEK_SET, NULL, &error);

	if (error != NULL) {
		tny_vfs_stream_set_errno (error);
		return -1;
	}

	if (real != priv->position && priv->eos)
		priv->eos = FALSE;

	priv->position = real;

	return real;
}

static off_t
tny_vfs_tell (TnySeekable *self)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	return priv->position;
}

static gint 
tny_vfs_set_bounds (TnySeekable *self, off_t start, off_t end)
{
	TnyVfsStreamPrivate *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	priv->bound_end = end;
	priv->bound_start = start;

	return 0;
}

static void
tny_stream_init (gpointer g, gpointer iface_data)
{
	TnyStreamIface *iface = (TnyStreamIface *)g;

	iface->reset= tny_vfs_reset;
	iface->flush= tny_vfs_flush;
	iface->is_eos= tny_vfs_is_eos;
	iface->read= tny_vfs_stream_read;
	iface->write= tny_vfs_stream_write;
	iface->close= tny_vfs_stream_close;
	iface->write_to_stream= tny_vfs_stream_write_to_stream;
}


static void
tny_seekable_init (gpointer g, gpointer iface_data)
{
	TnySeekableIface *klass = (TnySeekableIface *)g;

	klass->seek= tny_vfs_seek;
	klass->tell= tny_vfs_tell;
	klass->set_bounds= tny_vfs_set_bounds;
}

static void 
tny_vfs_stream_class_init (TnyVfsStreamClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = tny_vfs_stream_finalize;
}
