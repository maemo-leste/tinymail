/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000-2003 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* POSIX requires <sys/types.h> be included before <regex.h> */
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>  /* for MAXHOSTNAMELEN */
#include <sys/stat.h>

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 1024
#endif

#include <glib.h>

#include <libedataserver/e-lite-iconv.h>
#include <libedataserver/e-lite-time-utils.h>

#include "camel-charset-map.h"
#include "camel-mime-utils.h"
#include "camel-net-utils.h"
#include "camel-utf8.h"

#ifndef CLEAN_DATE
#include "broken-date-parser.h"
#endif

#ifdef G_OS_WIN32
/* Undef the similar macro from pthread.h, it doesn't check if
 * gmtime() returns NULL.
 */
#undef gmtime_r

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#if 0
int strdup_count = 0;
int malloc_count = 0;
int free_count = 0;

#define g_strdup(x) (strdup_count++, g_strdup(x))
#define g_malloc(x) (malloc_count++, g_malloc(x))
#define g_free(x) (free_count++, g_free(x))
#endif

/* for all non-essential warnings ... */
#define w(x)

#define d(x)
#define d2(x)

#define CAMEL_UUENCODE_CHAR(c)  ((c) ? (c) + ' ' : '`')
#define	CAMEL_UUDECODE_CHAR(c)	(((c) - ' ') & 077)

static const unsigned char tohex[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

/**
 * camel_lite_base64_encode_close:
 * @in: input stream
 * @inlen: length of the input
 * @break_lines: whether or not to break long lines
 * @out: output string
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Base64 encodes the input stream to the output stream. Call this
 * when finished encoding data with #camel_lite_base64_encode_step
 * to flush off the last little bit.
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_base64_encode_close(unsigned char *in, size_t inlen, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	gsize bytes = 0;

	if (inlen > 0)
		bytes += g_base64_encode_step (in, inlen, break_lines, (gchar *) out, state, save);

	bytes += g_base64_encode_close (break_lines, (gchar *) out, state, save);

	return bytes;
}


/**
 * camel_lite_base64_encode_step:
 * @in: input stream
 * @inlen: length of the input
 * @break_lines: break long lines
 * @out: output string
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Base64 encodes a chunk of data. Performs an 'encode step', only
 * encodes blocks of 3 characters to the output at a time, saves
 * left-over state in state and save (initialise to 0 on first
 * invocation).
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_base64_encode_step(unsigned char *in, size_t len, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	return g_base64_encode_step (in, len, break_lines, (gchar *) out, state, save);
}


/**
 * camel_lite_base64_decode_step: decode a chunk of base64 encoded data
 * @in: input stream
 * @len: max length of data to decode
 * @out: output stream
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been decoded
 *
 * Decodes a chunk of base64 encoded data
 *
 * Returns the number of bytes decoded (which have been dumped in @out)
 **/
size_t
camel_lite_base64_decode_step(unsigned char *in, size_t len, unsigned char *out, int *state, unsigned int *save)
{
	return g_base64_decode_step ((gchar *) in, len, out, state, save);
}


/**
 * camel_lite_base64_encode_simple:
 * @data: binary stream of data to encode
 * @len: length of data
 *
 * Base64 encodes a block of memory.
 *
 * Returns a string containing the base64 encoded data
 **/
char *
camel_lite_base64_encode_simple (const char *data, size_t len)
{
	return g_base64_encode ((const guchar *) data, len);
}


/**
 * camel_lite_base64_decode_simple:
 * @data: data to decode
 * @len: length of data
 *
 * Base64 decodes @data inline (overwrites @data with the decoded version).
 *
 * Returns the new length of @data
 **/
size_t
camel_lite_base64_decode_simple (char *data, size_t len)
{
	guchar *out_data;
	gsize out_len = 0;

	g_return_val_if_fail (data != NULL, 0);
	g_return_val_if_fail (strlen (data) > 1, 0);

	out_data = g_base64_decode (data, &out_len);
	g_assert (out_len <= len); /* sanity check */
	memcpy (data, out_data, out_len);
	data[out_len] = '\0';
	g_free (out_data);

	return out_len;
}

/**
 * camel_lite_uuencode_close:
 * @in: input stream
 * @len: input stream length
 * @out: output stream
 * @uubuf: temporary buffer of 60 bytes
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Uuencodes a chunk of data. Call this when finished encoding data
 * with #camel_lite_uuencode_step to flush off the last little bit.
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_uuencode_close (unsigned char *in, size_t len, unsigned char *out, unsigned char *uubuf, int *state, guint32 *save)
{
	register unsigned char *outptr, *bufptr;
	register guint32 saved;
	int uulen, uufill, i;

	outptr = out;

	if (len > 0)
		outptr += camel_lite_uuencode_step (in, len, out, uubuf, state, save);

	uufill = 0;

	saved = *save;
	i = *state & 0xff;
	uulen = (*state >> 8) & 0xff;

	bufptr = uubuf + ((uulen / 3) * 4);

	if (i > 0) {
		while (i < 3) {
			saved <<= 8 | 0;
			uufill++;
			i++;
		}

		if (i == 3) {
			/* convert 3 normal bytes into 4 uuencoded bytes */
			unsigned char b0, b1, b2;

			b0 = saved >> 16;
			b1 = saved >> 8 & 0xff;
			b2 = saved & 0xff;

			*bufptr++ = CAMEL_UUENCODE_CHAR ((b0 >> 2) & 0x3f);
			*bufptr++ = CAMEL_UUENCODE_CHAR (((b0 << 4) | ((b1 >> 4) & 0xf)) & 0x3f);
			*bufptr++ = CAMEL_UUENCODE_CHAR (((b1 << 2) | ((b2 >> 6) & 0x3)) & 0x3f);
			*bufptr++ = CAMEL_UUENCODE_CHAR (b2 & 0x3f);

			i = 0;
			saved = 0;
			uulen += 3;
		}
	}

	if (uulen > 0) {
		int cplen = ((uulen / 3) * 4);

		*outptr++ = CAMEL_UUENCODE_CHAR ((uulen - uufill) & 0xff);
		memcpy (outptr, uubuf, cplen);
		outptr += cplen;
		*outptr++ = '\n';
		uulen = 0;
	}

	*outptr++ = CAMEL_UUENCODE_CHAR (uulen & 0xff);
	*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr - out;
}


/**
 * camel_lite_uuencode_step:
 * @in: input stream
 * @len: input stream length
 * @out: output stream
 * @uubuf: temporary buffer of 60 bytes
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Uuencodes a chunk of data. Performs an 'encode step', only encodes
 * blocks of 45 characters to the output at a time, saves left-over
 * state in @uubuf, @state and @save (initialize to 0 on first
 * invocation).
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_uuencode_step (unsigned char *in, size_t len, unsigned char *out, unsigned char *uubuf, int *state, guint32 *save)
{
	register unsigned char *inptr, *outptr, *bufptr;
	unsigned char *inend;
	register guint32 saved;
	int uulen, i;

	saved = *save;
	i = *state & 0xff;
	uulen = (*state >> 8) & 0xff;

	inptr = in;
	inend = in + len;

	outptr = out;

	bufptr = uubuf + ((uulen / 3) * 4);

	while (inptr < inend) {
		while (uulen < 45 && inptr < inend) {
			while (i < 3 && inptr < inend) {
				saved = (saved << 8) | *inptr++;
				i++;
			}

			if (i == 3) {
				/* convert 3 normal bytes into 4 uuencoded bytes */
				unsigned char b0, b1, b2;

				b0 = saved >> 16;
				b1 = saved >> 8 & 0xff;
				b2 = saved & 0xff;

				*bufptr++ = CAMEL_UUENCODE_CHAR ((b0 >> 2) & 0x3f);
				*bufptr++ = CAMEL_UUENCODE_CHAR (((b0 << 4) | ((b1 >> 4) & 0xf)) & 0x3f);
				*bufptr++ = CAMEL_UUENCODE_CHAR (((b1 << 2) | ((b2 >> 6) & 0x3)) & 0x3f);
				*bufptr++ = CAMEL_UUENCODE_CHAR (b2 & 0x3f);

				i = 0;
				saved = 0;
				uulen += 3;
			}
		}

		if (uulen >= 45) {
			*outptr++ = CAMEL_UUENCODE_CHAR (uulen & 0xff);
			memcpy (outptr, uubuf, ((uulen / 3) * 4));
			outptr += ((uulen / 3) * 4);
			*outptr++ = '\n';
			uulen = 0;
			bufptr = uubuf;
		}
	}

	*save = saved;
	*state = ((uulen & 0xff) << 8) | (i & 0xff);

	return outptr - out;
}


/**
 * camel_lite_uudecode_step:
 * @in: input stream
 * @inlen: max length of data to decode
 * @out: output stream
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been decoded
 *
 * Uudecodes a chunk of data. Performs a 'decode step' on a chunk of
 * uuencoded data. Assumes the "begin mode filename" line has
 * been stripped off.
 *
 * Returns the number of bytes decoded
 **/
size_t
camel_lite_uudecode_step (unsigned char *in, size_t len, unsigned char *out, int *state, guint32 *save)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, ch;
	register guint32 saved;
	gboolean last_was_eoln;
	int uulen, i;

	if (*state & CAMEL_UUDECODE_STATE_END)
		return 0;

	saved = *save;
	i = *state & 0xff;
	uulen = (*state >> 8) & 0xff;
	if (uulen == 0)
		last_was_eoln = TRUE;
	else
		last_was_eoln = FALSE;

	inend = in + len;
	outptr = out;

	inptr = in;
	while (inptr < inend) {
		if (*inptr == '\n' || last_was_eoln) {
			if (last_was_eoln && *inptr != '\n') {
				uulen = CAMEL_UUDECODE_CHAR (*inptr);
				last_was_eoln = FALSE;
				if (uulen == 0) {
					*state |= CAMEL_UUDECODE_STATE_END;
					break;
				}
			} else {
				last_was_eoln = TRUE;
			}

			inptr++;
			continue;
		}

		ch = *inptr++;

		if (uulen > 0) {
			/* save the byte */
			saved = (saved << 8) | ch;
			i++;
			if (i == 4) {
				/* convert 4 uuencoded bytes to 3 normal bytes */
				unsigned char b0, b1, b2, b3;

				b0 = saved >> 24;
				b1 = saved >> 16 & 0xff;
				b2 = saved >> 8 & 0xff;
				b3 = saved & 0xff;

				if (uulen >= 3) {
					*outptr++ = CAMEL_UUDECODE_CHAR (b0) << 2 | CAMEL_UUDECODE_CHAR (b1) >> 4;
					*outptr++ = CAMEL_UUDECODE_CHAR (b1) << 4 | CAMEL_UUDECODE_CHAR (b2) >> 2;
				        *outptr++ = CAMEL_UUDECODE_CHAR (b2) << 6 | CAMEL_UUDECODE_CHAR (b3);
				} else {
					if (uulen >= 1) {
						*outptr++ = CAMEL_UUDECODE_CHAR (b0) << 2 | CAMEL_UUDECODE_CHAR (b1) >> 4;
					}
					if (uulen >= 2) {
						*outptr++ = CAMEL_UUDECODE_CHAR (b1) << 4 | CAMEL_UUDECODE_CHAR (b2) >> 2;
					}
				}

				i = 0;
				saved = 0;
				uulen -= 3;
			}
		} else {
			break;
		}
	}

	*save = saved;
	*state = (*state & CAMEL_UUDECODE_STATE_MASK) | ((uulen & 0xff) << 8) | (i & 0xff);

	return outptr - out;
}


/**
 * camel_lite_quoted_encode_close:
 * @in: input stream
 * @len: length of the input
 * @out: output string
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Quoted-printable encodes a block of text. Call this when finished
 * encoding data with #camel_lite_quoted_encode_step to flush off
 * the last little bit.
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_quoted_encode_close(unsigned char *in, size_t len, unsigned char *out, int *state, int *save)
{
	register unsigned char *outptr = out;
	int last;

	if (len>0)
		outptr += camel_lite_quoted_encode_step(in, len, outptr, state, save);

	last = *state;
	if (last != -1) {
		/* space/tab must be encoded if it's the last character on
		   the line */
		if (camel_lite_mime_is_qpsafe(last) && last!=' ' && last!=9) {
			*outptr++ = last;
		} else {
			*outptr++ = '=';
			*outptr++ = tohex[(last>>4) & 0xf];
			*outptr++ = tohex[last & 0xf];
		}
	}

	*save = 0;
	*state = -1;

	return outptr-out;
}


/**
 * camel_lite_quoted_encode_step:
 * @in: input stream
 * @len: length of the input
 * @out: output string
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been encoded
 *
 * Quoted-printable encodes a block of text. Performs an 'encode
 * step', saves left-over state in state and save (initialise to -1 on
 * first invocation).
 *
 * Returns the number of bytes encoded
 **/
size_t
camel_lite_quoted_encode_step (unsigned char *in, size_t len, unsigned char *out, int *statep, int *save)
{
	register guchar *inptr, *outptr, *inend;
	unsigned char c;
	register int sofar = *save;  /* keeps track of how many chars on a line */
	register int last = *statep; /* keeps track if last char to end was a space cr etc */

	inptr = in;
	inend = in + len;
	outptr = out;
	while (inptr < inend) {
		c = *inptr++;
		if (c == '\r') {
			if (last != -1) {
				*outptr++ = '=';
				*outptr++ = tohex[(last >> 4) & 0xf];
				*outptr++ = tohex[last & 0xf];
				sofar += 3;
			}
			last = c;
		} else if (c == '\n') {
			if (last != -1 && last != '\r') {
				*outptr++ = '=';
				*outptr++ = tohex[(last >> 4) & 0xf];
				*outptr++ = tohex[last & 0xf];
			}
			*outptr++ = '\n';
			sofar = 0;
			last = -1;
		} else {
			if (last != -1) {
				if (camel_lite_mime_is_qpsafe(last)) {
					*outptr++ = last;
					sofar++;
				} else {
					*outptr++ = '=';
					*outptr++ = tohex[(last >> 4) & 0xf];
					*outptr++ = tohex[last & 0xf];
					sofar += 3;
				}
			}

			if (camel_lite_mime_is_qpsafe(c)) {
				if (sofar > 74) {
					*outptr++ = '=';
					*outptr++ = '\n';
					sofar = 0;
				}

				/* delay output of space char */
				if (c==' ' || c=='\t') {
					last = c;
				} else {
					*outptr++ = c;
					sofar++;
					last = -1;
				}
			} else {
				if (sofar > 72) {
					*outptr++ = '=';
					*outptr++ = '\n';
					sofar = 3;
				} else
					sofar += 3;

				*outptr++ = '=';
				*outptr++ = tohex[(c >> 4) & 0xf];
				*outptr++ = tohex[c & 0xf];
				last = -1;
			}
		}
	}
	*save = sofar;
	*statep = last;

	return (outptr - out);
}

/*
  FIXME: this does not strip trailing spaces from lines (as it should, rfc 2045, section 6.7)
  Should it also canonicalise the end of line to CR LF??

  Note: Trailing rubbish (at the end of input), like = or =x or =\r will be lost.
*/

/**
 * camel_lite_quoted_decode_step:
 * @in: input stream
 * @len: max length of data to decode
 * @out: output stream
 * @savestate: holds the number of bits that are stored in @save
 * @saved: leftover bits that have not yet been decoded
 *
 * Decodes a block of quoted-printable encoded data. Performs a
 * 'decode step' on a chunk of QP encoded data.
 *
 * Returns the number of bytes decoded
 **/
