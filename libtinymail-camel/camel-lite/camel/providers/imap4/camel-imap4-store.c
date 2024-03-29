/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2007 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-net-utils.h"
#include "camel-private.h"
#include "camel-sasl.h"
#include "camel-tcp-stream-raw.h"
#include "camel-utf8.h"

#ifdef HAVE_SSL
#include "camel-tcp-stream-ssl.h"
#endif

#include "camel-imap4-command.h"
#include "camel-imap4-engine.h"
#include "camel-imap4-folder.h"
#include "camel-imap4-store-summary.h"
#include "camel-imap4-store.h"
#include "camel-imap4-stream.h"
#include "camel-imap4-summary.h"
#include "camel-imap4-utils.h"

#define d(x) x

static void camel_lite_imap4_store_class_init (CamelIMAP4StoreClass *klass);
static void camel_lite_imap4_store_init (CamelIMAP4Store *store, CamelIMAP4StoreClass *klass);
static void camel_lite_imap4_store_finalize (CamelObject *object);

/* service methods */
static void imap4_construct (CamelService *service, CamelSession *session,
			     CamelProvider *provider, CamelURL *url,
			     CamelException *ex);
static char *imap4_get_name (CamelService *service, gboolean brief);
static gboolean imap4_connect (CamelService *service, CamelException *ex);
static gboolean imap4_reconnect (CamelIMAP4Engine *engine, CamelException *ex);
static gboolean imap4_disconnect (CamelService *service, gboolean clean, CamelException *ex);
static GList *imap4_query_auth_types (CamelService *service, CamelException *ex);

/* store methods */
static CamelFolder *imap4_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex);
static CamelFolderInfo *imap4_create_folder (CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex);
static void imap4_delete_folder (CamelStore *store, const char *folder_name, CamelException *ex);
static void imap4_rename_folder (CamelStore *store, const char *old_name, const char *new_name, CamelException *ex);
static CamelFolderInfo *imap4_get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex);
static void imap4_subscribe_folder (CamelStore *store, const char *folder_name, CamelException *ex);
static void imap4_unsubscribe_folder (CamelStore *store, const char *folder_name, CamelException *ex);
static void imap4_noop (CamelStore *store, CamelException *ex);


static CamelOfflineStoreClass *parent_class = NULL;


CamelType
camel_lite_imap4_store_get_type (void)
{
	static CamelType type = 0;

	if (!type) {
		type = camel_lite_type_register (camel_lite_offline_store_get_type (),
					    "CamelLiteIMAP4Store",
					    sizeof (CamelIMAP4Store),
					    sizeof (CamelIMAP4StoreClass),
					    (CamelObjectClassInitFunc) camel_lite_imap4_store_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_imap4_store_init,
					    (CamelObjectFinalizeFunc) camel_lite_imap4_store_finalize);
	}

	return type;
}

static guint
imap4_hash_folder_name (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static int
imap4_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		bname = "INBOX";

	return g_str_equal (aname, bname);
}

static void
camel_lite_imap4_store_class_init (CamelIMAP4StoreClass *klass)
{
	CamelServiceClass *service_class = (CamelServiceClass *) klass;
	CamelStoreClass *store_class = (CamelStoreClass *) klass;

	parent_class = (CamelOfflineStoreClass *) camel_lite_type_get_global_classfuncs (CAMEL_TYPE_OFFLINE_STORE);

	service_class->construct = imap4_construct;
	service_class->get_name = imap4_get_name;
	service_class->connect = imap4_connect;
	service_class->disconnect = imap4_disconnect;
	service_class->query_auth_types = imap4_query_auth_types;

	store_class->hash_folder_name = imap4_hash_folder_name;
	store_class->compare_folder_name = imap4_compare_folder_name;

	store_class->get_folder = imap4_get_folder;
	store_class->create_folder = imap4_create_folder;
	store_class->delete_folder = imap4_delete_folder;
	store_class->rename_folder = imap4_rename_folder;
	store_class->get_folder_info = imap4_get_folder_info;
	store_class->subscribe_folder = imap4_subscribe_folder;
	store_class->unsubscribe_folder = imap4_unsubscribe_folder;
	store_class->noop = imap4_noop;

	/* FIXME: implement folder_subscribed */
}

static void
camel_lite_imap4_store_init (CamelIMAP4Store *store, CamelIMAP4StoreClass *klass)
{
	store->engine = NULL;
	store->summary = NULL;
}

static void
camel_lite_imap4_store_finalize (CamelObject *object)
{
	CamelIMAP4Store *store = (CamelIMAP4Store *) object;

	if (store->summary) {
		camel_lite_store_summary_save ((CamelStoreSummary *) store->summary);
		camel_lite_object_unref (store->summary);
	}

	if (store->engine)
		camel_lite_object_unref (store->engine);

	g_free (store->storage_path);
}


static void
imap4_construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	CamelIMAP4Store *store = (CamelIMAP4Store *) service;
	char *buf;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_lite_exception_is_set (ex))
		return;

	if (camel_lite_url_get_param (url, "use_lsub"))
		((CamelStore *) store)->flags |= CAMEL_STORE_SUBSCRIPTIONS;

	store->storage_path = camel_lite_session_get_storage_path (session, service, ex);
	store->engine = camel_lite_imap4_engine_new (service, imap4_reconnect);

	/* setup/load the summary */
	buf = g_alloca (strlen (store->storage_path) + 32);
	sprintf (buf, "%s/.summary", store->storage_path);
	store->summary = camel_lite_imap4_store_summary_new ();
	camel_lite_store_summary_set_filename ((CamelStoreSummary *) store->summary, buf);

	buf = camel_lite_url_to_string (service->url, CAMEL_URL_HIDE_ALL);
	url = camel_lite_url_new (buf, NULL);
	g_free (buf);
	camel_lite_store_summary_set_uri_base ((CamelStoreSummary *) store->summary, url);
	camel_lite_url_free (url);

	camel_lite_store_summary_load ((CamelStoreSummary *) store->summary);
}

