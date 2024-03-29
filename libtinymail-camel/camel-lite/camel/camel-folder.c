/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder.c: Abstract class for an email folder */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999-2003 Ximian, Inc. (www.ximian.com)
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

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/e-lite-memory.h>

#include "camel-debug.h"
#include "camel-exception.h"
#include "camel-filter-driver.h"
#include "camel-folder.h"
#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vtrash-folder.h"
#include "camel-multipart.h"

#define d(x)
#define w(x)

extern int camel_lite_verbose_debug;

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) ((CamelFolderClass *)((CamelObject *)(so))->klass)

static void camel_lite_folder_finalize (CamelObject *object);

static void refresh_info (CamelFolder *folder, CamelException *ex);

static void folder_sync (CamelFolder *folder, gboolean expunge,
			 CamelException *ex);

static const char *get_name (CamelFolder *folder);
static const char *get_full_name (CamelFolder *folder);
static CamelStore *get_parent_store   (CamelFolder *folder);

static guint32 get_permanent_flags(CamelFolder *folder);
static guint32 get_message_flags(CamelFolder *folder, const char *uid);
static gboolean set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static gboolean get_message_user_flag(CamelFolder *folder, const char *uid, const char *name);
static void set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value);
static const char *get_message_user_tag(CamelFolder *folder, const char *uid, const char *name);
static void set_message_user_tag(CamelFolder *folder, const char *uid, const char *name, const char *value);

static int get_message_count(CamelFolder *folder);

static void expunge             (CamelFolder *folder,
				 CamelException *ex);
static int folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args);
static void folder_free(CamelObject *o, guint32 tag, void *val);


static void append_message (CamelFolder *folder, CamelMimeMessage *message,
			    const CamelMessageInfo *info, char **appended_uid,
			    CamelException *ex);

static GPtrArray        *get_uids            (CamelFolder *folder);
static void              free_uids           (CamelFolder *folder,
					      GPtrArray *array);
static GPtrArray        *get_summary         (CamelFolder *folder);
static void              free_summary        (CamelFolder *folder,
					      GPtrArray *array);

static CamelMimeMessage *get_message         (CamelFolder *folder, const gchar *uid, CamelFolderReceiveType type, gint param, CamelException *ex);

static CamelMessageInfo *get_message_info    (CamelFolder *folder, const char *uid);
static void		 free_message_info   (CamelFolder *folder, CamelMessageInfo *info);
static void		 ref_message_info    (CamelFolder *folder, CamelMessageInfo *info);

static GPtrArray      *search_by_expression  (CamelFolder *folder, const char *exp, CamelException *ex);
static GPtrArray      *search_by_uids	     (CamelFolder *folder, const char *exp, GPtrArray *uids, CamelException *ex);
static void            search_free           (CamelFolder * folder, GPtrArray *result);

static void            transfer_messages_to  (CamelFolder *source, GPtrArray *uids, CamelFolder *dest,
					      GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static void            delete                (CamelFolder *folder);
static void            folder_rename         (CamelFolder *folder, const char *new);

static void            freeze                (CamelFolder *folder);
static void            thaw                  (CamelFolder *folder);
static gboolean        is_frozen             (CamelFolder *folder);

static gboolean        folder_changed        (CamelObject *object,
					      gpointer event_data);

static int get_local_size (CamelFolder *folder);


static char *
fetch_structure (CamelFolder *folder, const char *uid, CamelException *ex)
{
	camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, "No support for BODYSTRUCTURE");
	return NULL;
}

char *
camel_lite_folder_fetch_structure (CamelFolder *folder, const char *uid, CamelException *ex)
{
	return CF_CLASS (folder)->fetch_structure (folder, uid, ex);
}

static char *
get_cache_filename (CamelFolder *folder, const char *uid, const char *spec, CamelFolderPartState *state)
{
	*state = CAMEL_FOLDER_PART_STATE_NOT_CACHED;
	return NULL;
}

char *
camel_lite_folder_get_cache_filename (CamelFolder *folder, const char *uid, const char *spec, CamelFolderPartState *state)
{
	return CF_CLASS (folder)->get_cache_filename (folder, uid, spec, state);
}

static char * 
fetch (CamelFolder *folder, const char *uid, const char *spec, gboolean *binary, CamelException *ex)
{
	if (binary)
		*binary = TRUE;
	camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, "No support for BODYSTRUCTURE");
	return g_strdup ("/dev/null");
}

static char * 
convert (CamelFolder *folder, const char *uid, const char *spec, const char *cto, CamelException *ex)
{
	camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, "No support for CONVERT");
	return g_strdup ("/dev/null");
}

char* 
camel_lite_folder_convert (CamelFolder *folder, const char *uid, const char *spec, const char *convert_to, CamelException *ex)
{
	return CF_CLASS (folder)->convert (folder, uid, spec, convert_to, ex);
}

char * 
camel_lite_folder_fetch (CamelFolder *folder, const char *uid, const char *spec, gboolean *binary, CamelException *ex)
{
	return CF_CLASS (folder)->fetch (folder, uid, spec, binary, ex);
}

static void
delete_attachments (CamelFolder *folder, const char *uid)
{
}

static void
rewrite_cache (CamelFolder *folder, const char *uid, CamelMimeMessage *msg)
{
}

static gboolean
get_allow_external_images (CamelFolder *folder, const char *uid)
{
	return FALSE;
}

static void
set_allow_external_images (CamelFolder *folder, const char *uid, gboolean allow)
{
}

static void
folder_set_push_email (CamelFolder *folder, gboolean setting)
{
	return;
}

void
camel_lite_folder_set_push_email (CamelFolder *folder, gboolean setting)
{
	if (!folder)
		return;

	CF_CLASS (folder)->set_push_email (folder, setting);
}

static void
camel_lite_folder_class_init (CamelFolderClass *camel_lite_folder_class)
{
	CamelObjectClass *camel_lite_object_class = CAMEL_OBJECT_CLASS (camel_lite_folder_class);

	parent_class = camel_lite_type_get_global_classfuncs (camel_lite_object_get_type ());

	/* virtual method definition */
	camel_lite_folder_class->convert = convert;
	camel_lite_folder_class->get_cache_filename = get_cache_filename;
	camel_lite_folder_class->fetch_structure = fetch_structure;
	camel_lite_folder_class->fetch = fetch;
	camel_lite_folder_class->get_local_size = get_local_size;
	camel_lite_folder_class->set_push_email = folder_set_push_email;
	camel_lite_folder_class->sync = folder_sync;
	camel_lite_folder_class->refresh_info = refresh_info;
	camel_lite_folder_class->get_name = get_name;
	camel_lite_folder_class->get_full_name = get_full_name;
	camel_lite_folder_class->get_parent_store = get_parent_store;
	camel_lite_folder_class->expunge = expunge;
	camel_lite_folder_class->get_message_count = get_message_count;
	camel_lite_folder_class->append_message = append_message;
	camel_lite_folder_class->get_permanent_flags = get_permanent_flags;
	camel_lite_folder_class->get_message_flags = get_message_flags;
	camel_lite_folder_class->set_message_flags = set_message_flags;
	camel_lite_folder_class->get_message_user_flag = get_message_user_flag;
	camel_lite_folder_class->set_message_user_flag = set_message_user_flag;
	camel_lite_folder_class->get_message_user_tag = get_message_user_tag;
	camel_lite_folder_class->set_message_user_tag = set_message_user_tag;
	camel_lite_folder_class->get_message = get_message;
	camel_lite_folder_class->get_uids = get_uids;
	camel_lite_folder_class->free_uids = free_uids;
	camel_lite_folder_class->get_summary = get_summary;
	camel_lite_folder_class->free_summary = free_summary;
	camel_lite_folder_class->search_by_expression = search_by_expression;
	camel_lite_folder_class->search_by_uids = search_by_uids;
	camel_lite_folder_class->search_free = search_free;
	camel_lite_folder_class->get_message_info = get_message_info;
	camel_lite_folder_class->ref_message_info = ref_message_info;
	camel_lite_folder_class->free_message_info = free_message_info;
	camel_lite_folder_class->transfer_messages_to = transfer_messages_to;
	camel_lite_folder_class->delete = delete;
	camel_lite_folder_class->rename = folder_rename;
	camel_lite_folder_class->freeze = freeze;
	camel_lite_folder_class->thaw = thaw;
	camel_lite_folder_class->is_frozen = is_frozen;
	camel_lite_folder_class->delete_attachments = delete_attachments;
	camel_lite_folder_class->rewrite_cache = rewrite_cache;
	camel_lite_folder_class->get_allow_external_images = get_allow_external_images;
	camel_lite_folder_class->set_allow_external_images = set_allow_external_images;

	/* virtual method overload */
	camel_lite_object_class->getv = folder_getv;
	camel_lite_object_class->free = folder_free;

	/* events */
	camel_lite_object_class_add_event(camel_lite_object_class, "folder_changed", folder_changed);
	camel_lite_object_class_add_event(camel_lite_object_class, "deleted", NULL);
	camel_lite_object_class_add_event(camel_lite_object_class, "renamed", NULL);
}