size_t
camel_lite_quoted_decode_step(unsigned char *in, size_t len, unsigned char *out, int *savestate, int *saveme)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c;
	int state, save;

	inend = in+len;
	outptr = out;

	d(printf("quoted-printable, decoding text '%.*s'\n", len, in));

	state = *savestate;
	save = *saveme;
	inptr = in;
	while (inptr<inend) {
		switch (state) {
		case 0:
			while (inptr<inend) {
				c = *inptr++;
				if (c=='=') {
					state = 1;
					break;
				}
#ifdef CANONICALISE_EOL
				/*else if (c=='\r') {
					state = 3;
				} else if (c=='\n') {
					*outptr++ = '\r';
					*outptr++ = c;
					} */
#endif
				else {
					*outptr++ = c;
				}
			}
			break;
		case 1:
			c = *inptr++;
			if (c=='\n') {
				/* soft break ... unix end of line */
				state = 0;
			} else {
				save = c;
				state = 2;
			}
			break;
		case 2:
			c = *inptr++;
			if (isxdigit(c) && isxdigit(save)) {
				c = toupper(c);
				save = toupper(save);
				*outptr++ = (((save>='A'?save-'A'+10:save-'0')&0x0f) << 4)
					| ((c>='A'?c-'A'+10:c-'0')&0x0f);
			} else if (c=='\n' && save == '\r') {
				/* soft break ... canonical end of line */
			} else {
				/* just output the data */
				*outptr++ = '=';
				*outptr++ = save;
				*outptr++ = c;
			}
			state = 0;
			break;
#ifdef CANONICALISE_EOL
		case 3:
			/* convert \r -> to \r\n, leaves \r\n alone */
			c = *inptr++;
			if (c=='\n') {
				*outptr++ = '\r';
				*outptr++ = c;
			} else {
				*outptr++ = '\r';
				*outptr++ = '\n';
				*outptr++ = c;
			}
			state = 0;
			break;
#endif
		}
	}

	*savestate = state;
	*saveme = save;

	return outptr-out;
}

/*
  this is for the "Q" encoding of international words,
  which is slightly different than plain quoted-printable (mainly by allowing 0x20 <> _)
*/
static size_t
quoted_decode(const unsigned char *in, size_t len, unsigned char *out)
{
	register const unsigned char *inptr;
	register unsigned char *outptr;
	unsigned const char *inend;
	unsigned char c, c1;
	int ret = 0;

	inend = in+len;
	outptr = out;

	d(printf("decoding text '%.*s'\n", len, in));

	inptr = in;
	while (inptr<inend) {
		c = *inptr++;
		if (c=='=') {
			/* silently ignore truncated data? */
			if (inend-in>=2) {
				c = toupper(*inptr++);
				c1 = toupper(*inptr++);
				*outptr++ = (((c>='A'?c-'A'+10:c-'0')&0x0f) << 4)
					| ((c1>='A'?c1-'A'+10:c1-'0')&0x0f);
			} else {
				ret = -1;
				break;
			}
		} else if (c=='_') {
			*outptr++ = 0x20;
		} else {
			*outptr++ = c;
		}
	}
	if (ret==0) {
		return outptr-out;
	}
	return 0;
}

/* rfc2047 version of quoted-printable */
/* safemask is the mask to apply to the camel_lite_mime_special_table to determine what
   characters can safely be included without encoding */
static size_t
quoted_encode (const unsigned char *in, size_t len, unsigned char *out, unsigned short safemask)
{
	register const unsigned char *inptr, *inend;
	unsigned char *outptr;
	unsigned char c;

	inptr = in;
	inend = in + len;
	outptr = out;
	while (inptr < inend) {
		c = *inptr++;
		if (c==' ') {
			*outptr++ = '_';
		} else if (camel_lite_mime_special_table[c] & safemask) {
			*outptr++ = c;
		} else {
			*outptr++ = '=';
			*outptr++ = tohex[(c >> 4) & 0xf];
			*outptr++ = tohex[c & 0xf];
		}
	}

	d(printf("encoding '%.*s' = '%.*s'\n", len, in, outptr-out, out));

	return (outptr - out);
}


static void
header_decode_lwsp(const char **in)
{
	const char *inptr = *in;
	char c;

	d2(printf("is ws: '%s'\n", *in));

	while ((camel_lite_mime_is_lwsp(*inptr) || *inptr =='(') && *inptr != '\0') {
		while (camel_lite_mime_is_lwsp(*inptr) && *inptr != '\0') {
			d2(printf("(%c)", *inptr));
			inptr++;
		}
		d2(printf("\n"));

		/* check for comments */
		if (*inptr == '(') {
			int depth = 1;
			inptr++;
			while (depth && (c=*inptr) && *inptr != '\0') {
				if (c=='\\' && inptr[1]) {
					inptr++;
				} else if (c=='(') {
					depth++;
				} else if (c==')') {
					depth--;
				}
				inptr++;
			}
		}
	}
	*in = inptr;
}

static char *
camel_lite_iconv_strndup (iconv_t cd, const char *string, size_t n)
{
	size_t inleft, outleft, converted = 0;
	char *out, *outbuf;
	const char *inbuf;
	size_t outlen;
	int errnosav;
	
	if (cd == (iconv_t) -1)
		return g_strndup (string, n);
	
	outlen = n * 2 + 16;
	out = g_malloc (outlen + 4);
	
	inbuf = string;
	inleft = n;
	
	do {
		errno = 0;
		outbuf = out + converted;
		outleft = outlen - converted;
		
		converted = iconv (cd, (char **) &inbuf, &inleft, &outbuf, &outleft);
		if (converted == (size_t) -1) {
			if (errno != E2BIG && errno != EINVAL)
				goto fail;
		}
		
		/*
		 * E2BIG   There is not sufficient room at *outbuf.
		 *
		 * We just need to grow our outbuffer and try again.
		 */
		
		converted = outbuf - out;
		if (errno == E2BIG) {
			outlen += inleft * 2 + 16;
			out = g_realloc (out, outlen + 4);
			outbuf = out + converted;
		}
	} while (errno == E2BIG && inleft > 0);
	
	/*
	 * EINVAL  An  incomplete  multibyte sequence has been encoun­
	 *         tered in the input.
	 *
	 * We'll just have to ignore it...
	 */
	
	/* flush the iconv conversion */
	iconv (cd, NULL, NULL, &outbuf, &outleft);
	
	/* Note: not all charsets can be nul-terminated with a single
           nul byte. UCS2, for example, needs 2 nul bytes and UCS4
           needs 4. I hope that 4 nul bytes is enough to terminate all
           multibyte charsets? */
	
	/* nul-terminate the string */
	memset (outbuf, 0, 4);
	
	/* reset the cd */
	iconv (cd, NULL, NULL, NULL, NULL);
	
	return out;
	
 fail:
	
	errnosav = errno;
	
	w(g_warning ("camel_lite_iconv_strndup: %s at byte %lu", strerror (errno), n - inleft));
	
	g_free (out);
	
	/* reset the cd */
	iconv (cd, NULL, NULL, NULL, NULL);
	
	errno = errnosav;
	
	return NULL;
}

#define is_ascii(c) isascii ((int) ((unsigned char) (c)))

static char *
decode_8bit (const char *text, size_t len, const char *default_charset)
{
	const char *charsets[4] = { "UTF-8", NULL, NULL, NULL };
	size_t inleft, outleft, outlen, rc, min, n;
	const char *locale_charset, *best;
	char *out, *outbuf;
	const char *inbuf;
	iconv_t cd;
	int i = 1;
	
	if (default_charset && g_ascii_strcasecmp (default_charset, "UTF-8") != 0)
		charsets[i++] = default_charset;
	
	locale_charset = e_iconv_locale_charset ();
	if (locale_charset && g_ascii_strcasecmp (locale_charset, "UTF-8") != 0)
		charsets[i++] = locale_charset;
	
	min = len;
	best = charsets[0];
	
	outlen = (len * 2) + 16; 
	out = g_malloc (outlen + 1);
	
	for (i = 0; charsets[i]; i++) {
		if ((cd = e_iconv_open ("UTF-8", charsets[i])) == (iconv_t) -1)
			continue;
		
		outleft = outlen;
		outbuf = out;
		inleft = len;
		inbuf = text;
		n = 0;
		
		do {
			rc = iconv (cd, (char **) &inbuf, &inleft, &outbuf, &outleft);
			if (rc == (size_t) -1) {
				if (errno == EINVAL) {
					/* incomplete sequence at the end of the input buffer */
					n += inleft;
					break;
				}
				
				if (errno == E2BIG) {
					outlen += (inleft * 2) + 16;
					rc = (size_t) (outbuf - out);
					out = g_realloc (out, outlen + 1);
					outleft = outlen - rc;
					outbuf = out + rc;
				} else {
					inleft--;
					inbuf++;
					n++;
				}
			}
		} while (inleft > 0);
		
		rc = iconv (cd, NULL, NULL, &outbuf, &outleft);
		*outbuf = '\0';
		
		e_iconv_close (cd);
		
		if (rc != (size_t) -1 && n == 0)
			return out;
		
		if (n < min) {
			best = charsets[i];
			min = n;
		}
	}
	
	/* if we get here, then none of the charsets fit the 8bit text flawlessly...
	 * try to find the one that fit the best and use that to convert what we can,
	 * replacing any byte we can't convert with a '?' */
	
	if ((cd = e_iconv_open ("UTF-8", best)) == (iconv_t) -1) {
		/* this shouldn't happen... but if we are here, then
		 * it did...  the only thing we can do at this point
		 * is replace the 8bit garbage and pray */
		register const char *inptr = text;
		const char *inend = inptr + len;
		
		outbuf = out;
		
		while (inptr < inend) {
			if (is_ascii (*inptr))
				*outbuf++ = *inptr++;
			else
				*outbuf++ = '?';
		}
		
		*outbuf = '\0';
		
		return out;
	}
	
	outleft = outlen;
	outbuf = out;
	inleft = len;
	inbuf = text;
	
	do {
		rc = iconv (cd, (char **) &inbuf, &inleft, &outbuf, &outleft);
		if (rc == (size_t) -1) {
			if (errno == EINVAL) {
				/* incomplete sequence at the end of the input buffer */
				break;
			}
			
			if (errno == E2BIG) {
				rc = outbuf - out;
				outlen += inleft * 2 + 16;
				out = g_realloc (out, outlen + 1);
				outleft = outlen - rc;
				outbuf = out + rc;
			} else {
				*outbuf++ = '?';
				outleft--;
				inleft--;
				inbuf++;
			}
		}
	} while (inleft > 0);
	
	iconv (cd, NULL, NULL, &outbuf, &outleft);
	*outbuf = '\0';
	
	e_iconv_close (cd);
	
	return out;
}

#define is_rfc2047_encoded_word(atom, len) (len >= 7 && !strncmp (atom, "=?", 2) && !strncmp (atom + len - 2, "?=", 2))

/* decode an rfc2047 encoded-word token */
static char *
rfc2047_decode_word (const char *in, size_t inlen, const char *default_charset)
{
	const unsigned char *instart = (const unsigned char *) in;
	const register unsigned char *inptr = instart + 2;
	const unsigned char *inend = instart + inlen - 2;
	unsigned char *decoded;
	const char *charset;
	char *charenc, *p;
	guint32 save = 0;
	ssize_t declen;
	int state = 0;
	size_t len;
	iconv_t cd;
	char *buf;
	
	/* skip over the charset */
	if (inlen < 8 || !(inptr = memchr (inptr, '?', inend - inptr)) || inptr[2] != '?')
		return NULL;
	
	inptr++;
	
	switch (*inptr) {
	case 'B':
	case 'b':
		inptr += 2;
		decoded = g_alloca (inend - inptr);
		declen = camel_lite_base64_decode_step ((unsigned char *) inptr, inend - inptr, decoded, &state, &save);
		break;
	case 'Q':
	case 'q':
		inptr += 2;
		decoded = g_alloca (inend - inptr);
		declen = quoted_decode (inptr, inend - inptr, decoded);
		
		if (declen == -1) {
			d(fprintf (stderr, "encountered broken 'Q' encoding\n"));
			return NULL;
		}
		break;
	default:
		d(fprintf (stderr, "unknown encoding\n"));
		return NULL;
	}

	/* never return empty string, return rather NULL */
	if (!declen)
		return NULL;

	len = (inptr - 3) - (instart + 2);
	charenc = g_alloca (len + 1);
	memcpy (charenc, in + 2, len);
	charenc[len] = '\0';
	charset = charenc;
	
	/* rfc2231 updates rfc2047 encoded words...
	 * The ABNF given in RFC 2047 for encoded-words is:
	 *   encoded-word := "=?" charset "?" encoding "?" encoded-text "?="
	 * This specification changes this ABNF to:
	 *   encoded-word := "=?" charset ["*" language] "?" encoding "?" encoded-text "?="
	 */
	
	/* trim off the 'language' part if it's there... */
	if ((p = strchr (charset, '*')))
		*p = '\0';

	/* slight optimization? */
	if (!g_ascii_strcasecmp (charset, "UTF-8")) {
		char *str_8bit;

		str_8bit = decode_8bit ((char *) decoded, declen, default_charset);
		p = (char *) str_8bit;
		len = strlen (str_8bit);
		
		while (!g_utf8_validate (p, len, (const char **) &p)) {
			len = declen - (p - (char *) decoded);
			*p = '?';
		}
		
		return str_8bit;
	}
	
	if (charset[0])
		charset = e_iconv_charset_name (charset);
	
	if (!charset[0] || (cd = e_iconv_open ("UTF-8", charset)) == (iconv_t) -1) {
		w(g_warning ("Cannot convert from %s to UTF-8, header display may "
			     "be corrupt: %s", charset[0] ? charset : "unspecified charset",
			     g_strerror (errno)));
		
		return decode_8bit ((char *) decoded, declen, default_charset);
	}
	
	buf = camel_lite_iconv_strndup (cd, (char *) decoded, declen);
	e_iconv_close (cd);
	
	if (buf != NULL)
		return buf;
	
	w(g_warning ("Failed to convert \"%.*s\" to UTF-8, display may be "
		     "corrupt: %s", declen, decoded, g_strerror (errno)));
	
	return decode_8bit ((char *) decoded, declen, charset);
}

/* ok, a lot of mailers are BROKEN, and send iso-latin1 encoded
   headers, when they should just be sticking to US-ASCII
   according to the rfc's.  Anyway, since the conversion to utf-8
   is trivial, just do it here without iconv */
static GString *
append_latin1 (GString *out, const char *in, size_t len)
{
	unsigned int c;

	while (len) {
		c = (unsigned int)*in++;
		len--;
		if (c & 0x80) {
			out = g_string_append_c (out, 0xc0 | ((c >> 6) & 0x3));  /* 110000xx */
			out = g_string_append_c (out, 0x80 | (c & 0x3f));        /* 10xxxxxx */
		} else {
			out = g_string_append_c (out, c);
		}
	}
	return out;
}

static int
append_8bit (GString *out, const char *inbuf, size_t inlen, const char *charset)
{
	char *outbase, *outbuf;
	size_t outlen;
	iconv_t ic;

	ic = e_iconv_open ("UTF-8", charset);
	if (ic == (iconv_t) -1)
		return FALSE;

	outlen = inlen * 6 + 16;
	outbuf = outbase = g_malloc(outlen);

	if (e_iconv (ic, &inbuf, &inlen, &outbuf, &outlen) == (size_t) -1) {
		w(g_warning("Conversion to '%s' failed: %s", charset, strerror (errno)));
		g_free(outbase);
		e_iconv_close (ic);
		return FALSE;
	}

	e_iconv (ic, NULL, NULL, &outbuf, &outlen);

	*outbuf = 0;
	g_string_append(out, outbase);
	g_free(outbase);
	e_iconv_close (ic);

	return TRUE;

}