static char *
imap4_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("IMAP server %s"), service->url->host);
	else
		return g_strdup_printf (_("IMAP service for %s on %s"),
					service->url->user, service->url->host);
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS,
};

#ifdef HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3 | CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelIMAP4Engine *engine, struct addrinfo *ai, int ssl_mode, CamelException *ex)
{
	CamelService *service = engine->service;
	CamelSockOptData sockopt;
	CamelStream *tcp_stream;
#ifdef HAVE_SSL
	CamelIMAP4Command *ic;
	int id;
#endif

	if (ssl_mode != MODE_CLEAR) {
#ifdef HAVE_SSL
		if (ssl_mode == MODE_TLS) {
			tcp_stream = camel_lite_tcp_stream_ssl_new_raw (service->session, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = camel_lite_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
		}
#else
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not connect to %s: %s"),
				      service->url->host, _("SSL unavailable"));

		return FALSE;
#endif /* HAVE_SSL */
	} else {
		tcp_stream = camel_lite_tcp_stream_raw_new ();
	}

	if (camel_lite_tcp_stream_connect ((CamelTcpStream *) tcp_stream, ai) == -1) {
		if (errno == EINTR)
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     _("Connection canceled"));
		else
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect to %s: %s"),
					      service->url->host,
					      g_strerror (errno));

		camel_lite_object_unref (tcp_stream);

		return FALSE;
	}

	/* set some socket options to better tailor the connection to our needs */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_lite_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_lite_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

	if (camel_lite_imap4_engine_take_stream (engine, tcp_stream, ex) == -1)
		return FALSE;

	if (camel_lite_imap4_engine_capability (engine, ex) == -1)
		return FALSE;

	camel_lite_imap4_store_summary_set_capabilities (((CamelIMAP4Store *) service)->summary, engine->capa);

	if (ssl_mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}

#ifdef HAVE_SSL
	if (!(engine->capa & CAMEL_IMAP4_CAPABILITY_STARTTLS)) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to connect to IMAP server %s in secure mode: %s"),
				      service->url->host, _("SSL negotiations failed"));

		return FALSE;
	}

	ic = camel_lite_imap4_engine_prequeue (engine, NULL, "STARTTLS\r\n");
	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->result != CAMEL_IMAP4_RESULT_OK) {
		if (ic->result != CAMEL_IMAP4_RESULT_OK) {
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Failed to connect to IMAP server %s in secure mode: %s"),
					      service->url->host, _("Unknown error"));
		} else {
			camel_lite_exception_xfer (ex, &ic->ex);
		}

		camel_lite_imap4_command_unref (ic);

		return FALSE;
	}

	camel_lite_imap4_command_unref (ic);

	if (camel_lite_tcp_stream_ssl_enable_ssl ((CamelTcpStreamSSL *) tcp_stream) == -1) {
                camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
                                      _("Failed to connect to IMAP server %s in secure mode: %s"),
                                      service->url->host, _("TLS negotiations failed"));
		return FALSE;
	}

	return TRUE;
#else
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			      _("Failed to connect to IMAP server %s in secure mode: %s"),
			      service->url->host, _("SSL is not available in this build"));

	return FALSE;
#endif /* HAVE_SSL */
}

static struct {
	char *value;
	char *serv;
	char *port;
	int mode;
} ssl_options[] = {
	{ "",              "imaps", "993", MODE_SSL   },  /* really old (1.x) */
	{ "always",        "imaps", "993", MODE_SSL   },
	{ "when-possible", "imap",  "143", MODE_TLS   },
	{ "never",         "imap",  "143", MODE_CLEAR },
	{ NULL,            "imap",  "143", MODE_CLEAR },
};

static gboolean
connect_to_server_wrapper (CamelIMAP4Engine *engine, CamelException *ex)
{
	CamelService *service = engine->service;
	struct addrinfo *ai, hints;
	const char *ssl_mode;
	int mode, ret, i;
	const char *port;
	char *serv;

	if ((ssl_mode = camel_lite_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = ssl_options[i].serv;
		port = ssl_options[i].port;
	} else {
		mode = MODE_CLEAR;
		serv = "imap";
		port = "143";
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		port = NULL;
	}

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	ai = camel_lite_getaddrinfo (service->url->host, serv, &hints, ex);
	if (ai == NULL && port != NULL && camel_lite_exception_get_id(ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		camel_lite_exception_clear (ex);
		ai = camel_lite_getaddrinfo (service->url->host, port, &hints, ex);
	}

	if (ai == NULL)
		return FALSE;

	ret = connect_to_server (engine, ai, mode, ex);

	camel_lite_freeaddrinfo (ai);

	return ret;
}

static int
sasl_auth (CamelIMAP4Engine *engine, CamelIMAP4Command *ic, const unsigned char *linebuf, size_t linelen, CamelException *ex)
{
	/* Perform a single challenge iteration */
	CamelSasl *sasl = ic->user_data;
	char *challenge;

	if (camel_lite_sasl_authenticated (sasl)) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				      _("Cannot authenticate to IMAP server %s using the %s authentication mechanism"),
				      engine->url->host, engine->url->authmech);
		return -1;
	}

	while (isspace (*linebuf))
		linebuf++;

	if (*linebuf == '\0')
		linebuf = NULL;

	if (!(challenge = camel_lite_sasl_challenge_base64 (sasl, (const char *) linebuf, ex)))
		return -1;

	d(fprintf (stderr, "sending : %s\r\n", challenge));

	if (camel_lite_stream_printf (engine->ostream, "%s\r\n", challenge) == -1) {
		g_free (challenge);
		return -1;
	}

	g_free (challenge);

	if (camel_lite_stream_flush (engine->ostream) == -1)
		return -1;

	return 0;
}

