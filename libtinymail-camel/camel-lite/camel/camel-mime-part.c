/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camelMimePart.c : Abstract class for a mime_part */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
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

#include <libedataserver/e-lite-iconv.h>

#include "camel-charset-map.h"
#include "camel-exception.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-parser.h"
#include "camel-mime-part-utils.h"
#include "camel-mime-part.h"
#include "camel-mime-utils.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream-null.h"
#include "camel-string-utils.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

typedef enum {
	HEADER_UNKNOWN,
	HEADER_DESCRIPTION,
	HEADER_DISPOSITION,
	HEADER_CONTENT_ID,
	HEADER_ENCODING,
	HEADER_CONTENT_MD5,
	HEADER_CONTENT_LOCATION,
	HEADER_CONTENT_LANGUAGES,
	HEADER_CONTENT_TYPE
} CamelHeaderType;


static GHashTable *header_name_table;
static GHashTable *header_formatted_table;

static CamelMediumClass *parent_class=NULL;

/* Returns the class for a CamelMimePart */
#define CMP_CLASS(so) CAMEL_MIME_PART_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMD_CLASS(so) CAMEL_MEDIUM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

/* from CamelDataWrapper */
static ssize_t         write_to_stream                 (CamelDataWrapper *dw, CamelStream *stream);
static int	       construct_from_stream	       (CamelDataWrapper *dw, CamelStream *stream);

/* from CamelMedium */
static void            add_header                      (CamelMedium *medium, const char *name, const void *value);
static void            set_header                      (CamelMedium *medium, const char *name, const void *value);
static void            remove_header                   (CamelMedium *medium, const char *name);
static const void     *get_header                      (CamelMedium *medium, const char *name);
static GArray         *get_headers                     (CamelMedium *medium);
static void            free_headers                    (CamelMedium *medium, GArray *headers);

static void            set_content_object              (CamelMedium *medium, CamelDataWrapper *content);

/* from camel mime parser */
static int             construct_from_parser           (CamelMimePart *mime_part, CamelMimeParser *mp);

/* forward references */
static void set_disposition (CamelMimePart *mime_part, const char *disposition);

/* format output of headers */
static ssize_t write_references(CamelStream *stream, struct _camel_lite_header_raw *h);
/*static int write_fold(CamelStream *stream, struct _camel_lite_header_raw *h);*/
static ssize_t write_raw(CamelStream *stream, struct _camel_lite_header_raw *h);


/* loads in a hash table the set of header names we */
/* recognize and associate them with a unique enum  */
/* identifier (see CamelHeaderType above)           */
static void
init_header_name_table(void)
{
	header_name_table = g_hash_table_new (camel_lite_strcase_hash, camel_lite_strcase_equal);
	g_hash_table_insert (header_name_table, "Content-Description", (gpointer)HEADER_DESCRIPTION);
	g_hash_table_insert (header_name_table, "Content-Disposition", (gpointer)HEADER_DISPOSITION);
	g_hash_table_insert (header_name_table, "Content-id", (gpointer)HEADER_CONTENT_ID);
	g_hash_table_insert (header_name_table, "Content-Transfer-Encoding", (gpointer)HEADER_ENCODING);
	g_hash_table_insert (header_name_table, "Content-MD5", (gpointer)HEADER_CONTENT_MD5);
	g_hash_table_insert (header_name_table, "Content-Location", (gpointer)HEADER_CONTENT_LOCATION);
	g_hash_table_insert (header_name_table, "Content-Type", (gpointer)HEADER_CONTENT_TYPE);

	header_formatted_table = g_hash_table_new (camel_lite_strcase_hash, camel_lite_strcase_equal);
	g_hash_table_insert(header_formatted_table, "Content-Type", write_raw);
	g_hash_table_insert(header_formatted_table, "Content-Disposition", write_raw);
	g_hash_table_insert(header_formatted_table, "To", write_raw);
	g_hash_table_insert(header_formatted_table, "From", write_raw);
	g_hash_table_insert(header_formatted_table, "Reply-To", write_raw);
	g_hash_table_insert(header_formatted_table, "Cc", write_raw);
	g_hash_table_insert(header_formatted_table, "Bcc", write_raw);
	g_hash_table_insert(header_formatted_table, "Message-ID", write_raw);
	g_hash_table_insert(header_formatted_table, "In-Reply-To", write_raw);
	g_hash_table_insert(header_formatted_table, "References", write_references);
}

