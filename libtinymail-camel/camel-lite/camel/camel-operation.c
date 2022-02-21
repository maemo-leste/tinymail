/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <NotZed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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


#include "camel-operation.h"

#define d(x)

/* ********************************************************************** */

struct _status_stack {
	guint32 flags;
	char *msg;
	int sofar, oftotal;
	unsigned int stamp;		/* last stamp reported */
};


#ifndef CAMEL_OPERATION_CANCELLED
#define CAMEL_OPERATION_CANCELLED (1<<0)
#endif
#define CAMEL_OPERATION_TRANSIENT (1<<1)

/* Delay before a transient operation has any effect on the status */
#define CAMEL_OPERATION_TRANSIENT_DELAY (5)

static pthread_mutex_t operation_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&operation_lock)
#define UNLOCK() pthread_mutex_unlock(&operation_lock)


static unsigned int stamp (void);
static EDList operation_list = E_DLIST_INITIALISER(operation_list);
static pthread_key_t operation_key;
static pthread_once_t operation_once = PTHREAD_ONCE_INIT;

typedef struct _CamelOperationMsg {
	EMsg msg;
} CamelOperationMsg ;

static void
co_createspecific(void)
{
	pthread_key_create(&operation_key, NULL);
}

static CamelOperation *
co_getcc(void)
{
	pthread_once(&operation_once, co_createspecific);

	return (CamelOperation *)pthread_getspecific(operation_key);
}

/**
 * camel_lite_operation_new:
 * @status: Callback for receiving status messages.  This will always
 * be called with an internal lock held.
 * @status_data: User data.
 *
 * Create a new camel operation handle.  Camel operation handles can
 * be used in a multithreaded application (or a single operation
 * handle can be used in a non threaded appliation) to cancel running
 * operations and to obtain notification messages of the internal
 * status of messages.
 *
 * Return value: A new operation handle.
 **/
CamelOperation *
camel_lite_operation_new (CamelOperationStatusFunc status, void *status_data)
{
	CamelOperation *cc;

	cc = g_malloc0(sizeof(*cc));

	cc->flags = 0;
	cc->blocked = 0;
	cc->refcount = 1;
	cc->status = status;
	cc->status_data = status_data;
	cc->cancel_port = e_msgport_new();
	cc->cancel_fd = -1;

	LOCK();
	e_dlist_addtail(&operation_list, (EDListNode *)cc);
	UNLOCK();

	return cc;
}

/**
 * camel_lite_operation_mute:
 * @cc:
 *
 * mutes a camel operation permanently.  from this point on you will never
 * receive operation updates, even if more are sent.
 **/
void
camel_lite_operation_mute(CamelOperation *cc)
{
	LOCK();
	cc->status = NULL;
	cc->status_data = NULL;
	UNLOCK();
}

/**
 * camel_lite_operation_registered:
 *
 * Returns the registered operation, or %NULL if none registered.
 **/
CamelOperation *
camel_lite_operation_registered (void)
{
	CamelOperation *cc = co_getcc();

	if (cc)
		camel_lite_operation_ref(cc);

	return cc;
}

/**
 * camel_lite_operation_ref:
 * @cc: operation context
 *
 * Add a reference to the CamelOperation @cc.
 **/
void
camel_lite_operation_ref (CamelOperation *cc)
{
	g_assert(cc->refcount > 0);

	LOCK();
	cc->refcount++;
	UNLOCK();
}

/**
 * camel_lite_operation_unref:
 * @cc: operation context
 *
 * Unref and potentially free @cc.
 **/
void
camel_lite_operation_unref (CamelOperation *cc)
{
	GSList *n;

	g_assert(cc->refcount > 0);

	LOCK();
	if (cc->refcount == 1) {
		CamelOperationMsg *msg;

		e_dlist_remove((EDListNode *)cc);

		while ((msg = (CamelOperationMsg *)e_msgport_get(cc->cancel_port)))
			g_free(msg);

		e_msgport_destroy(cc->cancel_port);

		n = cc->status_stack;
		while (n) {
			g_warning("CamelLite operation status stack non empty: %s", (char *)n->data);
			g_free(n->data);
			n = n->next;
		}
		g_slist_free(cc->status_stack);

		g_free(cc);
	} else {
		cc->refcount--;
	}
	UNLOCK();
}