static void
camel_lite_folder_init (gpointer object, gpointer klass)
{
	CamelFolder *folder = object;

	folder->folder_flags = 0;
	folder->priv = g_malloc0(sizeof(*folder->priv));
	folder->priv->frozen = 0;
	folder->priv->changed_frozen = camel_lite_folder_change_info_new();
	g_static_rec_mutex_init(&folder->priv->lock);
	g_static_mutex_init(&folder->priv->change_lock);
}

static void
camel_lite_folder_finalize (CamelObject *object)
{
	CamelFolder *camel_lite_folder = CAMEL_FOLDER (object);
	struct _CamelFolderPrivate *p = camel_lite_folder->priv;
	CamelException nex = CAMEL_EXCEPTION_INITIALISER;

	g_free(camel_lite_folder->name);
	g_free(camel_lite_folder->full_name);
	g_free(camel_lite_folder->description);

	if (camel_lite_folder->parent_store)
		camel_lite_object_unref (camel_lite_folder->parent_store);

	if (camel_lite_folder->summary) {
		camel_lite_folder_summary_save (camel_lite_folder->summary, &nex);
		camel_lite_object_unref (camel_lite_folder->summary);
	}

	camel_lite_folder_change_info_free(p->changed_frozen);

	g_static_rec_mutex_free(&p->lock);
	g_static_mutex_free(&p->change_lock);

	g_free(p);
}

CamelType
camel_lite_folder_get_type (void)
{
	static CamelType camel_lite_folder_type = CAMEL_INVALID_TYPE;

	if (camel_lite_folder_type == CAMEL_INVALID_TYPE)	{
		camel_lite_folder_type = camel_lite_type_register (CAMEL_OBJECT_TYPE, "CamelLiteFolder",
							 sizeof (CamelFolder),
							 sizeof (CamelFolderClass),
							 (CamelObjectClassInitFunc) camel_lite_folder_class_init,
							 NULL,
							 (CamelObjectInitFunc) camel_lite_folder_init,
							 (CamelObjectFinalizeFunc) camel_lite_folder_finalize );
	}

	return camel_lite_folder_type;
}


/**
 * camel_lite_folder_construct:
 * @folder: a #CamelFolder object to construct
 * @parent_store: parent #CamelStore object of the folder
 * @full_name: full name of the folder
 * @name: short name of the folder
 *
 * Initalizes the folder by setting the parent store and name.
 **/
void
camel_lite_folder_construct (CamelFolder *folder, CamelStore *parent_store,
			const char *full_name, const char *name)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_STORE (parent_store));
	g_return_if_fail (folder->parent_store == NULL);
	g_return_if_fail (folder->name == NULL);

	folder->parent_store = parent_store;
	if (parent_store)
		camel_lite_object_ref(parent_store);

	folder->name = g_strdup (name);
	folder->full_name = g_strdup (full_name);
}


static void
folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	w(g_warning ("CamelLiteFolder::sync not implemented for `%s'",
		     camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
}


/**
 * camel_lite_folder_sync:
 * @folder: a #CamelFolder object
 * @expunge: whether or not to expunge deleted messages
 * @ex: a #CamelException
 *
 * Sync changes made to a folder to its backing store, possibly
 * expunging deleted messages as well.
 **/
void
camel_lite_folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CAMEL_FOLDER_REC_LOCK(folder, lock);

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		CF_CLASS (folder)->sync (folder, expunge, ex);

	CAMEL_FOLDER_REC_UNLOCK(folder, lock);
}


static void
refresh_info (CamelFolder *folder, CamelException *ex)
{
	/* No op */
}


/**
 * camel_lite_folder_refresh_info:
 * @folder: a #CamelFolder object
 * @ex: a #CamelException
 *
 * Updates a folder's summary to be in sync with its backing store.
 **/
void
camel_lite_folder_refresh_info (CamelFolder *folder, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->refresh_info (folder, ex);
}

void
camel_lite_folder_delete_attachments (CamelFolder *folder, const char *uid)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->delete_attachments (folder, uid);

	return;
}

void
camel_lite_folder_rewrite_cache (CamelFolder *folder, const char *uid, CamelMimeMessage *msg)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->rewrite_cache (folder, uid, msg);

	return;
}

gboolean
camel_lite_folder_get_allow_external_images (CamelFolder *folder, const char *uid)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
     
	return CF_CLASS (folder)->get_allow_external_images (folder, uid);
}

void
camel_lite_folder_set_allow_external_images (CamelFolder *folder, const char *uid, gboolean allow)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
     
	CF_CLASS (folder)->set_allow_external_images (folder, uid, allow);

	return;
}

static int
folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i;
	guint32 tag;
	int unread = -1, deleted = 0, count = -1;

	for (i = 0; i < args->argc; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
			/* CamelObject args */
		case CAMEL_OBJECT_ARG_DESCRIPTION:
			if (folder->description == NULL)
				folder->description = g_strdup_printf("%s", folder->full_name);
			*arg->ca_str = folder->description;
			break;

			/* CamelFolder args */
		case CAMEL_FOLDER_ARG_NAME:
			*arg->ca_str = folder->name;
			break;
		case CAMEL_FOLDER_ARG_FULL_NAME:
			*arg->ca_str = folder->full_name;
			break;
		case CAMEL_FOLDER_ARG_STORE:
			*arg->ca_object = folder->parent_store;
			break;
		case CAMEL_FOLDER_ARG_PERMANENTFLAGS:
			*arg->ca_int = folder->permanent_flags;
			break;
		case CAMEL_FOLDER_ARG_TOTAL:
			*arg->ca_int = camel_lite_folder_summary_count(folder->summary);
			break;
		case CAMEL_FOLDER_ARG_UNREAD:
		case CAMEL_FOLDER_ARG_DELETED:

			/* This is so we can get the values atomically, and also
			 * so we can calculate them only once */

			if (unread == -1) {
				int j;
				CamelMessageInfo *info;

				/* TODO: Locking? */
				unread = 0;
				count = camel_lite_folder_summary_count (folder->summary);
				for (j = 0; j < count; j++) {
					info = camel_lite_folder_summary_index (folder->summary, j);
					if (info) {
						guint32 flags = camel_lite_message_info_flags (info);

						/* TNY Observation: We assume that
						 * deleted messages are seen too */

						if (flags & CAMEL_MESSAGE_DELETED)
							deleted++;
						else if ((flags & CAMEL_MESSAGE_SEEN) == 0)
							unread++;

						camel_lite_message_info_free(info);
					}
				}
			}

			switch (tag & CAMEL_ARG_TAG) {
			case CAMEL_FOLDER_ARG_UNREAD:
				count = unread;
				break;
			case CAMEL_FOLDER_ARG_DELETED:
				count = deleted;
				break;
			}

			*arg->ca_int = count;
			break;
		case CAMEL_FOLDER_ARG_UID_ARRAY: {
			int j;
			CamelMessageInfo *info;
			GPtrArray *array;

			count = camel_lite_folder_summary_count(folder->summary);
			array = g_ptr_array_new();
			g_ptr_array_set_size(array, count);
			for (j=0; j<count; j++) {
				if ((info = camel_lite_folder_summary_index(folder->summary, j))) {
					array->pdata[i] = g_strdup(camel_lite_message_info_uid(info));
					camel_lite_message_info_free(info);
				}
			}
			*arg->ca_ptr = array;
			break; }
		case CAMEL_FOLDER_ARG_INFO_ARRAY:
			*arg->ca_ptr = camel_lite_folder_summary_array(folder->summary);
			break;
		case CAMEL_FOLDER_ARG_PROPERTIES:
			*arg->ca_ptr = NULL;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return parent_class->getv(object, ex, args);
}

