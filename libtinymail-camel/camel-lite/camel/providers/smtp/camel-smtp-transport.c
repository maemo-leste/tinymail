/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-transport.c : class for a smtp transport */

/*
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc. (www.ximian.com)
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#undef MIN
#undef MAX

#include "camel-exception.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-message.h"
#include "camel-mime-part.h"
#include "camel-multipart.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-sasl.h"
#include "camel-session.h"
#include "camel-smtp-transport.h"
#include "camel-stream-buffer.h"
#include "camel-stream-filter.h"
#include "camel-tcp-stream-raw.h"
#include "camel-tcp-stream.h"

#ifdef HAVE_SSL
#include "camel-tcp-stream-ssl.h"
#endif

extern int camel_lite_verbose_debug;
#define d(x) (camel_lite_verbose_debug ? (x) : 0)

/* Specified in RFC 821 */
#define SMTP_PORT "25"
#define SMTPS_PORT "465"

/* camel smtp transport class prototypes */
static gboolean smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
			      CamelAddress *from, CamelAddress *recipients, CamelException *ex);

/* support prototypes */
static void smtp_construct (CamelService *service, CamelSession *session,
			    CamelProvider *provider, CamelURL *url,
			    CamelException *ex);
static gboolean smtp_connect (CamelService *service, CamelException *ex);
static gboolean smtp_disconnect (CamelService *service, gboolean clean, CamelException *ex);
static GHashTable *esmtp_get_authtypes (const unsigned char *buffer);
static GList *query_auth_types (CamelService *service, CamelException *ex);
static char *get_name (CamelService *service, gboolean brief);

static gboolean smtp_helo (CamelSmtpTransport *transport, CamelException *ex);
static gboolean smtp_auth (CamelSmtpTransport *transport, const char *mech, CamelException *ex);
static gboolean smtp_mail (CamelSmtpTransport *transport, const char *sender,
			   gboolean has_8bit_parts, CamelException *ex);
static gboolean smtp_rcpt (CamelSmtpTransport *transport, const char *recipient, CamelException *ex);
static gboolean smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message, CamelException *ex);
static gboolean smtp_rset (CamelSmtpTransport *transport, CamelException *ex);
static gboolean smtp_quit (CamelSmtpTransport *transport, CamelException *ex);

static void smtp_set_exception (CamelSmtpTransport *transport, gboolean disconnect, const char *respbuf,
				const char *message, CamelException *ex);

/* private data members */
static CamelTransportClass *parent_class = NULL;

static void
camel_lite_smtp_transport_class_init (CamelSmtpTransportClass *camel_lite_smtp_transport_class)
{
	CamelTransportClass *camel_lite_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_lite_smtp_transport_class);
	CamelServiceClass *camel_lite_service_class =
		CAMEL_SERVICE_CLASS (camel_lite_smtp_transport_class);

	parent_class = CAMEL_TRANSPORT_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_transport_get_type ()));

	/* virtual method overload */
	camel_lite_service_class->construct = smtp_construct;
	camel_lite_service_class->connect = smtp_connect;
	camel_lite_service_class->disconnect = smtp_disconnect;
	camel_lite_service_class->query_auth_types = query_auth_types;
	camel_lite_service_class->get_name = get_name;

	camel_lite_transport_class->send_to = smtp_send_to;
}

static void
camel_lite_smtp_transport_init (gpointer object)
{
	CamelSmtpTransport *smtp = CAMEL_SMTP_TRANSPORT (object);

	smtp->flags = 0;
	smtp->connected = FALSE;
}

static void
camel_lite_smtp_transport_finalize (CamelSmtpTransport *self)
{
	return;
}

CamelType
camel_lite_smtp_transport_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (CAMEL_TRANSPORT_TYPE,
					    "CamelLiteSmtpTransport",
					    sizeof (CamelSmtpTransport),
					    sizeof (CamelSmtpTransportClass),
					    (CamelObjectClassInitFunc) camel_lite_smtp_transport_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_smtp_transport_init,
					    (CamelObjectFinalizeFunc) camel_lite_smtp_transport_finalize);
	}

	return type;
}

static void
smtp_construct (CamelService *service, CamelSession *session,
		CamelProvider *provider, CamelURL *url,
		CamelException *ex)
{
	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
}

