/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright 2005 Novell, Inc. (www.novell.com)
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

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-offline-folder.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

#define CAMEL_OFFLINE_FOLDER_GET_CLASS(f) (CAMEL_OFFLINE_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (f)))

static void camel_lite_offline_folder_class_init (CamelOfflineFolderClass *klass);
static void camel_lite_offline_folder_init (CamelOfflineFolder *folder, CamelOfflineFolderClass *klass);
static void camel_lite_offline_folder_finalize (CamelObject *object);

static int offline_folder_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);
static int offline_folder_setv (CamelObject *object, CamelException *ex, CamelArgV *args);

static void offline_folder_downsync (CamelOfflineFolder *offline, const char *expression, CamelException *ex);

static CamelFolderClass *parent_class = NULL;

static GSList *offline_folder_props = NULL;

static CamelProperty offline_prop_list[] = {
	{ CAMEL_OFFLINE_FOLDER_SYNC_OFFLINE, "sync_offline", N_("Copy folder content locally for offline operation") },
};


CamelType
camel_lite_offline_folder_get_type (void)
{
	static CamelType type = NULL;

	if (!type) {
		type = camel_lite_type_register (CAMEL_FOLDER_TYPE,
					    "CamelLiteOfflineFolder",
					    sizeof (CamelOfflineFolder),
					    sizeof (CamelOfflineFolderClass),
					    (CamelObjectClassInitFunc) camel_lite_offline_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_offline_folder_init,
					    (CamelObjectFinalizeFunc) camel_lite_offline_folder_finalize);
	}

	return type;
}


static void
camel_lite_offline_folder_class_init (CamelOfflineFolderClass *klass)
{
	int i;

	parent_class = (CamelFolderClass *) camel_lite_type_get_global_classfuncs (CAMEL_FOLDER_TYPE);

	if (offline_folder_props == NULL) {
		for (i = 0; i < G_N_ELEMENTS (offline_prop_list); i++) {
			offline_prop_list[i].description = _(offline_prop_list[i].description);
			offline_folder_props = g_slist_prepend (offline_folder_props, &offline_prop_list[i]);
		}
	}

	((CamelObjectClass *) klass)->getv = offline_folder_getv;
	((CamelObjectClass *) klass)->setv = offline_folder_setv;

	klass->downsync = offline_folder_downsync;
}


struct _offline_downsync_msg {
	CamelSessionThreadMsg msg;

	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
};

static void
offline_downsync_sync (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;
	CamelMimeMessage *message;
	int i;

	camel_lite_operation_start (NULL, _("Downloading new messages for offline mode"));

	if (m->changes) {
		for (i = 0; i < m->changes->uid_added->len; i++) {

			camel_lite_operation_progress (NULL, i, m->changes->uid_added->len);
			/* TNY: Partial message retrieval here is always body only */
			/* TODO: Maybe detect what the original message was, if there was a message? */
			if ((message = camel_lite_folder_get_message (m->folder, m->changes->uid_added->pdata[i], CAMEL_FOLDER_RECEIVE_PARTIAL, -1, &mm->ex)))
				camel_lite_object_unref (message);
		}
	} else {
		camel_lite_offline_folder_downsync ((CamelOfflineFolder *) m->folder, "(match-all)", &mm->ex);
	}

	camel_lite_operation_end (NULL);
}

static void
offline_downsync_free (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;

	if (m->changes)
		camel_lite_folder_change_info_free (m->changes);

	camel_lite_object_unref (m->folder);
}

static CamelSessionThreadOps offline_downsync_ops = {
	offline_downsync_sync,
	offline_downsync_free,
};