static GString *
append_quoted_pair (GString *str, const char *in, size_t inlen)
{
	register const char *inptr = in;
	const char *inend = in + inlen;
	char c;

	while (inptr < inend) {
		c = *inptr++;
		if (c == '\\' && inptr < inend)
			g_string_append_c (str, *inptr++);
		else
			g_string_append_c (str, c);
	}

	return str;
}

/* decodes a simple text, rfc822 + rfc2047 */
static char *
header_decode_text (const char *in, int ctext, const char *default_charset)
{
	register const char *inptr = in;
	gboolean encoded = FALSE;
	const char *lwsp, *text;
	size_t nlwsp, n;
	gboolean ascii;
	char *decoded;
	GString *out;
	
	if (in == NULL)
		return g_strdup ("");
	
	out = g_string_sized_new (strlen (in) + 1);
	
	while (*inptr != '\0') {
		lwsp = inptr;
		while (camel_lite_mime_is_lwsp (*inptr))
			inptr++;
		
		nlwsp = (size_t) (inptr - lwsp);
		
		if (*inptr != '\0') {
			text = inptr;
			ascii = TRUE;
			
			if (!strncmp (inptr, "=?", 2)) {
				inptr += 2;
				
				/* skip past the charset (if one is even declared, sigh) */
				while (*inptr && *inptr != '?') {
					ascii = ascii && is_ascii (*inptr);
					inptr++;
				}
				
				/* sanity check encoding type */
				if (inptr[0] != '?' || !strchr ("BbQq", inptr[1]) || !inptr[1] || inptr[2] != '?')
					goto non_rfc2047;
				
				inptr += 3;
				
				/* find the end of the rfc2047 encoded word token */
				while (*inptr && strncmp (inptr, "?=", 2) != 0) {
					ascii = ascii && is_ascii (*inptr);
					inptr++;
				}
				
				if (!strncmp (inptr, "?=", 2))
					inptr += 2;
			} else {
			non_rfc2047:
				/* stop if we encounter a possible rfc2047 encoded
				 * token even if it's inside another word, sigh. */
				while (*inptr && !camel_lite_mime_is_lwsp (*inptr) &&
				       strncmp (inptr, "=?", 2) != 0) {
					ascii = ascii && is_ascii (*inptr);
					inptr++;
				}
			}
			
			n = (size_t) (inptr - text);
			if (is_rfc2047_encoded_word (text, n)) {
				if ((decoded = rfc2047_decode_word (text, n, default_charset))) {
					/* rfc2047 states that you must ignore all
					 * whitespace between encoded words */
					if (!encoded)
						g_string_append_len (out, lwsp, nlwsp);
					
					g_string_append (out, decoded);
					g_free (decoded);
					
					encoded = TRUE;
				} else {
					/* append lwsp and invalid rfc2047 encoded-word token */
					g_string_append_len (out, lwsp, nlwsp + n);
					encoded = FALSE;
				}
			} else {
				/* append lwsp */
				g_string_append_len (out, lwsp, nlwsp);
				
				/* append word token */
				if (!ascii) {
					/* *sigh* I hate broken mailers... */
					decoded = decode_8bit (text, n, default_charset);
					n = strlen (decoded);
					text = decoded;
				} else {
					decoded = NULL;
				}
				
				if (!ctext)
					g_string_append_len (out, text, n);
				else
					append_quoted_pair (out, text, n);
				
				g_free (decoded);
				
				encoded = FALSE;
			}
		} else {
			/* appending trailing lwsp */
			g_string_append_len (out, lwsp, nlwsp);
			break;
		}
	}
	
	decoded = out->str;
	g_string_free (out, FALSE);
	
	return decoded;
}


/**
 * camel_lite_header_decode_string:
 * @in: input header value string
 * @default_charset: default charset to use if improperly encoded
 *
 * Decodes rfc2047 encoded-word tokens
 *
 * Returns a string containing the UTF-8 version of the decoded header
 * value
 **/
char *
camel_lite_header_decode_string (const char *in, const char *default_charset)
{
	if (in == NULL)
		return NULL;
	
	return header_decode_text (in, FALSE, default_charset);
}


/**
 * camel_lite_header_format_ctext:
 * @in: input header value string
 * @default_charset: default charset to use if improperly encoded
 *
 * Decodes a header which contains rfc2047 encoded-word tokens that
 * may or may not be within a comment.
 *
 * Returns a string containing the UTF-8 version of the decoded header
 * value
 **/
char *
camel_lite_header_format_ctext (const char *in, const char *default_charset)
{
	if (in == NULL)
		return NULL;
	
	return header_decode_text (in, TRUE, default_charset);
}

/* how long a sequence of pre-encoded words should be less than, to attempt to
   fit into a properly folded word.  Only a guide. */
#define CAMEL_FOLD_PREENCODED (24)

/* FIXME: needs a way to cache iconv opens for different charsets? */
static void
rfc2047_encode_word(GString *outstring, const char *in, size_t len, const char *type, unsigned short safemask)
{
	iconv_t ic = (iconv_t) -1;
	char *buffer, *out, *ascii;
	size_t inlen, outlen, enclen, bufflen;
	const char *inptr, *p;
	int first = 1;

	d(printf("Converting [%d] '%.*s' to %s\n", len, len, in, type));

	/* convert utf8->encoding */
	bufflen = len * 6 + 16;
	buffer = g_alloca (bufflen);
	inlen = len;
	inptr = in;

	ascii = g_alloca (bufflen);

	if (g_ascii_strcasecmp (type, "UTF-8") != 0)
		ic = e_iconv_open (type, "UTF-8");

	while (inlen) {
		ssize_t convlen, proclen;
		int i;

		/* break up words into smaller bits, what we really want is encoded + overhead < 75,
		   but we'll just guess what that means in terms of input chars, and assume its good enough */

		out = buffer;
		outlen = bufflen;

		if (ic == (iconv_t) -1) {
			/* native encoding case, the easy one (?) */
			/* we work out how much we can convert, and still be in length */
			/* proclen will be the result of input characters that we can convert, to the nearest
			   (approximated) valid utf8 char */
			convlen = 0;
			proclen = -1;
			p = inptr;
			i = 0;
			while (p < (in+len) && convlen < (75 - strlen("=?utf-8?q?\?="))) {
				unsigned char c = *p++;

				if (c >= 0xc0)
					proclen = i;
				i++;
				if (c < 0x80)
					proclen = i;
				if (camel_lite_mime_special_table[c] & safemask)
					convlen += 1;
				else
					convlen += 3;
			}
			
			if (proclen >= 0 && proclen < i && convlen < (75 - strlen("=?utf-8?q?\?=")))
				proclen = i;
			
			/* well, we probably have broken utf8, just copy it anyway what the heck */
			if (proclen == -1) {
				w(g_warning("Appear to have truncated utf8 sequence"));
				proclen = inlen;
			}
			
			memcpy(out, inptr, proclen);
			inptr += proclen;
			inlen -= proclen;
			out += proclen;
		} else {
			/* well we could do similar, but we can't (without undue effort), we'll just break it up into
			   hopefully-small-enough chunks, and leave it at that */
			convlen = MIN(inlen, CAMEL_FOLD_PREENCODED);
			p = inptr;
			if (e_iconv (ic, &inptr, &convlen, &out, &outlen) == (size_t) -1 && errno != EINVAL) {
				w(g_warning("Conversion problem: conversion truncated: %s", strerror (errno)));
				/* blah, we include it anyway, better than infinite loop ... */
				inptr += convlen;
			} else {
				/* make sure we flush out any shift state */
				e_iconv (ic, NULL, 0, &out, &outlen);
			}
			inlen -= (inptr - p);
		}

		enclen = out-buffer;

		if (enclen) {
			/* create token */
			out = ascii;
			if (first)
				first = 0;
			else
				*out++ = ' ';
			out += sprintf (out, "=?%s?Q?", type);
			out += quoted_encode ((unsigned char *) buffer, enclen, (unsigned char *) out, safemask);
			sprintf (out, "?=");

			d(printf("converted part = %s\n", ascii));

			g_string_append (outstring, ascii);
		}
	}

	if (ic != (iconv_t) -1)
		e_iconv_close (ic);
}


/* TODO: Should this worry about quotes?? */
/**
 * camel_lite_header_encode_string:
 * @in: input string
 *
 * Encodes a 'text' header according to the rules of rfc2047.
 *
 * Returns the rfc2047 encoded header
 **/
char *
camel_lite_header_encode_string (const unsigned char *in)
{
	const unsigned char *inptr = in, *start, *word;
	gboolean last_was_encoded = FALSE;
	gboolean last_was_space = FALSE;
	const char *charset;
	int encoding;
	GString *out;
	char *outstr;

	g_return_val_if_fail (g_utf8_validate ((const gchar *) in, -1, NULL), NULL);

	if (in == NULL)
		return NULL;

	/* do a quick us-ascii check (the common case?) */
	while (*inptr) {
		if (*inptr > 127)
			break;
		inptr++;
	}
	if (*inptr == '\0')
		return g_strdup ((gchar *) in);

	/* This gets each word out of the input, and checks to see what charset
	   can be used to encode it. */
	/* TODO: Work out when to merge subsequent words, or across word-parts */
	out = g_string_new ("");
	inptr = in;
	encoding = 0;
	word = NULL;
	start = inptr;
	while (inptr && *inptr) {
		gunichar c;
		const char *newinptr;

		newinptr = g_utf8_next_char (inptr);
		c = g_utf8_get_char ((gchar *) inptr);
		if (newinptr == NULL || !g_unichar_validate (c)) {
			w(g_warning ("Invalid UTF-8 sequence encountered (pos %d, char '%c'): %s",
				     (inptr-in), inptr[0], in));
			inptr++;
			continue;
		}

		if (c < 256 && camel_lite_mime_is_lwsp (c) && !last_was_space) {
			/* we've reached the end of a 'word' */
			if (word && !(last_was_encoded && encoding)) {
				/* output lwsp between non-encoded words */
				g_string_append_len (out, (const gchar *) start, word - start);
				start = word;
			}

			switch (encoding) {
			case 0:
				g_string_append_len (out, (const char *) start, inptr - start);
				last_was_encoded = FALSE;
				break;
			case 1:
				if (last_was_encoded)
					g_string_append_c (out, ' ');

				rfc2047_encode_word (out, (const char *) start, inptr - start, "ISO-8859-1", CAMEL_MIME_IS_ESAFE);
				last_was_encoded = TRUE;
				break;
			case 2:
				if (last_was_encoded)
					g_string_append_c (out, ' ');

				if (!(charset = camel_lite_charset_best ((const char *) start, inptr - start)))
					charset = "UTF-8";
				rfc2047_encode_word (out, (const char *) start, inptr - start, charset, CAMEL_MIME_IS_ESAFE);
				last_was_encoded = TRUE;
				break;
			}

			last_was_space = TRUE;
			start = inptr;
			word = NULL;
			encoding = 0;
		} else if (c > 127 && c < 256) {
			encoding = MAX (encoding, 1);
			last_was_space = FALSE;
		} else if (c >= 256) {
			encoding = MAX (encoding, 2);
			last_was_space = FALSE;
		} else if (!camel_lite_mime_is_lwsp (c)) {
			last_was_space = FALSE;
		}

		if (!(c < 256 && camel_lite_mime_is_lwsp (c)) && !word)
			word = inptr;

		inptr = (const unsigned char *) newinptr;
	}

	if (inptr - start) {
		if (word && !(last_was_encoded && encoding)) {
			g_string_append_len (out, (const gchar *) start, word - start);
			start = word;
		}

		switch (encoding) {
		case 0:
			g_string_append_len (out, (const gchar *) start, inptr - start);
			break;
		case 1:
			if (last_was_encoded)
				g_string_append_c (out, ' ');

			rfc2047_encode_word (out, (const char *) start, inptr - start, "ISO-8859-1", CAMEL_MIME_IS_ESAFE);
			break;
		case 2:
			if (last_was_encoded)
				g_string_append_c (out, ' ');

			if (!(charset = camel_lite_charset_best ((const char *) start, inptr - start)))
				charset = "UTF-8";
			rfc2047_encode_word (out, (const char *) start, inptr - start, charset, CAMEL_MIME_IS_ESAFE);
			break;
		}
	}

	outstr = out->str;
	g_string_free (out, FALSE);

	return outstr;
}

/* apply quoted-string rules to a string */
static void
quote_word(GString *out, gboolean do_quotes, const char *start, size_t len)
{
	int i, c;

	/* TODO: What about folding on long lines? */
	if (do_quotes)
		g_string_append_c(out, '"');
	for (i=0;i<len;i++) {
		c = *start++;
		if (c == '\"' || c=='\\' || c=='\r')
			g_string_append_c(out, '\\');
		g_string_append_c(out, c);
	}
	if (do_quotes)
		g_string_append_c(out, '"');
}

/* incrementing possibility for the word type */
enum _phrase_word_t {
	WORD_ATOM,
	WORD_QSTRING,
	WORD_2047
};

struct _phrase_word {
	const unsigned char *start, *end;
	enum _phrase_word_t type;
	int encoding;
};

static gboolean
word_types_compatable (enum _phrase_word_t type1, enum _phrase_word_t type2)
{
	switch (type1) {
	case WORD_ATOM:
		return type2 == WORD_QSTRING;
	case WORD_QSTRING:
		return type2 != WORD_2047;
	case WORD_2047:
		return type2 == WORD_2047;
	default:
		return FALSE;
	}
}

/* split the input into words with info about each word
 * merge common word types clean up */
static GList *
header_encode_phrase_get_words (const unsigned char *in)
{
	const unsigned char *inptr = in, *start, *last;
	struct _phrase_word *word;
	enum _phrase_word_t type;
	int encoding, count = 0;
	GList *words = NULL;

	/* break the input into words */
	type = WORD_ATOM;
	last = inptr;
	start = inptr;
	encoding = 0;
	while (inptr && *inptr) {
		gunichar c;
		const char *newinptr;

		newinptr = g_utf8_next_char (inptr);
		c = g_utf8_get_char ((gchar *) inptr);

		if (!g_unichar_validate (c)) {
			w(g_warning ("Invalid UTF-8 sequence encountered (pos %d, char '%c'): %s",
				     (inptr - in), inptr[0], in));
			inptr++;
			continue;
		}

		inptr = (const unsigned char *) newinptr;
		if (g_unichar_isspace (c)) {
			if (count > 0) {
				word = g_new0 (struct _phrase_word, 1);
				word->start = start;
				word->end = last;
				word->type = type;
				word->encoding = encoding;
				words = g_list_append (words, word);
				count = 0;
			}

			start = inptr;
			type = WORD_ATOM;
			encoding = 0;
		} else {
			count++;
			if (c < 128) {
				if (!camel_lite_mime_is_atom (c))
					type = MAX (type, WORD_QSTRING);
			} else if (c > 127 && c < 256) {
				type = WORD_2047;
				encoding = MAX (encoding, 1);
			} else if (c >= 256) {
				type = WORD_2047;
				encoding = MAX (encoding, 2);
			}
		}

		last = inptr;
	}

	if (count > 0) {
		word = g_new0 (struct _phrase_word, 1);
		word->start = start;
		word->end = last;
		word->type = type;
		word->encoding = encoding;
		words = g_list_append (words, word);
	}

	return words;
}

