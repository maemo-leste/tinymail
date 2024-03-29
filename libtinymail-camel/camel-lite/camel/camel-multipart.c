/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-multipart.c : Abstract class for a multipart */
/*
 *
 * Author :
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

#include <errno.h>
#include <string.h> /* strlen() */
#include <time.h>   /* for time */
#include <unistd.h> /* for getpid */

#include <libedataserver/md5-utils.h>

#include "camel-exception.h"
#include "camel-mime-part.h"
#include "camel-multipart.h"
#include "camel-stream-mem.h"

#define d(x)

static gboolean              is_offline        (CamelDataWrapper *data_wrapper);
static void                  add_part          (CamelMultipart *multipart,
						CamelMimePart *part);
static void                  add_part_at       (CamelMultipart *multipart,
						CamelMimePart *part,
						guint index);
static void                  remove_part       (CamelMultipart *multipart,
						CamelMimePart *part);
static CamelMimePart *       remove_part_at    (CamelMultipart *multipart,
						guint index);
static CamelMimePart *       get_part          (CamelMultipart *multipart,
						guint index);
static guint                 get_number        (CamelMultipart *multipart);
static void                  set_boundary      (CamelMultipart *multipart,
						const char *boundary);
static const gchar *         get_boundary      (CamelMultipart *multipart);
static ssize_t               write_to_stream   (CamelDataWrapper *data_wrapper,
						CamelStream *stream);

static int construct_from_parser(CamelMultipart *multipart, struct _CamelMimeParser *mp);

static CamelDataWrapperClass *parent_class = NULL;



/* Returns the class for a CamelMultipart */
#define CMP_CLASS(so) CAMEL_MULTIPART_CLASS (CAMEL_OBJECT_GET_CLASS(so))

/* Returns the class for a CamelDataWrapper */
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))


static void
camel_lite_multipart_class_init (CamelMultipartClass *camel_lite_multipart_class)
{
	CamelDataWrapperClass *camel_lite_data_wrapper_class =
		CAMEL_DATA_WRAPPER_CLASS (camel_lite_multipart_class);

	parent_class = (CamelDataWrapperClass *) camel_lite_data_wrapper_get_type ();

	/* virtual method definition */
	camel_lite_multipart_class->add_part = add_part;
	camel_lite_multipart_class->add_part_at = add_part_at;
	camel_lite_multipart_class->remove_part = remove_part;
	camel_lite_multipart_class->remove_part_at = remove_part_at;
	camel_lite_multipart_class->get_part = get_part;
	camel_lite_multipart_class->get_number = get_number;
	camel_lite_multipart_class->set_boundary = set_boundary;
	camel_lite_multipart_class->get_boundary = get_boundary;
	camel_lite_multipart_class->construct_from_parser = construct_from_parser;

	/* virtual method overload */
	camel_lite_data_wrapper_class->write_to_stream = write_to_stream;
	camel_lite_data_wrapper_class->decode_to_stream = write_to_stream;
	camel_lite_data_wrapper_class->is_offline = is_offline;
}

static void
camel_lite_multipart_init (gpointer object, gpointer klass)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	camel_lite_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart),
					  "multipart/mixed");
	multipart->parts = NULL;
	multipart->preface = NULL;
	multipart->postface = NULL;
}

static void
camel_lite_multipart_finalize (CamelObject *object)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	g_list_foreach (multipart->parts, (GFunc) camel_lite_object_unref, NULL);

	if (multipart->parts)
		g_list_free (multipart->parts);

	/*if (multipart->boundary)
	  g_free (multipart->boundary);*/

	if (multipart->preface)
		g_free (multipart->preface);
	if (multipart->postface)
		g_free (multipart->postface);
}


CamelType
camel_lite_multipart_get_type (void)
{
	static CamelType camel_lite_multipart_type = CAMEL_INVALID_TYPE;

	if (camel_lite_multipart_type == CAMEL_INVALID_TYPE) {
		camel_lite_multipart_type = camel_lite_type_register (camel_lite_data_wrapper_get_type (), "CamelLiteMultipart",
							    sizeof (CamelMultipart),
							    sizeof (CamelMultipartClass),
							    (CamelObjectClassInitFunc) camel_lite_multipart_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_lite_multipart_init,
							    (CamelObjectFinalizeFunc) camel_lite_multipart_finalize);
	}

	return camel_lite_multipart_type;
}


