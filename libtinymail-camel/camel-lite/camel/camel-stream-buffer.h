/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-buffer.h :stream which buffers another stream */

/*
 *
 * Author :
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 2000 Ximian Inc. (www.ximian.com) .
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


#ifndef CAMEL_STREAM_BUFFER_H
#define CAMEL_STREAM_BUFFER_H 1

#include <stdio.h>
#include <camel/camel-seekable-stream.h>

#define CAMEL_STREAM_BUFFER_TYPE     (camel_lite_stream_buffer_get_type ())
#define CAMEL_STREAM_BUFFER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STREAM_BUFFER_TYPE, CamelStreamBuffer))
#define CAMEL_STREAM_BUFFER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STREAM_BUFFER_TYPE, CamelStreamBufferClass))
#define CAMEL_IS_STREAM_BUFFER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STREAM_BUFFER_TYPE))

G_BEGIN_DECLS

typedef enum {
	CAMEL_STREAM_BUFFER_BUFFER = 0,
	CAMEL_STREAM_BUFFER_NONE,
	CAMEL_STREAM_BUFFER_READ = 0x00,
	CAMEL_STREAM_BUFFER_WRITE = 0x80,
	CAMEL_STREAM_BUFFER_MODE = 0x80
} CamelStreamBufferMode;

struct _CamelStreamBuffer
{
	CamelStream parent_object;

	/* these are all of course, private */
	CamelStream *stream;

	unsigned char *buf, *ptr, *end;
	int size;

	unsigned char *linebuf;	/* for reading lines at a time */
	int linesize;

	CamelStreamBufferMode mode;
	unsigned int flags;	/* internal flags */
};


typedef struct {
	CamelStreamClass parent_class;

	/* Virtual methods */
	void (*init) (CamelStreamBuffer *stream_buffer, CamelStream *stream,
		      CamelStreamBufferMode mode);
	void (*init_vbuf) (CamelStreamBuffer *stream_buffer,
			   CamelStream *stream, CamelStreamBufferMode mode,
			   char *buf, guint32 size);

} CamelStreamBufferClass;


/* Standard Camel function */
CamelType camel_lite_stream_buffer_get_type (void);


/* public methods */
CamelStream *camel_lite_stream_buffer_new (CamelStream *stream,
				      CamelStreamBufferMode mode);
CamelStream *camel_lite_stream_buffer_new_with_vbuf (CamelStream *stream,
						CamelStreamBufferMode mode,
						char *buf, guint32 size);

/* unimplemented
   CamelStream *camel_lite_stream_buffer_set_vbuf (CamelStreamBuffer *b, CamelStreamBufferMode mode, char *buf, guint32 size); */

/* read a line of characters */
int camel_lite_stream_buffer_gets (CamelStreamBuffer *sbf, char *buf, unsigned int max);

char *camel_lite_stream_buffer_read_line (CamelStreamBuffer *sbf);
int camel_lite_tcp_stream_buffer_gets_nb (CamelStreamBuffer *sbf, char *buf, unsigned int max);

ssize_t camel_lite_stream_buffer_read_opp (CamelStream *stream, char *buffer, size_t n, int len);

G_END_DECLS

#endif /* CAMEL_STREAM_BUFFER_H */