static int
imap4_try_authenticate (CamelIMAP4Engine *engine, gboolean reprompt, const char *errmsg, CamelException *ex)
{
	CamelService *service = engine->service;
	CamelSession *session = service->session;
	CamelServiceAuthType *mech = NULL;
	CamelSasl *sasl = NULL;
	CamelIMAP4Command *ic;
	int id;

	if (service->url->authmech)
		mech = g_hash_table_lookup (engine->authtypes, service->url->authmech);

	if ((!mech || (mech && mech->need_password)) && !service->url->passwd) {
		guint32 flags = CAMEL_SESSION_PASSWORD_SECRET;
		char *base_prompt;
		char *full_prompt;

		if (reprompt)
			flags |= CAMEL_SESSION_PASSWORD_REPROMPT;

		base_prompt = camel_lite_session_build_password_prompt (
			"IMAP", service->url->user, service->url->host);

		if (errmsg != NULL)
			full_prompt = g_strconcat (errmsg, base_prompt, NULL);
		else
			full_prompt = g_strdup (full_prompt);

		service->url->passwd = camel_lite_session_get_password (
			session, service, NULL, full_prompt,
			"password", flags, ex);

		g_free (base_prompt);
		g_free (full_prompt);

		if (!service->url->passwd)
			return FALSE;
	}

	if (service->url->authmech) {
		sasl = camel_lite_sasl_new ("imap", mech->authproto, service);

		ic = camel_lite_imap4_engine_prequeue (engine, NULL, "AUTHENTICATE %s\r\n", service->url->authmech);
		ic->plus = sasl_auth;
		ic->user_data = sasl;
	} else {
		ic = camel_lite_imap4_engine_prequeue (engine, NULL, "LOGIN %S %S\r\n",
						  service->url->user, service->url->passwd);
	}

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (sasl != NULL)
		camel_lite_object_unref (sasl);

	if (id == -1 || ic->status == CAMEL_IMAP4_COMMAND_ERROR) {
		/* unrecoverable error */
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);

		return FALSE;
	}

	if (ic->result != CAMEL_IMAP4_RESULT_OK) {
		camel_lite_imap4_command_unref (ic);

		/* try again */

		return TRUE;
	}

	camel_lite_imap4_command_unref (ic);

	return FALSE;
}

static gboolean
imap4_reconnect (CamelIMAP4Engine *engine, CamelException *ex)
{
	CamelService *service = engine->service;
	gboolean reprompt = FALSE;
	char *errmsg = NULL;
	CamelException lex;

	if (!connect_to_server_wrapper (engine, ex))
		return FALSE;

	if (engine->state != CAMEL_IMAP4_ENGINE_AUTHENTICATED) {
#define CANT_USE_AUTHMECH (!g_hash_table_lookup (engine->authtypes, service->url->authmech))
		if (service->url->authmech && CANT_USE_AUTHMECH) {
			/* Oops. We can't AUTH using the requested mechanism */
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("Cannot authenticate to IMAP server %s using %s"),
					      service->url->host, service->url->authmech);

			return FALSE;
		}

		camel_lite_exception_init (&lex);
		while (imap4_try_authenticate (engine, reprompt, errmsg, &lex)) {
			g_free (errmsg);
			errmsg = g_markup_escape_text (lex.desc, -1);
			camel_lite_exception_clear (&lex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
			reprompt = TRUE;
		}
		g_free (errmsg);

		if (camel_lite_exception_is_set (&lex)) {
			camel_lite_exception_xfer (ex, &lex);
			return FALSE;
		}
	}

	if (camel_lite_imap4_engine_namespace (engine, ex) == -1)
		return FALSE;

	camel_lite_imap4_store_summary_set_namespaces (((CamelIMAP4Store *) service)->summary, &engine->namespaces);

	return TRUE;
}

static gboolean
imap4_connect (CamelService *service, CamelException *ex)
{
	CamelIMAP4Store *store = (CamelIMAP4Store *) service;
	gboolean retval;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	CAMEL_SERVICE_REC_LOCK (service, connect_lock);
	if (store->engine->state == CAMEL_IMAP4_ENGINE_DISCONNECTED)
		retval = imap4_reconnect (store->engine, ex);
	else
		retval = TRUE;
	CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);

	return retval;
}

static gboolean
imap4_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelIMAP4Store *store = (CamelIMAP4Store *) service;
	CamelIMAP4Command *ic;
	int id;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	if (clean && store->engine->state != CAMEL_IMAP4_ENGINE_DISCONNECTED) {
		ic = camel_lite_imap4_engine_queue (store->engine, NULL, "LOGOUT\r\n");
		while ((id = camel_lite_imap4_engine_iterate (store->engine)) < ic->id && id != -1)
			;

		camel_lite_imap4_command_unref (ic);
	}
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	return 0;
}

extern CamelServiceAuthType camel_lite_imap4_password_authtype;