/**
 * camel_lite_multipart_new:
 *
 * Create a new #CamelMultipart object.
 *
 * Returns a new #CamelMultipart object
 **/
CamelMultipart *
camel_lite_multipart_new (void)
{
	CamelMultipart *multipart;

	multipart = (CamelMultipart *)camel_lite_object_new (CAMEL_MULTIPART_TYPE);
	multipart->preface = NULL;
	multipart->postface = NULL;

	return multipart;
}


static void
add_part (CamelMultipart *multipart, CamelMimePart *part)
{
	multipart->parts = g_list_append (multipart->parts, part);
	camel_lite_object_ref (part);
}


/**
 * camel_lite_multipart_add_part:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to add
 *
 * Appends the part to the multipart object.
 **/
void
camel_lite_multipart_add_part (CamelMultipart *multipart, CamelMimePart *part)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->add_part (multipart, part);
}


static void
add_part_at (CamelMultipart *multipart, CamelMimePart *part, guint index)
{
	multipart->parts = g_list_insert (multipart->parts, part, index);
	camel_lite_object_ref (part);
}

/**
 * camel_lite_multipart_add_part_at:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to add
 * @index: index to add the multipart at
 *
 * Adds the part to the multipart object after the @index'th
 * element. If @index is greater than the number of parts, it is
 * equivalent to #camel_lite_multipart_add_part.
 **/
void
camel_lite_multipart_add_part_at (CamelMultipart *multipart,
			     CamelMimePart *part, guint index)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->add_part_at (multipart, part, index);
}


static void
remove_part (CamelMultipart *multipart, CamelMimePart *part)
{
	if (!multipart->parts)
		return;
	multipart->parts = g_list_remove (multipart->parts, part);
	camel_lite_object_unref (part);
}

/**
 * camel_lite_multipart_remove_part:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to remove
 *
 * Removes @part from @multipart.
 **/
void
camel_lite_multipart_remove_part (CamelMultipart *multipart,
			     CamelMimePart *part)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->remove_part (multipart, part);
}


static CamelMimePart *
remove_part_at (CamelMultipart *multipart, guint index)
{
	GList *parts_list;
	GList *part_to_remove;
	CamelMimePart *removed_part;

	if (!(multipart->parts))
		return NULL;

	parts_list = multipart->parts;
	part_to_remove = g_list_nth (parts_list, index);
	if (!part_to_remove) {
		g_warning ("CamelLiteMultipart::remove_part_at: "
			   "part to remove is NULL\n");
		return NULL;
	}
	removed_part = CAMEL_MIME_PART (part_to_remove->data);

	multipart->parts = g_list_remove_link (parts_list, part_to_remove);
	if (part_to_remove->data)
		camel_lite_object_unref (part_to_remove->data);
	g_list_free_1 (part_to_remove);

	return removed_part;
}

/**
 * camel_lite_multipart_remove_part_at:
 * @multipart: a #CamelMultipart object
 * @index: a zero-based index indicating the part to remove
 *
 * Remove the indicated part from the multipart object.
 *
 * Returns the removed part. Note that it is #camel_lite_object_unref'ed
 * before being returned, which may cause it to be destroyed.
 **/
CamelMimePart *
camel_lite_multipart_remove_part_at (CamelMultipart *multipart, guint index)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	return CMP_CLASS (multipart)->remove_part_at (multipart, index);
}


static CamelMimePart *
get_part (CamelMultipart *multipart, guint index)
{
	GList *part;

	if (!(multipart->parts))
		return NULL;

	part = g_list_nth (multipart->parts, index);
	if (part)
		return CAMEL_MIME_PART (part->data);
	else
		return NULL;
}