static void
camel_lite_mime_part_class_init (CamelMimePartClass *camel_lite_mime_part_class)
{
	CamelMediumClass *camel_lite_medium_class = CAMEL_MEDIUM_CLASS (camel_lite_mime_part_class);
	CamelDataWrapperClass *camel_lite_data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (camel_lite_mime_part_class);

	parent_class = CAMEL_MEDIUM_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_medium_get_type ()));
	init_header_name_table();

	camel_lite_mime_part_class->construct_from_parser = construct_from_parser;

	/* virtual method overload */
	camel_lite_medium_class->add_header                = add_header;
	camel_lite_medium_class->set_header                = set_header;
	camel_lite_medium_class->get_header                = get_header;
	camel_lite_medium_class->remove_header             = remove_header;
	camel_lite_medium_class->get_headers               = get_headers;
	camel_lite_medium_class->free_headers              = free_headers;
	camel_lite_medium_class->set_content_object        = set_content_object;

	camel_lite_data_wrapper_class->write_to_stream     = write_to_stream;
	camel_lite_data_wrapper_class->construct_from_stream= construct_from_stream;
}

static void
camel_lite_mime_part_init (gpointer object, gpointer klass)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (object);

	if (((CamelDataWrapper *) mime_part)->mime_type)
		camel_lite_content_type_unref (((CamelDataWrapper *) mime_part)->mime_type);
	((CamelDataWrapper *) mime_part)->mime_type = camel_lite_content_type_new ("text", "plain");

	mime_part->description          = NULL;
	mime_part->disposition          = NULL;
	mime_part->content_id           = NULL;
	mime_part->content_MD5          = NULL;
	mime_part->content_location     = NULL;
	mime_part->content_languages    = NULL;
	mime_part->encoding = CAMEL_TRANSFER_ENCODING_DEFAULT;
}


static void
camel_lite_mime_part_finalize (CamelObject *object)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (object);

	g_free (mime_part->description);
	g_free (mime_part->content_id);
	g_free (mime_part->content_MD5);
	g_free (mime_part->content_location);
	camel_lite_string_list_free (mime_part->content_languages);
	camel_lite_content_disposition_unref(mime_part->disposition);

	camel_lite_header_raw_clear(&mime_part->headers);
}



CamelType
camel_lite_mime_part_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE)	{
		type = camel_lite_type_register (CAMEL_MEDIUM_TYPE,
					    "CamelLiteMimePart",
					    sizeof (CamelMimePart),
					    sizeof (CamelMimePartClass),
					    (CamelObjectClassInitFunc) camel_lite_mime_part_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_mime_part_init,
					    (CamelObjectFinalizeFunc) camel_lite_mime_part_finalize);
	}

	return type;
}


/* **** */

static gboolean
process_header(CamelMedium *medium, const char *name, const char *value)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (medium);
	CamelHeaderType header_type;
	const char *charset;
	char *text;

	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	header_type = (CamelHeaderType) g_hash_table_lookup (header_name_table, name);
	switch (header_type) {
	case HEADER_DESCRIPTION: /* raw header->utf8 conversion */
		g_free (mime_part->description);
		if (((CamelDataWrapper *) mime_part)->mime_type) {
			charset = camel_lite_content_type_param (((CamelDataWrapper *) mime_part)->mime_type, "charset");
			charset = e_iconv_charset_name (charset);
		} else
			charset = NULL;
		mime_part->description = g_strstrip (camel_lite_header_decode_string (value, charset));
		break;
	case HEADER_DISPOSITION:
		set_disposition (mime_part, value);
		break;
	case HEADER_CONTENT_ID:
		g_free (mime_part->content_id);
		mime_part->content_id = camel_lite_header_contentid_decode (value);
		break;
	case HEADER_ENCODING:
		text = camel_lite_header_token_decode (value);
		mime_part->encoding = camel_lite_transfer_encoding_from_string (text);
		g_free (text);
		break;
	case HEADER_CONTENT_MD5:
		g_free (mime_part->content_MD5);
		mime_part->content_MD5 = g_strdup (value);
		break;
	case HEADER_CONTENT_LOCATION:
		g_free (mime_part->content_location);
		mime_part->content_location = camel_lite_header_location_decode (value);
		break;
	case HEADER_CONTENT_TYPE:
		if (((CamelDataWrapper *) mime_part)->mime_type)
			camel_lite_content_type_unref (((CamelDataWrapper *) mime_part)->mime_type);
		((CamelDataWrapper *) mime_part)->mime_type = camel_lite_content_type_decode (value);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static void
set_header (CamelMedium *medium, const char *name, const void *value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);

	process_header(medium, name, value);
	camel_lite_header_raw_replace(&part->headers, name, value, -1);
}