static void
folder_free(CamelObject *o, guint32 tag, void *val)
{
	CamelFolder *folder = (CamelFolder *)o;

	switch (tag & CAMEL_ARG_TAG) {
	case CAMEL_FOLDER_ARG_UID_ARRAY: {
		GPtrArray *array = val;
		int i;

		for (i=0; i<array->len; i++)
			g_free(array->pdata[i]);
		g_ptr_array_free(array, TRUE);
		break; }
	case CAMEL_FOLDER_ARG_INFO_ARRAY:
		camel_lite_folder_summary_array_free(folder->summary, val);
		break;
	case CAMEL_FOLDER_ARG_PROPERTIES:
		g_slist_free(val);
		break;
	default:
		parent_class->free(o, tag, val);
	}
}

static const char *
get_name (CamelFolder *folder)
{
	return folder->name;
}


/**
 * camel_lite_folder_get_name:
 * @folder: a #CamelFolder object
 *
 * Get the (short) name of the folder. The fully qualified name
 * can be obtained with the #camel_lite_folder_get_full_name method.
 *
 * Returns the short name of the folder
 **/
const char *
camel_lite_folder_get_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_name (folder);
}


static const char *
get_full_name (CamelFolder *folder)
{
	return folder->full_name;
}


/**
 * camel_lite_folder_get_full_name:
 * @folder: a #CamelFolder object
 *
 * Get the full name of the folder.
 *
 * Returns the full name of the folder
 **/
const char *
camel_lite_folder_get_full_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_full_name (folder);
}


static CamelStore *
get_parent_store (CamelFolder * folder)
{
	return folder->parent_store;
}


/**
 * camel_lite_folder_get_parent_store:
 * @folder: a #CamelFolder object
 *
 * Returns the parent #CamelStore of the folder
 **/
CamelStore *
camel_lite_folder_get_parent_store (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_parent_store (folder);
}


static void
expunge (CamelFolder *folder, CamelException *ex)
{
	w(g_warning ("CamelLiteFolder::expunge not implemented for `%s'",
		     camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
}


/**
 * camel_lite_folder_expunge:
 * @folder: a #CamelFolder object
 * @ex: a #CamelException
 *
 * Delete messages which have been marked as "DELETED"
 **/
void
camel_lite_folder_expunge (CamelFolder *folder, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CAMEL_FOLDER_REC_LOCK(folder, lock);

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		CF_CLASS (folder)->expunge (folder, ex);

	CAMEL_FOLDER_REC_UNLOCK(folder, lock);
}

static int
get_message_count (CamelFolder *folder)
{
	g_return_val_if_fail(folder->summary != NULL, -1);

	return camel_lite_folder_summary_count(folder->summary);
}


/**
 * camel_lite_folder_get_message_count:
 * @folder: a #CamelFolder object
 *
 * Returns the number of messages in the folder, or %-1 if unknown
 **/
int
camel_lite_folder_get_message_count (CamelFolder *folder)
{
	int ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	ret = CF_CLASS (folder)->get_message_count (folder);

	return ret;
}


/**
 * camel_lite_folder_get_unread_message_count:
 * @folder: a #CamelFolder object
 *
 * DEPRECATED: use #camel_lite_object_get instead.
 *
 * Returns the number of unread messages in the folder, or %-1 if
 * unknown
 **/
int
camel_lite_folder_get_unread_message_count (CamelFolder *folder)
{
	int count = -1;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	camel_lite_object_get(folder, NULL, CAMEL_FOLDER_UNREAD, &count, 0);

	return count;
}


static int
get_local_size (CamelFolder *folder)
{
	return 0;
}

int
camel_lite_folder_get_local_size (CamelFolder *folder)
{
	return CF_CLASS (folder)->get_local_size (folder);
}

/**
 * camel_lite_folder_get_deleted_message_count:
 * @folder: a #CamelFolder object
 *
 * Returns the number of deleted messages in the folder, or %-1 if
 * unknown
 **/
int
camel_lite_folder_get_deleted_message_count (CamelFolder *folder)
{
	int count = -1;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	camel_lite_object_get(folder, NULL, CAMEL_FOLDER_DELETED, &count, 0);

	return count;
}

static void
append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, char **appended_uid,
		CamelException *ex)
{
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
			      _("Unsupported operation: append message: for %s"),
			      camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));

	w(g_warning ("CamelLiteFolder::append_message not implemented for `%s'",
		     camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));

	return;

}


/**
 * camel_lite_folder_append_message:
 * @folder: a #CamelFolder object
 * @message: a #CamelMimeMessage object
 * @info: a #CamelMessageInfo with additional flags/etc to set on
 * new message, or %NULL
 * @appended_uid: if non-%NULL, the UID of the appended message will
 * be returned here, if it is known.
 * @ex: a #CamelException
 *
 * Append @message to @folder. Only the flag and tag data from @info
 * are used. If @info is %NULL, no flags or tags will be set.
 **/
void
camel_lite_folder_append_message (CamelFolder *folder, CamelMimeMessage *message,
			     const CamelMessageInfo *info, char **appended_uid,
			     CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CAMEL_FOLDER_REC_LOCK(folder, lock);

	CF_CLASS (folder)->append_message (folder, message, info, appended_uid, ex);

	CAMEL_FOLDER_REC_UNLOCK(folder, lock);
}


static guint32
get_permanent_flags (CamelFolder *folder)
{
	return folder->permanent_flags;
}


/**
 * camel_lite_folder_get_permanent_flags:
 * @folder: a #CamelFolder object
 *
 * Returns the set of #CamelMessageFlags that can be permanently
 * stored on a message between sessions. If it includes
 * #CAMEL_FLAG_USER, then user-defined flags will be remembered.
 **/
guint32
camel_lite_folder_get_permanent_flags (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return CF_CLASS (folder)->get_permanent_flags (folder);
}

static guint32
get_message_flags(CamelFolder *folder, const char *uid)
{
	CamelMessageInfo *info;
	guint32 flags;

	g_return_val_if_fail(folder->summary != NULL, 0);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return 0;

	flags = camel_lite_message_info_flags(info);
	camel_lite_message_info_free(info);

	return flags;
}


/**
 * camel_lite_folder_get_message_flags:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 *
 * Deprecated: Use #camel_lite_folder_get_message_info instead.
 *
 * Returns the #CamelMessageFlags that are set on the indicated
 * message.
 **/
guint32
camel_lite_folder_get_message_flags (CamelFolder *folder, const char *uid)
{
	guint32 ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	ret = CF_CLASS (folder)->get_message_flags (folder, uid);

	return ret;
}

