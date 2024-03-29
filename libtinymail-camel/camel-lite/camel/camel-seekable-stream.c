/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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

#include "camel-seekable-stream.h"

static CamelStreamClass *parent_class = NULL;

/* Returns the class for a CamelSeekableStream */
#define CSS_CLASS(so) CAMEL_SEEKABLE_STREAM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static off_t seek        (CamelSeekableStream *stream, off_t offset,
			  CamelStreamSeekPolicy policy);
static off_t stream_tell (CamelSeekableStream *stream);
static int   reset       (CamelStream *stream);
static int   set_bounds  (CamelSeekableStream *stream, off_t start, off_t end);

static void
camel_lite_seekable_stream_class_init (CamelSeekableStreamClass *camel_lite_seekable_stream_class)
{
	CamelStreamClass *camel_lite_stream_class =
		CAMEL_STREAM_CLASS (camel_lite_seekable_stream_class);

	parent_class = CAMEL_STREAM_CLASS( camel_lite_type_get_global_classfuncs( CAMEL_STREAM_TYPE ) );

	/* seekable stream methods */
	camel_lite_seekable_stream_class->seek = seek;
	camel_lite_seekable_stream_class->tell = stream_tell;
	camel_lite_seekable_stream_class->set_bounds = set_bounds;

	/* camel stream methods overload */
	camel_lite_stream_class->reset = reset;
}

static void
camel_lite_seekable_stream_init (void *o)
{
	CamelSeekableStream *stream = (CamelSeekableStream *)o;

	stream->position = 0;
	stream->bound_start = 0;
	stream->bound_end = CAMEL_STREAM_UNBOUND;
}

CamelType
camel_lite_seekable_stream_get_type (void)
{
	static CamelType camel_lite_seekable_stream_type = CAMEL_INVALID_TYPE;

	if (camel_lite_seekable_stream_type == CAMEL_INVALID_TYPE) {
		camel_lite_seekable_stream_type = camel_lite_type_register( CAMEL_STREAM_TYPE,
								  "CamelLiteSeekableStream",
								  sizeof( CamelSeekableStream ),
								  sizeof( CamelSeekableStreamClass ),
								  (CamelObjectClassInitFunc) camel_lite_seekable_stream_class_init,
								  NULL,
								  (CamelObjectInitFunc) camel_lite_seekable_stream_init,
								  NULL );
	}

	return camel_lite_seekable_stream_type;
}


static off_t
seek (CamelSeekableStream *stream, off_t offset,
      CamelStreamSeekPolicy policy)
{
	g_warning ("CamelLiteSeekableStream::seek called on default "
		   "implementation\n");
	return -1;
}

/**
 * camel_lite_seekable_stream_seek:
 * @stream: a #CamelStream object
 * @offset: offset value
 * @policy: what to do with the offset
 *
 * Seek to the specified position in @stream.
 *
 * If @policy is #CAMEL_STREAM_SET, seeks to @offset.
 *
 * If @policy is #CAMEL_STREAM_CUR, seeks to the current position plus
 * @offset.
 *
 * If @policy is #CAMEL_STREAM_END, seeks to the end of the stream plus
 * @offset.
 *
 * Regardless of @policy, the stream's final position will be clamped
 * to the range specified by its lower and upper bounds, and the
 * stream's eos state will be updated.
 *
 * Return value: new position, %-1 if operation failed.
 **/
off_t
camel_lite_seekable_stream_seek (CamelSeekableStream *stream, off_t offset,
			    CamelStreamSeekPolicy policy)
{
	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);

	return CSS_CLASS (stream)->seek (stream, offset, policy);
}


static off_t
stream_tell (CamelSeekableStream *stream)
{
	return stream->position;
}

/**
 * camel_lite_seekable_stream_tell:
 * @stream: a #CamelSeekableStream object
 *
 * Get the current position of a seekable stream.
 *
 * Returns the current position of the stream.
 **/
off_t
camel_lite_seekable_stream_tell (CamelSeekableStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);

	return CSS_CLASS (stream)->tell (stream);
}

static int
set_bounds (CamelSeekableStream *stream, off_t start, off_t end)
{
	/* store the bounds */
	stream->bound_start = start;
	stream->bound_end = end;

	if (start > stream->position)
		return camel_lite_seekable_stream_seek (stream, start, CAMEL_STREAM_SET);

	return 0;
}

/**
 * camel_lite_seekable_stream_set_bounds:
 * @stream: a #CamelSeekableStream object
 * @start: the first valid position
 * @end: the first invalid position, or #CAMEL_STREAM_UNBOUND
 *
 * Set the range of valid data this stream is allowed to cover.  If
 * there is to be no @end value, then @end should be set to
 * #CAMEL_STREAM_UNBOUND.
 *
 * Returns %-1 on error.
 **/
int
camel_lite_seekable_stream_set_bounds (CamelSeekableStream *stream,
				  off_t start, off_t end)
{
	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);
	g_return_val_if_fail (end == CAMEL_STREAM_UNBOUND || end >= start, -1);

	return CSS_CLASS (stream)->set_bounds (stream, start, end);
}

/* a default implementation of reset for seekable streams */
static int
reset (CamelStream *stream)
{
	CamelSeekableStream *seekable_stream;

	seekable_stream = CAMEL_SEEKABLE_STREAM (stream);

	return camel_lite_seekable_stream_seek (seekable_stream,
					   seekable_stream->bound_start,
					   CAMEL_STREAM_SET);
}