static void
add_header (CamelMedium *medium, const char *name, const void *value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);

	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	/* If it was one of the headers we handled, it must be unique, set it instead of add */
	if (process_header(medium, name, value))
		camel_lite_header_raw_replace(&part->headers, name, value, -1);
	else
		camel_lite_header_raw_append(&part->headers, name, value, -1);
}

static void
remove_header (CamelMedium *medium, const char *name)
{
	CamelMimePart *part = (CamelMimePart *)medium;

	process_header(medium, name, NULL);
	camel_lite_header_raw_remove(&part->headers, name);
}

static const void *
get_header (CamelMedium *medium, const char *name)
{
	CamelMimePart *part = (CamelMimePart *)medium;

	return camel_lite_header_raw_find(&part->headers, name, NULL);
}

static GArray *
get_headers (CamelMedium *medium)
{
	CamelMimePart *part = (CamelMimePart *)medium;
	GArray *headers;
	CamelMediumHeader header;
	struct _camel_lite_header_raw *h;

	headers = g_array_new (FALSE, FALSE, sizeof (CamelMediumHeader));
	for (h = part->headers; h; h = h->next) {
		header.name = h->name;
		header.value = h->value;
		g_array_append_val (headers, header);
	}

	return headers;
}

static void
free_headers (CamelMedium *medium, GArray *gheaders)
{
	g_array_free (gheaders, TRUE);
}

/* **** Content-Description */

/**
 * camel_lite_mime_part_set_description:
 * @mime_part: a #CamelMimePart object
 * @description: description of the MIME part
 *
 * Set a description on the MIME part.
 **/
void
camel_lite_mime_part_set_description (CamelMimePart *mime_part, const char *description)
{
	char *text = camel_lite_header_encode_string ((const unsigned char *) description);

	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Description", text);
	g_free (text);
}


/**
 * camel_lite_mime_part_get_description:
 * @mime_part: a #CamelMimePart object
 *
 * Get the description of the MIME part.
 *
 * Returns the description
 **/
const char *
camel_lite_mime_part_get_description (CamelMimePart *mime_part)
{
	return mime_part->description;
}

/* **** Content-Disposition */

static void
set_disposition (CamelMimePart *mime_part, const char *disposition)
{

	camel_lite_content_disposition_unref(mime_part->disposition);
	if (disposition) {
		const char *charset = NULL;

		if (!g_utf8_validate (disposition, -1, NULL) && mime_part->headers) {
			struct _camel_lite_header_raw *h = mime_part->headers;
			const char *content;
			CamelContentType *ct = NULL;

			if ((content = camel_lite_header_raw_find(&h, "Content-Type", NULL))
			     && (ct = camel_lite_content_type_decode(content))
			     && (charset = camel_lite_content_type_param(ct, "charset"))
			     && (g_ascii_strcasecmp(charset, "us-ascii") == 0))
				charset = NULL;

			charset = charset ? e_iconv_charset_name (charset) : NULL;

			if (ct)
				camel_lite_content_type_unref (ct);
		}

		mime_part->disposition = camel_lite_content_disposition_decode (disposition, charset);
	} else
		mime_part->disposition = NULL;
}


