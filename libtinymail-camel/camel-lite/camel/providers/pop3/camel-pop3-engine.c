/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-pop3-engine.h"
#include "camel-pop3-stream.h"
#include "camel-sasl.h"
#include "camel-service.h"
#include "camel-string-utils.h"

/* max 'outstanding' bytes in output stream, so we can't deadlock waiting
   for the server to accept our data when pipelining */
#define CAMEL_POP3_SEND_LIMIT (1024)


extern int camel_lite_verbose_debug;
#define dd(x) (camel_lite_verbose_debug?(x):0)

static void get_capabilities(CamelPOP3Engine *pe);

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelStream */
#define CS_CLASS(so) CAMEL_POP3_ENGINE_CLASS(CAMEL_OBJECT_GET_CLASS(so))

static void
camel_lite_pop3_engine_class_init (CamelPOP3EngineClass *camel_lite_pop3_engine_class)
{
	parent_class = camel_lite_type_get_global_classfuncs( CAMEL_OBJECT_TYPE );
}

static void
camel_lite_pop3_engine_init(CamelPOP3Engine *pe, CamelPOP3EngineClass *peclass)
{
	pe->login_delay = 300;
	pe->lock = g_new0 (GStaticRecMutex, 1);
	g_static_rec_mutex_init (pe->lock);

	e_dlist_init(&pe->active);
	e_dlist_init(&pe->queue);
	e_dlist_init(&pe->done);
	pe->state = CAMEL_POP3_ENGINE_DISCONNECT;
}

static void
camel_lite_pop3_engine_finalise(CamelPOP3Engine *pe)
{
	/* FIXME: Also flush/free any outstanding requests, etc */

	g_static_rec_mutex_lock (pe->lock);

	if (pe->stream)
		camel_lite_object_unref(pe->stream);

	g_list_free(pe->auth);
	if (pe->apop)
		g_free(pe->apop);

	g_static_rec_mutex_unlock (pe->lock);

	/* g_static_rec_mutex_free (pe->lock); */
	g_free (pe->lock);
	pe->lock = NULL;

	return;
}

CamelType
camel_lite_pop3_engine_get_type (void)
{
	static CamelType camel_lite_pop3_engine_type = CAMEL_INVALID_TYPE;

	if (camel_lite_pop3_engine_type == CAMEL_INVALID_TYPE) {
		camel_lite_pop3_engine_type = camel_lite_type_register(camel_lite_object_get_type(),
							     "CamelLitePOP3Engine",
							     sizeof( CamelPOP3Engine ),
							     sizeof( CamelPOP3EngineClass ),
							     (CamelObjectClassInitFunc) camel_lite_pop3_engine_class_init,
							     NULL,
							     (CamelObjectInitFunc) camel_lite_pop3_engine_init,
							     (CamelObjectFinalizeFunc) camel_lite_pop3_engine_finalise );
	}

	return camel_lite_pop3_engine_type;
}

static int
read_greeting (CamelPOP3Engine *pe)
{
	extern CamelServiceAuthType camel_lite_pop3_password_authtype;
	extern CamelServiceAuthType camel_lite_pop3_apop_authtype;
	unsigned char *line, *apop, *apopend;
	unsigned int len;


	g_static_rec_mutex_lock (pe->lock);

	/* first, read the greeting */
	if (camel_lite_pop3_stream_line (pe->stream, &line, &len) == -1
	    || strncmp ((char *) line, "+OK", 3) != 0) {
		g_static_rec_mutex_unlock (pe->lock);
		return -1;
	}

	if ((apop = (unsigned char *) strchr ((char *) line + 3, '<'))
	    && (apopend = (unsigned char *) strchr ((char *) apop, '>'))) {
		apopend[1] = 0;
		pe->apop = g_strdup ((gchar *) apop);
		pe->capa = CAMEL_POP3_CAP_APOP;
		pe->auth = g_list_append (pe->auth, &camel_lite_pop3_apop_authtype);
	}

	pe->auth = g_list_prepend (pe->auth, &camel_lite_pop3_password_authtype);

	g_static_rec_mutex_unlock (pe->lock);

	return 0;
}

/**
 * camel_lite_pop3_engine_new:
 * @source: source stream
 * @flags: engine flags
 *
 * Returns a NULL stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Return value: the stream
 **/