static gboolean
set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelMessageInfo *info;
	int res;

	g_return_val_if_fail(folder->summary != NULL, FALSE);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return FALSE;

	res = camel_lite_message_info_set_flags(info, flags, set);
	camel_lite_message_info_free(info);

	return res;
}


/**
 * camel_lite_folder_set_message_flags:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @flags: a set of #CamelMessageFlag values to set
 * @set: the mask of values in @flags to use.
 *
 * Sets those flags specified by @flags to the values specified by @set
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_lite_folder_get_permanent_flags)
 *
 * E.g. to set the deleted flag and clear the draft flag, use
 * #camel_lite_folder_set_message_flags(folder, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DELETED);
 *
 * DEPRECATED: Use #camel_lite_message_info_set_flags on the message info directly
 * (when it works)
 *
 * Returns %TRUE if the flags were changed or %FALSE otherwise
 **/
gboolean
camel_lite_folder_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	g_return_val_if_fail(CAMEL_IS_FOLDER(folder), FALSE);

	return CF_CLASS(folder)->set_message_flags(folder, uid, flags, set);
}

static gboolean
get_message_user_flag(CamelFolder *folder, const char *uid, const char *name)
{
	CamelMessageInfo *info;
	gboolean ret;

	g_return_val_if_fail(folder->summary != NULL, FALSE);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return FALSE;

	ret = camel_lite_message_info_user_flag(info, name);
	camel_lite_message_info_free(info);

	return ret;
}


/**
 * camel_lite_folder_get_message_user_flag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of a user flag
 *
 * DEPRECATED: Use #camel_lite_message_info_get_user_flag on the message
 * info directly
 *
 * Returns %TRUE if the given user flag is set on the message or
 * %FALSE otherwise
 **/
gboolean
camel_lite_folder_get_message_user_flag (CamelFolder *folder, const char *uid,
				    const char *name)
{
	gboolean ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	ret = CF_CLASS (folder)->get_message_user_flag (folder, uid, name);

	return ret;
}

static void
set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	CamelMessageInfo *info;

	g_return_if_fail(folder->summary != NULL);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return;

	camel_lite_message_info_set_user_flag(info, name, value);
	camel_lite_message_info_free(info);
}


/**
 * camel_lite_folder_set_message_user_flag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of the user flag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use #camel_lite_message_info_set_user_flag on the
 * #CamelMessageInfo directly (when it works)
 *
 * Sets the user flag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_lite_folder_get_permanent_flags)
 **/
void
camel_lite_folder_set_message_user_flag (CamelFolder *folder, const char *uid,
				    const char *name, gboolean value)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->set_message_user_flag (folder, uid, name, value);
}

static const char *
get_message_user_tag(CamelFolder *folder, const char *uid, const char *name)
{
	CamelMessageInfo *info;
	const char *ret;

	g_return_val_if_fail(folder->summary != NULL, NULL);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return NULL;

	ret = camel_lite_message_info_user_tag(info, name);
	camel_lite_message_info_free(info);

	return ret;
}


/**
 * camel_lite_folder_get_message_user_tag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of a user tag
 *
 * DEPRECATED: Use #camel_lite_message_info_get_user_tag on the
 * #CamelMessageInfo directly.
 *
 * Returns the value of the user tag
 **/
const char *
camel_lite_folder_get_message_user_tag (CamelFolder *folder, const char *uid,  const char *name)
{
	const char *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* FIXME: should duplicate string */
	ret = CF_CLASS (folder)->get_message_user_tag (folder, uid, name);

	return ret;
}

static void
set_message_user_tag(CamelFolder *folder, const char *uid, const char *name, const char *value)
{
	CamelMessageInfo *info;

	g_return_if_fail(folder->summary != NULL);

	info = camel_lite_folder_summary_uid(folder->summary, uid);
	if (info == NULL)
		return;

	camel_lite_message_info_set_user_tag(info, name, value);
	camel_lite_message_info_free(info);
}


/**
 * camel_lite_folder_set_message_user_tag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of the user tag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use #camel_lite_message_info_set_user_tag on the
 * #CamelMessageInfo directly (when it works).
 *
 * Sets the user tag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_lite_folder_get_permanent_flags)
 **/
void
camel_lite_folder_set_message_user_tag (CamelFolder *folder, const char *uid, const char *name, const char *value)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->set_message_user_tag (folder, uid, name, value);
}

static CamelMessageInfo *
get_message_info (CamelFolder *folder, const char *uid)
{
	g_return_val_if_fail(folder->summary != NULL, NULL);

	return camel_lite_folder_summary_uid(folder->summary, uid);
}


/**
 * camel_lite_folder_get_message_info:
 * @folder: a #CamelFolder object
 * @uid: the uid of a message
 *
 * Retrieve the #CamelMessageInfo for the specified @uid.  This return
 * must be freed using #camel_lite_folder_free_message_info.
 *
 * Returns the summary information for the indicated message, or %NULL
 * if the uid does not exist
 **/
CamelMessageInfo *
camel_lite_folder_get_message_info (CamelFolder *folder, const char *uid)
{
	CamelMessageInfo *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	ret = CF_CLASS (folder)->get_message_info (folder, uid);

	return ret;
}

static void
free_message_info (CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail(folder->summary != NULL);

	camel_lite_message_info_free(info);
}


/**
 * camel_lite_folder_free_message_info:
 * @folder: a #CamelFolder object
 * @info: a #CamelMessageInfo
 *
 * Free (unref) a #CamelMessageInfo, previously obtained with
 * #camel_lite_folder_get_message_info.
 **/
void
camel_lite_folder_free_message_info(CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail(CAMEL_IS_FOLDER (folder));
	g_return_if_fail(info != NULL);

	CF_CLASS (folder)->free_message_info(folder, info);
}

static void
ref_message_info (CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail(folder->summary != NULL);

	camel_lite_message_info_ref(info);
}


/**
 * camel_lite_folder_ref_message_info:
 * @folder: a #CamelFolder object
 * @info: a #CamelMessageInfo
 *
 * DEPRECATED: Use #camel_lite_message_info_ref directly.
 *
 * Ref a #CamelMessageInfo, previously obtained with
 * #camel_lite_folder_get_message_info.
 **/
void
camel_lite_folder_ref_message_info(CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail(CAMEL_IS_FOLDER (folder));
	g_return_if_fail(info != NULL);

	CF_CLASS (folder)->ref_message_info(folder, info);
}


/* TODO: is this function required anyway? */
/**
 * camel_lite_folder_has_summary_capability:
 * @folder: a #CamelFolder object
 *
 * Get whether or not the folder has a summary.
 *
 * Returns %TRUE if a summary is available or %FALSE otherwise
 **/
gboolean
camel_lite_folder_has_summary_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;
}


/* UIDs stuff */

static CamelMimeMessage *
get_message (CamelFolder *folder, const char *uid, CamelFolderReceiveType type, gint param, CamelException *ex)
{
	w(g_warning ("CamelLiteFolder::get_message not implemented for `%s'",
		     camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));

	return NULL;
}


/**
 * camel_lite_folder_get_message:
 * @folder: a #CamelFolder object
 * @uid: the UID
 * @ex: a #CamelException
 *
 * Get a message from its UID in the folder.
 *
 * Returns a #CamelMimeMessage corresponding to @uid
 **/
