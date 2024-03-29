/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-fs.c : file system based stream
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 1999-2003 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-seekable-substream.h"

static CamelSeekableStreamClass *parent_class = NULL;

/* Returns the class for a CamelSeekableSubStream */
#define CSS_CLASS(so) CAMEL_SEEKABLE_SUBSTREAM_CLASS (CAMEL_OBJECT(so)->klass)

static	ssize_t	 stream_read  (CamelStream *stream, char *buffer, size_t n);
static	ssize_t	 stream_write (CamelStream *stream, const char *buffer, size_t n);
static	int	 stream_flush (CamelStream *stream);
static	int	 stream_close (CamelStream *stream);
static	gboolean eos	      (CamelStream *stream);
static	off_t	 stream_seek  (CamelSeekableStream *stream, off_t offset,
			       CamelStreamSeekPolicy policy);

static void
camel_lite_seekable_substream_class_init (CamelSeekableSubstreamClass *camel_lite_seekable_substream_class)
{
	CamelSeekableStreamClass *camel_lite_seekable_stream_class =
		CAMEL_SEEKABLE_STREAM_CLASS (camel_lite_seekable_substream_class);
	CamelStreamClass *camel_lite_stream_class =
		CAMEL_STREAM_CLASS (camel_lite_seekable_substream_class);

	parent_class = CAMEL_SEEKABLE_STREAM_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_seekable_stream_get_type ()));

	/* virtual method definition */

	/* virtual method overload */
	camel_lite_stream_class->read = stream_read;
	camel_lite_stream_class->write = stream_write;
	camel_lite_stream_class->flush = stream_flush;
	camel_lite_stream_class->close = stream_close;
	camel_lite_stream_class->eos = eos;

	camel_lite_seekable_stream_class->seek = stream_seek;

}

static void
camel_lite_seekable_substream_finalize (CamelObject *object)
{
	CamelSeekableSubstream *seekable_substream =
		CAMEL_SEEKABLE_SUBSTREAM (object);

	if (seekable_substream->parent_stream)
		camel_lite_object_unref (seekable_substream->parent_stream);
}

static void
camel_lite_seekable_substream_init (gpointer object, gpointer klass)
{
	CamelSeekableSubstream *seekable_substream = (CamelSeekableSubstream *) object;
	seekable_substream->parent_stream = NULL;
	return;
}

CamelType
camel_lite_seekable_substream_get_type (void)
{
	static CamelType camel_lite_seekable_substream_type = CAMEL_INVALID_TYPE;

	if (camel_lite_seekable_substream_type == CAMEL_INVALID_TYPE) {
		camel_lite_seekable_substream_type = camel_lite_type_register (camel_lite_seekable_stream_get_type (), "CamelLiteSeekableSubstream",
								     sizeof (CamelSeekableSubstream),
								     sizeof (CamelSeekableSubstreamClass),
								     (CamelObjectClassInitFunc) camel_lite_seekable_substream_class_init,
								     NULL,
								     (CamelObjectInitFunc) camel_lite_seekable_substream_init,
								     (CamelObjectFinalizeFunc) camel_lite_seekable_substream_finalize);
	}

	return camel_lite_seekable_substream_type;
}

/**
 * camel_lite_seekable_substream_new:
 * @parent_stream: a #CamelSeekableStream object
 * @inf_bound: a lower bound
 * @sup_bound: an upper bound
 *
 * Creates a new CamelSeekableSubstream that references the portion
 * of @parent_stream from @inf_bound to @sup_bound. (If @sup_bound is
 * #CAMEL_STREAM_UNBOUND, it references to the end of stream, even if
 * the stream grows.)
 *
 * While the substream is open, the caller cannot assume anything about
 * the current position of @parent_stream. After the substream has been
 * closed, @parent_stream will stabilize again.
 *
 * Return value: the substream
 **/
CamelStream *
camel_lite_seekable_substream_new(CamelSeekableStream *parent_stream, off_t start, off_t end)
{
	CamelSeekableSubstream *seekable_substream;

	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (parent_stream), NULL);

	/* Create the seekable substream. */
	seekable_substream = CAMEL_SEEKABLE_SUBSTREAM (camel_lite_object_new (camel_lite_seekable_substream_get_type ()));

	/* Initialize it. */
	seekable_substream->parent_stream = parent_stream;
	camel_lite_object_ref (parent_stream);

	/* Set the bound of the substream. We can ignore any possible error
	 * here, because if we fail to seek now, it will try again later.
	 */
	camel_lite_seekable_stream_set_bounds ((CamelSeekableStream *)seekable_substream, start, end);

	return CAMEL_STREAM (seekable_substream);
}