CamelPOP3Engine *
camel_lite_pop3_engine_new(CamelStream *source, guint32 flags)
{
	CamelPOP3Engine *pe;

	pe = (CamelPOP3Engine *)camel_lite_object_new(camel_lite_pop3_engine_get_type ());

	g_static_rec_mutex_lock (pe->lock);

	pe->stream = (CamelPOP3Stream *)camel_lite_pop3_stream_new(source);
	pe->state = CAMEL_POP3_ENGINE_AUTH;
	pe->flags = flags;

	if (read_greeting (pe) == -1) {
		g_static_rec_mutex_unlock (pe->lock);
		camel_lite_object_unref (pe);
		return NULL;
	}

	get_capabilities (pe);
	g_static_rec_mutex_unlock (pe->lock);

	return pe;
}


/**
 * camel_lite_pop3_engine_reget_capabilities:
 * @engine: pop3 engine
 *
 * Regets server capabilities (needed after a STLS command is issued for example).
 **/
void
camel_lite_pop3_engine_reget_capabilities (CamelPOP3Engine *engine)
{
	g_return_if_fail (CAMEL_IS_POP3_ENGINE (engine));

	g_static_rec_mutex_lock (engine->lock);
	get_capabilities (engine);
	g_static_rec_mutex_unlock (engine->lock);
}


/* TODO: read implementation too?
   etc? */
static struct {
	char *cap;
	guint32 flag;
} capa[] = {
	{ "APOP" , CAMEL_POP3_CAP_APOP },
	{ "TOP" , CAMEL_POP3_CAP_TOP },
	{ "UIDL", CAMEL_POP3_CAP_UIDL },
	{ "PIPELINING", CAMEL_POP3_CAP_PIPE },
	{ "STLS", CAMEL_POP3_CAP_STLS },  /* STARTTLS */
	{ "LOGIN-DELAY", CAMEL_POP3_CAP_LOGIN_DELAY } 
};

static int
cmd_capa(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	unsigned char *line, *tok, *next;
	unsigned int len;
	int ret;
	int i;
	CamelServiceAuthType *auth;

	g_static_rec_mutex_lock (pe->lock);

	dd(printf("cmd_capa\n"));

	do {
		ret = camel_lite_pop3_stream_line(stream, &line, &len);
		if (ret >= 0) {
			if (strncmp((char *) line, "SASL ", 5) == 0) {
				tok = line+5;
				dd(printf("scanning tokens '%s'\n", tok));
				while (tok) {
					next = (unsigned char *) strchr((char *) tok, ' ');
					if (next)
						*next++ = 0;
					auth = camel_lite_sasl_authtype((const char *) tok);
					if (auth) {
						dd(printf("got auth type '%s'\n", tok));
						pe->auth = g_list_prepend(pe->auth, auth);
					} else {
						dd(printf("unsupported auth type '%s'\n", tok));
					}
					tok = next;
				}
			} if (camel_lite_strstrcase ((char *) line, "LOGIN-DELAY")) {
				char *delay;
				pe->capa |= CAMEL_POP3_CAP_LOGIN_DELAY;
				delay = strchr ((const char *) line, ' ');
				if (delay) {
					delay++;
					pe->login_delay = strtoul (delay, &delay, 10);

					/* printf ("SET DELAY: %s (%d) OVERW 5\n", delay, pe->login_delay);
					pe->login_delay = 5; */
				}
			} else {
				for (i=0;i<sizeof(capa)/sizeof(capa[0]);i++) {
					if (strcmp((char *) capa[i].cap, (char *) line) == 0)
						pe->capa |= capa[i].flag;
				}
			}
		}
	} while (ret>0);

	g_static_rec_mutex_unlock (pe->lock);

	return 1;
}

