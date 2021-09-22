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

static GObjectClass *parent_class = NULL;

typedef struct _TnyVfsStreamPriv TnyVfsStreamPriv;

struct _TnyVfsStreamPriv
{
	GFile *handle;
	gboolean eos;
	off_t position;		/* current postion in the stream */
	off_t bound_start;	/* first valid position */
	off_t bound_end;	/* first invalid position */
};

#define TNY_VFS_STREAM_GET_PRIVATE(o) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), TNY_TYPE_VFS_STREAM, TnyVfsStreamPriv))

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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize nread = 0;
	GError *error = NULL;

	if (priv->bound_end != (~0))
		n = MIN (priv->bound_end - priv->position, n);

	nread = g_input_stream_read ((GInputStream*)priv->handle, buffer, n,
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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize nwritten = 0;
	GError *error = NULL;

	if (priv->bound_end != (~0))
		n = MIN (priv->bound_end - priv->position, n);

	nwritten = g_output_stream_write ((GOutputStream *)priv->handle, buffer,
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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	GError *error = NULL;
	gboolean success;

	if (priv->handle == NULL)  {
		errno = EINVAL;
		return -1;
	}

	success = g_io_stream_close ((GIOStream *)priv->handle, NULL, &error);

	priv->handle = NULL;
	priv->eos = TRUE;

	if (success)
		return 0;

	tny_vfs_stream_set_errno (error);

	return -1;
}


/**
 * tny_vfs_stream_set_handle:
 * @self: A #TnyVfsStream instance
 * @handle: The #GFile to write to or read from
 *
 * Set the #GFile to play adaptor for
 *
 **/
void
tny_vfs_stream_set_handle (TnyVfsStream *self, GFile *handle)
{
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	if (priv->handle) {
		g_io_stream_close ((GIOStream *)priv->handle, NULL, NULL);
		priv->handle = NULL;
	}

	if (!handle)
		return;

	priv->handle = handle;
	priv->eos = FALSE;
	priv->position = 0;
	g_seekable_seek ((GSeekable *)handle, priv->position, G_SEEK_SET,
			 NULL, NULL);

	return;
}

/**
 * tny_vfs_stream_new:
 * @handle: The #GFile to write to or read from
 *
 * Create an adaptor instance between #TnyStream and #GFile
 *
 * Return value: a new #TnyStream instance
 **/
TnyStream*
tny_vfs_stream_new (GFile *handle)
{
	TnyVfsStream *self = g_object_new (TNY_TYPE_VFS_STREAM, NULL);

	tny_vfs_stream_set_handle (self, handle);

	return TNY_STREAM (self);
}

static void
tny_vfs_stream_instance_init (GTypeInstance *instance, gpointer g_class)
{
	TnyVfsStream *self = (TnyVfsStream *)instance;
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	priv->eos = FALSE;
	priv->handle = NULL;
	priv->bound_start = 0;
	priv->bound_end = (~0);
	priv->position = 0;

	return;
}

static void
tny_vfs_stream_finalize (GObject *object)
{
	TnyVfsStream *self = (TnyVfsStream *)object;	
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	if (G_LIKELY (priv->handle))
		g_io_stream_close ((GIOStream *)priv->handle, NULL, NULL);

	(*parent_class->finalize) (object);

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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);

	return priv->eos;
}

static gint 
tny_vfs_reset (TnyStream *self)
{
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gint retval = 0;
	GError *error = NULL;

	if (priv->handle == NULL) 
	{
		errno = EINVAL;
		return -1;
	}

	g_seekable_seek ((GSeekable *)priv->handle, 0, G_SEEK_SET, NULL,
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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	gssize real = 0;
	GError *error = NULL;
	GFile *handle = priv->handle;

	switch (policy) {
	case SEEK_SET:
		real = offset;
		break;
	case SEEK_CUR:
		real = priv->position + offset;
		break;
	case SEEK_END:
		if (priv->bound_end == (~0)) {
			g_seekable_seek ((GSeekable *)handle, offset,
					 G_SEEK_END, NULL, &error);
			if (error != NULL) {
				tny_vfs_stream_set_errno (error);
				return -1;
			}
			real = g_seekable_tell ((GSeekable *)handle);
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

	g_seekable_seek ((GSeekable *)handle, real, G_SEEK_SET, NULL, &error);
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
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	return priv->position;
}

static gint 
tny_vfs_set_bounds (TnySeekable *self, off_t start, off_t end)
{
	TnyVfsStreamPriv *priv = TNY_VFS_STREAM_GET_PRIVATE (self);
	priv->bound_end = end;
	priv->bound_start = start;
	return 0;
}

static void
tny_stream_init (gpointer g, gpointer iface_data)
{
	TnyStreamIface *klass = (TnyStreamIface *)g;

	klass->reset= tny_vfs_reset;
	klass->flush= tny_vfs_flush;
	klass->is_eos= tny_vfs_is_eos;
	klass->read= tny_vfs_stream_read;
	klass->write= tny_vfs_stream_write;
	klass->close= tny_vfs_stream_close;
	klass->write_to_stream= tny_vfs_stream_write_to_stream;

	return;
}


static void
tny_seekable_init (gpointer g, gpointer iface_data)
{
	TnySeekableIface *klass = (TnySeekableIface *)g;

	klass->seek= tny_vfs_seek;
	klass->tell= tny_vfs_tell;
	klass->set_bounds= tny_vfs_set_bounds;

	return;
}

static void 
tny_vfs_stream_class_init (TnyVfsStreamClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	object_class = (GObjectClass*) class;

	object_class->finalize = tny_vfs_stream_finalize;

	g_type_class_add_private (object_class, sizeof (TnyVfsStreamPriv));

	return;
}

static gpointer 
tny_vfs_stream_register_type (gpointer notused)
{
	GType type = 0;

	static const GTypeInfo info = 
		{
			sizeof (TnyVfsStreamClass),
			NULL,   /* base_init */
			NULL,   /* base_finalize */
			(GClassInitFunc) tny_vfs_stream_class_init,   /* class_init */
			NULL,   /* class_finalize */
			NULL,   /* class_data */
			sizeof (TnyVfsStream),
			0,      /* n_preallocs */
			tny_vfs_stream_instance_init,/* instance_init */
			NULL
		};
	
	static const GInterfaceInfo tny_stream_info = 
		{
			(GInterfaceInitFunc) tny_stream_init, /* interface_init */
			NULL,         /* interface_finalize */
			NULL          /* interface_data */
		};
	
	static const GInterfaceInfo tny_seekable_info = 
		{
			(GInterfaceInitFunc) tny_seekable_init, /* interface_init */
			NULL,         /* interface_finalize */
			NULL          /* interface_data */
		};
	
	type = g_type_register_static (G_TYPE_OBJECT,
				       "TnyVfsStream",
				       &info, 0);
	
	g_type_add_interface_static (type, TNY_TYPE_STREAM, 
				     &tny_stream_info);
	
	g_type_add_interface_static (type, TNY_TYPE_SEEKABLE, 
				     &tny_seekable_info);
	
	return GSIZE_TO_POINTER (type);
}

GType 
tny_vfs_stream_get_type (void)
{
	static GOnce once = G_ONCE_INIT;
	g_once (&once, tny_vfs_stream_register_type, NULL);
	return GPOINTER_TO_SIZE (once.retval);
}