static GList *
imap4_query_auth_types (CamelService *service, CamelException *ex)
{
	CamelIMAP4Store *store = (CamelIMAP4Store *) service;
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	gboolean connected;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return NULL;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	connected = connect_to_server_wrapper (store->engine, ex);
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
	if (!connected)
		return NULL;

	sasl_types = camel_lite_sasl_authtype_list (FALSE);
	for (t = sasl_types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (store->engine->authtypes, authtype->authproto)) {
			sasl_types = g_list_remove_link (sasl_types, t);
			g_list_free_1 (t);
		}
	}

	return g_list_prepend (sasl_types, &camel_lite_imap4_password_authtype);
}

static char *
imap4_folder_utf7_name (CamelStore *store, const char *folder_name, char wildcard)
{
	char *real_name, *p;
	char sep = '\0';
	int len;

	if (*folder_name) {
		sep = camel_lite_imap4_get_path_delim (((CamelIMAP4Store *) store)->summary, folder_name);

		if (sep != '/') {
			p = real_name = g_alloca (strlen (folder_name) + 1);
			strcpy (real_name, folder_name);
			while (*p != '\0') {
				if (*p == '/')
					*p = sep;
				p++;
			}

			folder_name = real_name;
		}

		real_name = camel_lite_utf8_utf7 (folder_name);
	} else
		real_name = g_strdup ("");

	if (wildcard) {
		len = strlen (real_name);
		real_name = g_realloc (real_name, len + 3);

		if (len > 0)
			real_name[len++] = sep;

		real_name[len++] = wildcard;
		real_name[len] = '\0';
	}

	return real_name;
}

static CamelFolder *
imap4_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelFolder *folder = NULL;
	camel_lite_imap4_list_t *list;
	CamelIMAP4Command *ic;
	CamelFolderInfo *fi;
	GPtrArray *array;
	char *utf7_name;
	int create;
	int id, i;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		if ((flags & CAMEL_STORE_FOLDER_CREATE) != 0) {
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create IMAP folders in offline mode."));
		} else {
			folder = camel_lite_imap4_folder_new (store, folder_name, ex);
		}

		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

		return folder;
	}

	/* make sure the folder exists - try LISTing it? */
	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "LIST \"\" %S\r\n", utf7_name);
	camel_lite_imap4_command_register_untagged (ic, "LIST", camel_lite_imap4_untagged_list);
	ic->user_data = array = g_ptr_array_new ();
	g_free (utf7_name);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);
		g_ptr_array_free (array, TRUE);
		goto done;
	}

	create = array->len == 0;

	for (i = 0; i < array->len; i++) {
		list = array->pdata[i];
		g_free (list->name);
		g_free (list);
	}

	g_ptr_array_free (array, TRUE);

	if (ic->result != CAMEL_IMAP4_RESULT_OK) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get folder `%s' on IMAP server %s: Unknown"),
				      folder_name, ((CamelService *) store)->url->host);
		camel_lite_imap4_command_unref (ic);
		goto done;
	}

	camel_lite_imap4_command_unref (ic);

	if (create) {
		const char *basename;
		char *parent;
		int len;

		if (!(flags & CAMEL_STORE_FOLDER_CREATE))
			goto done;

		if (!(basename = strrchr (folder_name, '/')))
			basename = folder_name;
		else
			basename++;

		len = basename > folder_name ? (basename - folder_name) - 1 : 0;
		parent = g_alloca (len + 1);
		memcpy (parent, folder_name, len);
		parent[len] = '\0';

		if (!(fi = imap4_create_folder (store, parent, basename, ex)))
			goto done;

		camel_lite_store_free_folder_info (store, fi);
	}

	folder = camel_lite_imap4_folder_new (store, folder_name, ex);

 done:

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	return folder;
}


static gboolean
imap4_folder_can_contain_folders (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	guint32 flags = CAMEL_FOLDER_NOINFERIORS;
	camel_lite_imap4_list_t *list;
	CamelIMAP4Command *ic;
	GPtrArray *array;
	char *utf7_name;
	int id, i;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');

	ic = camel_lite_imap4_engine_queue (engine, NULL, "LIST \"\" %S\r\n", utf7_name);
	camel_lite_imap4_command_register_untagged (ic, "LIST", camel_lite_imap4_untagged_list);
	ic->user_data = array = g_ptr_array_new ();

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);

		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}

		goto done;
	}

	if (ic->result != CAMEL_IMAP4_RESULT_OK) {
		camel_lite_imap4_command_unref (ic);

		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get LIST information for `%s' on IMAP server %s: %s"),
				      folder_name, engine->url->host, ic->result == CAMEL_IMAP4_RESULT_BAD ?
				      _("Bad command") : _("Unknown"));

		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}

		goto done;
	}

	flags = 0;
	for (i = 0; i < array->len; i++) {
		list = array->pdata[i];
		if (!strcmp (list->name, utf7_name))
			flags |= list->flags;
		g_free (list->name);
		g_free (list);
	}

 done:

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	g_ptr_array_free (array, TRUE);
	g_free (utf7_name);

	return (flags & CAMEL_FOLDER_NOINFERIORS) == 0;
}

