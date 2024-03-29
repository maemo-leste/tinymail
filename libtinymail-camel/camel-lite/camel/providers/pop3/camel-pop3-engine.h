/*
 * Copyright (C) 2001 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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
 */

#ifndef _CAMEL_POP3_ENGINE_H
#define _CAMEL_POP3_ENGINE_H

#include <camel/camel-object.h>
#include <libedataserver/e-lite-msgport.h>
#include "camel-pop3-stream.h"

#define CAMEL_POP3_ENGINE(obj)         CAMEL_CHECK_CAST (obj, camel_lite_pop3_engine_get_type (), CamelPOP3Engine)
#define CAMEL_POP3_ENGINE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_pop3_engine_get_type (), CamelPOP3EngineClass)
#define CAMEL_IS_POP3_ENGINE(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_pop3_engine_get_type ())

G_BEGIN_DECLS

typedef struct _CamelPOP3EngineClass CamelPOP3EngineClass;
typedef struct _CamelPOP3Engine CamelPOP3Engine;
typedef struct _CamelPOP3Command CamelPOP3Command;

/* pop 3 connection states, actually since we're given a connected socket, we always start in auth state */
typedef enum {
	CAMEL_POP3_ENGINE_DISCONNECT = 0,
	CAMEL_POP3_ENGINE_AUTH,
	CAMEL_POP3_ENGINE_TRANSACTION,
	CAMEL_POP3_ENGINE_UPDATE
} camel_lite_pop3_engine_t;

/* state of a command */
typedef enum {
	CAMEL_POP3_COMMAND_IDLE = 0, /* command created or queued, not yet sent (e.g. non pipelined server) */
	CAMEL_POP3_COMMAND_DISPATCHED, /* command sent to server */

	/* completion codes */
	CAMEL_POP3_COMMAND_OK,	/* plain ok response */
	CAMEL_POP3_COMMAND_DATA, /* processing command response */
	CAMEL_POP3_COMMAND_ERR	/* error response */
} camel_lite_pop3_command_t;

/* flags for command types */
enum {
	CAMEL_POP3_COMMAND_SIMPLE = 0, /* dont expect multiline response */
	CAMEL_POP3_COMMAND_MULTI = 1 /* expect multiline response */
};

/* flags for server options */
enum {
	CAMEL_POP3_CAP_APOP = 1<<0,
	CAMEL_POP3_CAP_UIDL = 1<<1,
	CAMEL_POP3_CAP_SASL = 1<<2,
	CAMEL_POP3_CAP_TOP  = 1<<3,
	CAMEL_POP3_CAP_PIPE = 1<<4,
	CAMEL_POP3_CAP_STLS = 1<<5,
	CAMEL_POP3_CAP_LOGIN_DELAY = 1<<6
};

/* enable/disable flags for the engine itself */
enum {
	CAMEL_POP3_ENGINE_DISABLE_EXTENSIONS = 1<<0
};

typedef int (*CamelPOP3CommandFunc)(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data);

struct _CamelPOP3Command {
	struct _CamelPOP3Command *next;
	struct _CamelPOP3Command *prev;

	guint32 flags;
	camel_lite_pop3_command_t state;

	CamelPOP3CommandFunc func;
	void *func_data;

	int data_size;
	char *data;
};

struct _CamelPOP3Engine {
	CamelObject parent;

	guint32 flags;

	camel_lite_pop3_engine_t state;

	GList *auth;		/* authtypes supported */

	guint32 capa;		/* capabilities */
	char *apop;		/* apop time string */

	unsigned char *line;	/* current line buffer */
	unsigned int linelen;

	struct _CamelPOP3Stream *stream;

	unsigned int sentlen;	/* data sent (so we dont overflow network buffer) */

	EDList active;		/* active commands */
	EDList queue;		/* queue of waiting commands */
	EDList done;		/* list of done commands, awaiting free */

	CamelPOP3Command *current; /* currently busy (downloading) response */
	void *store; gboolean partial_happening;
	gint type; gint param;
	guint login_delay;

	GStaticRecMutex *lock;
};

struct _CamelPOP3EngineClass {
	CamelObjectClass parent_class;
};

CamelType		  camel_lite_pop3_engine_get_type	(void);

CamelPOP3Engine  *camel_lite_pop3_engine_new		(CamelStream *source, guint32 flags);

void              camel_lite_pop3_engine_reget_capabilities (CamelPOP3Engine *engine);

void              camel_lite_pop3_engine_command_free(CamelPOP3Engine *pe, CamelPOP3Command *pc);

int 		  camel_lite_pop3_engine_iterate	(CamelPOP3Engine *pe, CamelPOP3Command *pc);

CamelPOP3Command *camel_lite_pop3_engine_command_new	(CamelPOP3Engine *pe, guint32 flags, CamelPOP3CommandFunc func, void *data, const char *fmt, ...);

G_END_DECLS

#endif /* ! _CAMEL_POP3_ENGINE_H */