static const char *
smtp_error_string (int error)
{
	/* SMTP error codes grabbed from rfc821 */
	switch (error) {
	case 0:
		/* looks like a read problem, check errno */
		if (errno)
			return g_strerror (errno);
		else
			return _("Unknown");
	case 500:
		return _("Syntax error, command unrecognized");
	case 501:
		return _("Syntax error in parameters or arguments");
	case 502:
		return _("Command not implemented");
	case 504:
		return _("Command parameter not implemented");
	case 211:
		return _("System status, or system help reply");
	case 214:
		return _("Help message");
	case 220:
		return _("Service ready");
	case 221:
		return _("Service closing transmission channel");
	case 421:
		return _("Service not available, closing transmission channel");
	case 250:
		return _("Requested mail action okay, completed");
	case 251:
		return _("User not local; will forward to <forward-path>");
	case 450:
		return _("Requested mail action not taken: mailbox unavailable");
	case 550:
		return _("Requested action not taken: mailbox unavailable");
	case 451:
		return _("Requested action aborted: error in processing");
	case 551:
		return _("User not local; please try <forward-path>");
	case 452:
		return _("Requested action not taken: insufficient system storage");
	case 552:
		return _("Requested mail action aborted: exceeded storage allocation");
	case 553:
		return _("Requested action not taken: mailbox name not allowed");
	case 354:
		return _("Start mail input; end with <CRLF>.<CRLF>");
	case 554:
		return _("Transaction failed");

	/* AUTH error codes: */
	case 432:
		return _("A password transition is needed");
	case 534:
		return _("Authentication mechanism is too weak");
	case 538:
		return _("Encryption required for requested authentication mechanism");
	case 454:
		return _("Temporary authentication failure");
	case 530:
		return _("Authentication required");

	default:
		return _("Unknown");
	}
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3 | CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelService *service, struct addrinfo *ai, int ssl_mode, int must_tls, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelStream *tcp_stream;
	char *respbuf = NULL;
	int ret;

	if (!CAMEL_SERVICE_CLASS (parent_class)->connect (service, ex))
		return FALSE;

	/* set some smtp transport defaults */
	transport->flags = 0;
	transport->authtypes = NULL;

	if (ssl_mode != MODE_CLEAR) {
#ifdef HAVE_SSL
		if (ssl_mode == MODE_TLS) {
			tcp_stream = camel_lite_tcp_stream_ssl_new_raw (service, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = camel_lite_tcp_stream_ssl_new (service, service->url->host, SSL_PORT_FLAGS);
		}
#else
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CONNECT,
				      _("Could not connect to %s: %s"),
				      service->url->host, _("SSL unavailable"));

		return FALSE;
#endif /* HAVE_SSL */
	} else {
		tcp_stream = camel_lite_tcp_stream_raw_new ();
	}

	if ((ret = camel_lite_tcp_stream_connect ((CamelTcpStream *) tcp_stream, ai)) == -1) {
		if (errno == EINTR)
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     _("Connection canceled"));
		else
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CONNECT,
					      _("Could not connect to %s: %s"),
					      service->url->host, g_strerror (errno));

		camel_lite_object_unref (tcp_stream);

		return FALSE;
	}

	transport->connected = TRUE;

	/* get the localaddr - needed later by smtp_helo */
	transport->localaddr = camel_lite_tcp_stream_get_local_address (CAMEL_TCP_STREAM (tcp_stream), &transport->localaddrlen);

	transport->ostream = tcp_stream;
	transport->istream = camel_lite_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);

	/* Read the greeting, note whether the server is ESMTP or not. */
	do {
		/* Check for "220" */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		if (!respbuf || strncmp (respbuf, "220", 3)) {
			smtp_set_exception (transport, FALSE, respbuf, _("Welcome response error"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */
	g_free (respbuf);

	/* Try sending EHLO */
	transport->flags |= CAMEL_SMTP_TRANSPORT_IS_ESMTP;
	if (!smtp_helo (transport, ex)) {
		if (!transport->connected)
			return FALSE;

		/* Fall back to HELO */
		camel_lite_exception_clear (ex);
		transport->flags &= ~CAMEL_SMTP_TRANSPORT_IS_ESMTP;

		if (!smtp_helo (transport, ex)) {
			camel_lite_service_disconnect ((CamelService *) transport, TRUE, NULL);

			return FALSE;
		}
	}

	/* clear any EHLO/HELO exception and assume that any SMTP errors encountered were non-fatal */
	camel_lite_exception_clear (ex);

	if (ssl_mode != MODE_TLS) {

		int i = 0;

		i++;

		/* we're done */
		return TRUE;
	} else {

#ifdef HAVE_SSL
	if (!(transport->flags & CAMEL_SMTP_TRANSPORT_STARTTLS)) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CONNECT,
				      _("Failed to connect to SMTP server %s in secure mode: %s"),
				      service->url->host, _("STARTTLS not supported"));

		goto exception_cleanup;
	}

	smtp_debug ("-> STARTTLS\r\n");
	if (camel_lite_stream_write (tcp_stream, "STARTTLS\r\n", 10) == -1) {
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("STARTTLS command failed: %s"),
				      g_strerror (errno));
		goto exception_cleanup;
	}

	respbuf = NULL;

	do {
		/* Check for "220 Ready for TLS" */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

		if (!respbuf || strncmp (respbuf, "220", 3)) {
			smtp_set_exception (transport, FALSE, respbuf, _("STARTTLS command failed"), ex);
			g_free (respbuf);
			goto exception_cleanup;
		}
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */

	/* Okay, now toggle SSL/TLS mode */
	if (camel_lite_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CONNECT,
				      _("Failed to connect to SMTP server %s in secure mode: %s"),
				      service->url->host, g_strerror (errno));
		goto exception_cleanup;
	}
#else
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CONNECT,
			      _("Failed to connect to SMTP server %s in secure mode: %s"),
			      service->url->host, _("SSL is not available in this build"));
	goto exception_cleanup;
#endif /* HAVE_SSL */

	/* We are supposed to re-EHLO after a successful STARTTLS to
           re-fetch any supported extensions. */
	if (!smtp_helo (transport, ex)) {
		camel_lite_service_disconnect ((CamelService *) transport, TRUE, NULL);

		return FALSE;
	}

	return TRUE;

	}

 exception_cleanup:

	camel_lite_object_unref (transport->istream);
	transport->istream = NULL;
	camel_lite_object_unref (transport->ostream);
	transport->ostream = NULL;

	transport->connected = FALSE;

	return FALSE;
}