static CamelFolderInfo *
imap4_folder_create (CamelStore *store, const char *folder_name, const char *subfolder_hint, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelFolderInfo *fi = NULL;
	CamelIMAP4Command *ic;
	char *utf7_name;
	CamelURL *url;
	const char *c;
	int id;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "CREATE %S%s\r\n", utf7_name, subfolder_hint);
	g_free (utf7_name);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);
		goto done;
	}

	switch (ic->result) {
	case CAMEL_IMAP4_RESULT_OK:
		url = camel_lite_url_copy (engine->url);
		camel_lite_url_set_fragment (url, folder_name);

		c = strrchr (folder_name, '/');

		fi = g_malloc0 (sizeof (CamelFolderInfo));
		fi->full_name = g_strdup (folder_name);
		fi->name = g_strdup (c ? c + 1: folder_name);
		fi->uri = camel_lite_url_to_string (url, CAMEL_URL_HIDE_ALL);
		camel_lite_url_free (url);
		fi->flags = 0;
		fi->unread = -1;
		fi->total = -1;

		camel_lite_imap4_store_summary_note_info (((CamelIMAP4Store *) store)->summary, fi);

		camel_lite_object_trigger_event (store, "folder_created", fi);
		break;
	case CAMEL_IMAP4_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder `%s': Invalid mailbox name"),
				      folder_name);
		break;
	case CAMEL_IMAP4_RESULT_BAD:
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder `%s': Bad command"),
				      folder_name);
		break;
	default:
		g_assert_not_reached ();
	}

	camel_lite_imap4_command_unref (ic);

 done:

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	return fi;
}

static gboolean
imap4_folder_recreate (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelFolderInfo *fi = NULL;
	char hint[2];
	char sep;

	sep = camel_lite_imap4_get_path_delim (((CamelIMAP4Store *) store)->summary, folder_name);
	sprintf (hint, "%c", sep);

	imap4_delete_folder (store, folder_name, ex);
	if (camel_lite_exception_is_set (ex))
		return FALSE;

	if (!(fi = imap4_folder_create (store, folder_name, hint, ex)))
		return FALSE;

	camel_lite_folder_info_free (fi);

	return TRUE;
}

static CamelFolderInfo *
imap4_create_folder (CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex)
{
	CamelFolderInfo *fi = NULL;
	const char *c;
	char *name;
	char sep;

	sep = camel_lite_imap4_get_path_delim (((CamelIMAP4Store *) store)->summary, parent_name);

	c = folder_name;
	while (*c != '\0') {
		if (*c == sep || strchr ("/#%*", *c)) {
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_PATH,
					      _("The folder name \"%s\" is invalid because "
						"it contains the character \"%c\""),
					      folder_name, *c);
			return NULL;
		}

		c++;
	}

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create IMAP folders in offline mode."));
		return NULL;
	}

	if (parent_name != NULL && *parent_name) {
		CamelException lex;

		camel_lite_exception_init (&lex);
		if (!imap4_folder_can_contain_folders (store, parent_name, &lex)) {
			if (camel_lite_exception_is_set (&lex)) {
				camel_lite_exception_xfer (ex, &lex);
				return NULL;
			}

			if (!imap4_folder_recreate (store, parent_name, &lex)) {
				camel_lite_exception_xfer (ex, &lex);
				return NULL;
			}
		}

		name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	} else
		name = g_strdup (folder_name);

	fi = imap4_folder_create (store, name, "", ex);
	g_free (name);

	return fi;
}

static void
imap4_delete_folder (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelFolder *selected = (CamelFolder *) engine->folder;
	CamelIMAP4Command *ic, *ic0 = NULL;
	CamelFolderInfo *fi;
	char *utf7_name;
	CamelURL *url;
	const char *p;
	int id;

	if (!g_ascii_strcasecmp (folder_name, "INBOX")) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot delete folder `%s': Special folder"),
				      folder_name);

		return;
	}

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot delete IMAP folders in offline mode."));
		return;
	}

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	if (selected && !strcmp (folder_name, selected->full_name))
		ic0 = camel_lite_imap4_engine_queue (engine, NULL, "CLOSE\r\n");

	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "DELETE %S\r\n", utf7_name);
	g_free (utf7_name);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		if (ic0 && ic0->status != CAMEL_IMAP4_COMMAND_COMPLETE)
			camel_lite_exception_xfer (ex, &ic0->ex);
		else
			camel_lite_exception_xfer (ex, &ic->ex);

		if (ic0 != NULL)
			camel_lite_imap4_command_unref (ic0);

		camel_lite_imap4_command_unref (ic);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	if (ic0 != NULL)
		camel_lite_imap4_command_unref (ic0);

	switch (ic->result) {
	case CAMEL_IMAP4_RESULT_OK:
		/* deleted */
		url = camel_lite_url_copy (engine->url);
		camel_lite_url_set_fragment (url, folder_name);

		p = strrchr (folder_name, '/');

		fi = g_malloc0 (sizeof (CamelFolderInfo));
		fi->full_name = g_strdup (folder_name);
		fi->name = g_strdup (p ? p + 1: folder_name);
		fi->uri = camel_lite_url_to_string (url, CAMEL_URL_HIDE_ALL);
		camel_lite_url_free (url);
		fi->flags = 0;
		fi->unread = -1;
		fi->total = -1;

		camel_lite_imap4_store_summary_unnote_info (((CamelIMAP4Store *) store)->summary, fi);

		camel_lite_object_trigger_event (store, "folder_deleted", fi);

		camel_lite_folder_info_free (fi);
		break;
	case CAMEL_IMAP4_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot delete folder `%s': Invalid mailbox name"),
				      folder_name);
		break;
	case CAMEL_IMAP4_RESULT_BAD:
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot delete folder `%s': Bad command"),
				      folder_name);
		break;
	}

	camel_lite_imap4_command_unref (ic);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static void
imap4_rename_folder (CamelStore *store, const char *old_name, const char *new_name, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	char *old_uname, *new_uname;
	CamelIMAP4Command *ic;
	int id;

	if (!g_ascii_strcasecmp (old_name, "INBOX")) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot rename folder `%s' to `%s': Special folder"),
				      old_name, new_name);

		return;
	}

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename IMAP folders in offline mode."));
		return;
	}

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	old_uname = imap4_folder_utf7_name (store, old_name, '\0');
	new_uname = imap4_folder_utf7_name (store, new_name, '\0');

	ic = camel_lite_imap4_engine_queue (engine, NULL, "RENAME %S %S\r\n", old_uname, new_uname);
	g_free (old_uname);
	g_free (new_uname);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	switch (ic->result) {
	case CAMEL_IMAP4_RESULT_OK:
		/* FIXME: need to update state on the renamed folder object */
		/* FIXME: need to update cached summary info too */
		break;
	case CAMEL_IMAP4_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot rename folder `%s' to `%s': Invalid mailbox name"),
				      old_name, new_name);
		break;
	case CAMEL_IMAP4_RESULT_BAD:
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot rename folder `%s' to `%s': Bad command"),
				      old_name, new_name);
		break;
	}

	camel_lite_imap4_command_unref (ic);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static int
