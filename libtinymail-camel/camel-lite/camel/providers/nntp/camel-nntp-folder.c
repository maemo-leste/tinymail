/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-folder.c : Class for a news folder
 *
 * Authors : Chris Toshok <toshok@ximian.com>
 *           Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2001-2003 Ximian, Inc. (www.ximian.com)
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/e-lite-data-server-util.h>

#include "camel/camel-data-cache.h"
#include "camel/camel-data-wrapper.h"
#include "camel/camel-exception.h"
#include "camel/camel-file-utils.h"
#include "camel/camel-folder-search.h"
#include "camel/camel-mime-filter-crlf.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-mime-part.h"
#include "camel/camel-multipart.h"
#include "camel/camel-private.h"
#include "camel/camel-session.h"
#include "camel/camel-stream-buffer.h"
#include "camel/camel-stream-filter.h"
#include "camel/camel-stream-mem.h"

#include "camel-nntp-folder.h"
#include "camel-nntp-private.h"
#include "camel-nntp-store.h"
#include "camel-nntp-store.h"
#include "camel-nntp-summary.h"

static CamelFolderClass *folder_class = NULL;
static CamelDiscoFolderClass *parent_class = NULL;

/* Returns the class for a CamelNNTPFolder */
#define CNNTPF_CLASS(so) CAMEL_NNTP_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CNNTPS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

void
camel_lite_nntp_folder_selected(CamelNNTPFolder *folder, char *line, CamelException *ex)
{
	camel_lite_nntp_summary_check((CamelNNTPSummary *)((CamelFolder *)folder)->summary,
				 (CamelNNTPStore *)((CamelFolder *)folder)->parent_store,
				 line, folder->changes, ex);
}

static void
nntp_folder_refresh_info_online (CamelFolder *folder, CamelException *ex)
{
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes = NULL;
	CamelNNTPFolder *nntp_folder;
	char *line;

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	nntp_folder = (CamelNNTPFolder *) folder;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	camel_lite_nntp_command(nntp_store, ex, nntp_folder, &line, NULL);

	if (camel_lite_folder_change_info_changed(nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_lite_folder_change_info_new();
	}

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	if (changes) {
		camel_lite_object_trigger_event ((CamelObject *) folder, "folder_changed", changes);
		camel_lite_folder_change_info_free (changes);
	}
}

static void
nntp_folder_sync_online (CamelFolder *folder, CamelException *ex)
{
	CAMEL_SERVICE_REC_LOCK(folder->parent_store, connect_lock);
	camel_lite_folder_summary_save (folder->summary, ex);
	CAMEL_SERVICE_REC_UNLOCK(folder->parent_store, connect_lock);
}

static void
nntp_folder_sync_offline (CamelFolder *folder, CamelException *ex)
{
	CAMEL_SERVICE_REC_LOCK(folder->parent_store, connect_lock);
	camel_lite_folder_summary_save (folder->summary, ex);
	CAMEL_SERVICE_REC_UNLOCK(folder->parent_store, connect_lock);
}

static gboolean
nntp_folder_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
        return ((CamelFolderClass *) folder_class)->set_message_flags (folder, uid, flags, set);
}

static CamelStream *
nntp_folder_download_message (CamelNNTPFolder *nntp_folder, const char *id, const char *msgid, CamelException *ex)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *) ((CamelFolder *) nntp_folder)->parent_store;
	CamelStream *stream = NULL;
	int ret;
	char *line;

	ret = camel_lite_nntp_command (nntp_store, ex, nntp_folder, &line, "article %s", id);
	if (ret == 220) {
		stream = camel_lite_data_cache_add (nntp_store->cache, "cache", msgid, NULL);
		if (stream) {
			if (camel_lite_stream_write_to_stream ((CamelStream *) nntp_store->stream, stream) == -1)
				goto fail;
			if (camel_lite_stream_reset (stream) == -1)
				goto fail;
		} else {
			stream = (CamelStream *) nntp_store->stream;
			camel_lite_object_ref (stream);
		}
	} else if (ret == 423 || ret == 430) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID, _("Cannot get message %s: %s"), msgid, line);
	} else if (ret != -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"), msgid, line);
	}

	return stream;

 fail:
	if (errno == EINTR)
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User canceled"));
	else
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"), msgid, g_strerror (errno));

	return NULL;
}


