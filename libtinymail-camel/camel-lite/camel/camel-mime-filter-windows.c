/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "camel-charset-map.h"
#include "camel-mime-filter-windows.h"

#define d(x)
#define w(x)

static void camel_lite_mime_filter_windows_class_init (CamelMimeFilterWindowsClass *klass);
static void camel_lite_mime_filter_windows_init       (CamelObject *o);
static void camel_lite_mime_filter_windows_finalize   (CamelObject *o);

static CamelMimeFilterClass *parent_class = NULL;

CamelType
camel_lite_mime_filter_windows_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_mime_filter_get_type (),
					    "CamelLiteMimeFilterWindows",
					    sizeof (CamelMimeFilterWindows),
					    sizeof (CamelMimeFilterWindowsClass),
					    (CamelObjectClassInitFunc) camel_lite_mime_filter_windows_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_mime_filter_windows_init,
					    (CamelObjectFinalizeFunc) camel_lite_mime_filter_windows_finalize);
	}

	return type;
}

static void
camel_lite_mime_filter_windows_finalize (CamelObject *o)
{
	CamelMimeFilterWindows *windows = (CamelMimeFilterWindows *) o;

	g_free (windows->claimed_charset);
}

static void
camel_lite_mime_filter_windows_init (CamelObject *o)
{
	CamelMimeFilterWindows *windows = (CamelMimeFilterWindows *) o;

	windows->is_windows = FALSE;
	windows->claimed_charset = NULL;
}

static void
filter_filter (CamelMimeFilter *filter, char *in, size_t len, size_t prespace,
	       char **out, size_t *outlen, size_t *outprespace)
{
	CamelMimeFilterWindows *windows = (CamelMimeFilterWindows *) filter;
	register unsigned char *inptr;
	unsigned char *inend;

	if (!windows->is_windows) {
		inptr = (unsigned char *) in;
		inend = inptr + len;

		while (inptr < inend) {
			register unsigned char c = *inptr++;

			if (c >= 128 && c <= 159) {
				w(g_warning ("Encountered Windows charset masquerading as %s",
					     windows->claimed_charset));
				windows->is_windows = TRUE;
				break;
			}
		}
	}

	*out = in;
	*outlen = len;
	*outprespace = prespace;
}

static void
filter_complete (CamelMimeFilter *filter, char *in, size_t len, size_t prespace,
		 char **out, size_t *outlen, size_t *outprespace)
{
	filter_filter (filter, in, len, prespace, out, outlen, outprespace);
}

static void
filter_reset (CamelMimeFilter *filter)
{
	CamelMimeFilterWindows *windows = (CamelMimeFilterWindows *) filter;

	windows->is_windows = FALSE;
}

static void
camel_lite_mime_filter_windows_class_init (CamelMimeFilterWindowsClass *klass)
{
	CamelMimeFilterClass *filter_class = (CamelMimeFilterClass *) klass;

	parent_class = CAMEL_MIME_FILTER_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_mime_filter_get_type ()));

	filter_class->reset = filter_reset;
	filter_class->filter = filter_filter;
	filter_class->complete = filter_complete;
}


/**
 * camel_lite_mime_filter_windows_new:
 * @claimed_charset: ISO charset name
 *
 * Create a new #CamelMimeFilterWindows object that will analyse
 * whether or not the text is really encoded in @claimed_charset.
 *
 * Returns a new #CamelMimeFilter object
 **/
CamelMimeFilter *
camel_lite_mime_filter_windows_new (const char *claimed_charset)
{
	CamelMimeFilterWindows *new;

	g_return_val_if_fail (claimed_charset != NULL, NULL);

	new = CAMEL_MIME_FILTER_WINDOWS (camel_lite_object_new (camel_lite_mime_filter_windows_get_type ()));

	new->claimed_charset = g_strdup (claimed_charset);

	return CAMEL_MIME_FILTER (new);
}


/**
 * camel_lite_mime_filter_windows_is_windows_charset:
 * @filter: a #CamelMimeFilterWindows object
 *
 * Get whether or not the textual content filtered by @filetr is
 * really in a Microsoft Windows charset rather than the claimed ISO
 * charset.
 *
 * Returns %TRUE if the text was found to be in a Microsoft Windows
 * CP125x charset or %FALSE otherwise.
 **/
gboolean
camel_lite_mime_filter_windows_is_windows_charset (CamelMimeFilterWindows *filter)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_WINDOWS (filter), FALSE);

	return filter->is_windows;
}


/**
 * camel_lite_mime_filter_windows_real_charset:
 * @filter: a #CamelMimeFilterWindows object
 *
 * Get the name of the actual charset used to encode the textual
 * content filtered by @filter (it will either be the original
 * claimed_charset passed in at creation time or the Windows-CP125x
 * equivalent).
 *
 * Returns the name of the actual charset
 **/
const char *
camel_lite_mime_filter_windows_real_charset (CamelMimeFilterWindows *filter)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_WINDOWS (filter), NULL);

	if (filter->is_windows)
		return camel_lite_charset_iso_to_windows (filter->claimed_charset);
	else
		return filter->claimed_charset;
}
