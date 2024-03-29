/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.h : Abstract class for an email service */
/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *          Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_SERVICE_H
#define CAMEL_SERVICE_H 1

#include <camel/camel-object.h>
#include <camel/camel-url.h>
#include <camel/camel-provider.h>
#include <camel/camel-operation.h>

#define CAMEL_SERVICE_TYPE     (camel_lite_service_get_type ())
#define CAMEL_SERVICE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SERVICE_TYPE, CamelService))
#define CAMEL_SERVICE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SERVICE_TYPE, CamelServiceClass))
#define CAMEL_IS_SERVICE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SERVICE_TYPE))

G_BEGIN_DECLS

enum {
	CAMEL_SERVICE_ARG_FIRST  = CAMEL_ARG_FIRST + 100,
	CAMEL_SERVICE_ARG_USERNAME,
	CAMEL_SERVICE_ARG_AUTH,
	CAMEL_SERVICE_ARG_HOSTNAME,
	CAMEL_SERVICE_ARG_PORT,
	CAMEL_SERVICE_ARG_PATH
};

#define CAMEL_SERVICE_USERNAME     (CAMEL_SERVICE_ARG_USERNAME | CAMEL_ARG_STR)
#define CAMEL_SERVICE_AUTH         (CAMEL_SERVICE_ARG_AUTH | CAMEL_ARG_STR)
#define CAMEL_SERVICE_HOSTNAME     (CAMEL_SERVICE_ARG_HOSTNAME | CAMEL_ARG_STR)
#define CAMEL_SERVICE_PORT         (CAMEL_SERVICE_ARG_PORT | CAMEL_ARG_INT)
#define CAMEL_SERVICE_PATH         (CAMEL_SERVICE_ARG_PATH | CAMEL_ARG_STR)

typedef enum {
	CAMEL_SERVICE_DISCONNECTED,
	CAMEL_SERVICE_CONNECTING,
	CAMEL_SERVICE_CONNECTED,
	CAMEL_SERVICE_DISCONNECTING
} CamelServiceConnectionStatus;

typedef void (*con_op) (CamelService *service, gboolean suc, gpointer data);

struct _CamelService {
	CamelObject parent_object;
	struct _CamelServicePrivate *priv;

	CamelSession *session;
	CamelProvider *provider;
	CamelServiceConnectionStatus status;
	CamelOperation *connect_op;
	CamelURL *url;
	gpointer data;
	gboolean reconnecting;

	gboolean can_idle;

	con_op connecting;
	con_op disconnecting;
	con_op reconnecter;
	con_op reconnection;
};

typedef struct {
	CamelObjectClass parent_class;

	void      (*construct)         (CamelService *service,
					CamelSession *session,
					CamelProvider *provider,
					CamelURL *url,
					CamelException *ex);

	gboolean  (*connect)           (CamelService *service,
					CamelException *ex);
	gboolean  (*disconnect)        (CamelService *service,
					gboolean clean,
					CamelException *ex);
	void      (*cancel_connect)    (CamelService *service);

	GList *   (*query_auth_types)  (CamelService *service,
					CamelException *ex);

	char *    (*get_name)          (CamelService *service,
					gboolean brief);
	char *    (*get_path)          (CamelService *service);
	void      (*can_idle)          (CamelService *service, gboolean can_idle);

} CamelServiceClass;

/* query_auth_types returns a GList of these */
typedef struct {
	char *name;               /* user-friendly name */
	char *description;
	char *authproto;

	gboolean need_password;   /* needs a password to authenticate */
} CamelServiceAuthType;

/* public methods */
void                camel_lite_service_construct          (CamelService *service,
						      CamelSession *session,
						      CamelProvider *provider,
						      CamelURL *url,
						      CamelException *ex);
gboolean            camel_lite_service_connect            (CamelService *service,
						      CamelException *ex);
gboolean            camel_lite_service_disconnect         (CamelService *service,
						      gboolean clean,
						      CamelException *ex);
void                camel_lite_service_cancel_connect     (CamelService *service);
char *              camel_lite_service_get_url            (CamelService *service);
char *              camel_lite_service_get_name           (CamelService *service,
						      gboolean brief);
char *              camel_lite_service_get_path           (CamelService *service);
CamelSession *      camel_lite_service_get_session        (CamelService *service);
CamelProvider *     camel_lite_service_get_provider       (CamelService *service);
GList *             camel_lite_service_query_auth_types   (CamelService *service,
						      CamelException *ex);


gboolean            camel_lite_service_connect_r           (CamelService *service,
						      CamelException *ex);
gboolean            camel_lite_service_disconnect_r        (CamelService *service,
						      gboolean clean,
						      CamelException *ex);

void                camel_lite_service_can_idle            (CamelService *service,
						       gboolean can_idle);

/* Standard Camel function */
CamelType camel_lite_service_get_type (void);

G_END_DECLS

#endif /* CAMEL_SERVICE_H */