#define MERGED_WORD_LT_FOLDLEN(wordlen, type) ((type) == WORD_2047 ? (wordlen) < CAMEL_FOLD_PREENCODED : (wordlen) < (CAMEL_FOLD_SIZE - 8))

static gboolean
header_encode_phrase_merge_words (GList **wordsp)
{
	GList *wordl, *nextl, *words = *wordsp;
	struct _phrase_word *word, *next;
	gboolean merged = FALSE;

	/* scan the list, checking for words of similar types that can be merged */
	wordl = words;
	while (wordl) {
		word = wordl->data;
		nextl = g_list_next (wordl);

		while (nextl) {
			next = nextl->data;
			/* merge nodes of the same type AND we are not creating too long a string */
			if (word_types_compatable (word->type, next->type)) {
				if (MERGED_WORD_LT_FOLDLEN (next->end - word->start, MAX (word->type, next->type))) {
					/* the resulting word type is the MAX of the 2 types */
					word->type = MAX(word->type, next->type);
					word->encoding = MAX(word->encoding, next->encoding);
					word->end = next->end;
					words = g_list_remove_link (words, nextl);
					g_list_free_1 (nextl);
					g_free (next);

					nextl = g_list_next (wordl);

					merged = TRUE;
				} else {
					/* if it is going to be too long, make sure we include the
					   separating whitespace */
					word->end = next->start;
					break;
				}
			} else {
				break;
			}
		}

		wordl = g_list_next (wordl);
	}

	*wordsp = words;

	return merged;
}

/* encodes a phrase sequence (different quoting/encoding rules to strings) */
/**
 * camel_lite_header_encode_phrase:
 * @in: header to encode
 *
 * Encodes a 'phrase' header according to the rules in rfc2047.
 *
 * Returns the encoded 'phrase'
 **/
char *
camel_lite_header_encode_phrase (const unsigned char *in)
{
	struct _phrase_word *word = NULL, *last_word = NULL;
	GList *words, *wordl;
	const char *charset;
	GString *out;
	char *outstr;

	if (in == NULL)
		return NULL;

	words = header_encode_phrase_get_words (in);
	if (!words)
		return NULL;

	while (header_encode_phrase_merge_words (&words))
		;

	out = g_string_new ("");

	/* output words now with spaces between them */
	wordl = words;
	while (wordl) {
		const char *start;
		size_t len;

		word = wordl->data;

		/* append correct number of spaces between words */
		if (last_word && !(last_word->type == WORD_2047 && word->type == WORD_2047)) {
			/* one or both of the words are not encoded so we write the spaces out untouched */
			len = word->start - last_word->end;
			out = g_string_append_len (out, (char *) last_word->end, len);
		}

		switch (word->type) {
		case WORD_ATOM:
			out = g_string_append_len (out, (char *) word->start, word->end - word->start);
			break;
		case WORD_QSTRING:
			quote_word (out, TRUE, (char *) word->start, word->end - word->start);
			break;
		case WORD_2047:
			if (last_word && last_word->type == WORD_2047) {
				/* include the whitespace chars between these 2 words in the
                                   resulting rfc2047 encoded word. */
				len = word->end - last_word->end;
				start = (const char *) last_word->end;

				/* encoded words need to be separated by linear whitespace */
				g_string_append_c (out, ' ');
			} else {
				len = word->end - word->start;
				start = (const char *) word->start;
			}

			if (word->encoding == 1) {
				rfc2047_encode_word (out, start, len, "ISO-8859-1", CAMEL_MIME_IS_PSAFE);
			} else {
				if (!(charset = camel_lite_charset_best (start, len)))
					charset = "UTF-8";
				rfc2047_encode_word (out, start, len, charset, CAMEL_MIME_IS_PSAFE);
			}
			break;
		}

		g_free (last_word);
		wordl = g_list_next (wordl);

		last_word = word;
	}

	/* and we no longer need the list */
	g_free (word);
	g_list_free (words);

	outstr = out->str;
	g_string_free (out, FALSE);

	return outstr;
}


/* these are all internal parser functions */

static char *
decode_token (const char **in)
{
	const char *inptr = *in;
	const char *start;

	header_decode_lwsp (&inptr);
	start = inptr;
	while (camel_lite_mime_is_ttoken (*inptr))
		inptr++;
	if (inptr > start) {
		*in = inptr;
		return g_strndup (start, inptr - start);
	} else {
		return NULL;
	}
}


/**
 * camel_lite_header_token_decode:
 * @in: input string
 *
 * Gets the first token in the string according to the rules of
 * rfc0822.
 *
 * Returns a new string containing the first token in @in
 **/
char *
camel_lite_header_token_decode(const char *in)
{
	if (in == NULL)
		return NULL;

	return decode_token(&in);
}

/*
   <"> * ( <any char except <"> \, cr  /  \ <any char> ) <">
*/
static char *
header_decode_quoted_string(const char **in)
{
	const char *inptr = *in;
	char *out = NULL, *outptr;
	size_t outlen;
	int c;

	header_decode_lwsp(&inptr);
	if (*inptr == '"') {
		const char *intmp;
		int skip = 0;

		/* first, calc length */
		inptr++;
		intmp = inptr;
		while ( (c = *intmp++) && c!= '"') {
			if (c=='\\' && *intmp) {
				intmp++;
				skip++;
			}
		}
		outlen = intmp-inptr-skip;
		out = outptr = g_malloc(outlen+1);
		while ( (c = *inptr) && c!= '"') {
			inptr++;
			if (c=='\\' && *inptr) {
				c = *inptr++;
			}
			*outptr++ = c;
		}
		if (c)
			inptr++;
		*outptr = '\0';
	}
	*in = inptr;
	return out;
}

static char *
header_decode_atom(const char **in)
{
	const char *inptr = *in, *start;

	header_decode_lwsp(&inptr);
	start = inptr;
	while (camel_lite_mime_is_atom(*inptr))
		inptr++;
	*in = inptr;
	if (inptr > start)
		return g_strndup(start, inptr-start);
	else
		return NULL;
}

static char *
header_decode_word (const char **in)
{
	const char *inptr = *in;

	header_decode_lwsp (&inptr);
	if (*inptr == '"') {
		*in = inptr;
		return header_decode_quoted_string (in);
	} else {
		*in = inptr;
		return header_decode_atom (in);
	}
}

static char *
header_decode_value(const char **in)
{
	const char *inptr = *in;

	header_decode_lwsp(&inptr);
	if (*inptr == '"') {
		d(printf("decoding quoted string\n"));
		return header_decode_quoted_string(in);
	} else if (camel_lite_mime_is_ttoken(*inptr)) {
		d(printf("decoding token\n"));
		/* this may not have the right specials for all params? */
		return decode_token(in);
	}
	return NULL;
}

/* should this return -1 for no int? */

/**
 * camel_lite_header_decode_int:
 * @in: pointer to input string
 *
 * Extracts an integer token from @in and updates the pointer to point
 * to after the end of the integer token (sort of like strtol).
 *
 * Returns the int value
 **/
int
camel_lite_header_decode_int(const char **in)
{
	const char *inptr = *in;
	int c, v=0;

	header_decode_lwsp(&inptr);
	while ( (c=*inptr++ & 0xff)
		&& isdigit(c) ) {
		v = v*10+(c-'0');
	}
	*in = inptr-1;
	return v;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : tolower (c) - 'a' + 10)