static void
get_capabilities(CamelPOP3Engine *pe)
{
	CamelPOP3Command *pc;

	g_static_rec_mutex_lock (pe->lock);

	if (!(pe->flags & CAMEL_POP3_ENGINE_DISABLE_EXTENSIONS)) {
		pc = camel_lite_pop3_engine_command_new(pe, CAMEL_POP3_COMMAND_MULTI, cmd_capa, NULL, "CAPA\r\n");
		while (camel_lite_pop3_engine_iterate(pe, pc) > 0)
			;
		camel_lite_pop3_engine_command_free(pe, pc);

		if (pe->state == CAMEL_POP3_ENGINE_TRANSACTION && !(pe->capa & CAMEL_POP3_CAP_UIDL)) {
			/* check for UIDL support manually */
			pc = camel_lite_pop3_engine_command_new (pe, CAMEL_POP3_COMMAND_SIMPLE, NULL, NULL, "UIDL 1\r\n");
			while (camel_lite_pop3_engine_iterate (pe, pc) > 0)
				;

			if (pc->state == CAMEL_POP3_COMMAND_OK)
				pe->capa |= CAMEL_POP3_CAP_UIDL;

			camel_lite_pop3_engine_command_free (pe, pc);
		}

		if (pe->capa == 0) {
			/* Ugly assumptions */
			pe->capa |= CAMEL_POP3_CAP_TOP;
		}
	}

	g_static_rec_mutex_unlock (pe->lock);

}

/* returns true if the command was sent, false if it was just queued */
static int
engine_command_queue(CamelPOP3Engine *pe, CamelPOP3Command *pc)
{

	if (!pe)
		return -1;

	g_static_rec_mutex_lock (pe->lock);

	if (((pe->capa & CAMEL_POP3_CAP_PIPE) == 0 || (pe->sentlen + strlen(pc->data)) > CAMEL_POP3_SEND_LIMIT)
	    && pe->current != NULL) {
		e_dlist_addtail(&pe->queue, (EDListNode *)pc);
		g_static_rec_mutex_unlock (pe->lock);
		return FALSE;
	}

	/* TNY TODO: Check online status here, else it will crash
		Correction, it will not crash but will emit a SIGPIPE */

	if (camel_lite_stream_write((CamelStream *)pe->stream, pc->data, strlen(pc->data)) == -1) {
		e_dlist_addtail(&pe->queue, (EDListNode *)pc);
		g_static_rec_mutex_unlock (pe->lock);
		return FALSE;
	}

	pe->sentlen += strlen(pc->data);

	pc->state = CAMEL_POP3_COMMAND_DISPATCHED;

	if (pe->current == NULL)
		pe->current = pc;
	else
		e_dlist_addtail(&pe->active, (EDListNode *)pc);

	g_static_rec_mutex_unlock (pe->lock);

	return TRUE;
}