static struct {
	char *value;
	char *serv;
	char *port;
	int mode;
	int must_tls;
} ssl_options[] = {
	{ "",              "smtps", SMTPS_PORT, MODE_SSL, 0   },  /* really old (1.x) */
	{ "wrapped",       "smtps", SMTPS_PORT, MODE_SSL, 0   },
	{ "tls",           "smtp",  SMTP_PORT, MODE_TLS, 1   },
	{ "when-possible", "smtp",  SMTP_PORT, MODE_TLS, 0   },
	{ "never",         "smtp",  SMTP_PORT, MODE_CLEAR, 0 },
	{ NULL,            "smtp",  SMTP_PORT, MODE_CLEAR, 0 },
};

static gboolean
connect_to_server_wrapper (CamelService *service, CamelException *ex)
{
	struct addrinfo hints, *ai;
	const char *ssl_mode;
	int mode, ret, i, must_tls = 0;
	char *serv;
	const char *port;

	if ((ssl_mode = camel_lite_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = ssl_options[i].serv;
		port = ssl_options[i].port;
		must_tls = ssl_options[i].must_tls;
	} else {
		mode = MODE_CLEAR;
		serv = "smtp";
		port = SMTP_PORT;
		must_tls = 0;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		port = NULL;
	}

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	ai = camel_lite_getaddrinfo(service->url->host, serv, &hints, ex);
	if (ai == NULL && port != NULL && camel_lite_exception_get_id(ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		camel_lite_exception_clear (ex);
		ai = camel_lite_getaddrinfo(service->url->host, port, &hints, ex);
	}

	if (ai == NULL)
		return FALSE;

	ret = connect_to_server (service, ai, mode, must_tls, ex);

	camel_lite_freeaddrinfo (ai);

	return ret;
}

static gboolean
smtp_connect (CamelService *service, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	gboolean has_authtypes;
	gboolean mtry = 0;
	gboolean authenticated = FALSE;

	/* We (probably) need to check popb4smtp before we connect ... */
	if (service->url->authmech && !strcmp (service->url->authmech, "POPB4SMTP")) {
		int truth;
		GByteArray *chal;
		CamelSasl *sasl;

		sasl = camel_lite_sasl_new ("smtp", "POPB4SMTP", service);
		chal = camel_lite_sasl_challenge (sasl, NULL, ex);
		truth = camel_lite_sasl_authenticated (sasl);
		if (chal)
			g_byte_array_free (chal, TRUE);
		camel_lite_object_unref (sasl);

		if (!truth)
			return FALSE;

		return connect_to_server_wrapper (service, ex);
	}

	if (!connect_to_server_wrapper (service, ex))
		return FALSE;

	/* check to see if AUTH is required, if so...then AUTH ourselves */
	has_authtypes = transport->authtypes ? g_hash_table_size (transport->authtypes) > 0 : FALSE;
	if (service->url->authmech && (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) && has_authtypes) {
		CamelSession *session = camel_lite_service_get_session (service);
		CamelServiceAuthType *authtype;
		guint32 password_flags;
		char *errbuf = NULL;

		if (!g_hash_table_lookup (transport->authtypes, service->url->authmech)) {
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("SMTP server %s does not support requested "
						"authentication type %s."),
					      service->url->host, service->url->authmech);

			camel_lite_session_alert_user (session, CAMEL_SESSION_ALERT_ERROR,
				ex, FALSE, service);

			camel_lite_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}

		authtype = camel_lite_sasl_authtype (service->url->authmech);
		if (!authtype) {
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("No support for authentication type %s"),
					      service->url->authmech);
			camel_lite_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}

		if (!authtype->need_password) {
			/* authentication mechanism doesn't need a password,
			   so if it fails there's nothing we can do */
			authenticated = smtp_auth (transport, authtype->authproto, ex);
			if (!authenticated) {
				camel_lite_service_disconnect (service, TRUE, NULL);
				return FALSE;
			}
		}

		password_flags = CAMEL_SESSION_PASSWORD_SECRET;

		/* keep trying to login until either we succeed or the user cancels */
		while (!authenticated && mtry < 3) {
			mtry++;
			if (errbuf) {
				/* We need to un-cache the password before prompting again */
				camel_lite_session_forget_password (session, service, NULL, "password", NULL);
				g_free (service->url->passwd);
				service->url->passwd = NULL;
			}

			if (!service->url->passwd) {
				char *base_prompt;
				char *full_prompt;

				base_prompt = camel_lite_session_build_password_prompt (
					"SMTP", service->url->user, service->url->host);

				if (errbuf != NULL)
					full_prompt = g_strconcat (errbuf, base_prompt, NULL);
				else
					full_prompt = g_strdup (base_prompt);

				service->url->passwd = camel_lite_session_get_password (
					session, service, NULL, full_prompt,
					"password", CAMEL_SESSION_PASSWORD_SECRET, ex);

				g_free (base_prompt);
				g_free (full_prompt);
				g_free (errbuf);
				errbuf = NULL;

				if (!service->url->passwd) {
					camel_lite_service_disconnect (service, TRUE, NULL);
					authenticated = FALSE;
					goto error_situation;
				}
			}

			authenticated = smtp_auth (transport, authtype->authproto, ex);
			if (!authenticated) {
				if (camel_lite_exception_get_id (ex) == CAMEL_EXCEPTION_USER_CANCEL ||
				    camel_lite_exception_get_id (ex) == CAMEL_EXCEPTION_SERVICE_UNAVAILABLE)
					return FALSE;

				errbuf = g_markup_printf_escaped (
					_("Unable to authenticate "
					  "to SMTP server.\n%s\n\n"),
					camel_lite_exception_get_description (ex));
				camel_lite_exception_clear (ex);

				g_free (service->url->passwd);
				service->url->passwd = NULL;
			}

			/* Force a password prompt on the next pass, in
			 * case we have an invalid password cached.  This
			 * avoids repeated authentication attempts using
			 * the same invalid password. */
			password_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
		}
	} else 
		authenticated = TRUE;

error_situation:

	if (!authenticated)
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
			      _("Authentication error on SMTP server %s"),
			      service->url->host);

	return authenticated;
}