/**
 * camel_lite_multipart_get_part:
 * @multipart: a #CamelMultipart object
 * @index: a zero-based index indicating the part to get
 *
 * Returns the indicated subpart, or %NULL
 **/
CamelMimePart *
camel_lite_multipart_get_part (CamelMultipart *multipart, guint index)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	return CMP_CLASS (multipart)->get_part (multipart, index);
}

CamelMimePart *
camel_lite_multipart_get_part_wref (CamelMultipart *multipart, guint index)
{
	CamelObject *retval = NULL;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	retval = (CamelObject *) CMP_CLASS (multipart)->get_part (multipart, index);

	if (retval)
		camel_lite_object_ref (retval);

	return (CamelMimePart *) retval;
}

static guint
get_number (CamelMultipart *multipart)
{
	return g_list_length (multipart->parts);
}

/**
 * camel_lite_multipart_get_number:
 * @multipart: a #CamelMultipart object
 *
 * Returns the number of subparts in @multipart
 **/
guint
camel_lite_multipart_get_number (CamelMultipart *multipart)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), 0);

	return CMP_CLASS (multipart)->get_number (multipart);
}


static void
set_boundary (CamelMultipart *multipart, const char *boundary)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);
	char *bgen, digest[16], bbuf[27], *p;
	int state, save;

	g_return_if_fail (cdw->mime_type != NULL);

	if (!boundary) {
		/* Generate a fairly random boundary string. */
		bgen = g_strdup_printf ("%p:%lu:%lu", (void *) multipart,
					(unsigned long) getpid(),
					(unsigned long) time(NULL));
		md5_get_digest (bgen, strlen (bgen), (unsigned char *) digest);
		g_free (bgen);
		strcpy (bbuf, "=-");
		p = bbuf + 2;
		state = save = 0;
		p += g_base64_encode_step ((guchar *) digest, 16, FALSE, p, &state, &save);
		*p = '\0';

		boundary = bbuf;
	}

	camel_lite_content_type_set_param (cdw->mime_type, "boundary", boundary);
}

/**
 * camel_lite_multipart_set_boundary:
 * @multipart: a #CamelMultipart object
 * @boundary: the message boundary, or %NULL
 *
 * Sets the message boundary for @multipart to @boundary. This should
 * be a string which does not occur anywhere in any of @multipart's
 * subparts. If @boundary is %NULL, a randomly-generated boundary will
 * be used.
 **/
void
camel_lite_multipart_set_boundary (CamelMultipart *multipart, const char *boundary)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));

	CMP_CLASS (multipart)->set_boundary (multipart, boundary);
}


static const gchar *
get_boundary (CamelMultipart *multipart)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);

	g_return_val_if_fail (cdw->mime_type != NULL, NULL);
	return camel_lite_content_type_param (cdw->mime_type, "boundary");
}

/**
 * camel_lite_multipart_get_boundary:
 * @multipart: a #CamelMultipart object
 *
 * Returns the boundary
 **/
const char *
camel_lite_multipart_get_boundary (CamelMultipart *multipart)
{
	return CMP_CLASS (multipart)->get_boundary (multipart);
}

static gboolean
is_offline (CamelDataWrapper *data_wrapper)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (data_wrapper);
	GList *node;
	CamelDataWrapper *part;

	if (parent_class->is_offline (data_wrapper))
		return TRUE;
	for (node = multipart->parts; node; node = node->next) {
		part = node->data;
		if (camel_lite_data_wrapper_is_offline (part))
			return TRUE;
	}

	return FALSE;
}