/**
 * camel_lite_mime_part_set_disposition:
 * @mime_part: a #CamelMimePart object
 * @disposition: disposition of the MIME part
 *
 * Set a disposition on the MIME part.
 **/
void
camel_lite_mime_part_set_disposition (CamelMimePart *mime_part, const char *disposition)
{
	char *text;

	/* we poke in a new disposition (so we dont lose 'filename', etc) */
	if (mime_part->disposition == NULL) {
		set_disposition(mime_part, disposition);
	}
	if (mime_part->disposition != NULL) {
		g_free(mime_part->disposition->disposition);
		mime_part->disposition->disposition = g_strdup(disposition);
	}
	text = camel_lite_content_disposition_format(mime_part->disposition);

	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Disposition", text);

	g_free(text);
}


/**
 * camel_lite_mime_part_get_disposition:
 * @mime_part: a #CamelMimePart object
 *
 * Get the disposition of the MIME part.
 *
 * Returns the dispisition
 **/
const char *
camel_lite_mime_part_get_disposition (CamelMimePart *mime_part)
{
	if (mime_part->disposition)
		return mime_part->disposition->disposition;
	else
		return NULL;
}


/* **** Content-Disposition: filename="xxx" */

/**
 * camel_lite_mime_part_set_filename:
 * @mime_part: a #CamelMimePart object
 * @filename: filename given to the MIME part
 *
 * Set the filename on a MIME part.
 **/
void
camel_lite_mime_part_set_filename (CamelMimePart *mime_part, const char *filename)
{
	char *str;
	CamelDataWrapper *dw;

	if (mime_part->disposition == NULL)
		mime_part->disposition = camel_lite_content_disposition_decode("attachment", NULL);

	camel_lite_header_set_param(&mime_part->disposition->params, "filename", filename);
	str = camel_lite_content_disposition_format(mime_part->disposition);

	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Disposition", str);
	g_free(str);

	dw = (CamelDataWrapper *) mime_part;
	if (!dw->mime_type)
		dw->mime_type = camel_lite_content_type_new ("application", "octet-stream");
	camel_lite_content_type_set_param (dw->mime_type, "name", filename);
	str = camel_lite_content_type_format (dw->mime_type);
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-Type", str);
	g_free (str);
}


/**
 * camel_lite_mime_part_get_filename:
 * @mime_part: a #CamelMimePart object
 *
 * Get the filename of a MIME part.
 *
 * Returns the filename of the MIME part
 **/
const char *
camel_lite_mime_part_get_filename (CamelMimePart *mime_part)
{
	if (mime_part->disposition) {
		const char *name = camel_lite_header_param (mime_part->disposition->params, "filename");
		if (name)
			return name;
	}

	return camel_lite_content_type_param (((CamelDataWrapper *) mime_part)->mime_type, "name");
}


/* **** Content-ID: */

/**
 * camel_lite_mime_part_set_content_id:
 * @mime_part: a #CamelMimePart object
 * @contentid: content id
 *
 * Set the content-id field on a MIME part.
 **/
void
camel_lite_mime_part_set_content_id (CamelMimePart *mime_part, const char *contentid)
{
	char *cid, *id;

	if (contentid)
		id = g_strstrip (g_strdup (contentid));
	else
		id = camel_lite_header_msgid_generate ();

	cid = g_strdup_printf ("<%s>", id);
	g_free (id);
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-ID", cid);
	g_free (cid);
}


/**
 * camel_lite_mime_part_get_content_id:
 * @mime_part: a #CamelMimePart object
 *
 * Get the content-id field of a MIME part.
 *
 * Returns the content-id field of the MIME part
 **/
const char *
camel_lite_mime_part_get_content_id (CamelMimePart *mime_part)
{
	return mime_part->content_id;
}

/* **** Content-MD5: */

/**
 * camel_lite_mime_part_set_content_MD5:
 * @mime_part: a #CamelMimePart object
 * @md5sum: the md5sum of the MIME part
 *
 * Set the content-md5 field of the MIME part.
 **/
void
camel_lite_mime_part_set_content_MD5 (CamelMimePart *mime_part, const char *md5)
{
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-MD5", md5);
}