/**
 * camel_lite_operation_cancel_block:
 * @cc: operation context
 *
 * Block cancellation for this operation.  If @cc is NULL, then the
 * current thread is blocked.
 **/
void
camel_lite_operation_cancel_block (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc) {
		LOCK();
		cc->blocked++;
		UNLOCK();
	}
}

/**
 * camel_lite_operation_cancel_unblock:
 * @cc: operation context
 *
 * Unblock cancellation, when the unblock count reaches the block
 * count, then this operation can be cancelled.  If @cc is NULL, then
 * the current thread is unblocked.
 **/
void
camel_lite_operation_cancel_unblock (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc) {
		LOCK();
		cc->blocked--;
		UNLOCK();
	}
}

/**
 * camel_lite_operation_cancel:
 * @cc: operation context
 *
 * Cancel a given operation.  If @cc is NULL then all outstanding
 * operations are cancelled.
 **/
void
camel_lite_operation_cancel (CamelOperation *cc)
{
	CamelOperationMsg *msg;

	LOCK();

	if (cc == NULL) {
		CamelOperation *cn;

		cc = (CamelOperation *)operation_list.head;
		cn = cc->next;
		while (cn) {
			cc->flags |= CAMEL_OPERATION_CANCELLED;
			msg = g_malloc0(sizeof(*msg));
			e_msgport_put(cc->cancel_port, (EMsg *)msg);
			cc = cn;
			cn = cn->next;
		}
	} else if ((cc->flags & CAMEL_OPERATION_CANCELLED) == 0) {
		d(printf("cancelling thread %d\n", cc->id));

		cc->flags |= CAMEL_OPERATION_CANCELLED;
		msg = g_malloc0(sizeof(*msg));
		e_msgport_put(cc->cancel_port, (EMsg *)msg);
	}

	UNLOCK();
}

/**
 * camel_lite_operation_uncancel:
 * @cc: operation context
 *
 * Uncancel a cancelled operation.  If @cc is NULL then the current
 * operation is uncancelled.
 *
 * This is useful, if e.g. you need to do some cleaning up where a
 * cancellation lying around in the same thread will abort any
 * processing.
 **/
void
camel_lite_operation_uncancel(CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc) {
		CamelOperationMsg *msg;

		LOCK();
		while ((msg = (CamelOperationMsg *)e_msgport_get(cc->cancel_port)))
			g_free(msg);

		cc->flags &= ~CAMEL_OPERATION_CANCELLED;
		UNLOCK();
	}
}

/**
 * camel_lite_operation_register:
 * @cc: operation context
 *
 * Register a thread or the main thread for cancellation through @cc.
 * If @cc is NULL, then a new cancellation is created for this thread.
 *
 * All calls to operation_register() should save their value and call
 * operation_register again with that, to automatically stack
 * registrations.
 *
 * Return Value: Returns the previously registered operatoin.
 *
 **/
CamelOperation *
camel_lite_operation_register (CamelOperation *cc)
{
	CamelOperation *oldcc = co_getcc();

	pthread_setspecific(operation_key, cc);

	return oldcc;
}

/**
 * camel_lite_operation_unregister:
 * @cc: operation context
 *
 * Unregister the current thread for all cancellations.
 **/
void
camel_lite_operation_unregister (CamelOperation *cc)
{
	pthread_once(&operation_once, co_createspecific);
	pthread_setspecific(operation_key, NULL);
}

/**
 * camel_lite_operation_cancel_check:
 * @cc: operation context
 *
 * Check if cancellation has been applied to @cc.  If @cc is NULL,
 * then the CamelOperation registered for the current thread is used.
 *
 * Return value: TRUE if the operation has been cancelled.
 **/