list_sort (const camel_lite_imap4_list_t **list0, const camel_lite_imap4_list_t **list1)
{
	return strcmp ((*list0)->name, (*list1)->name);
}

static void
list_remove_duplicates (GPtrArray *array)
{
	camel_lite_imap4_list_t *list, *last;
	int i;

	last = array->pdata[0];
	for (i = 1; i < array->len; i++) {
		list = array->pdata[i];
		if (!strcmp (list->name, last->name)) {
			g_ptr_array_remove_index (array, i--);
			last->flags |= list->flags;
			g_free (list->name);
			g_free (list);
		}
	}
}

static void
imap4_status (CamelStore *store, CamelFolderInfo *fi)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	camel_lite_imap4_status_attr_t *attr, *next;
	camel_lite_imap4_status_t *status;
	CamelIMAP4Command *ic;
	GPtrArray *array;
	char *mailbox;
	int id, i;

	mailbox = imap4_folder_utf7_name (store, fi->full_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "STATUS %S (MESSAGES UNSEEN)\r\n", mailbox);
	g_free (mailbox);

	camel_lite_imap4_command_register_untagged (ic, "STATUS", camel_lite_imap4_untagged_status);
	ic->user_data = array = g_ptr_array_new ();

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_imap4_command_unref (ic);
		g_ptr_array_free (array, TRUE);
		return;
	}

	for (i = 0; i < array->len; i++) {
		status = array->pdata[i];
		attr = status->attr_list;
		while (attr != NULL) {
			next = attr->next;
			if (attr->type == CAMEL_IMAP4_STATUS_MESSAGES)
				fi->total = attr->value;
			else if (attr->type == CAMEL_IMAP4_STATUS_UNSEEN)
				fi->unread = attr->value;
			g_free (attr);
			attr = next;
		}

		g_free (status->mailbox);
		g_free (status);
	}

	camel_lite_imap4_command_unref (ic);
	g_ptr_array_free (array, TRUE);
}

static void
imap4_subscription_info (CamelStore *store, CamelFolderInfo *fi)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	camel_lite_imap4_list_t *lsub;
	CamelIMAP4Command *ic;
	GPtrArray *array;
	char *mailbox;
	int id, i;

	mailbox = imap4_folder_utf7_name (store, fi->full_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "LSUB \"\" %S\r\n", mailbox);
	camel_lite_imap4_command_register_untagged (ic, "LSUB", camel_lite_imap4_untagged_list);
	ic->user_data = array = g_ptr_array_new ();

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	camel_lite_imap4_command_unref (ic);
	for (i = 0; i < array->len; i++) {
		lsub = array->pdata[i];

		if (!strcmp (lsub->name, mailbox))
			fi->flags |= CAMEL_FOLDER_SUBSCRIBED;

		g_free (lsub->name);
		g_free (lsub);
	}

	g_ptr_array_free (array, TRUE);
}