/**
 * camel_lite_mime_part_get_content_MD5:
 * @mime_part: a #CamelMimePart object
 *
 * Get the content-md5 field of the MIME part.
 *
 * Returns the content-md5 field of the MIME part
 **/
const char *
camel_lite_mime_part_get_content_MD5 (CamelMimePart *mime_part)
{
	return mime_part->content_MD5;
}

/* **** Content-Location: */


/**
 * camel_lite_mime_part_set_content_location:
 * @mime_part: a #CamelMimePart object
 * @location: the content-location value of the MIME part
 *
 * Set the content-location field of the MIME part.
 **/
void
camel_lite_mime_part_set_content_location (CamelMimePart *mime_part, const char *location)
{
	/* FIXME: this should perform content-location folding */
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-Location", location);
}


/**
 * camel_lite_mime_part_get_content_location:
 * @mime_part: a #CamelMimePart object
 *
 * Get the content-location field of a MIME part.
 *
 * Returns the content-location field of a MIME part
 **/
const char *
camel_lite_mime_part_get_content_location (CamelMimePart *mime_part)
{
	return mime_part->content_location;
}

/* **** Content-Transfer-Encoding: */


/**
 * camel_lite_mime_part_set_encoding:
 * @mime_part: a #CamelMimePart object
 * @encoding: a #CamelTransferEncoding
 *
 * Set the Content-Transfer-Encoding to use on a MIME part.
 **/
void
camel_lite_mime_part_set_encoding (CamelMimePart *mime_part,
			      CamelTransferEncoding encoding)
{
	const char *text;

	text = camel_lite_transfer_encoding_to_string (encoding);
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Transfer-Encoding", text);
}


/**
 * camel_lite_mime_part_get_encoding:
 * @mime_part: a #CamelMimePart object
 *
 * Get the Content-Transfer-Encoding of a MIME part.
 *
 * Returns a #CamelTransferEncoding
 **/
CamelTransferEncoding
camel_lite_mime_part_get_encoding (CamelMimePart *mime_part)
{
	return mime_part->encoding;
}

/* FIXME: do something with this stuff ... */


/**
 * camel_lite_mime_part_set_content_languages:
 * @mime_part: a #CamelMimePart object
 * @content_languages: list of languages
 *
 * Set the Content-Languages field of a MIME part.
 **/
void
camel_lite_mime_part_set_content_languages (CamelMimePart *mime_part, GList *content_languages)
{
	if (mime_part->content_languages)
		camel_lite_string_list_free (mime_part->content_languages);

	mime_part->content_languages = content_languages;

	/* FIXME: translate to a header and set it */
}


/**
 * camel_lite_mime_part_get_content_languages:
 * @mime_part: a #CamelMimePart object
 *
 * Get the Content-Languages set on the MIME part.
 *
 * Returns a #GList of languages
 **/
const GList *
camel_lite_mime_part_get_content_languages (CamelMimePart *mime_part)
{
	return mime_part->content_languages;
}


/* **** */

/* **** Content-Type: */

/**
 * camel_lite_mime_part_set_content_type:
 * @mime_part: a #CamelMimePart object
 * @content_type: content-type string
 *
 * Set the content-type on a MIME part.
 **/
void
camel_lite_mime_part_set_content_type (CamelMimePart *mime_part, const char *content_type)
{
	camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Type", content_type);
}


/**
 * camel_lite_mime_part_get_content_type:
 * @mime_part: a #CamelMimePart object
 *
 * Get the Content-Type of a MIME part.
 *
 * Returns the parsed #CamelContentType of the MIME part
 **/
CamelContentType *
camel_lite_mime_part_get_content_type (CamelMimePart *mime_part)
{
	return ((CamelDataWrapper *) mime_part)->mime_type;
}

static void
set_content_object (CamelMedium *medium, CamelDataWrapper *content)
{
	CamelDataWrapper *mime_part = CAMEL_DATA_WRAPPER (medium);
	CamelContentType *content_type;

	parent_class->set_content_object (medium, content);

	content_type = camel_lite_data_wrapper_get_mime_type_field (content);
	if (mime_part->mime_type != content_type) {
		char *txt;

		txt = camel_lite_content_type_format (content_type);
		camel_lite_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-Type", txt);
		g_free (txt);
	}
}