static char *
hex_decode (const char *in, size_t len)
{
	const unsigned char *inend = (const unsigned char *) (in + len);
	unsigned char *inptr, *outptr;
	char *outbuf;

	outbuf = (char *) g_malloc (len + 1);
	outptr = (unsigned char *) outbuf;

	inptr = (unsigned char *) in;
	while (inptr < inend) {
		if (*inptr == '%') {
			if (isxdigit (inptr[1]) && isxdigit (inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else
				*outptr++ = *inptr++;
		} else
			*outptr++ = *inptr++;
	}

	*outptr = '\0';

	return outbuf;
}

/* Tries to convert @in @from charset @to charset.  Any failure, we get no data out rather than partial conversion */
static char *
header_convert(const char *to, const char *from, const char *in, size_t inlen)
{
	iconv_t ic;
	size_t outlen, ret;
	char *outbuf, *outbase, *result = NULL;

	ic = e_iconv_open(to, from);
	if (ic == (iconv_t) -1)
		return NULL;

	outlen = inlen * 6 + 16;
	outbuf = outbase = g_malloc(outlen);

	ret = e_iconv(ic, &in, &inlen, &outbuf, &outlen);
	if (ret != (size_t) -1) {
		e_iconv(ic, NULL, 0, &outbuf, &outlen);
		*outbuf = '\0';
		result = g_strdup(outbase);
	}
	e_iconv_close(ic);
	g_free(outbase);

	return result;
}

/* an rfc2184 encoded string looks something like:
 * us-ascii'en'This%20is%20even%20more%20
 */

static char *
rfc2184_decode (const char *in, size_t len)
{
	const char *inptr = in;
	const char *inend = in + len;
	const char *charset;
	char *decoded, *decword, *encoding;

	inptr = memchr (inptr, '\'', len);
	if (!inptr)
		return NULL;

	encoding = g_alloca(inptr-in+1);
	memcpy(encoding, in, inptr-in);
	encoding[inptr-in] = 0;
	charset = e_iconv_charset_name (encoding);

	inptr = memchr (inptr + 1, '\'', inend - inptr - 1);
	if (!inptr)
		return NULL;
	inptr++;
	if (inptr >= inend)
		return NULL;

	decword = hex_decode (inptr, inend - inptr);
	decoded = header_convert("UTF-8", charset, decword, strlen(decword));
	g_free(decword);

	return decoded;
}


/**
 * camel_lite_header_param:
 * @params: parameters
 * @name: name of param to find
 *
 * Searches @params for a param named @name and gets the value.
 *
 * Returns the value of the @name param
 **/
char *
camel_lite_header_param (struct _camel_lite_header_param *p, const char *name)
{
	while (p && p->name && g_ascii_strcasecmp (p->name, name) != 0)
		p = p->next;
	if (p)
		return p->value;
	return NULL;
}


/**
 * camel_lite_header_set_param:
 * @paramsp: poinetr to a list of params
 * @name: name of param to set
 * @value: value to set
 *
 * Set a parameter in the list.
 *
 * Returns the set param
 **/
struct _camel_lite_header_param *
camel_lite_header_set_param (struct _camel_lite_header_param **l, const char *name, const char *value)
{
	struct _camel_lite_header_param *p = (struct _camel_lite_header_param *) *l, *pn;

	if (name == NULL)
		return NULL;

	while (p && p->next) {
		pn = p->next;
		if (!g_ascii_strcasecmp (pn->name, name)) {
			g_free (pn->value);
			if (value) {
				pn->value = g_strdup (value);
				return pn;
			} else {
				p->next = pn->next;
				g_free (pn->name);
				g_free (pn);
				return NULL;
			}
		}
		p = pn;
	}

	if (value == NULL)
		return NULL;

	pn = g_malloc (sizeof (*pn));
	pn->next = 0;
	pn->name = g_strdup (name);
	pn->value = g_strdup (value);

	if (p) {
		p->next = pn;
	} else {
		*l = pn;
	}

	return pn;
}


/**
 * camel_lite_content_type_param:
 * @content_type: a #CamelContentType
 * @name: name of param to find
 *
 * Searches the params on s #CamelContentType for a param named @name
 * and gets the value.
 *
 * Returns the value of the @name param
 **/
const char *
camel_lite_content_type_param (CamelContentType *t, const char *name)
{
	if (t==NULL)
		return NULL;
	return camel_lite_header_param (t->params, name);
}


/**
 * camel_lite_content_type_set_param:
 * @content_type: a #CamelContentType
 * @name: name of param to set
 * @value: value of param to set
 *
 * Set a parameter on @content_type.
 **/
void
camel_lite_content_type_set_param (CamelContentType *t, const char *name, const char *value)
{
	camel_lite_header_set_param (&t->params, name, value);
}


/**
 * camel_lite_content_type_is:
 * @content_type: A content type specifier, or %NULL.
 * @type: A type to check against.
 * @subtype: A subtype to check against, or "*" to match any subtype.
 *
 * The subtype of "*" will match any subtype.  If @ct is %NULL, then
 * it will match the type "text/plain".
 *
 * Returns %TRUE if the content type @ct is of type @type/@subtype or
 * %FALSE otherwise
 **/
int
camel_lite_content_type_is(CamelContentType *ct, const char *type, const char *subtype)
{
	/* no type == text/plain or text/"*" */
	if (ct==NULL || (ct->type == NULL && ct->subtype == NULL)) {
		return (!g_ascii_strcasecmp(type, "text")
			&& (!g_ascii_strcasecmp(subtype, "plain")
			    || !strcmp(subtype, "*")));
	}

	return (ct->type != NULL
		&& (!g_ascii_strcasecmp(ct->type, type)
		    && ((ct->subtype != NULL
			 && !g_ascii_strcasecmp(ct->subtype, subtype))
			|| !strcmp("*", subtype))));
}


/**
 * camel_lite_header_param_list_free:
 * @params: a list of params
 *
 * Free the list of params.
 **/
void
camel_lite_header_param_list_free(struct _camel_lite_header_param *p)
{
	struct _camel_lite_header_param *n;

	while (p) {
		n = p->next;
		g_free(p->name);
		g_free(p->value);
		g_free(p);
		p = n;
	}
}


/**
 * camel_lite_content_type_new:
 * @type: the major type of the new content-type
 * @subtype: the subtype
 *
 * Create a new #CamelContentType.
 *
 * Returns the new #CamelContentType
 **/
CamelContentType *
camel_lite_content_type_new(const char *type, const char *subtype)
{
	CamelContentType *t;

	t = g_slice_new (CamelContentType);
	t->type = g_strdup(type);
	t->subtype = g_strdup(subtype);
	t->params = NULL;
	t->refcount = 1;

	return t;
}


/**
 * camel_lite_content_type_ref:
 * @content_type: a #CamelContentType
 *
 * Refs the content type.
 **/
void
camel_lite_content_type_ref(CamelContentType *ct)
{
	if (ct)
		ct->refcount++;
}


/**
 * camel_lite_content_type_unref:
 * @content_type: a #CamelContentType
 *
 * Unrefs, and potentially frees, the content type.
 **/
void
camel_lite_content_type_unref(CamelContentType *ct)
{
	if (ct) {
		if (ct->refcount <= 1) {
			camel_lite_header_param_list_free(ct->params);
			g_free(ct->type);
			g_free(ct->subtype);
			g_slice_free (CamelContentType, ct);
			ct = NULL;
		} else {
			ct->refcount--;
		}
	}
}

/* for decoding email addresses, canonically */
static char *
header_decode_domain(const char **in)
{
	const char *inptr = *in;
	int go = TRUE;
	char *ret;
	GString *domain = g_string_new("");

	/* domain ref | domain literal */
	header_decode_lwsp(&inptr);
	while (go) {
		if (*inptr == '[') { /* domain literal */
			domain = g_string_append_c(domain, '[');
			inptr++;
			header_decode_lwsp(&inptr);
			while (*inptr && camel_lite_mime_is_dtext (*inptr)) {
				domain = g_string_append_c(domain, *inptr);
				inptr++;
			}
			if (*inptr == ']') {
				domain = g_string_append_c(domain, ']');
				inptr++;
			} else {
				w(g_warning("closing ']' not found in domain: %s", *in));
			}
		} else {
			char *a = header_decode_atom(&inptr);
			if (a) {
				domain = g_string_append(domain, a);
				g_free(a);
			} else {
				w(g_warning("missing atom from domain-ref"));
				break;
			}
		}
		header_decode_lwsp(&inptr);
		if (*inptr == '.') { /* next sub-domain? */
			domain = g_string_append_c(domain, '.');
			inptr++;
			header_decode_lwsp(&inptr);
		} else
			go = FALSE;
	}

	*in = inptr;

	ret = domain->str;
	g_string_free(domain, FALSE);
	return ret;
}

static char *
header_decode_addrspec(const char **in)
{
	const char *inptr = *in;
	char *word;
	GString *addr = g_string_new("");

	header_decode_lwsp(&inptr);

	/* addr-spec */
	word = header_decode_word (&inptr);
	if (word) {
		addr = g_string_append(addr, word);
		header_decode_lwsp(&inptr);
		g_free(word);
		while (*inptr == '.' && word) {
			inptr++;
			addr = g_string_append_c(addr, '.');
			word = header_decode_word (&inptr);
			if (word) {
				addr = g_string_append(addr, word);
				header_decode_lwsp(&inptr);
				g_free(word);
			} else {
				w(g_warning("Invalid address spec: %s", *in));
			}
		}
		if (*inptr == '@') {
			inptr++;
			addr = g_string_append_c(addr, '@');
			word = header_decode_domain(&inptr);
			if (word) {
				addr = g_string_append(addr, word);
				g_free(word);
			} else {
				w(g_warning("Invalid address, missing domain: %s", *in));
			}
		} else {
			w(g_warning("Invalid addr-spec, missing @: %s", *in));
		}
	} else {
		w(g_warning("invalid addr-spec, no local part"));
		g_string_free(addr, TRUE);

		return NULL;
	}

	/* FIXME: return null on error? */

	*in = inptr;
	word = addr->str;
	g_string_free(addr, FALSE);
	return word;
}

/*
  address:
   word *('.' word) @ domain |
   *(word) '<' [ *('@' domain ) ':' ] word *( '.' word) @ domain |

   1*word ':' [ word ... etc (mailbox, as above) ] ';'
 */

/* mailbox:
   word *( '.' word ) '@' domain
   *(word) '<' [ *('@' domain ) ':' ] word *( '.' word) @ domain
   */

static void
fix_broken_rfc2047 (const char **in)
{
	gchar *p, *broken_start, *r;
	gboolean encoded = FALSE;
	gboolean broken = TRUE;

	p = *in;

	while (*p != '\0') {
		if (!encoded) {
			if (*p == '=' && *(p+1) == '?') {
				p++;
				encoded = TRUE;
				broken = FALSE;
			}
		} else if (!broken) {
			if (*p == '<') {
				broken = TRUE;
				broken_start = p;
			} else if ((*p == '?') && (*(p+1) == '=')) {
				encoded = FALSE;
				p++;
			}
		} else {
			if ((*p == '?') && (*(p+1) == '=')) {
				memmove (broken_start + 2, broken_start, p - broken_start);
				broken_start[0] = '?';
				broken_start[1] = '=';
				broken = FALSE;
				encoded = FALSE;
			}
		}
		p++;
	}

}

static struct _camel_lite_header_address *
header_decode_mailbox(const char **in, const char *charset)
{
	const char *inptr = *in;
	char *pre;
	int closeme = FALSE;
	GString *addr;
	GString *name = NULL;
	struct _camel_lite_header_address *address = NULL;
	const char *comment = NULL;

	addr = g_string_new("");

	if (strncmp (inptr, "=?", 2)==0) {
		/* check if we've got a wrong string as gmail sends and fix it.*/
		fix_broken_rfc2047 (in);
	}

	/* for each address */
	pre = header_decode_word (&inptr);
	header_decode_lwsp(&inptr);
	if (!(*inptr == '.' || *inptr == '@' || *inptr==',' || *inptr=='\0')) {
		/* ',' and '\0' required incase it is a simple address, no @ domain part (buggy writer) */
		name = g_string_new ("");
		while (pre) {
			char *text, *last;

			/* perform internationalised decoding, and append */
			text = camel_lite_header_decode_string (pre, charset);
			g_string_append (name, text);
			last = pre;
			g_free(text);

			pre = header_decode_word (&inptr);
			if (pre) {
				size_t l = strlen (last);
				size_t p = strlen (pre);

				/* dont append ' ' between sucsessive encoded words */
				if ((l>6 && last[l-2] == '?' && last[l-1] == '=')
				    && (p>6 && pre[0] == '=' && pre[1] == '?')) {
					/* dont append ' ' */
				} else {
					name = g_string_append_c(name, ' ');
				}
			} else {
				/* Fix for stupidly-broken-mailers that like to put '.''s in names unquoted */
				/* see bug #8147 */
				while (!pre && *inptr && *inptr != '<') {
					w(g_warning("Working around stupid mailer bug #5: unescaped characters in names"));
					name = g_string_append_c(name, *inptr++);
					pre = header_decode_word (&inptr);
				}
			}
			g_free(last);
		}
		header_decode_lwsp(&inptr);
		if (*inptr == '<') {
			closeme = TRUE;
		try_address_again:
			inptr++;
			header_decode_lwsp(&inptr);
			if (*inptr == '@') {
				while (*inptr == '@') {
					inptr++;
					header_decode_domain(&inptr);
					header_decode_lwsp(&inptr);
					if (*inptr == ',') {
						inptr++;
						header_decode_lwsp(&inptr);
					}
				}
				if (*inptr == ':') {
					inptr++;
				} else {
					w(g_warning("broken route-address, missing ':': %s", *in));
				}
			}
			pre = header_decode_word (&inptr);
			/*header_decode_lwsp(&inptr);*/
		} else {
			w(g_warning("broken address? %s", *in));
		}
	}

	if (pre) {
		addr = g_string_append(addr, pre);
	} else {
		w(g_warning("No local-part for email address: %s", *in));
	}

	/* should be at word '.' localpart */
	while (*inptr == '.' && pre) {
		inptr++;
		g_free(pre);
		pre = header_decode_word (&inptr);
		addr = g_string_append_c(addr, '.');
		if (pre)
			addr = g_string_append(addr, pre);
		comment = inptr;
		header_decode_lwsp(&inptr);
	}
	g_free(pre);

	/* now at '@' domain part */
	if (*inptr == '@') {
		char *dom;

		inptr++;
		addr = g_string_append_c(addr, '@');
		comment = inptr;
		dom = header_decode_domain(&inptr);
		addr = g_string_append(addr, dom);
		g_free(dom);
	} else if (*inptr != '>' || !closeme) {
		/* If we get a <, the address was probably a name part, lets try again shall we? */
		/* Another fix for seriously-broken-mailers */
		if (*inptr && *inptr != ',') {
			char *text;

			w(g_warning("We didn't get an '@' where we expected in '%s', trying again", *in));
			w(g_warning("Name is '%s', Addr is '%s' we're at '%s'\n", name?name->str:"<UNSET>", addr->str, inptr));

			/* need to keep *inptr, as try_address_again will drop the current character */
			if (*inptr == '<')
				closeme = TRUE;
			else
				g_string_append_c(addr, *inptr);

			/* check for address is encoded word ... */
			text = camel_lite_header_decode_string(addr->str, charset);
			if (name == NULL) {
				name = addr;
				addr = g_string_new("");
				if (text) {
					g_string_truncate(name, 0);
					g_string_append(name, text);
				}
			}/* else {
				g_string_append(name, text?text:addr->str);
				g_string_truncate(addr, 0);
			}*/
			g_free(text);

			/* or maybe that we've added up a bunch of broken bits to make an encoded word */
			if ((text = rfc2047_decode_word (name->str, name->len, charset))) {
				g_string_truncate(name, 0);
				g_string_append(name, text);
				g_free(text);
			}

			goto try_address_again;
		}
		w(g_warning("invalid address, no '@' domain part at %c: %s", *inptr, *in));
	}

	if (closeme) {
		header_decode_lwsp(&inptr);
		if (*inptr == '>') {
			inptr++;
		} else {
			w(g_warning("invalid route address, no closing '>': %s", *in));
		}
	} else if (name == NULL && comment != NULL && inptr>comment) { /* check for comment after address */
		char *text, *tmp;
		const char *comstart, *comend;

		/* this is a bit messy, we go from the last known position, because
		   decode_domain/etc skip over any comments on the way */
		/* FIXME: This wont detect comments inside the domain itself,
		   but nobody seems to use that feature anyway ... */

		d(printf("checking for comment from '%s'\n", comment));

		comstart = strchr(comment, '(');
		if (comstart) {
			comstart++;
			header_decode_lwsp(&inptr);
			comend = inptr-1;
			while (comend > comstart && comend[0] != ')')
				comend--;

			if (comend > comstart) {
				d(printf("  looking at subset '%.*s'\n", comend-comstart, comstart));
				tmp = g_strndup (comstart, comend-comstart);
				text = camel_lite_header_decode_string (tmp, charset);
				name = g_string_new (text);
				g_free (tmp);
				g_free (text);
			}
		}
	}

	*in = inptr;

	if (addr->len > 0) {
		if (!g_utf8_validate (addr->str, addr->len, NULL)) {
			/* workaround for invalid addr-specs containing 8bit chars (see bug #42170 for details) */
			const char *locale_charset;
			GString *out;

			locale_charset = e_iconv_locale_charset ();

			out = g_string_new ("");

			if ((charset == NULL || !append_8bit (out, addr->str, addr->len, charset))
			    && (locale_charset == NULL || !append_8bit (out, addr->str, addr->len, locale_charset)))
				append_latin1 (out, addr->str, addr->len);

			g_string_free (addr, TRUE);
			addr = out;
		}

		address = camel_lite_header_address_new_name(name ? name->str : "", addr->str);
	}

	d(printf("got mailbox: %s\n", addr->str));

	g_string_free(addr, TRUE);
	if (name)
		g_string_free(name, TRUE);

	return address;
}

static struct _camel_lite_header_address *
header_decode_address(const char **in, const char *charset)
{
	const char *inptr = *in;
	char *pre;
	GString *group = g_string_new("");
	struct _camel_lite_header_address *addr = NULL, *member;

	/* pre-scan, trying to work out format, discard results */
	header_decode_lwsp(&inptr);
	while ((pre = header_decode_word (&inptr))) {
		group = g_string_append(group, pre);
		group = g_string_append(group, " ");
		g_free(pre);
	}
	header_decode_lwsp(&inptr);
	if (*inptr == ':') {
		d(printf("group detected: %s\n", group->str));
		addr = camel_lite_header_address_new_group(group->str);
		/* that was a group spec, scan mailbox's */
		inptr++;
		/* FIXME: check rfc 2047 encodings of words, here or above in the loop */
		header_decode_lwsp(&inptr);
		if (*inptr != ';') {
			int go = TRUE;
			do {
				member = header_decode_mailbox(&inptr, charset);
				if (member)
					camel_lite_header_address_add_member(addr, member);
				header_decode_lwsp(&inptr);
				if (*inptr == ',')
					inptr++;
				else
					go = FALSE;
			} while (go);
			if (*inptr == ';') {
				inptr++;
			} else {
				w(g_warning("Invalid group spec, missing closing ';': %s", *in));
			}
		} else {
			inptr++;
		}
		*in = inptr;
	} else {
		addr = header_decode_mailbox(in, charset);
	}

	g_string_free(group, TRUE);

	return addr;
}

static char *
header_msgid_decode_internal(const char **in)
{
	const char *inptr = *in;
	char *msgid = NULL;

	d(printf("decoding Message-ID: '%s'\n", *in));

	header_decode_lwsp(&inptr);
	if (*inptr == '<') {
		inptr++;
		header_decode_lwsp(&inptr);
		msgid = header_decode_addrspec(&inptr);
		if (msgid) {
			header_decode_lwsp(&inptr);
			if (*inptr == '>') {
				inptr++;
			} else {
				w(g_warning("Missing closing '>' on message id: %s", *in));
			}
		} else {
			w(g_warning("Cannot find message id in: %s", *in));
		}
	} else {
		w(g_warning("missing opening '<' on message id: %s", *in));
	}
	*in = inptr;

	return msgid;
}


/**
 * camel_lite_header_msgid_decode:
 * @in: input string
 *
 * Extract a message-id token from @in.
 *
 * Returns the msg-id
 **/
char *
camel_lite_header_msgid_decode(const char *in)
{
	if (in == NULL)
		return NULL;

	return header_msgid_decode_internal(&in);
}


/**
 * camel_lite_header_contentid_decode:
 * @in: input string
 *
 * Extract a content-id from @in.
 *
 * Returns the extracted content-id
 **/
char *
camel_lite_header_contentid_decode (const char *in)
{
	const char *inptr = in;
	gboolean at = FALSE;
	GString *addr;
	char *buf;

	d(printf("decoding Content-ID: '%s'\n", in));

	header_decode_lwsp (&inptr);

	/* some lame mailers quote the Content-Id */
	if (*inptr == '"')
		inptr++;

	/* make sure the content-id is not "" which can happen if we get a
	 * content-id such as <.@> (which Eudora likes to use...) */
	if ((buf = camel_lite_header_msgid_decode (inptr)) != NULL && *buf)
		return buf;

	g_free (buf);

	/* ugh, not a valid msg-id - try to get something useful out of it then? */
	inptr = in;
	header_decode_lwsp (&inptr);
	if (*inptr == '<') {
		inptr++;
		header_decode_lwsp (&inptr);
	}

	/* Eudora has been known to use <.@> as a content-id */
	if (!(buf = header_decode_word (&inptr)) && !strchr (".@", *inptr))
		return NULL;

	addr = g_string_new ("");
	header_decode_lwsp (&inptr);
	while (buf != NULL || *inptr == '.' || (*inptr == '@' && !at)) {
		if (buf != NULL) {
			g_string_append (addr, buf);
			g_free (buf);
			buf = NULL;
		}

		if (!at) {
			if (*inptr == '.') {
				g_string_append_c (addr, *inptr++);
				buf = header_decode_word (&inptr);
			} else if (*inptr == '@') {
				g_string_append_c (addr, *inptr++);
				buf = header_decode_word (&inptr);
				at = TRUE;
			}
		} else if (strchr (".[]", *inptr)) {
			g_string_append_c (addr, *inptr++);
			buf = header_decode_atom (&inptr);
		}

		header_decode_lwsp (&inptr);
	}

	buf = addr->str;
	g_string_free (addr, FALSE);

	return buf;
}

void
camel_lite_header_references_list_append_asis(struct _camel_lite_header_references **list, char *ref)
{
	struct _camel_lite_header_references *w = (struct _camel_lite_header_references *)list, *n;
	while (w->next)
		w = w->next;
	n = g_malloc(sizeof(*n));
	n->id = ref;
	n->next = 0;
	w->next = n;
}

int
camel_lite_header_references_list_size(struct _camel_lite_header_references **list)
{
	int count = 0;
	struct _camel_lite_header_references *w = *list;
	while (w) {
		count++;
		w = w->next;
	}
	return count;
}

void
camel_lite_header_references_list_clear(struct _camel_lite_header_references **list)
{
	struct _camel_lite_header_references *w = *list, *n;
	while (w) {
		n = w->next;
		g_free(w->id);
		g_free(w);
		w = n;
	}
	*list = NULL;
}

static void
header_references_decode_single (const char **in, struct _camel_lite_header_references **head)
{
	struct _camel_lite_header_references *ref;
	const char *inptr = *in;
	char *id, *word;

	while (*inptr) {
		header_decode_lwsp (&inptr);
		if (*inptr == '<') {
			id = header_msgid_decode_internal (&inptr);
			if (id) {
				ref = g_malloc (sizeof (struct _camel_lite_header_references));
				ref->next = *head;
				ref->id = id;
				*head = ref;
				break;
			}
		} else {
			word = header_decode_word (&inptr);
			if (word)
				g_free (word);
			else if (*inptr != '\0')
				inptr++; /* Stupid mailer tricks */
		}
	}

	*in = inptr;
}

/* TODO: why is this needed?  Can't the other interface also work? */
struct _camel_lite_header_references *
camel_lite_header_references_inreplyto_decode (const char *in)
{
	struct _camel_lite_header_references *ref = NULL;

	if (in == NULL || in[0] == '\0')
		return NULL;

	header_references_decode_single (&in, &ref);

	return ref;
}

/* generate a list of references, from most recent up */
struct _camel_lite_header_references *
camel_lite_header_references_decode (const char *in)
{
	struct _camel_lite_header_references *refs = NULL;

	if (in == NULL || in[0] == '\0')
		return NULL;

	while (*in)
		header_references_decode_single (&in, &refs);

	return refs;
}

struct _camel_lite_header_references *
camel_lite_header_references_dup(const struct _camel_lite_header_references *list)
{
	struct _camel_lite_header_references *new = NULL, *tmp;

	while (list) {
		tmp = g_new(struct _camel_lite_header_references, 1);
		tmp->next = new;
		tmp->id = g_strdup(list->id);
		new = tmp;
		list = list->next;
	}
	return new;
}

struct _camel_lite_header_address *
camel_lite_header_mailbox_decode(const char *in, const char *charset)
{
	if (in == NULL)
		return NULL;

	return header_decode_mailbox(&in, charset);
}

#ifndef MAX_HEADER_ADDRESSES
#define MAX_HEADER_ADDRESSES 2000
#endif

struct _camel_lite_header_address *
camel_lite_header_address_decode(const char *in, const char *charset)
{
	const char *inptr = in, *last;
	struct _camel_lite_header_address *list = NULL, *addr;
	int cnt = 0;

	d(printf("decoding To: '%s'\n", in));

	if (in == NULL)
		return NULL;

	header_decode_lwsp(&inptr);
	if (*inptr == 0)
		return NULL;

	do {
		last = inptr;
		addr = header_decode_address(&inptr, charset);
		if (addr)
			camel_lite_header_address_list_append(&list, addr);
		header_decode_lwsp(&inptr);
		if (*inptr == ',' || *inptr == ';')
			inptr++;
		else
			break;

		cnt++;

		if (cnt > MAX_HEADER_ADDRESSES)
			break;

	} while (inptr != last);

	if (*inptr) {
		w(g_warning("Invalid input detected at %c (%d): %s\n or at: %s", *inptr, inptr-in, in, inptr));
	}

	if (inptr == last) {
		w(g_warning("detected invalid input loop at : %s", last));
	}

	return list;
}

struct _camel_lite_header_newsgroup *
camel_lite_header_newsgroups_decode(const char *in)
{
	const char *inptr = in;
	register char c;
	struct _camel_lite_header_newsgroup *head, *last, *ng;
	const char *start;

	head = NULL;
	last = (struct _camel_lite_header_newsgroup *)&head;

	do {
		header_decode_lwsp(&inptr);
		start = inptr;
		while ((c = *inptr++) && !camel_lite_mime_is_lwsp(c) && c != ',')
			;
		if (start != inptr-1) {
			ng = g_malloc(sizeof(*ng));
			ng->newsgroup = g_strndup(start, inptr-start-1);
			ng->next = NULL;
			last->next = ng;
			last = ng;
		}
	} while (c);

	return head;
}

void
camel_lite_header_newsgroups_free(struct _camel_lite_header_newsgroup *ng)
{
	while (ng) {
		struct _camel_lite_header_newsgroup *nng = ng->next;

		g_free(ng->newsgroup);
		g_free(ng);
		ng = nng;
	}
}

/* this must be kept in sync with the header */
static const char *encodings[] = {
	"",
	"7bit",
	"8bit",
	"base64",
	"quoted-printable",
	"binary",
	"x-uuencode",
};

const char *
camel_lite_transfer_encoding_to_string (CamelTransferEncoding encoding)
{
	if (encoding >= sizeof (encodings) / sizeof (encodings[0]))
		encoding = 0;

	return encodings[encoding];
}

CamelTransferEncoding
camel_lite_transfer_encoding_from_string (const char *string)
{
	int i;

	if (string != NULL) {
		for (i = 0; i < sizeof (encodings) / sizeof (encodings[0]); i++)
			if (!g_ascii_strcasecmp (string, encodings[i]))
				return i;
	}

	return CAMEL_TRANSFER_ENCODING_DEFAULT;
}

void
camel_lite_header_mime_decode(const char *in, int *maj, int *min)
{
	const char *inptr = in;
	int major=-1, minor=-1;

	d(printf("decoding MIME-Version: '%s'\n", in));

	if (in != NULL) {
		header_decode_lwsp(&inptr);
		if (isdigit(*inptr)) {
			major = camel_lite_header_decode_int(&inptr);
			header_decode_lwsp(&inptr);
			if (*inptr == '.') {
				inptr++;
				header_decode_lwsp(&inptr);
				if (isdigit(*inptr))
					minor = camel_lite_header_decode_int(&inptr);
			}
		}
	}

	if (maj)
		*maj = major;
	if (min)
		*min = minor;

	d(printf("major = %d, minor = %d\n", major, minor));
}

struct _rfc2184_param {
	struct _camel_lite_header_param param;
	int index;
};

static int
rfc2184_param_cmp(const void *ap, const void *bp)
{
	const struct _rfc2184_param *a = *(void **)ap;
	const struct _rfc2184_param *b = *(void **)bp;
	int res;

	res = strcmp(a->param.name, b->param.name);
	if (res == 0) {
		if (a->index > b->index)
			res = 1;
		else if (a->index < b->index)
			res = -1;
	}

	return res;
}

/* NB: Steals name and value */
static struct _camel_lite_header_param *
header_append_param(struct _camel_lite_header_param *last, char *name, char *value, const char *default_charset)
{
	struct _camel_lite_header_param *node;

	/* This handles -
	    8 bit data in parameters, illegal, tries to convert using locale, or just safens it up.
	    rfc2047 ecoded parameters, illegal, decodes them anyway.  Some Outlook & Mozilla do this?
	*/
	node = g_malloc(sizeof(*node));
	last->next = node;
	node->next = NULL;
	node->name = name;
	if (strncmp(value, "=?", 2) == 0
	    && (node->value = header_decode_text(value, FALSE, NULL))) {
		g_free(value);
	} else if (g_ascii_strcasecmp (name, "boundary") != 0 && !g_utf8_validate(value, -1, NULL)) {
		const char *charset = default_charset?default_charset:e_iconv_locale_charset();

		if ((node->value = header_convert("UTF-8", charset?charset:"ISO-8859-1", value, strlen(value)))) {
			g_free(value);
		} else {
			node->value = value;
			for (;*value;value++)
				if (!isascii((unsigned char)*value))
					*value = '_';
		}
	} else
		node->value = value;

	return node;
}

static struct _camel_lite_header_param *
header_decode_param_list (const char **in, const char *default_charset)
{
	struct _camel_lite_header_param *head = NULL, *last = (struct _camel_lite_header_param *)&head;
	GPtrArray *split = NULL;
	const char *inptr = *in;
	struct _rfc2184_param *work;
	char *tmp;

	/* Dump parameters into the output list, in the order found.  RFC 2184 split parameters are kept in an array */
	header_decode_lwsp(&inptr);
	while (*inptr == ';') {
		char *name;
		char *value = NULL;

		inptr++;
		name = decode_token(&inptr);
		header_decode_lwsp(&inptr);
		if (*inptr == '=') {
			inptr++;
			value = header_decode_value(&inptr);
		}

		if (name && value) {
			char *index = strchr(name, '*');

			if (index) {
				if (index[1] == 0) {
					/* VAL*="foo", decode immediately and append */
					*index = 0;
					tmp = rfc2184_decode(value, strlen(value));
					if (tmp) {
						g_free(value);
						value = tmp;
					}
					last = header_append_param(last, name, value, default_charset);
				} else {
					/* VAL*1="foo", save for later */
					*index++ = 0;
					work = g_malloc(sizeof(*work));
					work->param.name = name;
					work->param.value = value;
					work->index = atoi(index);
					if (split == NULL)
						split = g_ptr_array_new();
					g_ptr_array_add(split, work);
				}
			} else {
				last = header_append_param(last, name, value, default_charset);
			}
		} else {
			g_free(name);
			g_free(value);
		}

		header_decode_lwsp(&inptr);
	}

	/* Rejoin any RFC 2184 split parameters in the proper order */
	/* Parameters with the same index will be concatenated in undefined order */
	if (split) {
		GString *value = g_string_new("");
		struct _rfc2184_param *first;
		int i;

		qsort(split->pdata, split->len, sizeof(split->pdata[0]), rfc2184_param_cmp);
		first = split->pdata[0];
		for (i=0;i<split->len;i++) {
			work = split->pdata[i];
			if (split->len-1 == i)
				g_string_append(value, work->param.value);
			if (split->len-1 == i || strcmp(work->param.name, first->param.name) != 0) {
				tmp = rfc2184_decode(value->str, value->len);
				if (tmp == NULL)
					tmp = g_strdup(value->str);

				last = header_append_param(last, g_strdup(first->param.name), tmp, default_charset);
				g_string_truncate(value, 0);
				first = work;
			}
			if (split->len-1 != i)
				g_string_append(value, work->param.value);
		}
		g_string_free(value, TRUE);
		for (i=0;i<split->len;i++) {
			work = split->pdata[i];
			g_free(work->param.name);
			g_free(work->param.value);
			g_free(work);
		}
		g_ptr_array_free(split, TRUE);
	}

	*in = inptr;

	return head;
}

struct _camel_lite_header_param *
camel_lite_header_param_list_decode(const char *in, const char *default_charset)
{
	if (in == NULL)
		return NULL;

	return header_decode_param_list(&in, default_charset);
}

static char *
header_encode_param (const unsigned char *in, gboolean *encoded)
{
	const unsigned char *inptr = in;
	unsigned char *outbuf = NULL;
	const char *charset;
	GString *out;
	guint32 c;
	char *str;

	*encoded = FALSE;

	g_return_val_if_fail (in != NULL, NULL);

	/* if we have really broken utf8 passed in, we just treat it as binary data */

	charset = camel_lite_charset_best((char *) in, strlen((char *) in));
	if (charset == NULL)
		return g_strdup((char *) in);

	if (g_ascii_strcasecmp(charset, "UTF-8") != 0) {
		if ((outbuf = (unsigned char *) header_convert(charset, "UTF-8", (const char *) in, strlen((char *) in))))
			inptr = outbuf;
		else
			return g_strdup((char *) in);
	}

	/* FIXME: set the 'language' as well, assuming we can get that info...? */
	out = g_string_new (charset);
	g_string_append(out, "''");

	while ( (c = *inptr++) ) {
		if (camel_lite_mime_is_attrchar(c))
			g_string_append_c (out, c);
		else
			g_string_append_printf (out, "%%%c%c", tohex[(c >> 4) & 0xf], tohex[c & 0xf]);
	}
	g_free (outbuf);

	str = out->str;
	g_string_free (out, FALSE);
	*encoded = TRUE;

	return str;
}

void
camel_lite_header_param_list_format_append (GString *out, struct _camel_lite_header_param *p)
{
	int used = out->len;

	while (p) {
		gboolean encoded = FALSE;
		gboolean quote = FALSE;
		int here = out->len;
		size_t nlen, vlen;
		char *value;

		if (!p->value) {
			p = p->next;
			continue;
		}

		value = header_encode_param ((unsigned char *) p->value, &encoded);
		if (!value) {
			w(g_warning ("appending parameter %s=%s violates rfc2184", p->name, p->value));
			value = g_strdup (p->value);
		}

		if (!encoded) {
			char *ch;

			for (ch = value; *ch; ch++) {
				if (camel_lite_mime_is_tspecial (*ch) || camel_lite_mime_is_lwsp (*ch))
					break;
			}

			quote = ch && *ch;
		}

		nlen = strlen (p->name);
		vlen = strlen (value);

		if (used + nlen + vlen > CAMEL_FOLD_SIZE - 8) {
			out = g_string_append (out, ";\n\t");
			here = out->len;
			used = 0;
		} else
			out = g_string_append (out, "; ");

		if (nlen + vlen > CAMEL_FOLD_SIZE - 8) {
			/* we need to do special rfc2184 parameter wrapping */
			int maxlen = CAMEL_FOLD_SIZE - (nlen + 8);
			char *inptr, *inend;
			int i = 0;

			inptr = value;
			inend = value + vlen;

			while (inptr < inend) {
				char *ptr = inptr + MIN (inend - inptr, maxlen);

				if (encoded && ptr < inend) {
					/* be careful not to break an encoded char (ie %20) */
					char *q = ptr;
					int j = 2;

					for ( ; j > 0 && q > inptr && *q != '%'; j--, q--);
					if (*q == '%')
						ptr = q;
				}

				if (i != 0) {
					g_string_append (out, ";\n\t");
					here = out->len;
					used = 0;
				}

				g_string_append_printf (out, "%s*%d%s=", p->name, i++, encoded ? "*" : "");
				if (encoded || !quote)
					g_string_append_len (out, inptr, ptr - inptr);
				else
					quote_word (out, TRUE, inptr, ptr - inptr);

				d(printf ("wrote: %s\n", out->str + here));

				used += (out->len - here);

				inptr = ptr;
			}
		} else {
			g_string_append_printf (out, "%s%s=", p->name, encoded ? "*" : "");

			if (encoded || !quote)
				g_string_append (out, value);
			else
				quote_word (out, TRUE, value, vlen);

			used += (out->len - here);
		}

		g_free (value);

		p = p->next;
	}
}

char *
camel_lite_header_param_list_format(struct _camel_lite_header_param *p)
{
	GString *out = g_string_new("");
	char *ret;

	camel_lite_header_param_list_format_append(out, p);
	ret = out->str;
	g_string_free(out, FALSE);
	return ret;
}

CamelContentType *
camel_lite_content_type_decode(const char *in)
{
	const char *inptr = in;
	char *type, *subtype = NULL;
	CamelContentType *t = NULL;

	if (in==NULL)
		return NULL;

	type = decode_token(&inptr);
	header_decode_lwsp(&inptr);
	if (type) {
		if  (*inptr == '/') {
			inptr++;
			subtype = decode_token(&inptr);
		}
		if (subtype == NULL && (!g_ascii_strcasecmp(type, "text"))) {
			w(g_warning("text type with no subtype, resorting to text/plain: %s", in));
			subtype = g_strdup("plain");
		}
		if (subtype == NULL) {
			w(g_warning("MIME type with no subtype: %s", in));
		}

		t = camel_lite_content_type_new(type, subtype);
		t->params = header_decode_param_list(&inptr, NULL);
		g_free(type);
		g_free(subtype);
	} else {
		g_free(type);
		d(printf("cannot find MIME type in header (2) '%s'", in));
	}
	return t;
}

void
camel_lite_content_type_dump(CamelContentType *ct)
{
	struct _camel_lite_header_param *p;

	printf("Content-Type: ");
	if (ct==NULL) {
		printf("<NULL>\n");
		return;
	}
	printf("%s / %s", ct->type, ct->subtype);
	p = ct->params;
	if (p) {
		while (p) {
			printf(";\n\t%s=\"%s\"", p->name, p->value);
			p = p->next;
		}
	}
	printf("\n");
}

char *
camel_lite_content_type_format (CamelContentType *ct)
{
	GString *out;
	char *ret;

	if (ct == NULL)
		return NULL;

	out = g_string_new ("");
	if (ct->type == NULL) {
		g_string_append_printf (out, "text/plain");
		w(g_warning ("Content-Type with no main type"));
	} else if (ct->subtype == NULL) {
		w(g_warning ("Content-Type with no sub type: %s", ct->type));
		if (!g_ascii_strcasecmp (ct->type, "multipart"))
			g_string_append_printf (out, "%s/mixed", ct->type);
		else
			g_string_append_printf (out, "%s", ct->type);
	} else {
		g_string_append_printf (out, "%s/%s", ct->type, ct->subtype);
	}
	camel_lite_header_param_list_format_append (out, ct->params);

	ret = out->str;
	g_string_free (out, FALSE);

	return ret;
}

char *
camel_lite_content_type_simple (CamelContentType *ct)
{
	if (ct->type == NULL) {
		w(g_warning ("Content-Type with no main type"));
		return g_strdup ("text/plain");
	} else if (ct->subtype == NULL) {
		w(g_warning ("Content-Type with no sub type: %s", ct->type));
		if (!g_ascii_strcasecmp (ct->type, "multipart"))
			return g_strdup_printf ("%s/mixed", ct->type);
		else
			return g_strdup (ct->type);
	} else
		return g_strdup_printf ("%s/%s", ct->type, ct->subtype);
}

char *
camel_lite_content_transfer_encoding_decode (const char *in)
{
	if (in)
		return decode_token (&in);

	return NULL;
}

CamelContentDisposition *
camel_lite_content_disposition_decode(const char *in, const char *default_encoding)
{
	CamelContentDisposition *d = NULL;
	const char *inptr = in;

	if (in == NULL)
		return NULL;

	d = g_malloc(sizeof(*d));
	d->refcount = 1;
	d->disposition = decode_token(&inptr);
	if (d->disposition == NULL)
		w(g_warning("Empty disposition type"));
	d->params = header_decode_param_list(&inptr, default_encoding);
	return d;
}

void
camel_lite_content_disposition_ref(CamelContentDisposition *d)
{
	if (d)
		d->refcount++;
}

void
camel_lite_content_disposition_unref(CamelContentDisposition *d)
{
	if (d) {
		if (d->refcount<=1) {
			camel_lite_header_param_list_free(d->params);
			g_free(d->disposition);
			g_free(d);
		} else {
			d->refcount--;
		}
	}
}

char *
camel_lite_content_disposition_format(CamelContentDisposition *d)
{
	GString *out;
	char *ret;

	if (d==NULL)
		return NULL;

	out = g_string_new("");
	if (d->disposition)
		out = g_string_append(out, d->disposition);
	else
		out = g_string_append(out, "attachment");
	camel_lite_header_param_list_format_append(out, d->params);

	ret = out->str;
	g_string_free(out, FALSE);
	return ret;
}

/* hrm, is there a library for this shit? */
static struct {
	char *name;
	int offset;
} tz_offsets [] = {
	{ "UT", 0 },
	{ "GMT", 0 },
	{ "EST", -500 },	/* these are all US timezones.  bloody yanks */
	{ "EDT", -400 },
	{ "CST", -600 },
	{ "CDT", -500 },
	{ "MST", -700 },
	{ "MDT", -600 },
	{ "PST", -800 },
	{ "PDT", -700 },
	{ "Z", 0 },
	{ "A", -100 },
	{ "M", -1200 },
	{ "N", 100 },
	{ "Y", 1200 },
};

static const char tz_months [][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char tz_days [][4] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

char *
camel_lite_header_format_date(time_t time, int offset)
{
	struct tm tm;

	d(printf("offset = %d\n", offset));

	d(printf("converting date %s", ctime(&time)));

	time += ((offset / 100) * (60*60)) + (offset % 100)*60;

	d(printf("converting date %s", ctime(&time)));

	gmtime_r (&time, &tm);

	return g_strdup_printf("%s, %02d %s %04d %02d:%02d:%02d %+05d",
			       tz_days[tm.tm_wday],
			       tm.tm_mday, tz_months[tm.tm_mon],
			       tm.tm_year + 1900,
			       tm.tm_hour, tm.tm_min, tm.tm_sec,
			       offset);
}

/* convert a date to time_t representation */
/* this is an awful mess oh well */
time_t
camel_lite_header_decode_date(const char *in, int *saveoffset)
{
	const char *inptr = in;
	char *monthname;
	gboolean foundmonth;
	int year, offset = 0;
	struct tm tm;
	int i;
	time_t t;

	if (in == NULL) {
		if (saveoffset)
			*saveoffset = 0;
		return 0;
	}

	d(printf ("\ndecoding date '%s'\n", inptr));

	memset (&tm, 0, sizeof(tm));

	header_decode_lwsp (&inptr);
	if (!isdigit (*inptr)) {
		char *day = decode_token (&inptr);
		/* we dont really care about the day, it's only for display */
		if (day) {
			d(printf ("got day: %s\n", day));
			g_free (day);
			header_decode_lwsp (&inptr);
			if (*inptr == ',') {
				inptr++;
			} else {
#ifndef CLEAN_DATE
				return parse_broken_date (in, saveoffset);
#else
				if (saveoffset)
					*saveoffset = 0;
				return 0;
#endif /* ! CLEAN_DATE */
			}
		}
	}
	tm.tm_mday = camel_lite_header_decode_int(&inptr);
#ifndef CLEAN_DATE
	if (tm.tm_mday == 0) {
		return parse_broken_date (in, saveoffset);
	}
#endif /* ! CLEAN_DATE */

	monthname = decode_token(&inptr);
	foundmonth = FALSE;
	if (monthname) {
		for (i=0;i<sizeof(tz_months)/sizeof(tz_months[0]);i++) {
			if (!g_ascii_strcasecmp(tz_months[i], monthname)) {
				tm.tm_mon = i;
				foundmonth = TRUE;
				break;
			}
		}
		g_free(monthname);
	}
#ifndef CLEAN_DATE
	if (!foundmonth) {
		return parse_broken_date (in, saveoffset);
	}
#endif /* ! CLEAN_DATE */

	year = camel_lite_header_decode_int(&inptr);
	if (year < 69) {
		tm.tm_year = 100 + year;
	} else if (year < 100) {
		tm.tm_year = year;
	} else if (year >= 100 && year < 1900) {
		tm.tm_year = year;
	} else {
		tm.tm_year = year - 1900;
	}
	/* get the time ... yurck */
	tm.tm_hour = camel_lite_header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == ':')
		inptr++;
	tm.tm_min = camel_lite_header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == ':')
		inptr++;
	tm.tm_sec = camel_lite_header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == '+'
	    || *inptr == '-') {
		offset = (*inptr++)=='-'?-1:1;
		offset = offset * camel_lite_header_decode_int(&inptr);
		d(printf("abs signed offset = %d\n", offset));
		if (offset < -1200 || offset > 1400)
			offset = 0;
	} else if (isdigit(*inptr)) {
		offset = camel_lite_header_decode_int(&inptr);
		d(printf("abs offset = %d\n", offset));
		if (offset < -1200 || offset > 1400)
			offset = 0;
	} else {
		char *tz = decode_token(&inptr);

		if (tz) {
			for (i=0;i<sizeof(tz_offsets)/sizeof(tz_offsets[0]);i++) {
				if (!g_ascii_strcasecmp(tz_offsets[i].name, tz)) {
					offset = tz_offsets[i].offset;
					break;
				}
			}
			g_free(tz);
		}
		/* some broken mailers seem to put in things like GMT+1030 instead of just +1030 */
		header_decode_lwsp(&inptr);
		if (*inptr == '+' || *inptr == '-') {
			int sign = (*inptr++)=='-'?-1:1;
			offset = offset + (camel_lite_header_decode_int(&inptr)*sign);
		}
		d(printf("named offset = %d\n", offset));
	}

	t = e_mktime_utc(&tm);

	/* t is now GMT of the time we want, but not offset by the timezone ... */

	d(printf(" gmt normalized? = %s\n", ctime(&t)));

	/* this should convert the time to the GMT equiv time */
	t -= ( (offset/100) * 60*60) + (offset % 100)*60;

	d(printf(" gmt normalized for timezone? = %s\n", ctime(&t)));

	d({
		char *tmp;
		tmp = camel_lite_header_format_date(t, offset);
		printf(" encoded again: %s\n", tmp);
		g_free(tmp);
	});

	if (saveoffset)
		*saveoffset = offset;

	return t;
}

