/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-mime-part-utils : Utility for mime parsing and so on
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *          Michael Zucchi <notzed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libedataserver/e-lite-iconv.h>
#ifdef CAMEL_USE_E_ARRAY
#include <libedataserver/e-array.h>
#else
#define e_byte_array_new g_byte_array_new
#define e_byte_array_append g_byte_array_append
#endif

#include "camel-charset-map.h"
#include "camel-html-parser.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-filter-save.h"
#include "camel-mime-message.h"
#include "camel-mime-part-utils.h"
#include "camel-multipart-encrypted.h"
#include "camel-multipart-signed.h"
#include "camel-multipart.h"
#include "camel-seekable-substream.h"
#include "camel-stream-filter.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))
	       #include <stdio.h>*/

/* simple data wrapper */
static void
simple_data_wrapper_construct_from_parser (CamelDataWrapper *dw, CamelMimeParser *mp)
{
	GByteArray *buffer = NULL;
	CamelStream *stream;
	uint start, end;
	int fd = -1;
	size_t len;
	char *buf;
	
	d(printf ("simple_data_wrapper_construct_from_parser()\n"));
	
	if (!(stream = camel_lite_mime_parser_stream (mp)))
		fd = camel_lite_mime_parser_fd (mp);
	else if (!CAMEL_IS_SEEKABLE_SUBSTREAM (stream))
		stream = NULL;

	start = camel_lite_mime_parser_tell (mp);
	if ((stream || fd != -1) && (start != -1)) {
		/* we can keep content on disk */
	} else {
		/* need to load content into memory */
		buffer = e_byte_array_new ();
	}
	
	while (camel_lite_mime_parser_step (mp, &buf, &len) != CAMEL_MIME_PARSER_STATE_BODY_END) {
		if (buffer != NULL) {
			d(printf("appending o/p data: %d: %.*s\n", len, len, buf));
			e_byte_array_append (buffer, (guint8 *) buf, len);
		}
	}
	
	if (buffer == NULL) {
		end = camel_lite_mime_parser_tell (mp);
		
		if (stream != NULL) {
			uint offset;
			if (CAMEL_IS_SEEKABLE_SUBSTREAM (stream)) {
				offset = ((CamelSeekableStream *)stream)->bound_start;
			} else {
				offset = 0;
			}
			stream = camel_lite_seekable_substream_new ((CamelSeekableStream *) stream, start + offset, end + offset);
		} else
			stream = camel_lite_stream_fs_new_with_fd_and_bounds (dup (fd), start, end);
	} else {
		stream = camel_lite_stream_mem_new_with_byte_array (buffer);
	}
	
	camel_lite_data_wrapper_construct_from_stream (dw, stream);
	camel_lite_object_unref (stream);
}


/* This replaces the data wrapper repository ... and/or could be replaced by it? */
void
camel_lite_mime_part_construct_content_from_parser (CamelMimePart *dw, CamelMimeParser *mp)
{
	CamelDataWrapper *content = NULL;
	CamelContentType *ct;
	char *encoding;

	if (!dw)
		return;

	ct = camel_lite_mime_parser_content_type (mp);

	encoding = camel_lite_content_transfer_encoding_decode (camel_lite_mime_parser_header (mp, "Content-Transfer-Encoding", NULL));

	switch (camel_lite_mime_parser_state (mp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		d(printf("Creating body part\n"));
		/* multipart/signed is some fucked up type that we must treat as binary data, fun huh, idiots. */
		if (camel_lite_content_type_is (ct, "multipart", "signed")) {
			content = (CamelDataWrapper *) camel_lite_multipart_signed_new ();
			camel_lite_multipart_construct_from_parser ((CamelMultipart *) content, mp);
		} else {
			content = camel_lite_data_wrapper_new ();
			simple_data_wrapper_construct_from_parser (content, mp);
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		d(printf("Creating message part\n"));
		content = (CamelDataWrapper *) camel_lite_mime_message_new ();
		camel_lite_mime_part_construct_from_parser ((CamelMimePart *)content, mp);
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		d(printf("Creating multi-part\n"));
		if (camel_lite_content_type_is (ct, "multipart", "encrypted"))
			content = (CamelDataWrapper *) camel_lite_multipart_encrypted_new ();
		else if (camel_lite_content_type_is (ct, "multipart", "signed"))
			content = (CamelDataWrapper *) camel_lite_multipart_signed_new ();
		else
			content = (CamelDataWrapper *) camel_lite_multipart_new ();

		camel_lite_multipart_construct_from_parser((CamelMultipart *)content, mp);
		d(printf("Created multi-part\n"));
		break;
	default:
		g_warning("Invalid state encountered???: %u", camel_lite_mime_parser_state (mp));
	}

	if (content) {
		if (encoding)
			content->encoding = camel_lite_transfer_encoding_from_string (encoding);

		/* would you believe you have to set this BEFORE you set the content object???  oh my god !!!! */
		camel_lite_data_wrapper_set_mime_type_field (content, camel_lite_mime_part_get_content_type (dw));
		camel_lite_medium_set_content_object ((CamelMedium *)dw, content);
		camel_lite_object_unref (content);
	}

	g_free (encoding);
}