CamelMimeMessage *
camel_lite_folder_get_message (CamelFolder *folder, const char *uid, CamelFolderReceiveType type, gint param, CamelException *ex)
{
	CamelMimeMessage *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* CAMEL_FOLDER_REC_LOCK(folder, lock); */

	ret = CF_CLASS (folder)->get_message (folder, uid, type, param, ex);

	/* CAMEL_FOLDER_REC_UNLOCK(folder, lock); */

	if (ret && camel_lite_debug_start(":folder")) {
		printf("CamelLiteFolder:get_message('%s', '%s') =\n", folder->full_name, uid);
		camel_lite_mime_message_dump(ret, FALSE);
		camel_lite_debug_end();
	}

	return ret;
}

static GPtrArray *
get_uids(CamelFolder *folder)
{
	GPtrArray *array;
	int i, j, count;

	array = g_ptr_array_new();

	g_return_val_if_fail(folder->summary != NULL, array);

	count = camel_lite_folder_summary_count(folder->summary);
	g_ptr_array_set_size(array, count);
	for (i = 0, j = 0; i < count; i++) {
		CamelMessageInfo *info = camel_lite_folder_summary_index(folder->summary, i);

		if (info) {
			array->pdata[j++] = g_strdup (camel_lite_message_info_uid (info));
			camel_lite_message_info_free(info);
		}
	}

	g_ptr_array_set_size (array, j);

	return array;
}


/**
 * camel_lite_folder_get_uids:
 * @folder: a #CamelFolder object
 *
 * Get the list of UIDs available in a folder. This routine is useful
 * for finding what messages are available when the folder does not
 * support summaries. The returned array should not be modified, and
 * must be freed by passing it to #camel_lite_folder_free_uids.
 *
 * Returns a GPtrArray of UIDs corresponding to the messages available
 * in the folder
 **/
GPtrArray *
camel_lite_folder_get_uids (CamelFolder *folder)
{
	GPtrArray *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	ret = CF_CLASS (folder)->get_uids (folder);

	return ret;
}

static void
free_uids (CamelFolder *folder, GPtrArray *array)
{
	int i;

	for (i=0; i<array->len; i++)
		g_free(array->pdata[i]);
	g_ptr_array_free(array, TRUE);
}


/**
 * camel_lite_folder_free_uids:
 * @folder: a #CamelFolder object
 * @array: the array of uids to free
 *
 * Frees the array of UIDs returned by #camel_lite_folder_get_uids.
 **/
void
camel_lite_folder_free_uids (CamelFolder *folder, GPtrArray *array)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->free_uids (folder, array);
}

static GPtrArray *
get_summary(CamelFolder *folder)
{
	g_assert(folder->summary != NULL);

	return camel_lite_folder_summary_array(folder->summary);
}


/**
 * camel_lite_folder_get_summary:
 * @folder: a #CamelFolder object
 *
 * This returns the summary information for the folder. This array
 * should not be modified, and must be freed with
 * #camel_lite_folder_free_summary.
 *
 * Returns an array of #CamelMessageInfo
 **/
GPtrArray *
camel_lite_folder_get_summary (CamelFolder *folder)
{
	GPtrArray *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	ret = CF_CLASS (folder)->get_summary (folder);

	return ret;
}

static void
free_summary(CamelFolder *folder, GPtrArray *summary)
{
	g_assert(folder->summary != NULL);

	camel_lite_folder_summary_array_free(folder->summary, summary);
}


/**
 * camel_lite_folder_free_summary:
 * @folder: a #CamelFolder object
 * @array: the summary array to free
 *
 * Frees the summary array returned by #camel_lite_folder_get_summary.
 **/
void
camel_lite_folder_free_summary(CamelFolder *folder, GPtrArray *array)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));

	CF_CLASS(folder)->free_summary(folder, array);
}


/**
 * camel_lite_folder_has_search_capability:
 * @folder: a #CamelFolder object
 *
 * Checks if a folder supports searching.
 *
 * Returns %TRUE if the folder supports searching or %FALSE otherwise
 **/
gboolean
camel_lite_folder_has_search_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;
}

static GPtrArray *
search_by_expression (CamelFolder *folder, const char *expression,
		      CamelException *ex)
{
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
			      _("Unsupported operation: search by expression: for %s"),
			      camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));

	w(g_warning ("CamelLiteFolder::search_by_expression not implemented for "
		     "`%s'", camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));

	return NULL;
}


/**
 * camel_lite_folder_search_by_expression:
 * @folder: a #CamelFolder object
 * @expr: a search expression
 * @ex: a #CamelException
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Returns a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_lite_folder_search_by_expression (CamelFolder *folder, const char *expression,
				   CamelException *ex)
{
	GPtrArray *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY, NULL);

	/* NOTE: that it is upto the callee to lock */

	ret = CF_CLASS (folder)->search_by_expression (folder, expression, ex);

	return ret;
}

static GPtrArray *
search_by_uids(CamelFolder *folder, const char *exp, GPtrArray *uids, CamelException *ex)
{
	camel_lite_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
			      _("Unsupported operation: search by UIDs: for %s"),
			      camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));

	w(g_warning ("CamelLiteFolder::search_by_expression not implemented for "
		     "`%s'", camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));

	return NULL;
}


/**
 * camel_lite_folder_search_by_uids:
 * @folder: a #CamelFolder object
 * @expr: search expression
 * @uids: array of uid's to match against.
 * @ex: a #CamelException
 *
 * Search a subset of uid's for an expression match.
 *
 * Returns a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_lite_folder_search_by_uids(CamelFolder *folder, const char *expr, GPtrArray *uids, CamelException *ex)
{
	GPtrArray *ret;

	g_return_val_if_fail(CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail(folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY, NULL);

	/* NOTE: that it is upto the callee to lock */

	ret = CF_CLASS(folder)->search_by_uids(folder, expr, uids, ex);

	return ret;
}

static void
search_free (CamelFolder *folder, GPtrArray *result)
{
	int i;

	for (i = 0; i < result->len; i++)
		g_free (g_ptr_array_index (result, i));
	g_ptr_array_free (result, TRUE);
}


/**
 * camel_lite_folder_search_free:
 * @folder: a #CamelFolder object
 * @result: search results to free
 *
 * Free the result of a search as gotten by #camel_lite_folder_search or
 * #camel_lite_folder_search_by_uids.
 **/
void
camel_lite_folder_search_free (CamelFolder *folder, GPtrArray *result)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	/* NOTE: upto the callee to lock */
	CF_CLASS (folder)->search_free (folder, result);
}


static void
transfer_message_to (CamelFolder *source, const char *uid, CamelFolder *dest,
		     char **transferred_uid, gboolean delete_original,
		     CamelException *ex)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *minfo, *info;

	/* Default implementation. */

	/* TNY: Partial message retrieval exception (always transfer the full
		message, not just the body part) */
	msg = camel_lite_folder_get_message(source, uid, CAMEL_FOLDER_RECEIVE_FULL, -1, ex);
	if (!msg)
		return;

	/* if its deleted we poke the flags, so we need to copy the messageinfo */
	/* if ((source->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY)
	    && (minfo = camel_lite_folder_get_message_info(source, uid))) {
		info = camel_lite_message_info_clone(minfo);
		camel_lite_folder_free_message_info(source, minfo);
	} else */
	info = camel_lite_message_info_new_from_header(NULL, ((CamelMimePart *)msg)->headers);

	/*copying flags */
	if ((source->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY) &&
	    (minfo = camel_lite_folder_get_message_info (source, uid))) {
		camel_lite_message_info_set_flags (info, ~CAMEL_MESSAGE_SYSTEM_MASK, camel_lite_message_info_flags (minfo));
		camel_lite_folder_free_message_info (source, minfo);
	}

	/* we don't want to retain the deleted flag */
	camel_lite_message_info_set_flags(info, CAMEL_MESSAGE_DELETED, 0);

	camel_lite_folder_append_message (dest, msg, info, transferred_uid, ex);
	camel_lite_object_unref (msg);

	if (delete_original && !camel_lite_exception_is_set (ex))
		camel_lite_folder_set_message_flags (source, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN, ~0);

	camel_lite_message_info_free (info);
}