char *
camel_lite_header_location_decode(const char *in)
{
	int quote = 0;
	GString *out = g_string_new("");
	char c, *res;

	/* Sigh. RFC2557 says:
	 *   content-location =   "Content-Location:" [CFWS] URI [CFWS]
	 *      where URI is restricted to the syntax for URLs as
	 *      defined in Uniform Resource Locators [URL] until
	 *      IETF specifies other kinds of URIs.
	 *
	 * But Netscape puts quotes around the URI when sending web
	 * pages.
	 *
	 * Which is required as defined in rfc2017 [3.1].  Although
	 * outlook doesn't do this.
	 *
	 * Since we get headers already unfolded, we need just drop
	 * all whitespace.  URL's cannot contain whitespace or quoted
	 * characters, even when included in quotes.
	 */

	header_decode_lwsp(&in);
	if (*in == '"') {
		in++;
		quote = 1;
	}

	while ( (c = *in++) ) {
		if (quote && c=='"')
			break;
		if (!camel_lite_mime_is_lwsp(c))
			g_string_append_c(out, c);
	}

	/* We don't allow empty string as content location, prefer NULL */
	if (out->str != NULL && out->str[0] == '\0')
		res = NULL;
	else
		res = g_strdup(out->str);

	g_string_free(out, TRUE);

	return res;
}