static void
nntp_folder_cache_message (CamelDiscoFolder *disco_folder, const char *uid, CamelException *ex)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *)((CamelFolder *) disco_folder)->parent_store;
	CamelStream *stream;
	char *article, *msgid;

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr(article, ',');
	if (!msgid) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Internal error: UID in invalid format: %s"), uid);
		return;
	}
	*msgid++ = 0;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	stream = nntp_folder_download_message ((CamelNNTPFolder *) disco_folder, article, msgid, ex);
	if (stream)
		camel_lite_object_unref (stream);

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);
}

static CamelMimeMessage *
nntp_folder_get_message (CamelFolder *folder, const char *uid, CamelFolderReceiveType type, gint param, CamelException *ex)
{
	CamelMimeMessage *message = NULL;
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes;
	CamelNNTPFolder *nntp_folder;
	CamelStream *stream = NULL;
	char *article, *msgid;

	/* TNY TODO: Implement partial message retrieval if full==TRUE */

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	nntp_folder = (CamelNNTPFolder *) folder;

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr (article, ',');
	if (msgid == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Internal error: UID in invalid format: %s"), uid);
		return NULL;
	}
	*msgid++ = 0;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	/* Lookup in cache, NEWS is global messageid's so use a global cache path */
	stream = camel_lite_data_cache_get (nntp_store->cache, "cache", msgid, NULL);
	if (stream == NULL) {
		if (camel_lite_disco_store_status ((CamelDiscoStore *) nntp_store) == CAMEL_DISCO_STORE_OFFLINE) {
			camel_lite_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					     _("This message is not currently available"));
			goto fail;
		}

		stream = nntp_folder_download_message (nntp_folder, article, msgid, ex);
		if (stream == NULL)
			goto fail;
	}

	message = camel_lite_mime_message_new ();
	if (camel_lite_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		if (errno == EINTR)
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User canceled"));
		else
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"), uid, g_strerror (errno));
		camel_lite_object_unref(message);
		message = NULL;
	}

	camel_lite_object_unref (stream);
fail:
	if (camel_lite_folder_change_info_changed (nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_lite_folder_change_info_new ();
	} else {
		changes = NULL;
	}

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	if (changes) {
		camel_lite_object_trigger_event ((CamelObject *) folder, "folder_changed", changes);
		camel_lite_folder_change_info_free (changes);
	}

	return message;
}

static GPtrArray*
nntp_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);
	GPtrArray *matches;

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_lite_folder_search_new ();

	camel_lite_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_lite_folder_search_search(nntp_folder->search, expression, NULL, ex);

	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);

	return matches;
}

static GPtrArray *
nntp_folder_search_by_uids (CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	CamelNNTPFolder *nntp_folder = (CamelNNTPFolder *) folder;
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_NNTP_FOLDER_LOCK(folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_lite_folder_search_new ();

	camel_lite_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_lite_folder_search_search(nntp_folder->search, expression, uids, ex);

	CAMEL_NNTP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}

static void
nntp_folder_search_free (CamelFolder *folder, GPtrArray *result)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);

	camel_lite_folder_search_free_result (nntp_folder->search, result);
}

