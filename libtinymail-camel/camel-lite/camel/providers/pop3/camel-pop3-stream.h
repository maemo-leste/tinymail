/*
 * Copyright (C) 2002 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* This is *identical* to the camel-nntp-stream, so should probably
   work out a way to merge them */

#ifndef _CAMEL_POP3_STREAM_H
#define _CAMEL_POP3_STREAM_H

#include <camel/camel-stream.h>

#ifdef DEBUG
#define pop3_debug	g_print
#else
#define pop3_debug(o,...)
#endif


#define CAMEL_POP3_STREAM(obj)         CAMEL_CHECK_CAST (obj, camel_lite_pop3_stream_get_type (), CamelPOP3Stream)
#define CAMEL_POP3_STREAM_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_pop3_stream_get_type (), CamelPOP3StreamClass)
#define CAMEL_IS_POP3_STREAM(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_pop3_stream_get_type ())

G_BEGIN_DECLS

typedef struct _CamelPOP3StreamClass CamelPOP3StreamClass;
typedef struct _CamelPOP3Stream CamelPOP3Stream;

typedef enum {
	CAMEL_POP3_STREAM_LINE,
	CAMEL_POP3_STREAM_DATA,
	CAMEL_POP3_STREAM_EOD	/* end of data, acts as if end of stream */
} camel_lite_pop3_stream_mode_t;

struct _CamelPOP3Stream {
	CamelStream parent;

	CamelStream *source;

	camel_lite_pop3_stream_mode_t mode;
	int state;

	unsigned char *buf, *ptr, *end;
	unsigned char *linebuf, *lineptr, *lineend;
};

struct _CamelPOP3StreamClass {
	CamelStreamClass parent_class;
};

CamelType		 camel_lite_pop3_stream_get_type	(void);

CamelStream     *camel_lite_pop3_stream_new		(CamelStream *source);


void		 camel_lite_pop3_stream_set_mode     (CamelPOP3Stream *is, camel_lite_pop3_stream_mode_t mode);

int              camel_lite_pop3_stream_line		(CamelPOP3Stream *is, unsigned char **data, unsigned int *len);
int 		 camel_lite_pop3_stream_gets		(CamelPOP3Stream *is, unsigned char **start, unsigned int *len);
int 		 camel_lite_pop3_stream_getd		(CamelPOP3Stream *is, unsigned char **start, unsigned int *len);

G_END_DECLS

#endif /* ! _CAMEL_POP3_STREAM_H */