static void
authtypes_free (gpointer key, gpointer value, gpointer data)
{
	g_free (value);
}

static gboolean
smtp_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);

	/*if (!service->connected)
	 *	return TRUE;
	 */

	if (transport->connected && clean) {
		/* send the QUIT command to the SMTP server */
		smtp_quit (transport, ex);
	}

	if (!CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex))
		return FALSE;

	if (transport->authtypes) {
		g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
		g_hash_table_destroy (transport->authtypes);
		transport->authtypes = NULL;
	}

	if (transport->istream) {
		camel_lite_object_unref (transport->istream);
		transport->istream = NULL;
	}

	if (transport->ostream) {
		camel_lite_object_unref (transport->ostream);
		transport->ostream = NULL;
	}

	g_free(transport->localaddr);
	transport->localaddr = NULL;

	transport->connected = FALSE;

	return TRUE;
}

static GHashTable *
esmtp_get_authtypes (const unsigned char *buffer)
{
	const unsigned char *start, *end;
	GHashTable *table = NULL;

	/* advance to the first token */
	start = buffer;
	while (isspace ((int) *start) || *start == '=')
		start++;

	if (!*start)
		return NULL;

	table = g_hash_table_new (g_str_hash, g_str_equal);

	for ( ; *start; ) {
		char *type;

		/* advance to the end of the token */
		end = start;
		while (*end && !isspace ((int) *end))
			end++;

		type = g_strndup ((gchar*) start, end - start);
		g_hash_table_insert (table, type, type);

		/* advance to the next token */
		start = end;
		while (isspace ((int) *start))
			start++;
	}

	return table;
}

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelServiceAuthType *authtype;
	GList *types, *t, *next;

	if (!connect_to_server_wrapper (service, ex))
		return NULL;

	types = g_list_copy (service->provider->authtypes);
	for (t = types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (transport->authtypes, authtype->authproto)) {
			types = g_list_remove_link (types, t);
			g_list_free_1 (t);
		}
	}

	smtp_disconnect (service, TRUE, NULL);

	return types;
}

static char *
get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("SMTP server %s"), service->url->host);
	else {
		return g_strdup_printf (_("SMTP mail delivery via %s"),
					service->url->host);
	}
}