/* extra rfc checks */
#define CHECKS

#ifdef CHECKS
static void
check_header(struct _camel_lite_header_raw *h)
{
	unsigned char *p;

	p = (unsigned char *) h->value;
	while (p && *p) {
		if (!isascii(*p)) {
			w(g_warning("Appending header violates rfc: %s: %s", h->name, h->value));
			return;
		}
		p++;
	}
}
#endif

void
camel_lite_header_raw_append_parse(struct _camel_lite_header_raw **list, const char *header, int offset)
{
	register const char *in;
	size_t fieldlen;
	char *name;

	in = header;
	while (camel_lite_mime_is_fieldname(*in) || *in==':')
		in++;
	fieldlen = in-header-1;
	while (camel_lite_mime_is_lwsp(*in))
		in++;
	if (fieldlen == 0 || header[fieldlen] != ':') {
		printf("Invalid header line: '%s'\n", header);
		return;
	}
	name = g_alloca (fieldlen + 1);
	memcpy(name, header, fieldlen);
	name[fieldlen] = 0;

	camel_lite_header_raw_append(list, name, in, offset);
}

void
camel_lite_header_raw_append(struct _camel_lite_header_raw **list, const char *name, const char *value, int offset)
{
	struct _camel_lite_header_raw *l, *n;

	d(printf("Header: %s: %s\n", name, value));

	n = g_malloc(sizeof(*n));
	n->next = NULL;
	n->name = g_strdup(name);
	n->value = g_strdup(value);
	n->offset = offset;
#ifdef CHECKS
	check_header(n);
#endif
	l = (struct _camel_lite_header_raw *)list;
	while (l->next) {
		l = l->next;
	}
	l->next = n;

	/* debug */
#if 0
	if (!g_ascii_strcasecmp(name, "To")) {
		printf("- Decoding To\n");
		camel_lite_header_to_decode(value);
	} else if (!g_ascii_strcasecmp(name, "Content-type")) {
		printf("- Decoding content-type\n");
		camel_lite_content_type_dump(camel_lite_content_type_decode(value));
	} else if (!g_ascii_strcasecmp(name, "MIME-Version")) {
		printf("- Decoding mime version\n");
		camel_lite_header_mime_decode(value);
	}
#endif
}

static struct _camel_lite_header_raw *
header_raw_find_node(struct _camel_lite_header_raw **list, const char *name)
{
	struct _camel_lite_header_raw *l;

	l = *list;
	while (l) {
		if (!g_ascii_strcasecmp(l->name, name))
			break;
		l = l->next;
	}
	return l;
}

const char *
camel_lite_header_raw_find(struct _camel_lite_header_raw **list, const char *name, int *offset)
{
	struct _camel_lite_header_raw *l;

	l = header_raw_find_node(list, name);
	if (l) {
		if (offset)
			*offset = l->offset;
		return l->value;
	} else
		return NULL;
}

const char *
camel_lite_header_raw_find_next(struct _camel_lite_header_raw **list, const char *name, int *offset, const char *last)
{
	struct _camel_lite_header_raw *l;

	if (last == NULL || name == NULL)
		return NULL;

	l = *list;
	while (l && l->value != last)
		l = l->next;
	return camel_lite_header_raw_find(&l, name, offset);
}

static void
header_raw_free(struct _camel_lite_header_raw *l)
{
	g_free(l->name);
	g_free(l->value);
	g_free(l);
}

void
camel_lite_header_raw_remove(struct _camel_lite_header_raw **list, const char *name)
{
	struct _camel_lite_header_raw *l, *p;

	/* the next pointer is at the head of the structure, so this is safe */
	p = (struct _camel_lite_header_raw *)list;
	l = *list;
	while (l) {
		if (!g_ascii_strcasecmp(l->name, name)) {
			p->next = l->next;
			header_raw_free(l);
			l = p->next;
		} else {
			p = l;
			l = l->next;
		}
	}
}

void
camel_lite_header_raw_replace(struct _camel_lite_header_raw **list, const char *name, const char *value, int offset)
{
	camel_lite_header_raw_remove(list, name);
	camel_lite_header_raw_append(list, name, value, offset);
}

void
camel_lite_header_raw_clear(struct _camel_lite_header_raw **list)
{
	struct _camel_lite_header_raw *l, *n;
	l = *list;
	while (l) {
		n = l->next;
		header_raw_free(l);
		l = n;
	}
	*list = NULL;
}

char *
camel_lite_header_msgid_generate (void)
{
	static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
#define COUNT_LOCK() pthread_mutex_lock (&count_lock)
#define COUNT_UNLOCK() pthread_mutex_unlock (&count_lock)
	char host[MAXHOSTNAMELEN];
	char *name;
	static int count = 0;
	char *msgid;
	int retval;
	struct addrinfo *ai = NULL, hints = { 0 };

	retval = gethostname (host, sizeof (host));
	if (retval == 0 && *host) {
		hints.ai_flags = AI_CANONNAME;
		ai = camel_lite_getaddrinfo(host, NULL, &hints, NULL);
		if (ai && ai->ai_canonname)
			name = ai->ai_canonname;
		else
			name = host;
	} else
		name = "localhost.localdomain";

	COUNT_LOCK ();
	msgid = g_strdup_printf ("%d.%d.%d.camel@%s", (int) time (NULL), getpid (), count++, name);
	COUNT_UNLOCK ();

	if (ai)
		camel_lite_freeaddrinfo(ai);

	return msgid;
}


