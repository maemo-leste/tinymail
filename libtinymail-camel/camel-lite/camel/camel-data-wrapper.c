/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; -*- */
/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
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

#include <errno.h>

#include "camel-data-wrapper.h"
#include "camel-exception.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-crlf.h"
#include "camel-private.h"
#include "camel-stream-filter.h"
#include "camel-stream.h"

#define d(x)

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelDataWrapper */
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static int construct_from_stream(CamelDataWrapper *, CamelStream *);
static ssize_t write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream);
static ssize_t decode_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream);
static void set_mime_type (CamelDataWrapper *data_wrapper, const char *mime_type);
static gchar *get_mime_type (CamelDataWrapper *data_wrapper);
static CamelContentType *get_mime_type_field (CamelDataWrapper *data_wrapper);
static void set_mime_type_field (CamelDataWrapper *data_wrapper, CamelContentType *mime_type);
static gboolean is_offline (CamelDataWrapper *data_wrapper);

static void
camel_lite_data_wrapper_class_init (CamelDataWrapperClass *camel_lite_data_wrapper_class)
{
	parent_class = camel_lite_type_get_global_classfuncs (camel_lite_object_get_type ());

	/* virtual method definition */
	camel_lite_data_wrapper_class->write_to_stream = write_to_stream;
	camel_lite_data_wrapper_class->decode_to_stream = decode_to_stream;
	camel_lite_data_wrapper_class->set_mime_type = set_mime_type;
	camel_lite_data_wrapper_class->get_mime_type = get_mime_type;
	camel_lite_data_wrapper_class->get_mime_type_field = get_mime_type_field;
	camel_lite_data_wrapper_class->set_mime_type_field = set_mime_type_field;
	camel_lite_data_wrapper_class->construct_from_stream = construct_from_stream;
	camel_lite_data_wrapper_class->is_offline = is_offline;
}

static void
camel_lite_data_wrapper_init (gpointer object, gpointer klass)
{
	CamelDataWrapper *camel_lite_data_wrapper = CAMEL_DATA_WRAPPER (object);

	camel_lite_data_wrapper->priv = g_malloc (sizeof (struct _CamelDataWrapperPrivate));
	pthread_mutex_init (&camel_lite_data_wrapper->priv->stream_lock, NULL);

	camel_lite_data_wrapper->mime_type = camel_lite_content_type_new ("application", "octet-stream");
	camel_lite_data_wrapper->encoding = CAMEL_TRANSFER_ENCODING_DEFAULT;
	camel_lite_data_wrapper->offline = FALSE;
}

static void
camel_lite_data_wrapper_finalize (CamelObject *object)
{
	CamelDataWrapper *camel_lite_data_wrapper = CAMEL_DATA_WRAPPER (object);

	pthread_mutex_destroy (&camel_lite_data_wrapper->priv->stream_lock);

	g_free (camel_lite_data_wrapper->priv);

	if (camel_lite_data_wrapper->mime_type)
		camel_lite_content_type_unref (camel_lite_data_wrapper->mime_type);

	if (camel_lite_data_wrapper->stream)
		camel_lite_object_unref (camel_lite_data_wrapper->stream);
}

CamelType
camel_lite_data_wrapper_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (CAMEL_OBJECT_TYPE,
					    "CamelLiteDataWrapper",
					    sizeof (CamelDataWrapper),
					    sizeof (CamelDataWrapperClass),
					    (CamelObjectClassInitFunc) camel_lite_data_wrapper_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_data_wrapper_init,
					    (CamelObjectFinalizeFunc) camel_lite_data_wrapper_finalize);
	}

	return type;
}


/**
 * camel_lite_data_wrapper_new:
 *
 * Create a new #CamelDataWrapper object.
 *
 * Returns a new #CamelDataWrapper object
 **/
CamelDataWrapper *
camel_lite_data_wrapper_new (void)
{
	return (CamelDataWrapper *) camel_lite_object_new (CAMEL_DATA_WRAPPER_TYPE);
}

static ssize_t
write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	ssize_t ret;

	if (data_wrapper->stream == NULL) {
		return -1;
	}

	CAMEL_DATA_WRAPPER_LOCK (data_wrapper, stream_lock);
	if (camel_lite_stream_reset (data_wrapper->stream) == -1) {
		CAMEL_DATA_WRAPPER_UNLOCK (data_wrapper, stream_lock);
		return -1;
	}

	ret = camel_lite_stream_write_to_stream (data_wrapper->stream, stream);

	CAMEL_DATA_WRAPPER_UNLOCK (data_wrapper, stream_lock);

	return ret;
}


/**
 * camel_lite_data_wrapper_write_to_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: a #CamelStream for output
 *
 * Writes the content of @data_wrapper to @stream in a machine-independent
 * format appropriate for the data. It should be possible to construct an
 * equivalent data wrapper object later by passing this stream to
 * #camel_lite_data_wrapper_construct_from_stream.
 *
 * Returns the number of bytes written, or %-1 on fail
 **/
ssize_t
camel_lite_data_wrapper_write_to_stream (CamelDataWrapper *data_wrapper,
				    CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CDW_CLASS (data_wrapper)->write_to_stream (data_wrapper, stream);
}