static gboolean
smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients,
	      CamelException *ex)
{
	CamelSmtpTransport *smtp_transport = CAMEL_SMTP_TRANSPORT (transport);
	const CamelInternetAddress *cia;
	gboolean has_8bit_parts;
	const char *addr;
	int i, len;

	if (!smtp_transport->connected) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED,
				      _("Cannot send message: service not connected."));
		return FALSE;
	}

	if (!camel_lite_internet_address_get (CAMEL_INTERNET_ADDRESS (from), 0, NULL, &addr)) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot send message: sender address not valid."));
		return FALSE;
	}

	camel_lite_operation_start (NULL, _("Sending message"));

	/* find out if the message has 8bit mime parts */
	has_8bit_parts = camel_lite_mime_message_has_8bit_parts (message);

	/* rfc1652 (8BITMIME) requires that you notify the ESMTP daemon that
	   you'll be sending an 8bit mime message at "MAIL FROM:" time. */
	if (!smtp_mail (smtp_transport, addr, has_8bit_parts, ex)) {
		camel_lite_operation_end (NULL);
		return FALSE;
	}

	len = camel_lite_address_length (recipients);
	if (len == 0) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot send message: no recipients defined."));
		camel_lite_operation_end (NULL);
		return FALSE;
	}

	cia = CAMEL_INTERNET_ADDRESS (recipients);
	for (i = 0; i < len; i++) {
		char *enc;

		if (!camel_lite_internet_address_get (cia, i, NULL, &addr)) {
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Cannot send message: one or more invalid recipients"));
			camel_lite_operation_end (NULL);
			return FALSE;
		}

		enc = camel_lite_internet_address_encode_address(NULL, NULL, addr);
		if (!smtp_rcpt (smtp_transport, enc, ex)) {
			g_free(enc);
			camel_lite_operation_end (NULL);
			return FALSE;
		}
		g_free(enc);
	}

	if (!smtp_data (smtp_transport, message, ex)) {
		camel_lite_operation_end (NULL);
		return FALSE;
	}

	/* reset the service for our next transfer session */
	if (!smtp_rset (smtp_transport, ex))
		camel_lite_exception_clear (ex);

	camel_lite_operation_end (NULL);

	camel_lite_exception_clear (ex);

	return TRUE;
}

static const char *
smtp_next_token (const char *buf)
{
	const unsigned char *token;

	token = (const unsigned char *) buf;
	while (*token && !isspace ((int) *token))
		token++;

	while (*token && isspace ((int) *token))
		token++;

	return (const char *) token;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : (c) - 'A' + 10)

/*
 * example (rfc2034):
 * 5.1.1 Mailbox "nosuchuser" does not exist
 *
 * The human-readable status code is what we want. Since this text
 * could possibly be encoded, we must decode it.
 *
 * "xtext" is formally defined as follows:
 *
 *   xtext = *( xchar / hexchar / linear-white-space / comment )
 *
 *   xchar = any ASCII CHAR between "!" (33) and "~" (126) inclusive,
 *        except for "+", "\" and "(".
 *
 * "hexchar"s are intended to encode octets that cannot be represented
 * as plain text, either because they are reserved, or because they are
 * non-printable.  However, any octet value may be represented by a
 * "hexchar".
 *
 *   hexchar = ASCII "+" immediately followed by two upper case
 *        hexadecimal digits
 */
static char *
smtp_decode_status_code (const char *in, size_t len)
{
	unsigned char *inptr, *outptr;
	const unsigned char *inend;
	char *outbuf;

	outbuf = (char *) g_malloc (len + 1);
	outptr = (unsigned char *) outbuf;

	inptr = (unsigned char *) in;
	inend = inptr + len;
	while (inptr < inend) {
		if (*inptr == '+') {
			if (isxdigit (inptr[1]) && isxdigit (inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else
				*outptr++ = *inptr++;
		} else
			*outptr++ = *inptr++;
	}

	*outptr = '\0';

	return outbuf;
}

/* converts string str to local encoding, thinking it's in utf8.
   If fails, then converts all character greater than 127 to hex values.
   Also those under 32, other than \n, \r, \t.
   Note that the c is signed character, so all characters above 127 have
   negative value.
*/
static void
convert_to_local (GString *str)
{
	char *buf;

	buf = g_locale_from_utf8 (str->str, str->len, NULL, NULL, NULL);

	if (!buf) {
		int i;
		gchar c;
		GString *s = g_string_new_len (str->str, str->len);

		g_string_truncate (str, 0);

		for (i = 0; i < s->len; i++) {
			c = s->str[i];

			if (c < 32 && c != '\n' && c != '\r' && c != '\t')
				g_string_append_printf (str, "<%X%X>", (c >> 4) & 0xF, c & 0xF);
			else
				g_string_append_c (str, c);
		}

		g_string_free (s, TRUE);
	} else {
		g_string_truncate (str, 0);
		g_string_append (str, buf);

		g_free (buf);
	}
}

static void
smtp_set_exception (CamelSmtpTransport *transport, gboolean disconnect, const char *respbuf, const char *message, CamelException *ex)
{
	const char *token, *rbuf = respbuf;
	char *buffer = NULL;
	GString *string;
	int error;

	if (!respbuf) {
	fake_status_code:
		error = respbuf ? atoi (respbuf) : 0;
		camel_lite_exception_setv (ex, error == 0 && errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      "%s: %s", message, smtp_error_string (error));
	} else {
		string = g_string_new ("");
		do {
			if (transport->flags & CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES)
				token = smtp_next_token (rbuf + 4);
			else
				token = rbuf + 4;

			if (*token == '\0') {
				g_free (buffer);
				g_string_free (string, TRUE);
				goto fake_status_code;
			}

			g_string_append (string, token);
			if (*(rbuf + 3) == '-') {
				g_free (buffer);
				buffer = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
				g_string_append_c (string, '\n');
			} else {
				g_free (buffer);
				buffer = NULL;
			}

			rbuf = buffer;
		} while (rbuf);

		convert_to_local (string);
		if (!(transport->flags & CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES) && string->len) {
			string->str = g_strstrip (string->str);
			string->len = strlen (string->str);

			if (!string->len) {
				g_string_free (string, TRUE);
				goto fake_status_code;
			}

			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      "%s: %s", message, string->str);

			g_string_free (string, TRUE);
		} else {
			buffer = smtp_decode_status_code (string->str, string->len);
			g_string_free (string, TRUE);
			if (!buffer)
				goto fake_status_code;

			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      "%s: %s", message, buffer);

			g_free (buffer);
		}
	}

	if (!respbuf) {
		/* we got disconnected */
		if (disconnect)
			camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);
		else
			transport->connected = FALSE;
	}
}