static void
transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex)
{
	CamelException local;
	char **ret_uid = NULL;
	int i;

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new ();
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	camel_lite_exception_init(&local);
	if (ex == NULL)
		ex = &local;

	camel_lite_operation_start(NULL, delete_originals ? _("Moving messages") : _("Copying messages"));

	if (uids->len > 1) {
		camel_lite_folder_freeze(dest);
		if (delete_originals)
			camel_lite_folder_freeze(source);
	}
	for (i = 0; i < uids->len && !camel_lite_exception_is_set (ex); i++) {
		if (transferred_uids)
			ret_uid = (char **)&((*transferred_uids)->pdata[i]);
		transfer_message_to (source, uids->pdata[i], dest, ret_uid, delete_originals, ex);
		camel_lite_operation_progress(NULL, i, uids->len);
	}
	if (uids->len > 1) {
		camel_lite_folder_thaw(dest);
		if (delete_originals)
			camel_lite_folder_thaw(source);
	}

	camel_lite_operation_end(NULL);
	camel_lite_exception_clear(&local);
}


/**
 * camel_lite_folder_transfer_messages_to:
 * @source: the source #CamelFolder object
 * @uids: message UIDs in @source
 * @dest: the destination #CamelFolder object
 * @transferred_uids: if non-%NULL, the UIDs of the resulting messages
 * in @dest will be stored here, if known.
 * @delete_originals: whether or not to delete the original messages
 * @ex: a #CamelException
 *
 * This copies or moves messages from one folder to another. If the
 * @source and @dest folders have the same parent_store, this may be
 * more efficient than using #camel_lite_folder_append_message.
 **/
void
camel_lite_folder_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
				   CamelFolder *dest, GPtrArray **transferred_uids,
				   gboolean delete_originals, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (source));
	g_return_if_fail (CAMEL_IS_FOLDER (dest));
	g_return_if_fail (uids != NULL);

	if (source == dest || uids->len == 0) {
		/* source and destination folders are the same, or no work to do, do nothing. */
		return;
	}

	if (source->parent_store == dest->parent_store) {
		/* If either folder is a vtrash, we need to use the
		 * vtrash transfer method.
		 */
		if (CAMEL_IS_VTRASH_FOLDER (dest))
			CF_CLASS (dest)->transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
		else
			CF_CLASS (source)->transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
	} else
		transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
}

static void
delete (CamelFolder *folder)
{
	if (folder->summary)
		camel_lite_folder_summary_clear (folder->summary);
}


/**
 * camel_lite_folder_delete:
 * @folder: a #CamelFolder object
 *
 * Marks a folder object as deleted and performs any required cleanup.
 **/
void
camel_lite_folder_delete (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CAMEL_FOLDER_REC_LOCK (folder, lock);
	if (folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
		CAMEL_FOLDER_REC_UNLOCK (folder, lock);
		return;
	}

	folder->folder_flags |= CAMEL_FOLDER_HAS_BEEN_DELETED;

	CF_CLASS (folder)->delete (folder);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	camel_lite_object_trigger_event (folder, "deleted", NULL);
}

static void
folder_rename (CamelFolder *folder, const char *new)
{
	char *tmp;

	d(printf("CamelLiteFolder:rename('%s')\n", new));

	g_free(folder->full_name);
	folder->full_name = g_strdup(new);
	g_free(folder->name);
	tmp = strrchr(new, '/');
	folder->name = g_strdup(tmp?tmp+1:new);
}


/**
 * camel_lite_folder_rename:
 * @folder: a #CamelFolder object
 * @new: new name for the folder
 *
 * Mark an active folder object as renamed.
 *
 * NOTE: This is an internal function used by camel stores, no locking
 * is performed on the folder.
 **/
void
camel_lite_folder_rename(CamelFolder *folder, const char *new)
{
	char *old;

	old = g_strdup(folder->full_name);

	CF_CLASS (folder)->rename(folder, new);

	camel_lite_object_trigger_event (folder, "renamed", old);
	g_free(old);
}

