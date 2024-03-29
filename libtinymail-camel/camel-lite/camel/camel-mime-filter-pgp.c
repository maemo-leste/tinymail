/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Matt Brown.
 *
 * Authors: Matt Brown <matt@mattb.net.nz>
 *          Jeffrey Stedfast <fejj@novell.com>
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

/* Strips PGP message headers from the input stream and also performs
 * pgp decoding as described in section 7.1 of RFC2440 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>

#include "camel-mime-filter-pgp.h"

static void filter (CamelMimeFilter *f, char *in, size_t len, size_t prespace,
		    char **out, size_t *outlen, size_t *outprespace);
static void complete (CamelMimeFilter *f, char *in, size_t len,
		      size_t prespace, char **out, size_t *outlen,
		      size_t *outprespace);
static void reset (CamelMimeFilter *f);

enum {
	PGP_PREFACE,
	PGP_HEADER,
	PGP_MESSAGE,
	PGP_FOOTER
};

static void
camel_lite_mime_filter_pgp_class_init (CamelMimeFilterPgpClass *klass)
{
	CamelMimeFilterClass *mime_filter_class = (CamelMimeFilterClass *) klass;

	mime_filter_class->filter = filter;
	mime_filter_class->complete = complete;
	mime_filter_class->reset = reset;
}

CamelType
camel_lite_mime_filter_pgp_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_mime_filter_get_type (),
					    "CamelLiteMimeFilterPgp",
					    sizeof (CamelMimeFilterPgp),
					    sizeof (CamelMimeFilterPgpClass),
					    (CamelObjectClassInitFunc) camel_lite_mime_filter_pgp_class_init,
					    NULL,
					    NULL,
					    NULL);
	}

	return type;
}

#define BEGIN_PGP_SIGNED_MESSAGE "-----BEGIN PGP SIGNED MESSAGE-----"
#define BEGIN_PGP_SIGNATURE      "-----BEGIN PGP SIGNATURE-----"
#define END_PGP_SIGNATURE        "-----END PGP SIGNATURE-----"

static void
filter_run(CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace, int last)
{
	CamelMimeFilterPgp *pgp = (CamelMimeFilterPgp *) f;
	const char *start, *inend = in + len;
	register const char *inptr = in;
	register char *o;

	/* only need as much space as the input, we're stripping chars */
	camel_lite_mime_filter_set_size (f, len, FALSE);

	o = f->outbuf;

	while (inptr < inend) {
		start = inptr;

		while (inptr < inend && *inptr != '\n')
			inptr++;

		if (inptr == inend) {
			if (!last) {
				camel_lite_mime_filter_backup (f, start, inend - start);
				inend = start;
			}
			break;
		}

		inptr++;

		switch (pgp->state) {
		case PGP_PREFACE:
			/* check for the beginning of the pgp block */
			if (!strncmp (start, BEGIN_PGP_SIGNED_MESSAGE, sizeof (BEGIN_PGP_SIGNED_MESSAGE) - 1)) {
				pgp->state++;
				break;
			}

			memcpy (o, start, inptr - start);
			o += (inptr - start);
			break;
		case PGP_HEADER:
			/* pgp headers (Hash: SHA1, etc) end with a blank (zero-length,
			   or containing only whitespace) line; see RFC2440 */
			if ((inptr - start) == 1 || ((inptr - start) == 2 && *(inptr - 2) == 0x20))
				pgp->state++;
			break;
		case PGP_MESSAGE:
			/* check for beginning of the pgp signature block */
			if (!strncmp (start, BEGIN_PGP_SIGNATURE, sizeof (BEGIN_PGP_SIGNATURE) - 1)) {
				pgp->state++;
				break;
			}

			/* do dash decoding */
			if (!strncmp (start, "- ", 2)) {
				/* Dash encoded line found, skip encoding */
				start += 2;
			}

			memcpy (o, start, inptr - start);
			o += (inptr - start);
			break;
		case PGP_FOOTER:
			if (!strncmp (start, END_PGP_SIGNATURE, sizeof (END_PGP_SIGNATURE) - 1))
				pgp->state = PGP_PREFACE;
			break;
		}
	}

	*out = f->outbuf;
	*outlen = o - f->outbuf;
	*outprespace = f->outpre;
}

static void
filter (CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	filter_run (f, in, len, prespace, out, outlen, outprespace, FALSE);
}

static void
complete (CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	filter_run (f, in, len, prespace, out, outlen, outprespace, TRUE);
}

static void
reset (CamelMimeFilter *f)
{
	((CamelMimeFilterPgp *) f)->state = PGP_PREFACE;
}

CamelMimeFilter *
camel_lite_mime_filter_pgp_new(void)
{
	return (CamelMimeFilter *) camel_lite_object_new (camel_lite_mime_filter_pgp_get_type ());
}