/**********************************************************************/

static ssize_t
write_references(CamelStream *stream, struct _camel_lite_header_raw *h)
{
	ssize_t len, out, total;
	char *v, *ids, *ide;

	/* this is only approximate, based on the next >, this way it retains any content
	   from the original which may not be properly formatted, etc.  It also doesn't handle
	   the case where an individual messageid is too long, however thats a bad mail to
	   start with ... */

	v = h->value;
	len = strlen(h->name)+1;
	total = camel_lite_stream_printf(stream, "%s%s", h->name, isspace(v[0])?":":": ");
	if (total == -1)
		return -1;
	while (*v) {
		ids = v;
		ide = strchr(ids+1, '>');
		if (ide)
			v = ++ide;
		else
			ide = v = strlen(ids)+ids;

		if (len>0 && len + (ide - ids) >= CAMEL_FOLD_SIZE) {
			out = camel_lite_stream_printf(stream, "\n\t");
			if (out == -1)
				return -1;
			total += out;
			len = 0;
		}
		out = camel_lite_stream_write(stream, ids, ide-ids);
		if (out == -1)
			return -1;
		len += out;
		total += out;
	}
	camel_lite_stream_write(stream, "\n", 1);

	return total;
}

#if 0
/* not needed - yet - handled by default case */
static ssize_t
write_fold(CamelStream *stream, struct _camel_lite_header_raw *h)
{
	char *val;
	int count;

	val = camel_lite_header_fold(h->value, strlen(h->name));
	count = camel_lite_stream_printf(stream, "%s%s%s\n", h->name, isspace(val[0]) ? ":" : ": ", val);
	g_free(val);

	return count;
}
#endif

static ssize_t
write_raw(CamelStream *stream, struct _camel_lite_header_raw *h)
{
	char *val = h->value;

	return camel_lite_stream_printf(stream, "%s%s%s\n", h->name, isspace(val[0]) ? ":" : ": ", val);
}

