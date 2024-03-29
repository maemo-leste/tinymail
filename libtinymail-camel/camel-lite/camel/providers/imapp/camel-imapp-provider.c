/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-provider.c: pop3 provider registration code */

/*
 * Authors :
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
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

#include <glib/gi18n-lib.h>

#include "camel/camel-provider.h"
#include "camel/camel-sasl.h"
#include "camel/camel-session.h"
#include "camel/camel-url.h"

#include "camel-imapp-store.h"

CamelProviderConfEntry imapp_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "storage", NULL,
	  N_("Message storage") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider imapp_provider = {
	"imapp",

	N_("IMAP+"),

	N_("Experimental IMAP 4(.1) client\n"
	   "This is untested and unsupported code, you want to use plain imap instead.\n\n"
	   " !!! DO NOT USE THIS FOR PRODUCTION EMAIL  !!!\n"),
	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL,

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	imapp_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_lite_imapp_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAP server using a "
	   "plaintext password."),

	"",
	TRUE
};

void camel_lite_imapp_module_init(void);

void
camel_lite_imapp_module_init(void)
{
	extern void camel_lite_exception_setup(void);

	imapp_provider.object_types[CAMEL_PROVIDER_STORE] = camel_lite_imapp_store_get_type();
	imapp_provider.url_hash = camel_lite_url_hash;
	imapp_provider.url_equal = camel_lite_url_equal;

	imapp_provider.authtypes = g_list_prepend(imapp_provider.authtypes, camel_lite_sasl_authtype_list(FALSE));
	imapp_provider.authtypes = g_list_prepend(imapp_provider.authtypes, &camel_lite_imapp_password_authtype);
	imapp_provider.translation_domain = GETTEXT_PACKAGE;

	/* blah ... could just use it in object setup? */
	/* TEMPORARY */
	camel_lite_exception_setup();

	camel_lite_provider_register(&imapp_provider);
}

void
camel_lite_provider_module_init(void)
{
	camel_lite_imapp_module_init();
}