static gboolean
smtp_helo (CamelSmtpTransport *transport, CamelException *ex)
{
	char *name = NULL, *cmdbuf = NULL, *respbuf = NULL;
	const char *token, *numeric = NULL;
	struct sockaddr *addr;
	socklen_t addrlen;

	/* these are flags that we set, so unset them in case we
	   are being called a second time (ie, after a STARTTLS) */
	transport->flags &= ~(CAMEL_SMTP_TRANSPORT_8BITMIME |
			      CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES |
			      CAMEL_SMTP_TRANSPORT_STARTTLS);

	if (transport->authtypes) {
		g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
		g_hash_table_destroy (transport->authtypes);
		transport->authtypes = NULL;
	}

	camel_lite_operation_start_transient (NULL, _("SMTP Greeting"));

	addr = transport->localaddr;
	addrlen = transport->localaddrlen;

	if (camel_lite_getnameinfo (addr, addrlen, &name, NULL, NI_NUMERICHOST, NULL) != 0) {
		name = g_strdup ("localhost.localdomain");
	} else {
		if (addr->sa_family == AF_INET6)
			numeric = "IPv6:";
		else
			numeric = "";
	}

	token = (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) ? "EHLO" : "HELO";
	if (numeric)
		cmdbuf = g_strdup_printf("%s [%s%s]\r\n", token, numeric, name);
	else
		cmdbuf = g_strdup_printf("%s %s\r\n", token, name);
	g_free (name);

	smtp_debug ("-> %s\n", cmdbuf);
	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("HELO command failed: %s"), g_strerror (errno));
		camel_lite_operation_end (NULL);

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, FALSE, respbuf, _("HELO command failed"), ex);
			camel_lite_operation_end (NULL);
			g_free (respbuf);

			return FALSE;
		}
		
		token = respbuf + 4;
		
		if (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) {
			if (!strncmp (token, "8BITMIME", 8)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_8BITMIME;
			} else if (!strncmp (token, "ENHANCEDSTATUSCODES", 19)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES;
			} else if (!strncmp (token, "STARTTLS", 8)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_STARTTLS;
			} else if (!strncmp (token, "AUTH", 4)) {
				if (!transport->authtypes || transport->flags & CAMEL_SMTP_TRANSPORT_AUTH_EQUAL) {
					/* Don't bother parsing any authtypes if we already have a list.
					 * Some servers will list AUTH twice, once the standard way and
					 * once the way Microsoft Outlook requires them to be:
					 *
					 * 250-AUTH LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 * 250-AUTH=LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 *
					 * Since they can come in any order, parse each list that we get
					 * until we parse an authtype list that does not use the AUTH=
					 * format. We want to let the standard way have priority over the
					 * broken way.
					 **/

					if (token[4] == '=')
						transport->flags |= CAMEL_SMTP_TRANSPORT_AUTH_EQUAL;
					else
						transport->flags &= ~CAMEL_SMTP_TRANSPORT_AUTH_EQUAL;

					/* parse for supported AUTH types */
					token += 5;

					if (transport->authtypes) {
						g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
						g_hash_table_destroy (transport->authtypes);
					}

					transport->authtypes = esmtp_get_authtypes ((const unsigned char *) token);
				}
			}
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	camel_lite_operation_end (NULL);

	return TRUE;
}