static CamelFolderInfo *
imap4_build_folder_info (CamelStore *store, const char *top, guint32 flags, GPtrArray *array)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelFolder *folder = (CamelFolder *) engine->folder;
	gboolean lsub = (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED);
	camel_lite_imap4_list_t *list;
	CamelFolderInfo *fi;
	char *name, *p;
	CamelURL *url;
	int i;

	if (array->len == 0) {
		g_ptr_array_free (array, TRUE);
		return NULL;
	}

	g_ptr_array_sort (array, (GCompareFunc) list_sort);

	list_remove_duplicates (array);

	url = camel_lite_url_copy (engine->url);

	if (!strcmp (top, "") && (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)) {
		/* clear the folder-info cache */
		camel_lite_store_summary_clear ((CamelStoreSummary *) ((CamelIMAP4Store *) store)->summary);
	}

	for (i = 0; i < array->len; i++) {
		list = array->pdata[i];
		fi = g_malloc0 (sizeof (CamelFolderInfo));

		p = name = camel_lite_utf7_utf8 (list->name);
		while (*p != '\0') {
			if (*p == list->delim)
				*p = '/';
			p++;
		}

		p = strrchr (name, '/');
		camel_lite_url_set_fragment (url, name);

		fi->full_name = name;
		fi->name = g_strdup (p ? p + 1: name);
		fi->uri = camel_lite_url_to_string (url, CAMEL_URL_HIDE_ALL);
		fi->flags = list->flags | (lsub ? CAMEL_FOLDER_SUBSCRIBED : 0);
		fi->unread = -1;
		fi->total = -1;

		if (!g_ascii_strcasecmp (fi->full_name, "INBOX"))
			fi->flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;

		/* SELECTED folder, just get it from the folder */
		if (folder && !strcmp (folder->full_name, fi->full_name)) {
			camel_lite_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &fi->total, CAMEL_FOLDER_UNREAD, &fi->unread, 0);
		} else if (!(flags & CAMEL_STORE_FOLDER_INFO_FAST)) {
			imap4_status (store, fi);
		}

		if (!(fi->flags & CAMEL_FOLDER_SUBSCRIBED))
			imap4_subscription_info (store, fi);

		array->pdata[i] = fi;

		camel_lite_imap4_store_summary_note_info (((CamelIMAP4Store *) store)->summary, fi);

		if (!g_ascii_strcasecmp (fi->full_name, "INBOX")) {
			g_free (fi->name);
			fi->name = g_strdup (_("Inbox"));
		}

		g_free (list->name);
		g_free (list);
	}

	fi = camel_lite_folder_info_build (array, top, '/', TRUE);

	camel_lite_url_free (url);

	g_ptr_array_free (array, TRUE);

	camel_lite_store_summary_save ((CamelStoreSummary *) ((CamelIMAP4Store *) store)->summary);

	return fi;
}

static CamelFolderInfo *
imap4_get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelIMAP4Command *ic, *ic0 = NULL, *ic1 = NULL;
	CamelFolderInfo *inbox = NULL, *fi = NULL;
	const char *base, *namespace;
	camel_lite_imap4_list_t *list;
	GPtrArray *array;
	const char *cmd;
	char *pattern;
	char wildcard;
	int id, i;

	if (top == NULL)
		top = "";

	if (!(namespace = camel_lite_url_get_param (((CamelService *) store)->url, "namespace")))
		namespace = "";

	if (!strcmp (top, ""))
		base = namespace;
	else
		base = top;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