static void
nntp_folder_append_message_online (CamelFolder *folder, CamelMimeMessage *mime_message,
				   const CamelMessageInfo *info, char **appended_uid,
				   CamelException *ex)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *) folder->parent_store;
	CamelStream *stream = (CamelStream*)nntp_store->stream;
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *crlffilter;
	int ret;
	unsigned int u;
	struct _camel_lite_header_raw *header, *savedhdrs, *n, *tail;
	char *group, *line;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	/* send 'POST' command */
	ret = camel_lite_nntp_command (nntp_store, ex, NULL, &line, "post");
	if (ret != 340) {
		if (ret == 440)
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					      _("Posting failed: %s"), line);
		else if (ret != -1)
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Posting failed: %s"), line);
		CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);
		return;
	}

	/* the 'Newsgroups: ' header */
	group = g_strdup_printf ("Newsgroups: %s\r\n", folder->full_name);

	/* setup stream filtering */
	crlffilter = camel_lite_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filtered_stream = camel_lite_stream_filter_new_with_stream (stream);
	camel_lite_stream_filter_add (filtered_stream, crlffilter);
	camel_lite_object_unref (crlffilter);

	/* remove mail 'To', 'CC', and 'BCC' headers */
	savedhdrs = NULL;
	tail = (struct _camel_lite_header_raw *) &savedhdrs;

	header = (struct _camel_lite_header_raw *) &CAMEL_MIME_PART (mime_message)->headers;
	n = header->next;
	while (n != NULL) {
		if (!g_ascii_strcasecmp (n->name, "To") || !g_ascii_strcasecmp (n->name, "Cc") || !g_ascii_strcasecmp (n->name, "Bcc")) {
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
	if (camel_lite_stream_write(stream, group, strlen(group)) == -1
	    || camel_lite_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (mime_message), CAMEL_STREAM (filtered_stream)) == -1
	    || camel_lite_stream_flush (CAMEL_STREAM (filtered_stream)) == -1
	    || camel_lite_stream_write (stream, "\r\n.\r\n", 5) == -1
	    || (ret = camel_lite_nntp_stream_line (nntp_store->stream, (unsigned char **)&line, &u)) == -1) {
		if (errno == EINTR)
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User canceled"));
		else
			camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Posting failed: %s"), g_strerror (errno));
	} else if (atoi(line) != 240) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Posting failed: %s"), line);
	}

	camel_lite_object_unref (filtered_stream);
	g_free(group);
	header->next = savedhdrs;

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	return;
}

static void
nntp_folder_append_message_offline (CamelFolder *folder, CamelMimeMessage *mime_message,
				    const CamelMessageInfo *info, char **appended_uid,
				    CamelException *ex)
{
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
	                      _("You cannot post NNTP messages while working offline!"));
}

/* I do not know what to do this exactly. Looking at the IMAP implementation for this, it
   seems to assume the message is copied to a folder on the same store. In that case, an
   NNTP implementation doesn't seem to make any sense. */
static void
nntp_folder_transfer_message (CamelFolder *source, GPtrArray *uids, CamelFolder *dest,
			      GPtrArray **transferred_uids, gboolean delete_orig, CamelException *ex)
{
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
	                      _("You cannot copy messages from a NNTP folder!"));
}

static gboolean
nntp_folder_get_allow_external_images (CamelFolder *folder, const char *uid)
{
	gboolean retval;
	CamelNNTPStore *nntp_store;

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	retval = camel_lite_data_cache_get_allow_external_images (nntp_store->cache, "cache", uid);
	
	return retval;
}

static void
nntp_folder_set_allow_external_images (CamelFolder *folder, const char *uid, gboolean allow)
{
	CamelNNTPStore *nntp_store;

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	camel_lite_data_cache_set_allow_external_images (nntp_store->cache, "cache", uid, allow);
}

static void
nntp_folder_init (CamelNNTPFolder *nntp_folder, CamelNNTPFolderClass *klass)
{
	struct _CamelNNTPFolderPrivate *p;

	nntp_folder->changes = camel_lite_folder_change_info_new ();
	p = nntp_folder->priv = g_malloc0 (sizeof (*nntp_folder->priv));
	p->search_lock = g_mutex_new ();
	p->cache_lock = g_mutex_new ();
}

static void
nntp_folder_finalise (CamelNNTPFolder *nntp_folder)
{
	struct _CamelNNTPFolderPrivate *p;
	CamelException nex  = CAMEL_EXCEPTION_INITIALISER;

	camel_lite_folder_summary_save (((CamelFolder*) nntp_folder)->summary, &nex);

	p = nntp_folder->priv;
	g_mutex_free (p->search_lock);
	g_mutex_free (p->cache_lock);
	g_free (p);
}