static gboolean
smtp_auth (CamelSmtpTransport *transport, const char *mech, CamelException *ex)
{
	char *cmdbuf, *respbuf = NULL, *challenge;
	gboolean auth_challenge = FALSE;
	CamelSasl *sasl = NULL;
	gboolean avoid_exception = FALSE;

	camel_lite_operation_start_transient (NULL, _("SMTP Authentication"));

	sasl = camel_lite_sasl_new ("smtp", mech, CAMEL_SERVICE (transport));
	if (!sasl) {
		camel_lite_operation_end (NULL);
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Error creating SASL authentication object."));
		return FALSE;
	}

	challenge = camel_lite_sasl_challenge_base64 (sasl, NULL, ex);
	if (challenge) {
		auth_challenge = TRUE;
		cmdbuf = g_strdup_printf ("AUTH %s %s\r\n", mech, challenge);
		g_free (challenge);
	} else {
		cmdbuf = g_strdup_printf ("AUTH %s\r\n", mech);
	}

	smtp_debug ("-> %s\n", cmdbuf);
	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("AUTH command failed: %s"), g_strerror (errno));
		goto lose;
	}
	g_free (cmdbuf);

	respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
	smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

	while (!camel_lite_sasl_authenticated (sasl)) {
		if (!respbuf) {
			camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
					      _("AUTH command failed: %s"), g_strerror (errno));
			goto lose;
		}

		/* the server challenge/response should follow a 334 code */
		if (strncmp (respbuf, "334", 3) != 0) {
			if (strncmp (respbuf, "535", 3) == 0) {
				/* Workaround for GMAIL smtp:
				 * If connection sasl dialog is broken, we restart the connection.
				 * This happens with Gmail SMTP server, where subsequent attempts
				 * to try a password fail with a 535 if we use the original connection
				 */
				camel_lite_service_disconnect (CAMEL_SERVICE (transport), FALSE, NULL);
				camel_lite_exception_clear (ex);
				if (connect_to_server_wrapper (CAMEL_SERVICE (transport), ex))
					avoid_exception = TRUE;
			} 
			if (!avoid_exception)  {
				smtp_set_exception (transport, FALSE, respbuf, _("AUTH command failed"), ex);
			}
			g_free (respbuf);
			goto lose;
		}

		if (FALSE) {
		broken_smtp_server:
			d(fprintf (stderr, "Your SMTP server's implementation of the %s SASL\n"
				   "authentication mechanism is broken. Please report this to the\n"
				   "appropriate vendor and suggest that they re-read rfc2554 again\n"
				   "for the first time (specifically Section 4).\n",
				   mech));
		}

		/* eat whtspc */
		for (challenge = respbuf + 4; isspace (*challenge); challenge++);

		challenge = camel_lite_sasl_challenge_base64 (sasl, challenge, ex);
		g_free (respbuf);
		if (challenge == NULL)
			goto break_and_lose;

		/* send our challenge */
		cmdbuf = g_strdup_printf ("%s\r\n", challenge);
		g_free (challenge);
		smtp_debug ("-> %s\n", cmdbuf);
		if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
			g_free (cmdbuf);
			goto lose;
		}
		g_free (cmdbuf);

		/* get the server's response */
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");
	}

	/* check that the server says we are authenticated */
	if (!respbuf || strncmp (respbuf, "235", 3)) {
		if (respbuf && !strncmp (respbuf, "334", 3)) {
			/* broken server, but lets try and work around it anyway... */
			goto broken_smtp_server;
		} else if (respbuf && strncmp (respbuf, "535", 3)) {
			g_free (CAMEL_SERVICE (transport)->url->passwd);
			CAMEL_SERVICE (transport)->url->passwd = NULL;
		}
		g_free (respbuf);
		goto lose;
	}

	g_free (respbuf);
	camel_lite_object_unref (sasl);
	camel_lite_operation_end (NULL);

	return TRUE;

 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	smtp_debug ("-> *\n");
	camel_lite_stream_write (transport->ostream, "*\r\n", 3);
	respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
	smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

 lose:
	if (!camel_lite_exception_is_set (ex) && !avoid_exception) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				     _("Bad authentication response from server.\n"));
	}

	camel_lite_object_unref (sasl);
	camel_lite_operation_end (NULL);

	return FALSE;
}