gboolean
camel_lite_operation_cancel_check (CamelOperation *cc)
{
	CamelOperationMsg *msg;
	int cancelled;

	d(printf("checking for cancel in thread %d\n", pthread_self()));

	if (cc == NULL)
		cc = co_getcc();

	LOCK();

	if (cc == NULL || cc->blocked > 0) {
		d(printf("ahah!  cancellation is blocked\n"));
		cancelled = FALSE;
	} else if (cc->flags & CAMEL_OPERATION_CANCELLED) {
		d(printf("previously cancelled\n"));
		cancelled = TRUE;
	} else if ((msg = (CamelOperationMsg *)e_msgport_get(cc->cancel_port))) {
		d(printf("Got cancellation message\n"));
		do {
			g_free(msg);
		} while ((msg = (CamelOperationMsg *)e_msgport_get(cc->cancel_port)));
		cc->flags |= CAMEL_OPERATION_CANCELLED;
		cancelled = TRUE;
	} else
		cancelled = FALSE;

	UNLOCK();

	return cancelled;
}

/**
 * camel_lite_operation_cancel_fd:
 * @cc: operation context
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Return value: The fd, or -1 if cancellation is not available
 * (blocked, or has not been registered for this thread).
 **/
int
camel_lite_operation_cancel_fd (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->blocked)
		return -1;

	LOCK();

	if (cc->cancel_fd == -1)
		cc->cancel_fd = e_msgport_fd(cc->cancel_port);

	UNLOCK();

	return cc->cancel_fd;
}

#ifdef HAVE_NSS
/**
 * camel_lite_operation_cancel_prfd:
 * @cc: operation context
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Return value: The fd, or NULL if cancellation is not available
 * (blocked, or has not been registered for this thread).
 **/
PRFileDesc *
camel_lite_operation_cancel_prfd (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->blocked)
		return NULL;

	LOCK();

	if (cc->cancel_prfd == NULL)
		cc->cancel_prfd = e_msgport_prfd(cc->cancel_port);

	UNLOCK();

	return cc->cancel_prfd;
}
#endif /* HAVE_NSS */

/**
 * camel_lite_operation_start:
 * @cc: operation context
 * @what: action being performed (printf-style format string)
 * @Varargs: varargs
 *
 * Report the start of an operation.  All start operations should have
 * similar end operations.
 **/
void
camel_lite_operation_start (CamelOperation *cc, char *what, ...)
{
	va_list ap;
	char *msg;
	struct _status_stack *s;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL) {
		UNLOCK();
		return;
	}

	va_start(ap, what);
	msg = g_strdup_vprintf(what, ap);
	va_end(ap);
	cc->status_update = 0;
	s = g_malloc0(sizeof(*s));
	s->msg = msg;
	s->flags = 0;
	s->sofar = 0;
	s->oftotal = 100;
	cc->lastreport = s;
	cc->status_stack = g_slist_prepend(cc->status_stack, s);

	UNLOCK();

	cc->status(cc, msg, CAMEL_OPERATION_START, 100, cc->status_data);

	d(printf("start '%s'\n", msg, pc));
}

/**
 * camel_lite_operation_start_transient:
 * @cc: operation context
 * @what: printf-style format string describing the action being performed
 * @Varargs: varargs
 *
 * Start a transient event.  We only update this to the display if it
 * takes very long to process, and if we do, we then go back to the
 * previous state when finished.
 **/
void
camel_lite_operation_start_transient (CamelOperation *cc, char *what, ...)
{
	va_list ap;
	char *msg;
	struct _status_stack *s;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->status == NULL)
		return;

	LOCK();

	va_start(ap, what);
	msg = g_strdup_vprintf(what, ap);
	va_end(ap);
	cc->status_update = 0;
	s = g_malloc0(sizeof(*s));
	s->msg = msg;
	s->flags = CAMEL_OPERATION_TRANSIENT;
	s->stamp = stamp();
	cc->status_stack = g_slist_prepend(cc->status_stack, s);
	d(printf("start '%s'\n", msg, pc));

	UNLOCK();

	/* we dont report it yet */
	/*cc->status(cc, msg, CAMEL_OPERATION_START, cc->status_data);*/
}