static void
nntp_folder_class_init (CamelNNTPFolderClass *camel_lite_nntp_folder_class)
{
	CamelDiscoFolderClass *camel_lite_disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (camel_lite_nntp_folder_class);
	CamelFolderClass *camel_lite_folder_class = CAMEL_FOLDER_CLASS (camel_lite_nntp_folder_class);

	parent_class = CAMEL_DISCO_FOLDER_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_disco_folder_get_type ()));
	folder_class = CAMEL_FOLDER_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_folder_get_type ()));

	/* virtual method definition */

	/* virtual method overload */
	camel_lite_disco_folder_class->sync_online = nntp_folder_sync_online;
	camel_lite_disco_folder_class->sync_resyncing = nntp_folder_sync_offline;
	camel_lite_disco_folder_class->sync_offline = nntp_folder_sync_offline;
	camel_lite_disco_folder_class->cache_message = nntp_folder_cache_message;
	camel_lite_disco_folder_class->append_online = nntp_folder_append_message_online;
	camel_lite_disco_folder_class->append_resyncing = nntp_folder_append_message_online;
	camel_lite_disco_folder_class->append_offline = nntp_folder_append_message_offline;
	camel_lite_disco_folder_class->transfer_online = nntp_folder_transfer_message;
	camel_lite_disco_folder_class->transfer_resyncing = nntp_folder_transfer_message;
	camel_lite_disco_folder_class->transfer_offline = nntp_folder_transfer_message;
	camel_lite_disco_folder_class->refresh_info_online = nntp_folder_refresh_info_online;

	camel_lite_folder_class->set_message_flags = nntp_folder_set_message_flags;
	camel_lite_folder_class->get_message = nntp_folder_get_message;
	camel_lite_folder_class->search_by_expression = nntp_folder_search_by_expression;
	camel_lite_folder_class->search_by_uids = nntp_folder_search_by_uids;
	camel_lite_folder_class->search_free = nntp_folder_search_free;
	camel_lite_folder_class->get_allow_external_images = nntp_folder_get_allow_external_images;
	camel_lite_folder_class->set_allow_external_images = nntp_folder_set_allow_external_images;
}

CamelType
camel_lite_nntp_folder_get_type (void)
{
	static CamelType camel_lite_nntp_folder_type = CAMEL_INVALID_TYPE;

	if (camel_lite_nntp_folder_type == CAMEL_INVALID_TYPE)	{
		camel_lite_nntp_folder_type = camel_lite_type_register (CAMEL_DISCO_FOLDER_TYPE, "CamelLiteNNTPFolder",
							      sizeof (CamelNNTPFolder),
							      sizeof (CamelNNTPFolderClass),
							      (CamelObjectClassInitFunc) nntp_folder_class_init,
							      NULL,
							      (CamelObjectInitFunc) nntp_folder_init,
							      (CamelObjectFinalizeFunc) nntp_folder_finalise);
	}

	return camel_lite_nntp_folder_type;
}

CamelFolder *
camel_lite_nntp_folder_new (CamelStore *parent, const char *folder_name, CamelException *ex)
{
	CamelFolder *folder;
	CamelNNTPFolder *nntp_folder;
	char *root;
	CamelService *service;
	CamelStoreInfo *si;
	gboolean subscribed = TRUE;

	service = (CamelService *) parent;
	root = camel_lite_session_get_storage_path (service->session, service, ex);
	if (root == NULL)
		return NULL;

	/* If this doesn't work, stuff wont save, but let it continue anyway */
	g_mkdir_with_parents (root, 0777);

	folder = (CamelFolder *) camel_lite_object_new (CAMEL_NNTP_FOLDER_TYPE);
	nntp_folder = (CamelNNTPFolder *)folder;

	camel_lite_folder_construct (folder, parent, folder_name, folder_name);
	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY|CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	nntp_folder->storage_path = g_build_filename (root, folder->full_name, NULL);
	g_free (root);

	root = g_strdup_printf ("%s.cmeta", nntp_folder->storage_path);
	camel_lite_object_set(nntp_folder, NULL, CAMEL_OBJECT_STATE_FILE, root, NULL);
	camel_lite_object_state_read(nntp_folder);
	g_free(root);

	root = g_strdup_printf("%s.ev-summary.mmap", nntp_folder->storage_path);
	folder->summary = (CamelFolderSummary *) camel_lite_nntp_summary_new (folder, root);
	g_free(root);
	camel_lite_folder_summary_load (folder->summary);

	si = camel_lite_store_summary_path ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, folder_name);
	if (si) {
		subscribed = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_lite_store_summary_info_free ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, si);
	}

	if (subscribed) {
		camel_lite_folder_refresh_info(folder, ex);
		if (camel_lite_exception_is_set(ex)) {
			camel_lite_object_unref (folder);
			folder = NULL;
		}
        }

	return folder;
}
