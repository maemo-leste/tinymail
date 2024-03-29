/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-sasl-login.h"
#include "camel-service.h"

CamelServiceAuthType camel_lite_sasl_login_authtype = {
	N_("Login"),

	N_("This option will connect to the server using a "
	   "simple password."),

	"LOGIN",
	TRUE
};

enum {
	LOGIN_USER,
	LOGIN_PASSWD
};

static CamelSaslClass *parent_class = NULL;

/* Returns the class for a CamelSaslLogin */
#define CSP_CLASS(so) CAMEL_SASL_LOGIN_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static GByteArray *login_challenge (CamelSasl *sasl, GByteArray *token, CamelException *ex);

struct _CamelSaslLoginPrivate {
	int state;
};

static void
camel_lite_sasl_login_class_init (CamelSaslLoginClass *camel_lite_sasl_login_class)
{
	CamelSaslClass *camel_lite_sasl_class = CAMEL_SASL_CLASS (camel_lite_sasl_login_class);

	parent_class = CAMEL_SASL_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_sasl_get_type ()));

	/* virtual method overload */
	camel_lite_sasl_class->challenge = login_challenge;
}

static void
camel_lite_sasl_login_init (gpointer object, gpointer klass)
{
	CamelSaslLogin *sasl_login = CAMEL_SASL_LOGIN (object);

	sasl_login->priv = g_new0 (struct _CamelSaslLoginPrivate, 1);
}

static void
camel_lite_sasl_login_finalize (CamelObject *object)
{
	CamelSaslLogin *sasl = CAMEL_SASL_LOGIN (object);

	g_free (sasl->priv);
}


CamelType
camel_lite_sasl_login_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_sasl_get_type (),
					    "CamelLiteSaslLogin",
					    sizeof (CamelSaslLogin),
					    sizeof (CamelSaslLoginClass),
					    (CamelObjectClassInitFunc) camel_lite_sasl_login_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_sasl_login_init,
					    (CamelObjectFinalizeFunc) camel_lite_sasl_login_finalize);
	}

	return type;
}

static GByteArray *
login_challenge (CamelSasl *sasl, GByteArray *token, CamelException *ex)
{
	struct _CamelSaslLoginPrivate *priv = CAMEL_SASL_LOGIN (sasl)->priv;
	GByteArray *buf = NULL;
	CamelURL *url = sasl->service->url;

	g_return_val_if_fail (url->passwd != NULL, NULL);

	/* Need to wait for the server */
	if (!token)
		return NULL;

	switch (priv->state) {
	case LOGIN_USER:
		buf = g_byte_array_new ();
		g_byte_array_append (buf, (guint8 *) url->user, strlen (url->user));
		break;
	case LOGIN_PASSWD:
		buf = g_byte_array_new ();
		g_byte_array_append (buf, (guint8 *) url->passwd, strlen (url->passwd));

		sasl->authenticated = TRUE;
		break;
	default:
		if (!camel_lite_exception_is_set (ex)) {
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					     _("Unknown authentication state."));
		}
	}

	priv->state++;

	return buf;
}