static ssize_t
write_to_stream (CamelDataWrapper *dw, CamelStream *stream)
{
	CamelMimePart *mp = CAMEL_MIME_PART (dw);
	CamelMedium *medium = CAMEL_MEDIUM (dw);
	CamelStream *ostream = stream;
	CamelDataWrapper *content;
	ssize_t total = 0;
	ssize_t count;
	int errnosav;
	const gchar *disposition;

	d(printf("mime_part::write_to_stream\n"));

	/* FIXME: something needs to be done about this ... */
	/* TODO: content-languages header? */

	if (mp->headers) {
		struct _camel_lite_header_raw *h = mp->headers;
		char *val;
		ssize_t (*writefn)(CamelStream *stream, struct _camel_lite_header_raw *);

		/* fold/write the headers.   But dont fold headers that are already formatted
		   (e.g. ones with parameter-lists, that we know about, and have created) */
		while (h) {
			val = h->value;
			if (val == NULL) {
				g_warning("h->value is NULL here for %s", h->name);
				count = 0;
			} else if ((writefn = g_hash_table_lookup(header_formatted_table, h->name)) == NULL) {
				val = camel_lite_header_fold(val, strlen(h->name));
				count = camel_lite_stream_printf(stream, "%s%s%s\n", h->name, isspace(val[0]) ? ":" : ": ", val);
				g_free(val);
			} else {
				count = writefn(stream, h);
			}
			if (count == -1)
				return -1;
			total += count;
			h = h->next;
		}
	}

	count = camel_lite_stream_write(stream, "\n", 1);
	if (count == -1)
		return -1;
	total += count;

	disposition = camel_lite_mime_part_get_disposition (mp);

	if (disposition != NULL && !strcmp (disposition, "purged"))
		camel_lite_mime_part_set_content (mp, " ", 1, "text/plain");

	content = camel_lite_medium_get_content_object(medium);
	if (content) {
		CamelMimeFilter *filter = NULL;
		CamelStreamFilter *filter_stream = NULL;
		CamelMimeFilter *charenc = NULL;
		const char *content_charset = NULL;
		const char *part_charset = NULL;
		gboolean reencode = FALSE;
		const char *filename;

		if (camel_lite_content_type_is (dw->mime_type, "text", "*")) {
			content_charset = camel_lite_content_type_param (content->mime_type, "charset");
			part_charset = camel_lite_content_type_param (dw->mime_type, "charset");

			if (content_charset && part_charset) {
				content_charset = e_iconv_charset_name (content_charset);
				part_charset = e_iconv_charset_name (part_charset);
			}
		}

		if (mp->encoding != content->encoding) {
			switch (mp->encoding) {
			case CAMEL_TRANSFER_ENCODING_BASE64:
				filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
				break;
			case CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE:
				filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_QP_ENC);
				break;
			case CAMEL_TRANSFER_ENCODING_UUENCODE:
				filename = camel_lite_mime_part_get_filename (mp);
				count = camel_lite_stream_printf (ostream, "begin 644 %s\n", filename ? filename : "untitled");
				if (count == -1)
					return -1;
				total += count;
				filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_UU_ENC);
				break;
			default:
				/* content is encoded but the part doesn't want to be... */
				reencode = TRUE;
				break;
			}
		}

		if (content_charset && part_charset && part_charset != content_charset)
			charenc = (CamelMimeFilter *) camel_lite_mime_filter_charset_new_convert (content_charset, part_charset);

		if (filter || charenc) {
			filter_stream = camel_lite_stream_filter_new_with_stream(stream);

			/* if we have a character encoder, add that always */
			if (charenc) {
				camel_lite_stream_filter_add(filter_stream, charenc);
				camel_lite_object_unref (charenc);
			}

			/* we only re-do crlf on encoded blocks */
			if (filter && camel_lite_content_type_is (dw->mime_type, "text", "*")) {
				CamelMimeFilter *crlf = camel_lite_mime_filter_crlf_new(CAMEL_MIME_FILTER_CRLF_ENCODE,
										   CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);

				camel_lite_stream_filter_add(filter_stream, crlf);
				camel_lite_object_unref (crlf);
			}

			if (filter) {
				camel_lite_stream_filter_add(filter_stream, filter);
				camel_lite_object_unref (filter);
			}

			stream = (CamelStream *)filter_stream;

			reencode = TRUE;
		}

		if (reencode)
			count = camel_lite_data_wrapper_decode_to_stream (content, stream);
		else
			count = camel_lite_data_wrapper_write_to_stream (content, stream);

		if (filter_stream) {
			errnosav = errno;
			camel_lite_stream_flush (stream);
			camel_lite_object_unref (filter_stream);
			errno = errnosav;
		}

		if (count == -1)
			return -1;

		total += count;

		if (reencode && mp->encoding == CAMEL_TRANSFER_ENCODING_UUENCODE) {
			count = camel_lite_stream_write (ostream, "end\n", 4);
			if (count == -1)
				return -1;
			total += count;
		}
	} else {
		g_warning("No content for medium, nothing to write");
	}

	return total;
}

/* mime_part */
static int
construct_from_parser (CamelMimePart *mime_part, CamelMimeParser *mp)
{
	CamelDataWrapper *dw = (CamelDataWrapper *) mime_part;
	struct _camel_lite_header_raw *headers;
	const char *content;
	char *buf;
	size_t len;
	int err;

	d(printf("mime_part::construct_from_parser()\n"));

	switch (camel_lite_mime_parser_step(mp, &buf, &len)) {
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		/* set the default type of a message always */
		if (dw->mime_type)
			camel_lite_content_type_unref (dw->mime_type);
		dw->mime_type = camel_lite_content_type_decode ("message/rfc822");
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		/* we have the headers, build them into 'us' */
		headers = camel_lite_mime_parser_headers_raw(mp);

		/* if content-type exists, process it first, set for fallback charset in headers */
		content = camel_lite_header_raw_find(&headers, "content-type", NULL);
		if (content)
			process_header((CamelMedium *)dw, "content-type", content);

		while (headers) {
			if (g_ascii_strcasecmp(headers->name, "content-type") == 0
			    && headers->value != content)
				camel_lite_medium_add_header((CamelMedium *)dw, "X-Invalid-Content-Type", headers->value);
			else
				camel_lite_medium_add_header((CamelMedium *)dw, headers->name, headers->value);
			headers = headers->next;
		}

		camel_lite_mime_part_construct_content_from_parser (mime_part, mp);
		break;
	default:
		g_warning("Invalid state encountered???: %u", camel_lite_mime_parser_state(mp));
	}

	d(printf("mime_part::construct_from_parser() leaving\n"));
	err = camel_lite_mime_parser_errno(mp);
	if (err != 0) {
		errno = err;
		return -1;
	}

	return 0;
}

