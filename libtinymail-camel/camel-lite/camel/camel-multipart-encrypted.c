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

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-mime-filter-crlf.h"
#include "camel-mime-part.h"
#include "camel-mime-utils.h"
#include "camel-multipart-encrypted.h"
#include "camel-stream-filter.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

static void camel_lite_multipart_encrypted_class_init (CamelMultipartEncryptedClass *klass);
static void camel_lite_multipart_encrypted_init (gpointer object, gpointer klass);
static void camel_lite_multipart_encrypted_finalize (CamelObject *object);

static void set_mime_type_field (CamelDataWrapper *data_wrapper, CamelContentType *mime_type);


static CamelMultipartClass *parent_class = NULL;


CamelType
camel_lite_multipart_encrypted_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_multipart_get_type (),
					    "CamelLiteMultipartEncrypted",
					    sizeof (CamelMultipartEncrypted),
					    sizeof (CamelMultipartEncryptedClass),
					    (CamelObjectClassInitFunc) camel_lite_multipart_encrypted_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_multipart_encrypted_init,
					    (CamelObjectFinalizeFunc) camel_lite_multipart_encrypted_finalize);
	}

	return type;
}


static void
camel_lite_multipart_encrypted_class_init (CamelMultipartEncryptedClass *klass)
{
	CamelDataWrapperClass *camel_lite_data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (klass);

	parent_class = (CamelMultipartClass *) camel_lite_multipart_get_type ();

	/* virtual method overload */
	camel_lite_data_wrapper_class->set_mime_type_field = set_mime_type_field;
}

static void
camel_lite_multipart_encrypted_init (gpointer object, gpointer klass)
{
	CamelMultipartEncrypted *multipart = (CamelMultipartEncrypted *) object;

	camel_lite_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/encrypted");

	multipart->decrypted = NULL;
}

static void
camel_lite_multipart_encrypted_finalize (CamelObject *object)
{
	CamelMultipartEncrypted *mpe = (CamelMultipartEncrypted *) object;

	g_free (mpe->protocol);

	if (mpe->decrypted)
		camel_lite_object_unref (mpe->decrypted);
}

/* we snoop the mime type to get the protocol */
static void
set_mime_type_field (CamelDataWrapper *data_wrapper, CamelContentType *mime_type)
{
	CamelMultipartEncrypted *mpe = (CamelMultipartEncrypted *) data_wrapper;

	if (mime_type) {
		const char *protocol;

		protocol = camel_lite_content_type_param (mime_type, "protocol");
		g_free (mpe->protocol);
		mpe->protocol = g_strdup (protocol);
	}

	((CamelDataWrapperClass *) parent_class)->set_mime_type_field (data_wrapper, mime_type);
}


/**
 * camel_lite_multipart_encrypted_new:
 *
 * Create a new #CamelMultipartEncrypted object.
 *
 * A MultipartEncrypted should be used to store and create parts of
 * type "multipart/encrypted".
 *
 * Returns a new #CamelMultipartEncrypted object
 **/
CamelMultipartEncrypted *
camel_lite_multipart_encrypted_new (void)
{
	CamelMultipartEncrypted *multipart;

	multipart = (CamelMultipartEncrypted *) camel_lite_object_new (CAMEL_MULTIPART_ENCRYPTED_TYPE);

	return multipart;
}