/* this is MIME specific, doesn't belong here really */
static ssize_t
write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (data_wrapper);
	const gchar *boundary;
	ssize_t total = 0;
	ssize_t count;
	GList *node;

	/* get the bundary text */
	boundary = camel_lite_multipart_get_boundary (multipart);

	/* we cannot write a multipart without a boundary string */
	g_return_val_if_fail (boundary, -1);

	/*
	 * write the preface text (usually something like
	 *   "This is a mime message, if you see this, then
	 *    your mail client probably doesn't support ...."
	 */
	if (multipart->preface) {
		count = camel_lite_stream_write_string (stream, multipart->preface);
		if (count == -1)
			return -1;
		total += count;
	}

	/*
	 * Now, write all the parts, separated by the boundary
	 * delimiter
	 */
	node = multipart->parts;
	while (node) {
		count = camel_lite_stream_printf (stream, "\n--%s\n", boundary);
		if (count == -1)
			return -1;
		total += count;

		count = camel_lite_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (node->data), stream);
		if (count == -1)
			return -1;
		total += count;
		node = node->next;
	}

	/* write the terminating boudary delimiter */
	count = camel_lite_stream_printf (stream, "\n--%s--\n", boundary);
	if (count == -1)
		return -1;
	total += count;

	/* and finally the postface */
	if (multipart->postface) {
		count = camel_lite_stream_write_string (stream, multipart->postface);
		if (count == -1)
			return -1;
		total += count;
	}

	return total;
}

/**
 * camel_lite_multipart_set_preface:
 * @multipart: a #CamelMultipart object
 * @preface: the multipart preface
 *
 * Set the preface text for this multipart.  Will be written out infront
 * of the multipart.  This text should only include US-ASCII strings, and
 * be relatively short, and will be ignored by any MIME mail client.
 **/
void
camel_lite_multipart_set_preface(CamelMultipart *multipart, const char *preface)
{
	if (multipart->preface != preface) {
		g_free(multipart->preface);
		if (preface)
			multipart->preface = g_strdup(preface);
		else
			multipart->preface = NULL;
	}
}

/**
 * camel_lite_multipart_set_postface:
 * @multipart: a #CamelMultipart object
 * @postface: multipat postface
 *
 * Set the postfix text for this multipart.  Will be written out after
 * the last boundary of the multipart, and ignored by any MIME mail
 * client.
 *
 * Generally postface texts should not be sent with multipart messages.
 **/
void
camel_lite_multipart_set_postface(CamelMultipart *multipart, const char *postface)
{
	if (multipart->postface != postface) {
		g_free(multipart->postface);
		if (postface)
			multipart->postface = g_strdup(postface);
		else
			multipart->postface = NULL;
	}
}

static int
construct_from_parser(CamelMultipart *multipart, struct _CamelMimeParser *mp)
{
	int err;
	CamelContentType *content_type;
	CamelMimePart *bodypart;
	char *buf;
	size_t len;

	g_assert(camel_lite_mime_parser_state(mp) == CAMEL_MIME_PARSER_STATE_MULTIPART);

	/* FIXME: we should use a came-mime-mutlipart, not jsut a camel-multipart, but who cares */
	d(printf("Creating multi-part\n"));

	content_type = camel_lite_mime_parser_content_type(mp);
	camel_lite_multipart_set_boundary(multipart,
				     camel_lite_content_type_param(content_type, "boundary"));

	while (camel_lite_mime_parser_step(mp, &buf, &len) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
		camel_lite_mime_parser_unstep(mp);
		bodypart = camel_lite_mime_part_new();
		camel_lite_mime_part_construct_from_parser(bodypart, mp);
		camel_lite_multipart_add_part(multipart, bodypart);
		camel_lite_object_unref((CamelObject *)bodypart);
	}

	/* these are only return valid data in the MULTIPART_END state */
	camel_lite_multipart_set_preface(multipart, camel_lite_mime_parser_preface (mp));
	camel_lite_multipart_set_postface(multipart, camel_lite_mime_parser_postface (mp));

	err = camel_lite_mime_parser_errno(mp);
	if (err != 0) {
		errno = err;
		return -1;
	} else
		return 0;
}


/**
 * camel_lite_multipart_construct_from_parser:
 * @multipart: a #CamelMultipart object
 * @parser: a #CamelMimeParser object
 *
 * Construct a multipart from a parser.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_multipart_construct_from_parser(CamelMultipart *multipart, struct _CamelMimeParser *mp)
{
	g_return_val_if_fail(CAMEL_IS_MULTIPART(multipart), -1);

	return CMP_CLASS(multipart)->construct_from_parser(multipart, mp);
}
