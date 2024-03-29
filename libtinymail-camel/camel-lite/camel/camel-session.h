/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.h : Abstract class for an email session */

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


#ifndef CAMEL_SESSION_H
#define CAMEL_SESSION_H 1

#include <camel/camel-object.h>
#include <camel/camel-provider.h>
#include <camel/camel-junk-plugin.h>

#include <libedataserver/e-lite-msgport.h>

#define CAMEL_SESSION_TYPE     (camel_lite_session_get_type ())
#define CAMEL_SESSION(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SESSION_TYPE, CamelSession))
#define CAMEL_SESSION_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SESSION_TYPE, CamelSessionClass))
#define CAMEL_IS_SESSION(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SESSION_TYPE))

G_BEGIN_DECLS

typedef gboolean (*CamelTimeoutCallback) (gpointer data);
typedef enum {
	CAMEL_SESSION_ALERT_INFO,
	CAMEL_SESSION_ALERT_WARNING,
	CAMEL_SESSION_ALERT_ERROR
} CamelSessionAlertType;

enum {
	CAMEL_SESSION_PASSWORD_REPROMPT = 1 << 0,
	CAMEL_SESSION_PASSWORD_SECRET = 1 << 2,
	CAMEL_SESSION_PASSWORD_STATIC = 1 << 3,
	CAMEL_SESSION_PASSPHRASE = 1 << 4
};

struct _CamelSession
{
	CamelObject parent_object;
	struct _CamelSessionPrivate *priv;

	char *storage_path;
	CamelJunkPlugin *junk_plugin;

	guint online:1;
	guint check_junk:1;
	guint network_state:1;
};

typedef struct _CamelSessionThreadOps CamelSessionThreadOps;
typedef struct _CamelSessionThreadMsg CamelSessionThreadMsg;

typedef struct {
	CamelObjectClass parent_class;

	CamelService *  (*get_service)       (CamelSession *session,
					      const char *url_string,
					      CamelProviderType type,
					      CamelException *ex);
	char *          (*get_storage_path)  (CamelSession *session,
					      CamelService *service,
					      CamelException *ex);

	char *          (*get_password)      (CamelSession *session,
					      CamelService *service,
					      const char *domain,
					      const char *prompt,
					      const char *item,
					      guint32 flags,
					      CamelException *ex);
	void            (*forget_password)   (CamelSession *session,
					      CamelService *service,
					      const char *domain,
					      const char *item,
					      CamelException *ex);
	gboolean        (*alert_user)        (CamelSession *session,
					      CamelSessionAlertType type,
					      CamelException *ex,
					      gboolean cancel,
					      CamelService *service);

	CamelFilterDriver * (*get_filter_driver) (CamelSession *session,
						  const char *type,
						  CamelException *ex);

	/* mechanism for creating and maintaining multiple threads of control */
	void *(*thread_msg_new)(CamelSession *session, CamelSessionThreadOps *ops, unsigned int size);
	void (*thread_msg_free)(CamelSession *session, CamelSessionThreadMsg *msg);
	int (*thread_queue)(CamelSession *session, CamelSessionThreadMsg *msg, int flags);
	void (*thread_wait)(CamelSession *session, int id);
	void (*thread_status)(CamelSession *session, CamelSessionThreadMsg *msg, const char *text, int pc);
	gboolean (*lookup_addressbook) (CamelSession *session, const char *name);
} CamelSessionClass;


/* public methods */

/* Standard Camel function */
CamelType camel_lite_session_get_type (void);


void            camel_lite_session_construct             (CamelSession *session,
						     const char *storage_path);

CamelService *  camel_lite_session_get_service           (CamelSession *session,
						     const char *url_string,
						     CamelProviderType type,
						     CamelException *ex);
CamelService *  camel_lite_session_get_service_connected (CamelSession *session,
						     const char *url_string,
						     CamelProviderType type,
						     CamelException *ex);

#define camel_lite_session_get_store(session, url_string, ex) \
	((CamelStore *) camel_lite_session_get_service_connected (session, url_string, CAMEL_PROVIDER_STORE, ex))
#define camel_lite_session_get_transport(session, url_string, ex) \
	((CamelTransport *) camel_lite_session_get_service_connected (session, url_string, CAMEL_PROVIDER_TRANSPORT, ex))

char *             camel_lite_session_get_storage_path   (CamelSession *session,
						     CamelService *service,
						     CamelException *ex);

char *             camel_lite_session_get_password       (CamelSession *session,
						     CamelService *service,
						     const char *domain,
						     const char *prompt,
						     const char *item,
						     guint32 flags,
						     CamelException *ex);
void               camel_lite_session_forget_password    (CamelSession *session,
						     CamelService *service,
						     const char *domain,
						     const char *item,
						     CamelException *ex);
gboolean           camel_lite_session_alert_user         (CamelSession *session,
						     CamelSessionAlertType type,
						     CamelException *ex,
						     gboolean cancel,
						     CamelService *service);
gboolean           camel_lite_session_alert_user_generic (CamelSession *session,
						     CamelSessionAlertType type,
						     const gchar* message,
						     gboolean cancel,
						     CamelService *service);
gboolean           camel_lite_session_alert_user_with_id (CamelSession *session,
						     CamelSessionAlertType type,
						     ExceptionId id,
						     const gchar* message,
						     gboolean cancel,
						     CamelService *service);

char *		   camel_lite_session_build_password_prompt
						    (const char *type,
						     const char *user,
						     const char *host);

gboolean           camel_lite_session_is_online          (CamelSession *session);
void               camel_lite_session_set_online         (CamelSession *session,
						     gboolean online);

CamelFilterDriver *camel_lite_session_get_filter_driver  (CamelSession *session,
						     const char *type,
						     CamelException *ex);

gboolean  camel_lite_session_check_junk               (CamelSession *session);
void      camel_lite_session_set_check_junk           (CamelSession *session,
						  gboolean      check_junk);

struct _CamelSessionThreadOps {
	void (*receive)(CamelSession *session, struct _CamelSessionThreadMsg *m);
	void (*free)(CamelSession *session, struct _CamelSessionThreadMsg *m);
};

struct _CamelSessionThreadMsg {
	EMsg msg;

	int id;

	CamelException ex;
	CamelSessionThreadOps *ops;
	struct _CamelOperation *op;
	CamelSession *session;

	void *data; /* free for implementation to define, not used by camel, do not use in client code */
	/* user fields follow */
};

void *camel_lite_session_thread_msg_new(CamelSession *session, CamelSessionThreadOps *ops, unsigned int size);
void camel_lite_session_thread_msg_free(CamelSession *session, CamelSessionThreadMsg *msg);
int camel_lite_session_thread_queue(CamelSession *session, CamelSessionThreadMsg *msg, int flags);
void camel_lite_session_thread_wait(CamelSession *session, int id);
gboolean camel_lite_session_get_network_state (CamelSession *session);
void camel_lite_session_set_network_state (CamelSession *session, gboolean network_state);
const GHashTable * camel_lite_session_get_junk_headers (CamelSession *session);
void camel_lite_session_set_junk_headers (CamelSession *session, const char **headers, const char **values, int len);
gboolean camel_lite_session_lookup_addressbook (CamelSession *session, const char *name);

G_END_DECLS

#endif /* CAMEL_SESSION_H */