static void
freeze (CamelFolder *folder)
{
	CAMEL_FOLDER_LOCK(folder, change_lock);

	g_assert(folder->priv->frozen >= 0);

	folder->priv->frozen++;

	d(printf ("freeze(%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));
	CAMEL_FOLDER_UNLOCK(folder, change_lock);
}


/**
 * camel_lite_folder_freeze:
 * @folder: a #CamelFolder
 *
 * Freezes the folder so that a series of operation can be performed
 * without "folder_changed" signals being emitted.  When the folder is
 * later thawed with #camel_lite_folder_thaw, the suppressed signals will
 * be emitted.
 **/
void
camel_lite_folder_freeze (CamelFolder * folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->freeze (folder);
}

static void
thaw (CamelFolder * folder)
{
	CamelFolderChangeInfo *info = NULL;

	CAMEL_FOLDER_LOCK(folder, change_lock);

	g_assert(folder->priv->frozen > 0);

	folder->priv->frozen--;

	d(printf ("thaw(%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));

	if (folder->priv->frozen == 0
	    && camel_lite_folder_change_info_changed(folder->priv->changed_frozen)) {
		info = folder->priv->changed_frozen;
		folder->priv->changed_frozen = camel_lite_folder_change_info_new();
	}

	CAMEL_FOLDER_UNLOCK(folder, change_lock);

	if (info) {
		camel_lite_object_trigger_event (folder, "folder_changed", info);
		camel_lite_folder_change_info_free(info);
	}
}

/**
 * camel_lite_folder_thaw:
 * @folder: a #CamelFolder object
 *
 * Thaws the folder and emits any pending folder_changed
 * signals.
 **/
void
camel_lite_folder_thaw (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (folder->priv->frozen != 0);

	CF_CLASS (folder)->thaw (folder);
}

static gboolean
is_frozen (CamelFolder *folder)
{
	return folder->priv->frozen != 0;
}


/**
 * camel_lite_folder_is_frozen:
 * @folder: a #CamelFolder object
 *
 * Returns whether or not the folder is frozen
 **/
gboolean
camel_lite_folder_is_frozen (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return CF_CLASS (folder)->is_frozen (folder);
}

struct _folder_filter_msg {
	CamelSessionThreadMsg msg;

	GPtrArray *recents;
	GPtrArray *junk;
	GPtrArray *notjunk;
	CamelFolder *folder;
	CamelFilterDriver *driver;
	CamelException ex;
};

#if 0
static void
filter_filter(CamelSession *session, CamelSessionThreadMsg *tmsg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *) tmsg;
	CamelMessageInfo *info;
	int i, status = 0;
	CamelURL *uri;
	char *source_url;
	CamelException ex;
	CamelJunkPlugin *csp = ((CamelService *)m->folder->parent_store)->session->junk_plugin;

	if (m->junk) {
		camel_lite_operation_start (NULL, _("Learning junk"));

		for (i = 0; i < m->junk->len; i ++) {
			/* TNY TODO: Partial message retrieval exception */
			CamelMimeMessage *msg = camel_lite_folder_get_message(m->folder, m->junk->pdata[i], CAMEL_FOLDER_RECEIVE_FULL, -1, NULL);

			camel_lite_operation_progress(NULL, i, m->junk->len);

			if (msg) {
				camel_lite_junk_plugin_report_junk (csp, msg);
				camel_lite_object_unref (msg);
			}
		}
		camel_lite_operation_end (NULL);
	}

	if (m->notjunk) {
		camel_lite_operation_start (NULL, _("Learning non-junk"));
		for (i = 0; i < m->notjunk->len; i ++) {
			/* TNY TODO: Partial message retrieval exception */
			CamelMimeMessage *msg = camel_lite_folder_get_message(m->folder, m->notjunk->pdata[i], CAMEL_FOLDER_RECEIVE_FULL, -1, NULL);

			camel_lite_operation_progress(NULL, i, m->notjunk->len);

			if (msg) {
				camel_lite_junk_plugin_report_notjunk (csp, msg);
				camel_lite_object_unref (msg);
			}
		}
		camel_lite_operation_end (NULL);
	}

	if (m->junk || m->notjunk)
		camel_lite_junk_plugin_commit_reports (csp);

	if (m->driver && m->recents) {
		camel_lite_operation_start(NULL, _("Filtering new message(s)"));

		source_url = camel_lite_service_get_url((CamelService *)m->folder->parent_store);
		uri = camel_lite_url_new(source_url, NULL);
		g_free(source_url);
		if (m->folder->full_name && m->folder->full_name[0] != '/') {
			char *tmp = alloca(strlen(m->folder->full_name)+2);

			sprintf(tmp, "/%s", m->folder->full_name);
			camel_lite_url_set_path(uri, tmp);
		} else
			camel_lite_url_set_path(uri, m->folder->full_name);
		source_url = camel_lite_url_to_string(uri, CAMEL_URL_HIDE_ALL);
		camel_lite_url_free(uri);

		for (i=0;status == 0 && i<m->recents->len;i++) {
			char *uid = m->recents->pdata[i];

			camel_lite_operation_progress(NULL, i, m->recents->len);

			info = camel_lite_folder_get_message_info(m->folder, uid);
			if (info == NULL) {
				g_warning("uid %s vanished from folder: %s", uid, source_url);
				continue;
			}

			status = camel_lite_filter_driver_filter_message(m->driver, NULL, info, uid, m->folder, source_url, source_url, &m->ex);

			camel_lite_folder_free_message_info(m->folder, info);
		}

		camel_lite_exception_init(&ex);
		camel_lite_filter_driver_flush(m->driver, &ex);
		if (!camel_lite_exception_is_set(&m->ex))
			camel_lite_exception_xfer(&m->ex, &ex);

		g_free(source_url);

		camel_lite_operation_end(NULL);
	}
}

static void
filter_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *)msg;

	if (m->driver)
		camel_lite_object_unref(m->driver);
	if (m->recents)
		camel_lite_folder_free_deep(m->folder, m->recents);
	if (m->junk)
		camel_lite_folder_free_deep(m->folder, m->junk);
	if (m->notjunk)
		camel_lite_folder_free_deep(m->folder, m->notjunk);

	camel_lite_folder_thaw(m->folder);
	camel_lite_object_unref(m->folder);
}
#endif


struct _CamelFolderChangeInfoPrivate {
	GHashTable *uid_stored;	/* what we have stored, which array they're in */
	GHashTable *uid_source;	/* used to create unique lists */
	GPtrArray  *uid_filter; /* uids to be filtered */
	struct _EMemPool *uid_pool;	/* pool used to store copies of uid strings */
};


/* Event hooks that block emission when frozen */
static gboolean
folder_changed (CamelObject *obj, gpointer event_data)
{
	CamelFolder *folder = (CamelFolder *)obj;
	CamelFolderChangeInfo *changed = event_data;
	CamelSession *session = ((CamelService *)folder->parent_store)->session;
	CamelFilterDriver *driver = NULL;
	GPtrArray *recents = NULL;
	int i;

	d(printf ("folder_changed(%p:'%s', %p), frozen=%d\n", obj, folder->full_name, event_data, folder->priv->frozen));
	d(printf(" added %d removed %d changed %d recent %d filter %d\n",
		 changed->uid_added->len, changed->uid_removed->len,
		 changed->uid_changed->len, changed->uid_recent->len,
		 p->uid_filter->len));

	if (changed == NULL) {
		w(g_warning ("Class %s is passing NULL to folder_changed event",
			     camel_lite_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
		return TRUE;
	}

	CAMEL_FOLDER_LOCK(folder, change_lock);
	if (folder->priv->frozen) {
		camel_lite_folder_change_info_cat(folder->priv->changed_frozen, changed);
		CAMEL_FOLDER_UNLOCK(folder, change_lock);

		return FALSE;
	}
	CAMEL_FOLDER_UNLOCK(folder, change_lock);


	if ((folder->folder_flags & CAMEL_FOLDER_FILTER_RECENT)
	    && changed->uid_recent->len > 0)
		driver = camel_lite_session_get_filter_driver(session, "incoming", NULL);

	if (driver) {
		recents = g_ptr_array_new();
		for (i=0;i<changed->uid_recent->len;i++)
			g_ptr_array_add(recents, g_strdup(changed->uid_recent->pdata[i]));
	}


	return TRUE;
}


/**
 * camel_lite_folder_free_nop:
 * @folder: a #CamelFolder object
 * @array: an array of uids or #CamelMessageInfo
 *
 * "Frees" the provided array by doing nothing. Used by #CamelFolder
 * subclasses as an implementation for free_uids, or free_summary when
 * the returned array is "static" information and should not be freed.
 **/
void
camel_lite_folder_free_nop (CamelFolder *folder, GPtrArray *array)
{
	;
}


/**
 * camel_lite_folder_free_shallow:
 * @folder: a #CamelFolder object
 * @array: an array of uids or #CamelMessageInfo
 *
 * Frees the provided array but not its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids or free_summary when
 * the returned array needs to be freed but its contents come from
 * "static" information.
 **/
void
camel_lite_folder_free_shallow (CamelFolder *folder, GPtrArray *array)
{
	g_ptr_array_free (array, TRUE);
}


/**
 * camel_lite_folder_free_deep:
 * @folder: a #CamelFolder object
 * @array: an array of uids
 *
 * Frees the provided array and its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids when the provided
 * information was created explicitly by the corresponding get_ call.
 **/
void
camel_lite_folder_free_deep (CamelFolder *folder, GPtrArray *array)
{
	int i;

	for (i = 0; i < array->len; i++)
		g_free (array->pdata[i]);
	g_ptr_array_free (array, TRUE);
}


/**
 * camel_lite_folder_change_info_new:
 *
 * Create a new folder change info structure.
 *
 * Change info structures are not MT-SAFE and must be
 * locked for exclusive access externally.
 *
 * Returns a new #CamelFolderChangeInfo
 **/
CamelFolderChangeInfo *
camel_lite_folder_change_info_new(void)
{
	CamelFolderChangeInfo *info;

	info = g_slice_new (CamelFolderChangeInfo);
	info->push_email_event = FALSE;
	info->uid_added = g_ptr_array_new();
	info->uid_removed = g_ptr_array_new();
	info->uid_changed = g_ptr_array_new();
	info->uid_recent = g_ptr_array_new();
	info->priv = g_slice_new (struct _CamelFolderChangeInfoPrivate);
	info->priv->uid_stored = g_hash_table_new(g_str_hash, g_str_equal);
	info->priv->uid_source = NULL;
	info->priv->uid_filter = g_ptr_array_new();
	info->priv->uid_pool = e_mempool_new(512, 256, E_MEMPOOL_ALIGN_BYTE);

	return info;
}


/**
 * camel_lite_folder_change_info_add_source:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a source uid for generating a changeset.
 **/
void
camel_lite_folder_change_info_add_source(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert(info != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new(g_str_hash, g_str_equal);

	if (g_hash_table_lookup(p->uid_source, uid) == NULL)
		g_hash_table_insert(p->uid_source, e_mempool_strdup(p->uid_pool, uid), GINT_TO_POINTER (1));
}


/**
 * camel_lite_folder_change_info_add_source_list:
 * @info: a #CamelFolderChangeInfo
 * @list: a list of uids
 *
 * Add a list of source uid's for generating a changeset.
 **/
void
camel_lite_folder_change_info_add_source_list(CamelFolderChangeInfo *info, const GPtrArray *list)
{
	struct _CamelFolderChangeInfoPrivate *p;
	int i;

	g_assert(info != NULL);
	g_assert(list != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new(g_str_hash, g_str_equal);

	for (i=0;i<list->len;i++) {
		char *uid = list->pdata[i];

		if (g_hash_table_lookup(p->uid_source, uid) == NULL)
			g_hash_table_insert(p->uid_source, e_mempool_strdup(p->uid_pool, uid), GINT_TO_POINTER (1));
	}
}


/**
 * camel_lite_folder_change_info_add_update:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid from the updated list, used to generate a changeset diff.
 **/
void
camel_lite_folder_change_info_add_update(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	char *key;
	int value;

	g_assert(info != NULL);

	p = info->priv;

	if (p->uid_source == NULL) {
		camel_lite_folder_change_info_add_uid(info, uid);
		return;
	}

	if (g_hash_table_lookup_extended(p->uid_source, uid, (gpointer) &key, (gpointer) &value)) {
		g_hash_table_remove(p->uid_source, key);
	} else {
		camel_lite_folder_change_info_add_uid(info, uid);
	}
}


/**
 * camel_lite_folder_change_info_add_update_list:
 * @info: a #CamelFolderChangeInfo
 * @list: a list of uids
 *
 * Add a list of uid's from the updated list.
 **/
void
camel_lite_folder_change_info_add_update_list(CamelFolderChangeInfo *info, const GPtrArray *list)
{
	int i;

	g_assert(info != NULL);
	g_assert(list != NULL);

	for (i=0;i<list->len;i++)
		camel_lite_folder_change_info_add_update(info, list->pdata[i]);
}

static void
change_info_remove(char *key, void *value, CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p = info->priv;
	GPtrArray *olduids;
	char *olduid;

	if (g_hash_table_lookup_extended(p->uid_stored, key, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_removed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	/* we dont need to copy this, as they've already been copied into our pool */
	g_ptr_array_add(info->uid_removed, key);
	g_hash_table_insert(p->uid_stored, key, info->uid_removed);
}


/**
 * camel_lite_folder_change_info_build_diff:
 * @info: a #CamelFolderChangeInfo
 *
 * Compare the source uid set to the updated uid set and generate the
 * differences into the added and removed lists.
 **/
void
camel_lite_folder_change_info_build_diff(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert(info != NULL);

	p = info->priv;

	if (p->uid_source) {
		g_hash_table_foreach(p->uid_source, (GHFunc)change_info_remove, info);
		g_hash_table_destroy(p->uid_source);
		p->uid_source = NULL;
	}
}

static void
change_info_recent_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;

	p = info->priv;

	/* always add to recent, but dont let anyone else know */
	if (!g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		olduid = e_mempool_strdup(p->uid_pool, uid);
	}
	g_ptr_array_add(info->uid_recent, olduid);
}

static void
change_info_filter_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;

	p = info->priv;

	/* always add to filter, but dont let anyone else know */
	if (!g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		olduid = e_mempool_strdup(p->uid_pool, uid);
	}
	g_ptr_array_add(p->uid_filter, olduid);
}

static void
change_info_cat(CamelFolderChangeInfo *info, GPtrArray *source, void (*add)(CamelFolderChangeInfo *info, const char *uid))
{
	int i;

	for (i=0;i<source->len;i++)
		add(info, source->pdata[i]);
}


/**
 * camel_lite_folder_change_info_cat:
 * @info: a #CamelFolderChangeInfo to append to
 * @src: a #CamelFolderChangeInfo to append from
 *
 * Concatenate one change info onto antoher.  Can be used to copy them
 * too.
 **/
void
camel_lite_folder_change_info_cat(CamelFolderChangeInfo *info, CamelFolderChangeInfo *source)
{
	g_assert(info != NULL);
	g_assert(source != NULL);

	change_info_cat(info, source->uid_added, camel_lite_folder_change_info_add_uid);
	change_info_cat(info, source->uid_removed, camel_lite_folder_change_info_remove_uid);
	change_info_cat(info, source->uid_changed, camel_lite_folder_change_info_change_uid);
	change_info_cat(info, source->uid_recent, change_info_recent_uid);
	change_info_cat(info, source->priv->uid_filter, change_info_filter_uid);
}

/**
 * camel_lite_folder_change_info_add_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a new uid to the changeinfo.
 **/
void
camel_lite_folder_change_info_add_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;

	g_assert(info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended(p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was removed then added, promote it to a changed */
		/* if it was changed then added, leave as changed */
		if (olduids == info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_changed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_changed);
		}
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_added, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_added);
}


/**
 * camel_lite_folder_change_info_remove_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the removed uid list.
 **/
void
camel_lite_folder_change_info_remove_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;

	g_assert(info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended(p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_removed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_removed, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
}


/**
 * camel_lite_folder_change_info_change_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the changed uid list.
 **/
void
camel_lite_folder_change_info_change_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;

	g_assert(info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended(p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if we have it already, leave it as that */
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_changed, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_changed);
}


/**
 * camel_lite_folder_change_info_recent_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a recent uid to the changedinfo.
 * This will also add the uid to the uid_filter array for potential
 * filtering
 **/
void
camel_lite_folder_change_info_recent_uid(CamelFolderChangeInfo *info, const char *uid)
{
	g_assert(info != NULL);

	change_info_recent_uid(info, uid);
	change_info_filter_uid(info, uid);
}

/**
 * camel_lite_folder_change_info_changed:
 * @info: a #CamelFolderChangeInfo
 *
 * Gets whether or not there have been any changes.
 *
 * Returns %TRUE if the changeset contains any changes or %FALSE
 * otherwise
 **/
gboolean
camel_lite_folder_change_info_changed(CamelFolderChangeInfo *info)
{
	g_assert(info != NULL);

	return (info->uid_added->len || info->uid_removed->len || info->uid_changed->len || info->uid_recent->len);
}


/**
 * camel_lite_folder_change_info_clear:
 * @info: a #CamelFolderChangeInfo
 *
 * Empty out the change info; called after changes have been
 * processed.
 **/
void
camel_lite_folder_change_info_clear(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert(info != NULL);
	info->push_email_event = FALSE;
	p = info->priv;

	g_ptr_array_set_size(info->uid_added, 0);
	g_ptr_array_set_size(info->uid_removed, 0);
	g_ptr_array_set_size(info->uid_changed, 0);
	g_ptr_array_set_size(info->uid_recent, 0);
	if (p->uid_source) {
		g_hash_table_destroy(p->uid_source);
		p->uid_source = NULL;
	}
	g_hash_table_destroy(p->uid_stored);
	p->uid_stored = g_hash_table_new(g_str_hash, g_str_equal);
	g_ptr_array_set_size(p->uid_filter, 0);
	e_mempool_flush(p->uid_pool, TRUE);
}


/**
 * camel_lite_folder_change_info_free:
 * @info: a #CamelFolderChangeInfo
 *
 * Free memory associated with the folder change info lists.
 **/
void
camel_lite_folder_change_info_free(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert(info != NULL);

	p = info->priv;

	if (p->uid_source)
		g_hash_table_destroy(p->uid_source);

	g_hash_table_destroy(p->uid_stored);
	g_ptr_array_free(p->uid_filter, TRUE);
	e_mempool_destroy(p->uid_pool);
	g_slice_free (struct _CamelFolderChangeInfoPrivate, p);

	g_ptr_array_free(info->uid_added, TRUE);
	g_ptr_array_free(info->uid_removed, TRUE);
	g_ptr_array_free(info->uid_changed, TRUE);
	g_ptr_array_free(info->uid_recent, TRUE);
	g_slice_free (CamelFolderChangeInfo, info);
}
