/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-diary.c: class for a disconnected operation log */

/*
 * Authors: Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2001 Ximian, Inc.
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

#define __USE_LARGEFILE 1
#include <stdio.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-disco-diary.h"
#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-exception.h"
#include "camel-file-utils.h"
#include "camel-folder.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-store.h"

#define d(x)

static void
camel_lite_disco_diary_class_init (CamelDiscoDiaryClass *camel_lite_disco_diary_class)
{
	/* virtual method definition */
}

static void
camel_lite_disco_diary_init (CamelDiscoDiary *diary)
{
	diary->folders = g_hash_table_new (g_str_hash, g_str_equal);
	diary->uidmap = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
unref_folder (gpointer key, gpointer value, gpointer data)
{
	camel_lite_object_unref (value);
}

static void
free_uid (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
camel_lite_disco_diary_finalize (CamelDiscoDiary *diary)
{
	if (diary->file)
		fclose (diary->file);
	if (diary->folders) {
		g_hash_table_foreach (diary->folders, unref_folder, NULL);
		g_hash_table_destroy (diary->folders);
	}
	if (diary->uidmap) {
		g_hash_table_foreach (diary->uidmap, free_uid, NULL);
		g_hash_table_destroy (diary->uidmap);
	}
}

CamelType
camel_lite_disco_diary_get_type (void)
{
	static CamelType camel_lite_disco_diary_type = CAMEL_INVALID_TYPE;

	if (camel_lite_disco_diary_type == CAMEL_INVALID_TYPE) {
		camel_lite_disco_diary_type = camel_lite_type_register (
			CAMEL_OBJECT_TYPE, "CamelLiteDiscoDiary",
			sizeof (CamelDiscoDiary),
			sizeof (CamelDiscoDiaryClass),
			(CamelObjectClassInitFunc) camel_lite_disco_diary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_lite_disco_diary_init,
			(CamelObjectFinalizeFunc) camel_lite_disco_diary_finalize);
	}

	return camel_lite_disco_diary_type;
}


static int
diary_encode_uids (CamelDiscoDiary *diary, GPtrArray *uids)
{
	int i, status;

	status = camel_lite_file_util_encode_uint32 (diary->file, uids->len);
	for (i = 0; status != -1 && i < uids->len; i++)
		status = camel_lite_file_util_encode_string (diary->file, uids->pdata[i]);
	return status;
}

void
camel_lite_disco_diary_log (CamelDiscoDiary *diary, CamelDiscoDiaryAction action,
		       ...)
{
	va_list ap;
	int status;

	d(printf("diary log: %s\n", diary->file?"ok":"no file!"));

	/* You may already be a loser. */
	if (!diary || !diary->file)
		return;

	status = camel_lite_file_util_encode_uint32 (diary->file, action);
	if (status == -1)
		goto lose;

	va_start (ap, action);
	switch (action) {
	case CAMEL_DISCO_DIARY_FOLDER_EXPUNGE:
	{
		CamelFolder *folder = va_arg (ap, CamelFolder *);
		GPtrArray *uids = va_arg (ap, GPtrArray *);

		d(printf(" folder expunge '%s'\n", folder->full_name));

		status = camel_lite_file_util_encode_string (diary->file, folder->full_name);
		if (status != -1)
			status = diary_encode_uids (diary, uids);
		break;
	}

	case CAMEL_DISCO_DIARY_FOLDER_APPEND:
	{
		CamelFolder *folder = va_arg (ap, CamelFolder *);
		char *uid = va_arg (ap, char *);

		d(printf(" folder append '%s'\n", folder->full_name));

		status = camel_lite_file_util_encode_string (diary->file, folder->full_name);
		if (status != -1)
			status = camel_lite_file_util_encode_string (diary->file, uid);
		break;
	}

	case CAMEL_DISCO_DIARY_FOLDER_TRANSFER:
	{
		CamelFolder *source = va_arg (ap, CamelFolder *);
		CamelFolder *destination = va_arg (ap, CamelFolder *);
		GPtrArray *uids = va_arg (ap, GPtrArray *);
		gboolean delete_originals = va_arg (ap, gboolean);

		d(printf(" folder transfer '%s' to '%s'\n", source->full_name, destination->full_name));

		status = camel_lite_file_util_encode_string (diary->file, source->full_name);
		if (status == -1)
			break;
		status = camel_lite_file_util_encode_string (diary->file, destination->full_name);
		if (status == -1)
			break;
		status = diary_encode_uids (diary, uids);
		if (status == -1)
			break;
		status = camel_lite_file_util_encode_uint32 (diary->file, delete_originals);
		break;
	}

	default:
		g_assert_not_reached ();
		break;
	}

	va_end (ap);

 lose:
	if (status == -1) {
		char *msg;

		msg = g_strdup_printf (_("Could not write log entry: %s\n"
					 "Further operations on this server "
					 "will not be replayed when you\n"
					 "reconnect to the network."),
				       g_strerror (errno));
		camel_lite_session_alert_user_generic (camel_lite_service_get_session (CAMEL_SERVICE (diary->store)),
					  CAMEL_SESSION_ALERT_ERROR,
					  msg, FALSE, CAMEL_SERVICE (diary->store));
		g_free (msg);

		fclose (diary->file);
		diary->file = NULL;
	}
}

static void
free_uids (GPtrArray *array)
{
	while (array->len--)
		g_free (array->pdata[array->len]);
	g_ptr_array_free (array, TRUE);
}

static GPtrArray *
diary_decode_uids (CamelDiscoDiary *diary)
{
	GPtrArray *uids;
	char *uid;
	guint32 i;

	if (camel_lite_file_util_decode_uint32 (diary->file, &i) == -1)
		return NULL;

	uids = g_ptr_array_new ();
	while (i--) {
		if (camel_lite_file_util_decode_string (diary->file, &uid) == -1) {
			free_uids (uids);
			return NULL;
		}
		g_ptr_array_add (uids, uid);
	}

	return uids;
}

static CamelFolder *
diary_decode_folder (CamelDiscoDiary *diary)
{
	CamelFolder *folder;
	char *name;

	if (camel_lite_file_util_decode_string (diary->file, &name) == -1)
		return NULL;
	folder = g_hash_table_lookup (diary->folders, name);
	if (!folder) {
		CamelException ex;
		char *msg;

		camel_lite_exception_init (&ex);
		folder = camel_lite_store_get_folder (CAMEL_STORE (diary->store),
						 name, 0, &ex);
		if (folder)
			g_hash_table_insert (diary->folders, name, folder);
		else {
			msg = g_strdup_printf (_("Could not open `%s':\n%s\nChanges made to this folder will not be resynchronized."),
					       name, camel_lite_exception_get_description (&ex));
			camel_lite_exception_clear (&ex);
			camel_lite_session_alert_user_generic (camel_lite_service_get_session (CAMEL_SERVICE (diary->store)),
						  CAMEL_SESSION_ALERT_WARNING,
						  msg, FALSE, CAMEL_SERVICE (diary->store));
			g_free (msg);
			g_free (name);
		}
	} else
		g_free (name);
	return folder;
}

static void
close_folder (gpointer name, gpointer folder, gpointer data)
{
	g_free (name);
	camel_lite_folder_sync (folder, FALSE, NULL);
	camel_lite_object_unref (folder);
}

void
camel_lite_disco_diary_replay (CamelDiscoDiary *diary, CamelException *ex)
{
	guint32 action;
	off_t size;

	d(printf("disco diary replay\n"));

	fseek (diary->file, 0, SEEK_END);
	size = ftell (diary->file);
	if (size == 0)
		return;
	rewind (diary->file);

	camel_lite_operation_start (NULL, _("Resynchronizing with server"));
	while (!camel_lite_exception_is_set (ex)) {
		camel_lite_operation_progress (NULL, ftell (diary->file) , size);

		if (camel_lite_file_util_decode_uint32 (diary->file, &action) == -1)
			break;
		if (action == CAMEL_DISCO_DIARY_END)
			break;

		switch (action) {
		case CAMEL_DISCO_DIARY_FOLDER_EXPUNGE:
		{
			CamelFolder *folder;
			GPtrArray *uids;

			folder = diary_decode_folder (diary);
			uids = diary_decode_uids (diary);
			if (!uids)
				goto lose;

			if (folder)
				camel_lite_disco_folder_expunge_uids (folder, uids, ex);
			free_uids (uids);
			break;
		}

		case CAMEL_DISCO_DIARY_FOLDER_APPEND:
		{
			CamelFolder *folder;
			char *uid, *ret_uid = NULL;
			CamelMimeMessage *message;
			CamelMessageInfo *info;

			folder = diary_decode_folder (diary);
			if (camel_lite_file_util_decode_string (diary->file, &uid) == -1)
				goto lose;

			if (!folder) {
				g_free (uid);
				continue;
			}

			/* TNY TODO Partial message retrieval exception */
			message = camel_lite_folder_get_message (folder, uid, CAMEL_FOLDER_RECEIVE_FULL, -1, NULL);
			if (!message) {
				/* The message was appended and then deleted. */
				g_free (uid);
				continue;
			}
			info = camel_lite_folder_get_message_info (folder, uid);

			camel_lite_folder_append_message (folder, message, info, &ret_uid, ex);
			camel_lite_folder_free_message_info (folder, info);

			if (ret_uid) {
				camel_lite_disco_diary_uidmap_add (diary, uid, ret_uid);
				g_free (ret_uid);
			}
			g_free (uid);

			break;
		}

		case CAMEL_DISCO_DIARY_FOLDER_TRANSFER:
		{
			CamelFolder *source, *destination;
			GPtrArray *uids = NULL, *ret_uids = NULL;
			guint32 delete_originals;
			int i;

			source = diary_decode_folder (diary);
			destination = diary_decode_folder (diary);
			uids = diary_decode_uids (diary);

			if (!uids)
				goto lose;

			if (camel_lite_file_util_decode_uint32 (diary->file, &delete_originals) == -1) {
				free_uids (uids);
				goto lose;
			}

			if (!source || !destination) {
				free_uids (uids);
				uids = NULL;
				continue;
			}

			camel_lite_folder_transfer_messages_to (source, uids, destination, &ret_uids, delete_originals, ex);

			if (ret_uids) {
				for (i = 0; i < uids->len; i++) {
					if (!ret_uids->pdata[i])
						continue;
					camel_lite_disco_diary_uidmap_add (diary, uids->pdata[i], ret_uids->pdata[i]);
					g_free (ret_uids->pdata[i]);
				}
				g_ptr_array_free (ret_uids, TRUE);
			}
			free_uids (uids);
			break;
		}

		}
	}

 lose:
	camel_lite_operation_end (NULL);

	/* Close folders */
	g_hash_table_foreach (diary->folders, close_folder, diary);
	g_hash_table_destroy (diary->folders);
	diary->folders = g_hash_table_new (g_str_hash, g_str_equal);

	/* Truncate the log */
	ftruncate (fileno (diary->file), 0);
}

CamelDiscoDiary *
camel_lite_disco_diary_new (CamelDiscoStore *store, const char *filename, CamelException *ex)
{
	CamelDiscoDiary *diary;

	g_return_val_if_fail (CAMEL_IS_DISCO_STORE (store), NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	diary = CAMEL_DISCO_DIARY (camel_lite_object_new (CAMEL_DISCO_DIARY_TYPE));
	diary->store = store;

	d(printf("diary log file '%s'\n", filename));

	/* Note that the linux man page says:

	   a+     Open for reading and appending (writing at end  of  file).   The
	          file  is created if it does not exist.  The stream is positioned
		  at the end of the file.
	   However, c99 (which glibc uses?) says:
	   a+     append; open or create text file for update, writing at
	           end-of-file

	   So we must seek ourselves.
	*/

	diary->file = g_fopen (filename, "a+b");
	if (!diary->file) {
		camel_lite_object_unref (diary);
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "Could not open journal file: %s",
				      g_strerror (errno));
		return NULL;
	}

	fseek(diary->file, 0, SEEK_END);

	d(printf(" is at %ld\n", ftell(diary->file)));

	return diary;
}

gboolean
camel_lite_disco_diary_empty  (CamelDiscoDiary *diary)
{
	return ftell (diary->file) == 0;
}

void
camel_lite_disco_diary_uidmap_add (CamelDiscoDiary *diary, const char *old_uid,
			      const char *new_uid)
{
	g_hash_table_insert (diary->uidmap, g_strdup (old_uid),
			     g_strdup (new_uid));
}

const char *
camel_lite_disco_diary_uidmap_lookup (CamelDiscoDiary *diary, const char *uid)
{
	return g_hash_table_lookup (diary->uidmap, uid);
}
