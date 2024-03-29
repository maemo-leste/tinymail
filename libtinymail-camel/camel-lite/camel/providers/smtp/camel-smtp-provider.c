/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-provider.c: smtp provider registration code */
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

#include <glib/gi18n-lib.h>

#include "camel-provider.h"
#include "camel-sasl.h"
#include "camel-session.h"
#include "camel-smtp-transport.h"
#include "camel-url.h"

static CamelProvider smtp_provider = {
	"smtp",
	N_("SMTP"),

	N_("For delivering mail by connecting to a remote mailhub "
	   "using SMTP."),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_SUPPORTS_SSL,

	CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_ALLOW_USER,

	/* ... */
};

void
camel_lite_provider_module_init(void)
{
	smtp_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_lite_smtp_transport_get_type ();
	smtp_provider.authtypes = g_list_append (camel_lite_sasl_authtype_list (TRUE), camel_lite_sasl_authtype ("LOGIN"));
	smtp_provider.authtypes = g_list_append (smtp_provider.authtypes, camel_lite_sasl_authtype ("POPB4SMTP"));
	smtp_provider.url_hash = camel_lite_url_hash;
	smtp_provider.url_equal = camel_lite_url_equal;
	smtp_provider.translation_domain = GETTEXT_PACKAGE;

	camel_lite_provider_register(&smtp_provider);
}