/**
 * camel_lite_mime_part_construct_from_parser:
 * @mime_part: a #CamelMimePart object
 * @parser: a #CamelMimeParser object
 *
 * Constructs a MIME part from a parser.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_mime_part_construct_from_parser(CamelMimePart *mime_part, CamelMimeParser *mp)
{
	return CMP_CLASS (mime_part)->construct_from_parser (mime_part, mp);
}

static int
construct_from_stream(CamelDataWrapper *dw, CamelStream *s)
{
	CamelMimeParser *mp;
	int ret;

	d(printf("mime_part::construct_from_stream()\n"));

	mp = camel_lite_mime_parser_new();
	if (camel_lite_mime_parser_init_with_stream(mp, s) == -1) {
		g_warning("Cannot create parser for stream");
		ret = -1;
	} else {
		ret = camel_lite_mime_part_construct_from_parser((CamelMimePart *)dw, mp);
	}
	camel_lite_object_unref((CamelObject *)mp);
	return ret;
}

/******************************/
/**  Misc utility functions  **/

/**
 * camel_lite_mime_part_new:
 *
 * Create a new MIME part.
 *
 * Returns a new #CamelMimePart object
 **/
CamelMimePart *
camel_lite_mime_part_new (void)
{
	return (CamelMimePart *)camel_lite_object_new (CAMEL_MIME_PART_TYPE);
}

/**
 * camel_lite_mime_part_set_content:
 * @mime_part: a #CamelMimePart object
 * @data: data to put into the part
 * @length: length of @data
 * @type: Content-Type of the data
 *
 * Utility function used to set the content of a mime part object to
 * be the provided data. If @length is 0, this routine can be used as
 * a way to remove old content (in which case @data and @type are
 * ignored and may be %NULL).
 **/
void
camel_lite_mime_part_set_content (CamelMimePart *mime_part,
			     const char *data, int length,
			     const char *type) /* why on earth is the type last? */
{
	CamelMedium *medium = CAMEL_MEDIUM (mime_part);

	if (length) {
		CamelDataWrapper *dw;
		CamelStream *stream;

		dw = camel_lite_data_wrapper_new ();
		camel_lite_data_wrapper_set_mime_type (dw, type);
		stream = camel_lite_stream_mem_new_with_buffer (data, length);
		camel_lite_data_wrapper_construct_from_stream (dw, stream);
		camel_lite_object_unref (stream);
		camel_lite_medium_set_content_object (medium, dw);
		camel_lite_object_unref (dw);
	} else {
		if (medium->content)
			camel_lite_object_unref (medium->content);
		medium->content = NULL;
	}
}

/**
 * camel_lite_mime_part_get_content_size:
 * @mime_part: a #CamelMimePart object
 *
 * Get the decoded size of the MIME part's content.
 *
 * Returns the size of the MIME part's content in bytes.
 **/
size_t
camel_lite_mime_part_get_content_size (CamelMimePart *mime_part)
{
	CamelStreamNull *null;
	CamelDataWrapper *dw;
	size_t size;
	
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), 0);
	
	dw = camel_lite_medium_get_content_object (CAMEL_MEDIUM (mime_part));
	
	null = (CamelStreamNull *) camel_lite_stream_null_new ();
	camel_lite_data_wrapper_decode_to_stream (dw, (CamelStream *) null);
	size = null->written;
	
	camel_lite_object_unref (null);
	
	return size;
}