#ifdef USE_FOLDER_INFO_CACHE_LOGIC_FOR_SPEED
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL
	    || engine->state == CAMEL_IMAP4_ENGINE_DISCONNECTED) {
		fi = camel_lite_imap4_store_summary_get_folder_info (((CamelIMAP4Store *) store)->summary, base, flags);
		if (base == namespace && *namespace) {
			inbox = camel_lite_imap4_store_summary_get_folder_info (((CamelIMAP4Store *) store)->summary, "INBOX",
									   flags & ~CAMEL_STORE_FOLDER_INFO_RECURSIVE);
			if (inbox) {
				inbox->next = fi;
				fi = inbox;
			}
		}

		if (fi == NULL && ((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL) {
			/* folder info hasn't yet been cached and the store hasn't been
			 * connected yet, but the network is available so we can connect
			 * and query the server. */
			goto check_online;
		}
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return fi;
	}
#else
	/* this is the way the old imap code was meant to work (except it was broken and disregarded the
	 * NETWORK_UNAVAIL state and went online anyway if fi was NULL, but we won't be evil like that)
	 */
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		fi = camel_lite_imap4_store_summary_get_folder_info (((CamelIMAP4Store *) store)->summary, base, flags);
		if (base == namespace && *namespace) {
			inbox = camel_lite_imap4_store_summary_get_folder_info (((CamelIMAP4Store *) store)->summary, "INBOX",
									   flags & ~CAMEL_STORE_FOLDER_INFO_RECURSIVE);
			if (inbox) {
				inbox->next = fi;
				fi = inbox;
			}
		}
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return fi;
	}
#endif

#ifdef USE_FOLDER_INFO_CACHE_LOGIC_FOR_SPEED
 check_online:
#endif

	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
		cmd = "LSUB";
	else
		cmd = "LIST";

	wildcard = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) ? '*' : '%';
	pattern = imap4_folder_utf7_name (store, base, wildcard);
	array = g_ptr_array_new ();

	if (base == namespace && *namespace) {
		/* Make sure to get INBOX: we always use LIST so the user sees his/her INBOX even
		   if it isn't subscribed and the user has enabled "Only show subscribed folders" */
		ic1 = camel_lite_imap4_engine_queue (engine, NULL, "LIST \"\" INBOX\r\n");
		camel_lite_imap4_command_register_untagged (ic1, "LIST", camel_lite_imap4_untagged_list);
		ic1->user_data = array;
	}

	if (*top != '\0') {
		size_t len;
		char sep;

		len = strlen (pattern);
		sep = pattern[len - 2];
		pattern[len - 2] = '\0';

		ic0 = camel_lite_imap4_engine_queue (engine, NULL, "%s \"\" %S\r\n", cmd, pattern);
		camel_lite_imap4_command_register_untagged (ic0, cmd, camel_lite_imap4_untagged_list);
		ic0->user_data = array;

		pattern[len - 2] = sep;
	}

	ic = camel_lite_imap4_engine_queue (engine, NULL, "%s \"\" %S\r\n", cmd, pattern);
	camel_lite_imap4_command_register_untagged (ic, cmd, camel_lite_imap4_untagged_list);
	ic->user_data = array;

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		if (ic1 && ic1->status != CAMEL_IMAP4_COMMAND_COMPLETE)
			camel_lite_exception_xfer (ex, &ic1->ex);
		else if (ic0 && ic0->status != CAMEL_IMAP4_COMMAND_COMPLETE)
			camel_lite_exception_xfer (ex, &ic0->ex);
		else
			camel_lite_exception_xfer (ex, &ic->ex);

		if (ic1 != NULL)
			camel_lite_imap4_command_unref (ic1);

		if (ic0 != NULL)
			camel_lite_imap4_command_unref (ic0);

		camel_lite_imap4_command_unref (ic);

		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}

		g_ptr_array_free (array, TRUE);
		g_free (pattern);

		goto done;
	}

	if (ic1 != NULL)
		camel_lite_imap4_command_unref (ic1);

	if (ic0 != NULL)
		camel_lite_imap4_command_unref (ic0);

	if (ic->result != CAMEL_IMAP4_RESULT_OK) {
		camel_lite_imap4_command_unref (ic);

		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get %s information for pattern `%s' on IMAP server %s: %s"),
				      cmd, pattern, engine->url->host, ic->result == CAMEL_IMAP4_RESULT_BAD ?
				      _("Bad command") : _("Unknown"));

		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}

		g_ptr_array_free (array, TRUE);

		g_free (pattern);

		goto done;
	}

	g_free (pattern);

	fi = imap4_build_folder_info (store, top, flags, array);

 done:

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	return fi;
}

static void
imap4_subscribe_folder (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelIMAP4Command *ic;
	CamelFolderInfo *fi;
	char *utf7_name;
	CamelURL *url;
	const char *p;
	int id;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot subscribe to IMAP folders in offline mode."));
		return;
	}

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "SUBSCRIBE %S\r\n", utf7_name);
	g_free (utf7_name);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	switch (ic->result) {
	case CAMEL_IMAP4_RESULT_OK:
		/* subscribed */
		url = camel_lite_url_copy (engine->url);
		camel_lite_url_set_fragment (url, folder_name);

		p = strrchr (folder_name, '/');

		fi = g_malloc0 (sizeof (CamelFolderInfo));
		fi->full_name = g_strdup (folder_name);
		fi->name = g_strdup (p ? p + 1: folder_name);
		fi->uri = camel_lite_url_to_string (url, CAMEL_URL_HIDE_ALL);
		camel_lite_url_free (url);
		fi->flags = CAMEL_FOLDER_NOCHILDREN;
		fi->unread = -1;
		fi->total = -1;

		camel_lite_imap4_store_summary_note_info (((CamelIMAP4Store *) store)->summary, fi);

		camel_lite_object_trigger_event (store, "folder_subscribed", fi);
		camel_lite_folder_info_free (fi);
		break;
	case CAMEL_IMAP4_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot subscribe to folder `%s': Invalid mailbox name"),
				      folder_name);
		break;
	case CAMEL_IMAP4_RESULT_BAD:
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot subscribe to folder `%s': Bad command"),
				      folder_name);
		break;
	}

	camel_lite_imap4_command_unref (ic);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static void
imap4_unsubscribe_folder (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelIMAP4Command *ic;
	CamelFolderInfo *fi;
	char *utf7_name;
	CamelURL *url;
	const char *p;
	int id;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot unsubscribe from IMAP folders in offline mode."));
		return;
	}

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	utf7_name = imap4_folder_utf7_name (store, folder_name, '\0');
	ic = camel_lite_imap4_engine_queue (engine, NULL, "UNSUBSCRIBE %S\r\n", utf7_name);
	g_free (utf7_name);

	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_exception_xfer (ex, &ic->ex);
		camel_lite_imap4_command_unref (ic);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	switch (ic->result) {
	case CAMEL_IMAP4_RESULT_OK:
		/* unsubscribed */
		url = camel_lite_url_copy (engine->url);
		camel_lite_url_set_fragment (url, folder_name);

		p = strrchr (folder_name, '/');

		fi = g_malloc0 (sizeof (CamelFolderInfo));
		fi->full_name = g_strdup (folder_name);
		fi->name = g_strdup (p ? p + 1: folder_name);
		fi->uri = camel_lite_url_to_string (url, CAMEL_URL_HIDE_ALL);
		camel_lite_url_free (url);
		fi->flags = 0;
		fi->unread = -1;
		fi->total = -1;

		camel_lite_imap4_store_summary_unnote_info (((CamelIMAP4Store *) store)->summary, fi);

		camel_lite_object_trigger_event (store, "folder_unsubscribed", fi);
		camel_lite_folder_info_free (fi);
		break;
	case CAMEL_IMAP4_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot unsubscribe from folder `%s': Invalid mailbox name"),
				      folder_name);
		break;
	case CAMEL_IMAP4_RESULT_BAD:
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot unsubscribe from folder `%s': Bad command"),
				      folder_name);
		break;
	}

	camel_lite_imap4_command_unref (ic);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static void
imap4_noop (CamelStore *store, CamelException *ex)
{
	CamelIMAP4Engine *engine = ((CamelIMAP4Store *) store)->engine;
	CamelFolder *folder = (CamelFolder *) engine->folder;
	CamelIMAP4Command *ic;
	int id;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	if (folder) {
		camel_lite_folder_sync (folder, FALSE, ex);
		if (camel_lite_exception_is_set (ex)) {
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
			return;
		}
	}

	ic = camel_lite_imap4_engine_queue (engine, NULL, "NOOP\r\n");
	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE)
		camel_lite_exception_xfer (ex, &ic->ex);

	camel_lite_imap4_command_unref (ic);

	if (folder && !camel_lite_exception_is_set (ex))
		camel_lite_imap4_summary_flush_updates (folder->summary, ex);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}