static gboolean
parent_reset (CamelSeekableSubstream *seekable_substream, CamelSeekableStream *parent)
{
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (seekable_substream);

	if (camel_lite_seekable_stream_tell (parent) == seekable_stream->position)
		return TRUE;

	return camel_lite_seekable_stream_seek (parent, (off_t) seekable_stream->position, CAMEL_STREAM_SET) == seekable_stream->position;
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelSeekableStream *parent;
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (stream);
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM (stream);
	ssize_t v;

	if (n == 0)
		return 0;

	parent = seekable_substream->parent_stream;

	/* Go to our position in the parent stream. */
	if (!parent_reset (seekable_substream, parent)) {
		stream->eos = TRUE;
		return 0;
	}

	/* Compute how many bytes should be read. */
	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable_stream->bound_end -  seekable_stream->position, n);

	if (n == 0) {
		stream->eos = TRUE;
		return 0;
	}

	v = camel_lite_stream_read (CAMEL_STREAM (parent), buffer, n);

	/* ignore <0 - it's an error, let the caller deal */
	if (v > 0)
		seekable_stream->position += v;

	return v;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelSeekableStream *parent;
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM(stream);
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM(stream);
	ssize_t v;

	if (n == 0)
		return 0;

	parent = seekable_substream->parent_stream;

	/* Go to our position in the parent stream. */
	if (!parent_reset (seekable_substream, parent)) {
		stream->eos = TRUE;
		return 0;
	}

	/* Compute how many bytes should be written. */
	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable_stream->bound_end -  seekable_stream->position, n);

	if (n == 0) {
		stream->eos = TRUE;
		return 0;
	}

	v = camel_lite_stream_write((CamelStream *)parent, buffer, n);

	/* ignore <0 - it's an error, let the caller deal */
	if (v > 0)
		seekable_stream->position += v;

	return v;

}

static int
stream_flush (CamelStream *stream)
{
	CamelSeekableSubstream *sus = (CamelSeekableSubstream *)stream;

	return camel_lite_stream_flush(CAMEL_STREAM(sus->parent_stream));
}

static int
stream_close (CamelStream *stream)
{
	/* we dont really want to close the substream ... */
	return 0;
}

static gboolean
eos (CamelStream *stream)
{
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM(stream);
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM(stream);
	CamelSeekableStream *parent;
	gboolean eos;

	if (stream->eos)
		eos = TRUE;
	else {
		parent = seekable_substream->parent_stream;
		if (!parent_reset (seekable_substream, parent))
			return TRUE;

		eos = camel_lite_stream_eos (CAMEL_STREAM (parent));
		if (!eos && (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)) {
			eos = seekable_stream->position >= seekable_stream->bound_end;
		}
	}

	return eos;
}

static off_t
stream_seek (CamelSeekableStream *seekable_stream, off_t offset,
	     CamelStreamSeekPolicy policy)
{
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM(seekable_stream);
	CamelStream *stream = CAMEL_STREAM(seekable_stream);
	off_t real_offset = 0;

	stream->eos = FALSE;

	switch (policy) {
	case CAMEL_STREAM_SET:
		real_offset = offset;
		break;

	case CAMEL_STREAM_CUR:
		real_offset = seekable_stream->position + offset;
		break;

	case CAMEL_STREAM_END:
		if (seekable_stream->bound_end == CAMEL_STREAM_UNBOUND) {
			real_offset = camel_lite_seekable_stream_seek(seekable_substream->parent_stream,
								 offset,
								 CAMEL_STREAM_END);
			if (real_offset != -1) {
				if (real_offset<seekable_stream->bound_start)
					real_offset = seekable_stream->bound_start;
				seekable_stream->position = real_offset;
			}
			return real_offset;
		}
		real_offset = seekable_stream->bound_end + offset;
		break;
	}

	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		real_offset = MIN (real_offset, seekable_stream->bound_end);

	if (real_offset<seekable_stream->bound_start)
		real_offset = seekable_stream->bound_start;

	seekable_stream->position = real_offset;
	return real_offset;
}
