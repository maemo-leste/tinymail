/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999, 2003 Ximian Inc.
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-data-wrapper.h"
#include "camel-exception.h"
#include "camel-mh-folder.h"
#include "camel-mh-store.h"
#include "camel-mh-summary.h"
#include "camel-mime-message.h"
#include "camel-stream-fs.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMhFolder */
#define CMHF_CLASS(so) CAMEL_MH_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMHS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static CamelLocalSummary *mh_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index);

static void mh_append_message(CamelFolder * folder, CamelMimeMessage * message, const CamelMessageInfo *info, char **appended_uid, CamelException * ex);
static CamelMimeMessage *mh_get_message(CamelFolder * folder, const gchar * uid, CamelFolderReceiveType type, gint param, CamelException * ex);

static void mh_finalize(CamelObject * object);

static void camel_lite_mh_folder_class_init(CamelObjectClass * camel_lite_mh_folder_class)
{
	CamelFolderClass *camel_lite_folder_class = CAMEL_FOLDER_CLASS(camel_lite_mh_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_lite_mh_folder_class;

	parent_class = CAMEL_LOCAL_FOLDER_CLASS (camel_lite_type_get_global_classfuncs(camel_lite_local_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	camel_lite_folder_class->append_message = mh_append_message;
	camel_lite_folder_class->get_message = mh_get_message;

	lclass->create_summary = mh_create_summary;
}

static void mh_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;
	  CamelMhFolder *mh_folder = object;*/
}

static void mh_finalize(CamelObject * object)
{
	/*CamelMhFolder *mh_folder = CAMEL_MH_FOLDER(object);*/
}

CamelType camel_lite_mh_folder_get_type(void)
{
	static CamelType camel_lite_mh_folder_type = CAMEL_INVALID_TYPE;

	if (camel_lite_mh_folder_type == CAMEL_INVALID_TYPE) {
		camel_lite_mh_folder_type = camel_lite_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelLiteMhFolder",
							   sizeof(CamelMhFolder),
							   sizeof(CamelMhFolderClass),
							   (CamelObjectClassInitFunc) camel_lite_mh_folder_class_init,
							   NULL,
							   (CamelObjectInitFunc) mh_init,
							   (CamelObjectFinalizeFunc) mh_finalize);
	}

	return camel_lite_mh_folder_type;
}

CamelFolder *
camel_lite_mh_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating mh folder: %s\n", full_name));

	folder = (CamelFolder *)camel_lite_object_new(CAMEL_MH_FOLDER_TYPE);
	folder = (CamelFolder *)camel_lite_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *mh_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_lite_mh_summary_new((CamelFolder *)lf, path, folder, index);
}

static void
mh_append_message (CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex)
{
	CamelMhFolder *mh_folder = (CamelMhFolder *)folder;
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream;
	CamelMessageInfo *mi;
	char *name;

	/* FIXME: probably needs additional locking (although mh doesn't appear do do it) */

	d(printf("Appending message\n"));

	/* add it to the summary/assign the uid, etc */
	mi = camel_lite_local_summary_add((CamelLocalSummary *)folder->summary, message, info, lf->changes, ex);
	if (camel_lite_exception_is_set (ex))
		return;

	d(printf("Appending message: uid is %s\n", camel_lite_message_info_uid(mi)));

	/* write it out, use the uid we got from the summary */
	name = g_strdup_printf("%s/%s", lf->folder_path, camel_lite_message_info_uid(mi));
	output_stream = camel_lite_stream_fs_new_with_name(name, O_WRONLY|O_CREAT, 0600);
	if (output_stream == NULL)
		goto fail_write;

	if (camel_lite_data_wrapper_write_to_stream ((CamelDataWrapper *)message, output_stream) == -1
	    || camel_lite_stream_close (output_stream) == -1)
		goto fail_write;

	/* close this? */
	camel_lite_object_unref (CAMEL_OBJECT (output_stream));

	g_free(name);

	camel_lite_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed",
				    ((CamelLocalFolder *)mh_folder)->changes);
	camel_lite_folder_change_info_clear (((CamelLocalFolder *)mh_folder)->changes);

	if (appended_uid)
		*appended_uid = g_strdup(camel_lite_message_info_uid(mi));

	return;

 fail_write:

	/* remove the summary info so we are not out-of-sync with the mh folder */
	camel_lite_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (folder->summary),
					 camel_lite_message_info_uid (mi));

	if (errno == EINTR)
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("MH append message canceled"));
	else
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message to mh folder: %s: %s"),
				      name, g_strerror (errno));

	if (output_stream) {
		camel_lite_object_unref (CAMEL_OBJECT (output_stream));
		unlink (name);
	}

	g_free (name);
}

static CamelMimeMessage *mh_get_message(CamelFolder * folder, const gchar * uid, CamelFolderReceiveType type, gint param, CamelException * ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *info;
	char *name;

	d(printf("getting message: %s\n", uid));

	/* TNY TODO: Implement partial message retrieval if full==TRUE
	   maybe remove the attachments if it happens to be FALSE in this case?
	 */

	/* get the message summary info */
	if ((info = camel_lite_folder_summary_uid(folder->summary, uid)) == NULL) {
		camel_lite_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s from folder %s\n  %s"), uid, lf->folder_path,
				     _("No such message"));
		return NULL;
	}

	/* we only need it to check the message exists */
	camel_lite_message_info_free(info);

	name = g_strdup_printf("%s/%s", lf->folder_path, uid);
	if ((message_stream = camel_lite_stream_fs_new_with_name(name, O_RDONLY, 0)) == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get message: %s from folder %s\n  %s"), name, lf->folder_path,
				      g_strerror (errno));
		g_free(name);
		return NULL;
	}

	message = camel_lite_mime_message_new();
	if (camel_lite_data_wrapper_construct_from_stream((CamelDataWrapper *)message, message_stream) == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get message: %s from folder %s\n  %s"), name, lf->folder_path,
				      _("Message construction failed."));
		g_free(name);
		camel_lite_object_unref((CamelObject *)message_stream);
		camel_lite_object_unref((CamelObject *)message);
		return NULL;

	}
	camel_lite_object_unref((CamelObject *)message_stream);
	g_free(name);

	return message;
}