static void
offline_folder_changed (CamelFolder *folder, CamelFolderChangeInfo *changes, void *dummy)
{
	CamelOfflineFolder *offline = (CamelOfflineFolder *) folder;
	CamelService *service = (CamelService *) folder->parent_store;

	if (changes->uid_added->len > 0 && (offline->sync_offline || camel_lite_url_get_param (service->url, "sync_offline"))) {
		CamelSession *session = service->session;
		struct _offline_downsync_msg *m;

		m = camel_lite_session_thread_msg_new (session, &offline_downsync_ops, sizeof (*m));
		m->changes = camel_lite_folder_change_info_new ();
		camel_lite_folder_change_info_cat (m->changes, changes);
		camel_lite_object_ref (folder);
		m->folder = folder;

		camel_lite_session_thread_queue (session, &m->msg, 0);
	}
}

static void
camel_lite_offline_folder_init (CamelOfflineFolder *folder, CamelOfflineFolderClass *klass)
{
	camel_lite_object_hook_event (folder, "folder_changed", (CamelObjectEventHookFunc) offline_folder_changed, NULL);
}

static void
camel_lite_offline_folder_finalize (CamelObject *object)
{
	;
}

static int
offline_folder_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelArgGetV props;
	int i, count = 0;
	guint32 tag;

	for (i = 0; i < args->argc; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES:
		case CAMEL_FOLDER_ARG_PROPERTIES:
			props.argc = 1;
			props.argv[0] = *arg;
			((CamelObjectClass *) parent_class)->getv (object, ex, &props);
			*arg->ca_ptr = g_slist_concat (*arg->ca_ptr, g_slist_copy (offline_folder_props));
			break;
		case CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE:
			*arg->ca_int = ((CamelOfflineFolder *) object)->sync_offline;
			break;
		default:
			count++;
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *) parent_class)->getv (object, ex, args);

	return 0;
}

static int
offline_folder_setv (CamelObject *object, CamelException *ex, CamelArgV *args)
{
	CamelOfflineFolder *folder = (CamelOfflineFolder *) object;
	gboolean save = FALSE;
	guint32 tag;
	int i;

	for (i = 0; i < args->argc; i++) {
		CamelArg *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE:
			if (folder->sync_offline != arg->ca_int) {
				folder->sync_offline = arg->ca_int;
				save = TRUE;
			}
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	/* if (save)
		camel_lite_object_state_write (object); */

	return ((CamelObjectClass *) parent_class)->setv (object, ex, args);
}

static void
offline_folder_downsync (CamelOfflineFolder *offline, const char *expression, CamelException *ex)
{
	CamelFolder *folder = (CamelFolder *) offline;
	CamelMimeMessage *message;
	GPtrArray *uids;
	int i;

	camel_lite_operation_start (NULL, _("Syncing messages in folder '%s' to disk"), folder->full_name);

	if (expression)
		uids = camel_lite_folder_search_by_expression (folder, expression, ex);
	else
		uids = camel_lite_folder_get_uids (folder);

	if (!uids) {
		camel_lite_operation_end (NULL);
		return;
	}

	for (i = 0; i < uids->len; i++) {

		/* TNY: Partial message retrieval here is always body only */
		/* TODO: Maybe detect what the original message was, if there was a message? */
		message = camel_lite_folder_get_message (folder, uids->pdata[i], CAMEL_FOLDER_RECEIVE_PARTIAL, -1, ex);
		camel_lite_operation_progress (NULL, i, uids->len);
		if (message == NULL)
			break;

		camel_lite_object_unref (message);
	}

	if (expression)
		camel_lite_folder_search_free (folder, uids);
	else
		camel_lite_folder_free_uids (folder, uids);

	camel_lite_operation_end (NULL);
}


/**
 * camel_lite_offline_folder_downsync:
 * @offline: a #CamelOfflineFolder object
 * @expression: search expression describing which set of messages to downsync (%NULL for all)
 * @ex: a #CamelException
 *
 * Syncs messages in @offline described by the search @expression to
 * the local machine for offline availability.
 **/
void
camel_lite_offline_folder_downsync (CamelOfflineFolder *offline, const char *expression, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_OFFLINE_FOLDER (offline));

	CAMEL_OFFLINE_FOLDER_GET_CLASS (offline)->downsync (offline, expression, ex);
}
