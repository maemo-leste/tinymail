/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_MIME_FILTER_BESTENC_H
#define _CAMEL_MIME_FILTER_BESTENC_H

#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-charset-map.h>

#define CAMEL_MIME_FILTER_BESTENC(obj)         CAMEL_CHECK_CAST (obj, camel_lite_mime_filter_bestenc_get_type (), CamelMimeFilterBestenc)
#define CAMEL_MIME_FILTER_BESTENC_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_mime_filter_bestenc_get_type (), CamelMimeFilterBestencClass)
#define CAMEL_IS_MIME_FILTER_BESTENC(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_mime_filter_bestenc_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterBestencClass CamelMimeFilterBestencClass;

typedef enum _CamelBestencRequired {
	CAMEL_BESTENC_GET_ENCODING = 1<<0,
	CAMEL_BESTENC_GET_CHARSET = 1<<1,

	/* do we treat 'lf' as if it were crlf? */
	CAMEL_BESTENC_LF_IS_CRLF = 1<<8,
	/* do we not allow "From " to appear at the start of a line in any part? */
	CAMEL_BESTENC_NO_FROM = 1<<9
} CamelBestencRequired;

typedef enum _CamelBestencEncoding {
	CAMEL_BESTENC_7BIT,
	CAMEL_BESTENC_8BIT,
	CAMEL_BESTENC_BINARY,

	/* is the content stream to be treated as text? */
	CAMEL_BESTENC_TEXT = 1<<8
} CamelBestencEncoding;

struct _CamelMimeFilterBestenc {
	CamelMimeFilter parent;

	unsigned int flags;	/* our creation flags, see above */

	unsigned int count0;	/* count of NUL characters */
	unsigned int count8;	/* count of 8 bit characters */
	unsigned int total;	/* total characters read */

	unsigned int lastc;	/* the last character read */
	int crlfnoorder;	/* if crlf's occured where they shouldn't have */

	int startofline;	/* are we at the start of a new line? */

	int fromcount;
	char fromsave[6];	/* save a few characters if we found an \n near the end of the buffer */
	int hadfrom;		/* did we encounter a "\nFrom " in the data? */

	unsigned int countline;	/* current count of characters on a given line */
	unsigned int maxline;	/* max length of any line */

	CamelCharset charset;	/* used to determine the best charset to use */
};

struct _CamelMimeFilterBestencClass {
	CamelMimeFilterClass parent_class;
};

CamelType		camel_lite_mime_filter_bestenc_get_type	(void);
CamelMimeFilterBestenc      *camel_lite_mime_filter_bestenc_new	(unsigned int flags);


CamelTransferEncoding	camel_lite_mime_filter_bestenc_get_best_encoding(CamelMimeFilterBestenc *filter, CamelBestencEncoding required);
const char *		camel_lite_mime_filter_bestenc_get_best_charset(CamelMimeFilterBestenc *filter);
void 			camel_lite_mime_filter_bestenc_set_flags(CamelMimeFilterBestenc *filter, unsigned int flags);

G_END_DECLS

#endif /* ! _CAMEL_MIME_FILTER_BESTENC_H */