static gboolean
smtp_mail (CamelSmtpTransport *transport, const char *sender, gboolean has_8bit_parts, CamelException *ex)
{
	/* we gotta tell the smtp server who we are. (our email addy) */
	char *cmdbuf, *respbuf = NULL;

	if (transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME && has_8bit_parts)
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s> BODY=8BITMIME\r\n", sender);
	else
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s>\r\n", sender);

	smtp_debug ("-> %s\n", cmdbuf);

	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("MAIL FROM command failed: %s: mail not sent"),
				      g_strerror (errno));

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250 Sender OK..." */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, TRUE, respbuf, _("MAIL FROM command failed"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_rcpt (CamelSmtpTransport *transport, const char *recipient, CamelException *ex)
{
	/* we gotta tell the smtp server who we are going to be sending
	 * our email to */
	char *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup_printf ("RCPT TO:<%s>\r\n", recipient);

	smtp_debug ("-> %s", cmdbuf);

	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("RCPT TO command failed: %s: mail not sent"),
				      g_strerror (errno));

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250 Recipient OK..." */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ( "<- %s\n", respbuf ? respbuf : "(null)");

		if (!respbuf || strncmp (respbuf, "250", 3)) {
			char *message;

			message = g_strdup_printf (_("RCPT TO <%s> failed"), recipient);
			smtp_set_exception (transport, TRUE, respbuf, message, ex);
			g_free (message);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message, CamelException *ex)
{
	CamelBestencEncoding enctype = CAMEL_BESTENC_8BIT;
	struct _camel_lite_header_raw *header, *savedbcc, *n, *tail;
	char *cmdbuf, *respbuf = NULL;
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *crlffilter;
	int ret, mtry;

	/* If the server doesn't support 8BITMIME, set our required encoding to be 7bit */
	if (!(transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME))
		enctype = CAMEL_BESTENC_7BIT;

	/* FIXME: should we get the best charset too?? */
	/* Changes the encoding of all mime parts to fit within our required
	   encoding type and also force any text parts with long lines (longer
	   than 998 octets) to wrap by QP or base64 encoding them. */
	camel_lite_mime_message_set_best_encoding (message, CAMEL_BESTENC_GET_ENCODING, enctype);

	cmdbuf = g_strdup ("DATA\r\n");

	smtp_debug ("-> %s\n", cmdbuf);

	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("DATA command failed: %s: mail not sent"),
				      g_strerror (errno));

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

	smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

	if (!respbuf || strncmp (respbuf, "354", 3)) {
		/* we should have gotten instructions on how to use the DATA command:
		 * 354 Enter mail, end with "." on a line by itself
		 */
		smtp_set_exception (transport, TRUE, respbuf, _("DATA command failed"), ex);
		g_free (respbuf);
		return FALSE;
	}

	g_free (respbuf);
	respbuf = NULL;

	/* setup stream filtering */
	crlffilter = camel_lite_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filtered_stream = camel_lite_stream_filter_new_with_stream (transport->ostream);
	camel_lite_stream_filter_add (filtered_stream, CAMEL_MIME_FILTER (crlffilter));
	camel_lite_object_unref (crlffilter);

	/* unlink the bcc headers */
	savedbcc = NULL;
	tail = (struct _camel_lite_header_raw *) &savedbcc;

	header = (struct _camel_lite_header_raw *) &CAMEL_MIME_PART (message)->headers;
	n = header->next;
	while (n != NULL) {
		if (!g_ascii_strcasecmp (n->name, "Bcc")) {
			header->next = n->next;
			tail->next = n;
			n->next = NULL;
			tail = n;
		} else {
			header = n;
		}

		n = header->next;
	}

	/* write the message */
	ret = camel_lite_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), CAMEL_STREAM (filtered_stream));

	/* restore the bcc headers */
	header->next = savedbcc;

	if (ret == -1) {
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("DATA command failed: %s: mail not sent"),
				      g_strerror (errno));

		camel_lite_object_unref (filtered_stream);

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}

	camel_lite_stream_flush (CAMEL_STREAM (filtered_stream));
	camel_lite_object_unref (filtered_stream);

	/* terminate the message body */

	smtp_debug ("-> \\r\\n.\\r\\n\n");

	if (camel_lite_stream_write (transport->ostream, "\r\n.\r\n", 5) == -1) {
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("DATA command failed: %s: mail not sent"),
				      g_strerror (errno));

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}

	mtry = 0;
	do {
		/* Check for "250 Sender OK..." */
		if (respbuf)
			g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

		if (respbuf && strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, TRUE, respbuf, _("DATA command failed"), ex);
			g_free (respbuf);
			return FALSE;
		}
		mtry++;
	} while ((!respbuf && mtry < 3) || (respbuf && *(respbuf+3) == '-')); /* if we got "250-" then loop again */

	if (mtry == 3)
		smtp_set_exception (transport, TRUE, respbuf, _("DATA command failed"), ex);

	if (respbuf)
		g_free (respbuf);

	return (mtry != 3);
}

static gboolean
smtp_rset (CamelSmtpTransport *transport, CamelException *ex)
{
	/* we are going to reset the smtp server (just to be nice) */
	char *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup ("RSET\r\n");

	smtp_debug ("-> %s", cmdbuf);

	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("RSET command failed: %s"), g_strerror (errno));

		camel_lite_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

		smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, TRUE, respbuf, _("RSET command failed"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_quit (CamelSmtpTransport *transport, CamelException *ex)
{
	/* we are going to reset the smtp server (just to be nice) */
	char *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup ("QUIT\r\n");

	smtp_debug ("-> %s", cmdbuf);

	if (camel_lite_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_lite_exception_setv (ex, errno == EINTR ? CAMEL_EXCEPTION_USER_CANCEL : CAMEL_EXCEPTION_SYSTEM,
				      _("QUIT command failed: %s"), g_strerror (errno));

		return FALSE;
	}
	g_free (cmdbuf);

	if (FALSE && transport->istream && CAMEL_STREAM_BUFFER (transport->istream))
	{
		do {
			/* Check for "221" */
			if (respbuf)
				g_free (respbuf);
			respbuf = camel_lite_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));

			smtp_debug ("<- %s\n", respbuf ? respbuf : "(null)");

			if (!respbuf || strncmp (respbuf, "221", 3)) {
				smtp_set_exception (transport, FALSE, respbuf, _("QUIT command failed"), ex);
				g_free (respbuf);
				return FALSE;
			}
		} while (*(respbuf+3) == '-'); /* if we got "221-" then loop again */
	}

	if (respbuf)
		g_free (respbuf);

	return TRUE;
}