static unsigned int stamp(void)
{
	GTimeVal tv;

	g_get_current_time(&tv);
	/* update 4 times/second */
	return (tv.tv_sec * 4) + tv.tv_usec / (1000000/4);
}

/**
 * camel_lite_operation_progress:
 * @cc: Operation to report to.
 * @sofar: Amount complete
 * @oftotal: Max amount (or, amount when completed)
 *
 * Report progress on the current operation.  If @cc is NULL, then the
 * currently registered operation is used.  @
 *
 * If the total percentage is not know, then use
 * camel_lite_operation_progress_count().
 **/
void
camel_lite_operation_progress (CamelOperation *cc, int sofar, int oftotal)
{
	unsigned int now;
	struct _status_stack *s;
	char *msg = NULL;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL || cc->status_stack == NULL) {
		UNLOCK();
		return;
	}

	s = cc->status_stack->data;
	s->sofar = sofar;
	s->oftotal = oftotal;

	/* Transient messages dont start updating till 4 seconds after
	   they started, then they update every second */
	now = stamp();
	if (cc->status_update == now) {
		cc = NULL;
	} else if (s->flags & CAMEL_OPERATION_TRANSIENT) {
		if (s->stamp + CAMEL_OPERATION_TRANSIENT_DELAY > now) {
			cc = NULL;
		} else {
			cc->status_update = now;
			cc->lastreport = s;
			msg = g_strdup(s->msg);
		}
	} else {
		s->stamp = cc->status_update = now;
		cc->lastreport = s;
		msg = g_strdup(s->msg);
	}

	UNLOCK();

	if (cc) {
		cc->status(cc, msg, sofar, oftotal, cc->status_data);
		g_free(msg);
	}
}

/**
 * camel_lite_operation_progress_count:
 * @cc: operation context
 * @sofar:
 *
 **/
void
camel_lite_operation_progress_count (CamelOperation *cc, int sofar)
{
	camel_lite_operation_progress(cc, sofar, 100);
}

/**
 * camel_lite_operation_end:
 * @cc: operation context
 * @what: Format string.
 * @Varargs: varargs
 *
 * Report the end of an operation.  If @cc is NULL, then the currently
 * registered operation is notified.
 **/
void
camel_lite_operation_end (CamelOperation *cc)
{
	struct _status_stack *s, *p;
	unsigned int now;
	char *msg = NULL;
	int sofar = 0;
	int oftotal = 100;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL || cc->status_stack == NULL) {
		UNLOCK();
		return;
	}

	/* so what we do here is this.  If the operation that just
	 * ended was transient, see if we have any other transient
	 * messages that haven't been updated yet above us, otherwise,
	 * re-update as a non-transient at the last reported pc */
	now = stamp();
	s = cc->status_stack->data;
	if (s->flags & CAMEL_OPERATION_TRANSIENT) {
		if (cc->lastreport == s) {
			GSList *l = cc->status_stack->next;
			while (l) {
				p = l->data;
				if (p->flags & CAMEL_OPERATION_TRANSIENT) {
					if (p->stamp + CAMEL_OPERATION_TRANSIENT_DELAY < now) {
						msg = g_strdup(p->msg);
						sofar = p->sofar;
						oftotal = p->oftotal;
						cc->lastreport = p;
						break;
					}
				} else {
					msg = g_strdup(p->msg);
					sofar = p->sofar;
					oftotal = p->oftotal;
					cc->lastreport = p;
					break;
				}
				l = l->next;
			}
		}
		g_free(s->msg);
	} else {
		msg = s->msg;
		sofar = CAMEL_OPERATION_END;
		oftotal = 100;
		cc->lastreport = s;
	}
	g_free(s);

	cc->status_stack = g_slist_delete_link(cc->status_stack, cc->status_stack);

	UNLOCK();

	if (msg) {
		cc->status(cc, msg, sofar, oftotal, cc->status_data);
		g_free(msg);
	}
}