static struct {
	char *name;
	char *pattern;
	regex_t regex;
} mail_list_magic[] = {
	/* List-Post: <mailto:gnome-hackers@gnome.org> */
	/* List-Post: <mailto:gnome-hackers> */
	{ "List-Post", "[ \t]*<mailto:([^@>]+)@?([^ \n\t\r>]*)" },
	/* List-Id: GNOME stuff <gnome-hackers.gnome.org> */
	/* List-Id: <gnome-hackers.gnome.org> */
	/* List-Id: <gnome-hackers> */
	/* This old one wasn't very useful: { "List-Id", " *([^<]+)" },*/
	{ "List-Id", "[^<]*<([^\\.>]+)\\.?([^ \n\t\r>]*)" },
	/* Mailing-List: list gnome-hackers@gnome.org; contact gnome-hackers-owner@gnome.org */
	{ "Mailing-List", "[ \t]*list ([^@]+)@?([^ \n\t\r>;]*)" },
	/* Originator: gnome-hackers@gnome.org */
	{ "Originator", "[ \t]*([^@]+)@?([^ \n\t\r>]*)" },
	/* X-Mailing-List: <gnome-hackers@gnome.org> arcive/latest/100 */
	/* X-Mailing-List: gnome-hackers@gnome.org */
	/* X-Mailing-List: gnome-hackers */
	/* X-Mailing-List: <gnome-hackers> */
	{ "X-Mailing-List", "[ \t]*<?([^@>]+)@?([^ \n\t\r>]*)" },
	/* X-Loop: gnome-hackers@gnome.org */
	{ "X-Loop", "[ \t]*([^@]+)@?([^ \n\t\r>]*)" },
	/* X-List: gnome-hackers */
	/* X-List: gnome-hackers@gnome.org */
	{ "X-List", "[ \t]*([^@]+)@?([^ \n\t\r>]*)" },
	/* Sender: owner-gnome-hackers@gnome.org */
	/* Sender: owner-gnome-hacekrs */
	{ "Sender", "[ \t]*owner-([^@]+)@?([^ @\n\t\r>]*)" },
	/* Sender: gnome-hackers-owner@gnome.org */
	/* Sender: gnome-hackers-owner */
	{ "Sender", "[ \t]*([^@]+)-owner@?([^ @\n\t\r>]*)" },
	/* Delivered-To: mailing list gnome-hackers@gnome.org */
	/* Delivered-To: mailing list gnome-hackers */
	{ "Delivered-To", "[ \t]*mailing list ([^@]+)@?([^ \n\t\r>]*)" },
	/* Sender: owner-gnome-hackers@gnome.org */
	/* Sender: <owner-gnome-hackers@gnome.org> */
	/* Sender: owner-gnome-hackers */
	/* Sender: <owner-gnome-hackers> */
	{ "Return-Path", "[ \t]*<?owner-([^@>]+)@?([^ \n\t\r>]*)" },
	/* X-BeenThere: gnome-hackers@gnome.org */
	/* X-BeenThere: gnome-hackers */
	{ "X-BeenThere", "[ \t]*([^@]+)@?([^ \n\t\r>]*)" },
	/* List-Unsubscribe:  <mailto:gnome-hackers-unsubscribe@gnome.org> */
	{ "List-Unsubscribe", "<mailto:(.+)-unsubscribe@([^ \n\t\r>]*)" },
};

static pthread_once_t mailing_list_init_once = PTHREAD_ONCE_INIT;

static void
mailing_list_init(void)
{
	int i, errcode, failed=0;

	/* precompile regex's for speed at runtime */
	for (i = 0; i < G_N_ELEMENTS (mail_list_magic); i++) {
		errcode = regcomp(&mail_list_magic[i].regex, mail_list_magic[i].pattern, REG_EXTENDED|REG_ICASE);
		if (errcode != 0) {
			char *errstr;
			size_t len;

			len = regerror(errcode, &mail_list_magic[i].regex, NULL, 0);
			errstr = g_malloc0(len + 1);
			regerror(errcode, &mail_list_magic[i].regex, errstr, len);

			g_warning("Internal error, compiling regex failed: %s: %s", mail_list_magic[i].pattern, errstr);
			g_free(errstr);
			failed++;
		}
	}

	g_assert(failed == 0);
}

char *
camel_lite_header_raw_check_mailing_list(struct _camel_lite_header_raw **list)
{
	const char *v;
	regmatch_t match[3];
	int i, j;

	pthread_once(&mailing_list_init_once, mailing_list_init);

	for (i = 0; i < sizeof (mail_list_magic) / sizeof (mail_list_magic[0]); i++) {
		v = camel_lite_header_raw_find (list, mail_list_magic[i].name, NULL);
		for (j=0;j<3;j++) {
			match[j].rm_so = -1;
			match[j].rm_eo = -1;
		}
		if (v != NULL && regexec (&mail_list_magic[i].regex, v, 3, match, 0) == 0 && match[1].rm_so != -1) {
			int len1, len2;
			char *mlist;

			len1 = match[1].rm_eo - match[1].rm_so;
			len2 = match[2].rm_eo - match[2].rm_so;

			mlist = g_malloc (len1 + len2 + 2);
			memcpy (mlist, v + match[1].rm_so, len1);
			if (len2) {
				mlist[len1] = '@';
				memcpy (mlist + len1 + 1, v + match[2].rm_so, len2);
				mlist[len1 + len2 + 1] = '\0';
			} else {
				mlist[len1] = '\0';
			}

			return mlist;
		}
	}

	return NULL;
}

/* ok, here's the address stuff, what a mess ... */
struct _camel_lite_header_address *
camel_lite_header_address_new (void)
{
	struct _camel_lite_header_address *h;
	h = g_malloc0(sizeof(*h));
	h->type = CAMEL_HEADER_ADDRESS_NONE;
	h->refcount = 1;
	return h;
}

struct _camel_lite_header_address *
camel_lite_header_address_new_name(const char *name, const char *addr)
{
	struct _camel_lite_header_address *h;
	h = camel_lite_header_address_new();
	h->type = CAMEL_HEADER_ADDRESS_NAME;
	h->name = g_strdup(name);
	h->v.addr = g_strdup(addr);
	return h;
}

struct _camel_lite_header_address *
camel_lite_header_address_new_group (const char *name)
{
	struct _camel_lite_header_address *h;

	h = camel_lite_header_address_new();
	h->type = CAMEL_HEADER_ADDRESS_GROUP;
	h->name = g_strdup(name);
	return h;
}

void
camel_lite_header_address_ref(struct _camel_lite_header_address *h)
{
	if (h)
		h->refcount++;
}

void
camel_lite_header_address_unref(struct _camel_lite_header_address *h)
{
	if (h) {
		if (h->refcount <= 1) {
			if (h->type == CAMEL_HEADER_ADDRESS_GROUP) {
				camel_lite_header_address_list_clear(&h->v.members);
			} else if (h->type == CAMEL_HEADER_ADDRESS_NAME) {
				g_free(h->v.addr);
			}
			g_free(h->name);
			g_free(h);
		} else {
			h->refcount--;
		}
	}
}

void
camel_lite_header_address_set_name(struct _camel_lite_header_address *h, const char *name)
{
	if (h) {
		g_free(h->name);
		h->name = g_strdup(name);
	}
}

void
camel_lite_header_address_set_addr(struct _camel_lite_header_address *h, const char *addr)
{
	if (h) {
		if (h->type == CAMEL_HEADER_ADDRESS_NAME
		    || h->type == CAMEL_HEADER_ADDRESS_NONE) {
			h->type = CAMEL_HEADER_ADDRESS_NAME;
			g_free(h->v.addr);
			h->v.addr = g_strdup(addr);
		} else {
			g_warning("Trying to set the address on a group");
		}
	}
}

void
camel_lite_header_address_set_members(struct _camel_lite_header_address *h, struct _camel_lite_header_address *group)
{
	if (h) {
		if (h->type == CAMEL_HEADER_ADDRESS_GROUP
		    || h->type == CAMEL_HEADER_ADDRESS_NONE) {
			h->type = CAMEL_HEADER_ADDRESS_GROUP;
			camel_lite_header_address_list_clear(&h->v.members);
			/* should this ref them? */
			h->v.members = group;
		} else {
			g_warning("Trying to set the members on a name, not group");
		}
	}
}

void
camel_lite_header_address_add_member(struct _camel_lite_header_address *h, struct _camel_lite_header_address *member)
{
	if (h) {
		if (h->type == CAMEL_HEADER_ADDRESS_GROUP
		    || h->type == CAMEL_HEADER_ADDRESS_NONE) {
			h->type = CAMEL_HEADER_ADDRESS_GROUP;
			camel_lite_header_address_list_append(&h->v.members, member);
		}
	}
}

void
camel_lite_header_address_list_append_list(struct _camel_lite_header_address **l, struct _camel_lite_header_address **h)
{
	if (l) {
		struct _camel_lite_header_address *n = (struct _camel_lite_header_address *)l;

		while (n->next)
			n = n->next;
		n->next = *h;
	}
}


void
camel_lite_header_address_list_append(struct _camel_lite_header_address **l, struct _camel_lite_header_address *h)
{
	if (h) {
		camel_lite_header_address_list_append_list(l, &h);
		h->next = NULL;
	}
}

void
camel_lite_header_address_list_clear(struct _camel_lite_header_address **l)
{
	struct _camel_lite_header_address *a, *n;
	a = *l;
	while (a) {
		n = a->next;
		camel_lite_header_address_unref(a);
		a = n;
	}
	*l = NULL;
}

/* if encode is true, then the result is suitable for mailing, otherwise
   the result is suitable for display only (and may not even be re-parsable) */
static void
header_address_list_encode_append (GString *out, int encode, struct _camel_lite_header_address *a)
{
	char *text;

	while (a) {
		switch (a->type) {
		case CAMEL_HEADER_ADDRESS_NAME:
			if (encode)
				text = camel_lite_header_encode_phrase ((unsigned char *) a->name);
			else
				text = a->name;
			if (text && *text)
				g_string_append_printf (out, "%s <%s>", text, a->v.addr);
			else
				g_string_append (out, a->v.addr);
			if (encode)
				g_free (text);
			break;
		case CAMEL_HEADER_ADDRESS_GROUP:
			if (encode)
				text = camel_lite_header_encode_phrase ((unsigned char *) a->name);
			else
				text = a->name;
			g_string_append_printf (out, "%s: ", text);
			header_address_list_encode_append (out, encode, a->v.members);
			g_string_append_printf (out, ";");
			if (encode)
				g_free (text);
			break;
		default:
			g_warning ("Invalid address type");
			break;
		}
		a = a->next;
		if (a)
			g_string_append (out, ", ");
	}
}

char *
camel_lite_header_address_list_encode (struct _camel_lite_header_address *a)
{
	GString *out;
	char *ret;

	if (a == NULL)
		return NULL;

	out = g_string_new ("");
	header_address_list_encode_append (out, TRUE, a);
	ret = out->str;
	g_string_free (out, FALSE);

	return ret;
}

char *
camel_lite_header_address_list_format (struct _camel_lite_header_address *a)
{
	GString *out;
	char *ret;

	if (a == NULL)
		return NULL;

	out = g_string_new ("");

	header_address_list_encode_append (out, FALSE, a);
	ret = out->str;
	g_string_free (out, FALSE);

	return ret;
}

char *
camel_lite_header_address_fold (const char *in, size_t headerlen)
{
	size_t len, outlen;
	const char *inptr = in, *space, *p, *n;
	GString *out;
	char *ret;
	int i, needunfold = FALSE;

	if (in == NULL)
		return NULL;

	/* first, check to see if we even need to fold */
	len = headerlen + 2;
	p = in;
	while (*p) {
		n = strchr (p, '\n');
		if (n == NULL) {
			len += strlen (p);
			break;
		}

		needunfold = TRUE;
		len += n-p;

		if (len >= CAMEL_FOLD_SIZE)
			break;
		len = 0;
		p = n + 1;
	}
	if (len < CAMEL_FOLD_SIZE)
		return g_strdup (in);

	/* we need to fold, so first unfold (if we need to), then process */
	if (needunfold)
		inptr = in = camel_lite_header_unfold (in);

	out = g_string_new ("");
	outlen = headerlen + 2;
	while (*inptr) {
		space = strchr (inptr, ' ');
		if (space) {
			len = space - inptr + 1;
		} else {
			len = strlen (inptr);
		}

		d(printf("next word '%.*s'\n", len, inptr));

		if (outlen + len > CAMEL_FOLD_SIZE) {
			d(printf("outlen = %d wordlen = %d\n", outlen, len));
			/* strip trailing space */
			if (out->len > 0 && out->str[out->len-1] == ' ')
				g_string_truncate (out, out->len-1);
			g_string_append (out, "\n\t");
			outlen = 1;
		}

		outlen += len;
		for (i = 0; i < len; i++) {
			g_string_append_c (out, inptr[i]);
		}

		inptr += len;
	}
	ret = out->str;
	g_string_free (out, FALSE);

	if (needunfold)
		g_free ((char *)in);

	return ret;
}

/* simple header folding */
/* will work even if the header is already folded */
char *
camel_lite_header_fold(const char *in, size_t headerlen)
{
	size_t len, outlen, i;
	const char *inptr = in, *space, *p, *n;
	GString *out;
	char *ret;
	int needunfold = FALSE;

	if (in == NULL)
		return NULL;

	/* first, check to see if we even need to fold */
	len = headerlen + 2;
	p = in;
	while (*p) {
		n = strchr(p, '\n');
		if (n == NULL) {
			len += strlen (p);
			break;
		}

		needunfold = TRUE;
		len += n-p;

		if (len >= CAMEL_FOLD_SIZE)
			break;
		len = 0;
		p = n + 1;
	}
	if (len < CAMEL_FOLD_SIZE)
		return g_strdup(in);

	/* we need to fold, so first unfold (if we need to), then process */
	if (needunfold)
		inptr = in = camel_lite_header_unfold(in);

	out = g_string_new("");
	outlen = headerlen+2;
	while (*inptr) {
		space = strchr(inptr, ' ');
		if (space) {
			len = space-inptr+1;
		} else {
			len = strlen(inptr);
		}
		d(printf("next word '%.*s'\n", len, inptr));
		if (outlen + len > CAMEL_FOLD_SIZE) {
			d(printf("outlen = %d wordlen = %d\n", outlen, len));
			/* strip trailing space */
			if (out->len > 0 && out->str[out->len-1] == ' ')
				g_string_truncate(out, out->len-1);
			g_string_append(out, "\n\t");
			outlen = 1;
			/* check for very long words, just cut them up */
			while (outlen+len > CAMEL_FOLD_MAX_SIZE) {
				for (i=0;i<CAMEL_FOLD_MAX_SIZE-outlen;i++)
					g_string_append_c(out, inptr[i]);
				inptr += CAMEL_FOLD_MAX_SIZE-outlen;
				len -= CAMEL_FOLD_MAX_SIZE-outlen;
				g_string_append(out, "\n\t");
				outlen = 1;
			}
		}
		outlen += len;
		for (i=0;i<len;i++) {
			g_string_append_c(out, inptr[i]);
		}
		inptr += len;
	}
	ret = out->str;
	g_string_free(out, FALSE);

	if (needunfold)
		g_free((char *)in);

	return ret;
}

char *
camel_lite_header_unfold(const char *in)
{
	char *out = g_malloc(strlen(in)+1);
	const char *inptr = in;
	char c, *o = out;

	o = out;
	while ((c = *inptr++)) {
		if (c == '\n') {
			if (camel_lite_mime_is_lwsp(*inptr)) {
				do {
					inptr++;
				} while (camel_lite_mime_is_lwsp(*inptr));
				*o++ = ' ';
			} else {
				*o++ = c;
			}
		} else {
			*o++ = c;
		}
	}
	*o = 0;

	return out;
}