static ssize_t
decode_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMimeFilter *filter;
	CamelStream *fstream;
	ssize_t ret;

	fstream = (CamelStream *) camel_lite_stream_filter_new_with_stream (stream);

	switch (data_wrapper->encoding) {
	case CAMEL_TRANSFER_ENCODING_BASE64:
		filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
		camel_lite_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		camel_lite_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE:
		filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_QP_DEC);
		camel_lite_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		camel_lite_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_UUENCODE:
		filter = (CamelMimeFilter *) camel_lite_mime_filter_basic_new_type (CAMEL_MIME_FILTER_BASIC_UU_DEC);
		camel_lite_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		camel_lite_object_unref (filter);
		break;
	default:
		break;
	}

	if (camel_lite_content_type_is (data_wrapper->mime_type, "text", "*")) {
		filter = camel_lite_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_DECODE,
						     CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
		camel_lite_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		camel_lite_object_unref (filter);
	}

	ret = camel_lite_data_wrapper_write_to_stream (data_wrapper, fstream);
	camel_lite_stream_flush (fstream);
	camel_lite_object_unref (fstream);

	return ret;
}


/**
 * camel_lite_data_wrapper_decode_to_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: a #CamelStream for decoded data to be written to
 *
 * Writes the decoded data content to @stream.
 *
 * Returns the number of bytes written, or %-1 on fail
 **/
ssize_t
camel_lite_data_wrapper_decode_to_stream (CamelDataWrapper *data_wrapper,
				     CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CDW_CLASS (data_wrapper)->decode_to_stream (data_wrapper, stream);
}


static int
construct_from_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	if (data_wrapper->stream)
		camel_lite_object_unref (data_wrapper->stream);

	data_wrapper->stream = stream;
	camel_lite_object_ref (stream);
	return 0;
}


/**
 * camel_lite_data_wrapper_construct_from_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: an input #CamelStream
 *
 * Constructs the content of @data_wrapper from the supplied @stream.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_data_wrapper_construct_from_stream (CamelDataWrapper *data_wrapper,
					  CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CDW_CLASS (data_wrapper)->construct_from_stream (data_wrapper, stream);
}


static void
set_mime_type (CamelDataWrapper *data_wrapper, const char *mime_type)
{
	if (data_wrapper->mime_type)
		camel_lite_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = camel_lite_content_type_decode (mime_type);
}


/**
 * camel_lite_data_wrapper_set_mime_type:
 * @data_wrapper: a #CamelDataWrapper object
 * @mime_type: a MIME type
 *
 * This sets the data wrapper's MIME type.
 *
 * It might fail, but you won't know. It will allow you to set
 * Content-Type parameters on the data wrapper, which are meaningless.
 * You should not be allowed to change the MIME type of a data wrapper
 * that contains data, or at least, if you do, it should invalidate the
 * data.
 **/
void
camel_lite_data_wrapper_set_mime_type (CamelDataWrapper *data_wrapper,
				  const char *mime_type)
{
	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	CDW_CLASS (data_wrapper)->set_mime_type (data_wrapper, mime_type);
}

static char *
get_mime_type (CamelDataWrapper *data_wrapper)
{
	return camel_lite_content_type_simple (data_wrapper->mime_type);
}


/**
 * camel_lite_data_wrapper_get_mime_type:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns the MIME type which must be freed by the caller
 **/
char *
camel_lite_data_wrapper_get_mime_type (CamelDataWrapper *data_wrapper)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	return CDW_CLASS (data_wrapper)->get_mime_type (data_wrapper);
}


static CamelContentType *
get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->mime_type;
}


/**
 * camel_lite_data_wrapper_get_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns the parsed form of the data wrapper's MIME type
 **/
CamelContentType *
camel_lite_data_wrapper_get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	return CDW_CLASS (data_wrapper)->get_mime_type_field (data_wrapper);
}

static void
set_mime_type_field (CamelDataWrapper *data_wrapper,
		     CamelContentType *mime_type)
{
	if (mime_type)
		camel_lite_content_type_ref (mime_type);
	if (data_wrapper->mime_type)
		camel_lite_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = mime_type;
}


/**
 * camel_lite_data_wrapper_set_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper object
 * @mime_type: a #CamelContentType
 *
 * This sets the data wrapper's MIME type. It suffers from the same
 * flaws as #camel_lite_data_wrapper_set_mime_type.
 **/
void
camel_lite_data_wrapper_set_mime_type_field (CamelDataWrapper *data_wrapper,
					CamelContentType *mime_type)
{
	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	CDW_CLASS (data_wrapper)->set_mime_type_field (data_wrapper, mime_type);
}


static gboolean
is_offline (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->offline;
}


/**
 * camel_lite_data_wrapper_is_offline:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns whether @data_wrapper is "offline" (data stored
 * remotely) or not. Some optional code paths may choose to not
 * operate on offline data.
 **/
gboolean
camel_lite_data_wrapper_is_offline (CamelDataWrapper *data_wrapper)
{
	return CDW_CLASS (data_wrapper)->is_offline (data_wrapper);
}