/* returns -1 on error (sets errno), 0 when no work to do, or >0 if work remaining */
int
camel_lite_pop3_engine_iterate(CamelPOP3Engine *pe, CamelPOP3Command *pcwait)
{
	unsigned char *p;
	unsigned int len;
	CamelPOP3Command *pc, *pw, *pn;
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;
	gboolean have_err = FALSE;

	g_static_rec_mutex_lock (pe->lock);

	camel_lite_object_ref (pe);

	if (pcwait && pcwait->state >= CAMEL_POP3_COMMAND_OK) {
		camel_lite_object_unref (pe);

		g_static_rec_mutex_unlock (pe->lock);
		return 0;
	}

	pc = pe->current;
	if (pc == NULL) {
		camel_lite_object_unref (pe);
		g_static_rec_mutex_unlock (pe->lock);
		return 0;
	}

	if (camel_lite_pop3_stream_line(pe->stream, &pe->line, &pe->linelen) == -1)
		goto ioerror;

	p = pe->line;
	switch (p[0]) {
	case '+':
		dd(printf("Got + response\n"));
		if (pc->flags & CAMEL_POP3_COMMAND_MULTI) {
			pc->state = CAMEL_POP3_COMMAND_DATA;
			camel_lite_pop3_stream_set_mode(pe->stream, CAMEL_POP3_STREAM_DATA);

			if (pc->func)
				if (pc->func(pe, pe->stream, pc->func_data) != 1) {
					g_warning ("Error occured\n");
					have_err = TRUE;
			}

			/* Make sure we get all data before going back to command mode */
			if (!pe->partial_happening)
				while (camel_lite_pop3_stream_getd(pe->stream, &p, &len) > 0)
					;

			if (!pe->partial_happening)
				camel_lite_pop3_stream_set_mode(pe->stream, CAMEL_POP3_STREAM_LINE);

			pe->partial_happening = FALSE;
		} else {
			if (pc->func && pc->data && strstr (pc->data, "STAT")) /* Moeha ugly ! */
				pc->func(pe, (CamelPOP3Stream *) pe->line, pc->func_data);
			pc->state = CAMEL_POP3_COMMAND_OK;
		}
		break;
	case '-':
		pc->state = CAMEL_POP3_COMMAND_ERR;
		break;
	default:
		/* what do we do now?  f'knows! */
		g_warning("Bad server response: %s\n", p);
		pc->state = CAMEL_POP3_COMMAND_ERR;
		break;
	}

	if (pc)
		e_dlist_addtail(&pe->done, (EDListNode *)pc);
	else
		g_warning ("Unexpected, pc == NULL");

	if (pc && pc->data)
		pe->sentlen -= strlen(pc->data);
	else
		g_warning ("Unexpected, pc == NULL");

	/* Set next command */
	pe->current = (CamelPOP3Command *)e_dlist_remhead(&pe->active);

	/* check the queue for sending any we can now send also */
	pw = (CamelPOP3Command *)pe->queue.head;
	pn = pw->next;

	while (pn) {
		if (((pe->capa & CAMEL_POP3_CAP_PIPE) == 0 || (pe->sentlen + strlen(pw->data)) > CAMEL_POP3_SEND_LIMIT)
		    && pe->current != NULL)
			break;

		if (camel_lite_stream_write((CamelStream *)pe->stream, pw->data, strlen(pw->data)) == -1)
			goto ioerror;

		e_dlist_remove((EDListNode *)pw);

		pe->sentlen += strlen(pw->data);
		pw->state = CAMEL_POP3_COMMAND_DISPATCHED;

		if (pe->current == NULL)
			pe->current = pw;
		else
			e_dlist_addtail(&pe->active, (EDListNode *)pw);

		pw = pn;
		pn = pn->next;
	}

	if (pcwait && pcwait->state >= CAMEL_POP3_COMMAND_OK) {
		if (have_err)
			camel_lite_service_disconnect (CAMEL_SERVICE (pe->store), FALSE, &ex);

		camel_lite_object_unref (pe);
		g_static_rec_mutex_unlock (pe->lock);
		return 0;
	}

	if (have_err)
		camel_lite_service_disconnect (CAMEL_SERVICE (pe->store), FALSE, &ex);

	camel_lite_object_unref (pe);
	g_static_rec_mutex_unlock (pe->lock);

	return pe->current==NULL?0:1;

ioerror:
	have_err = TRUE;

	/* we assume all outstanding commands are gunna fail now */
	while ( (pw = (CamelPOP3Command*)e_dlist_remhead(&pe->active)) ) {
		pw->state = CAMEL_POP3_COMMAND_ERR;
		e_dlist_addtail(&pe->done, (EDListNode *)pw);
	}

	while ( (pw = (CamelPOP3Command*)e_dlist_remhead(&pe->queue)) ) {
		pw->state = CAMEL_POP3_COMMAND_ERR;
		e_dlist_addtail(&pe->done, (EDListNode *)pw);
	}

	if (pe->current) {
		pe->current->state = CAMEL_POP3_COMMAND_ERR;
		e_dlist_addtail(&pe->done, (EDListNode *)pe->current);
		pe->current = NULL;
	}

	if (pe->store && CAMEL_IS_SERVICE (pe->store))
		camel_lite_service_disconnect ((CamelService *) pe->store, FALSE, &ex);

	camel_lite_object_unref (pe);
	g_static_rec_mutex_unlock (pe->lock);

	return -1;
}

CamelPOP3Command *
camel_lite_pop3_engine_command_new(CamelPOP3Engine *pe, guint32 flags, CamelPOP3CommandFunc func, void *data, const char *fmt, ...)
{
	CamelPOP3Command *pc;
	va_list ap;

	pc = g_malloc0(sizeof(*pc));
	pc->func = func;
	pc->func_data = data;
	pc->flags = flags;

	va_start(ap, fmt);
	pc->data = g_strdup_vprintf(fmt, ap);
	va_end (ap);
	pc->state = CAMEL_POP3_COMMAND_IDLE;

	/* TODO: what about write errors? */
	engine_command_queue(pe, pc);

	return pc;
}

void
camel_lite_pop3_engine_command_free(CamelPOP3Engine *pe, CamelPOP3Command *pc)
{

	if (pe)
		g_static_rec_mutex_lock (pe->lock);
	if (pe && pe->current != pc)
		e_dlist_remove((EDListNode *)pc);
	if (pc->data)
		g_free(pc->data);
	pc->data = NULL;
	g_free(pc);
	if (pe)
		g_static_rec_mutex_unlock (pe->lock);
}
