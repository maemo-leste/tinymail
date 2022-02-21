/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000-2003 Ximian Inc.
 *
 * This is the mmap version of camel-folder-summary.c which maps
 * the header data into memory in stead of fread()ing it. It uses
 * the mmap() syscall for this.
 *
 *  Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Philip Van Hoof <pvanhoof@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libedataserver/e-iconv.h>
#include <libedataserver/e-memory.h>
#include <libedataserver/md5-utils.h>


#include "camel-file-utils.h"
#include "camel-folder-summary.h"
#include "camel-folder.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-html.h"
#include "camel-mime-filter-index.h"
#include "camel-mime-filter.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-private.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream-null.h"
#include "camel-string-utils.h"
#include "camel-disco-folder.h"



#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include <glib.h>
#include <glib/gprintf.h>

static pthread_mutex_t info_lock = PTHREAD_MUTEX_INITIALIZER;

/* this lock is ONLY for the standalone messageinfo stuff */
#define GLOBAL_INFO_LOCK(i) pthread_mutex_lock(&info_lock)
#define GLOBAL_INFO_UNLOCK(i) pthread_mutex_unlock(&info_lock)


/* this should probably be conditional on it existing */
#define USE_BSEARCH

#define d(x)
#define io(x)			/* io debug */
#define w(x)

#if 0
extern int strdup_count, malloc_count, free_count;
#endif

#define CAMEL_FOLDER_SUMMARY_VERSION (15)

#define _PRIVATE(o) (((CamelFolderSummary *)(o))->priv)

static GStaticRecMutex global_lock = G_STATIC_REC_MUTEX_INIT;
static GStaticMutex global_lock2 = G_STATIC_MUTEX_INIT;

/* trivial lists, just because ... */
struct _node {
	struct _node *next;
};

static struct _node *my_list_append(struct _node **list, struct _node *n);
static int my_list_size(struct _node **list);

static int summary_header_load(CamelFolderSummary *);
static int summary_header_save(CamelFolderSummary *, FILE *out);

static CamelMessageInfo * message_info_new_from_header(CamelFolderSummary *, struct _camel_lite_header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg);
static CamelMessageInfo * message_info_load(CamelFolderSummary *, gboolean *must_add);
static int		  message_info_save(CamelFolderSummary *, FILE *, CamelMessageInfo *);
static void		  message_info_free(CamelFolderSummary *, CamelMessageInfo *);
static void destroy_possible_pstring_stuff (CamelFolderSummary *, CamelMessageInfo *, gboolean);
static CamelMessageContentInfo * content_info_new_from_header(CamelFolderSummary *, struct _camel_lite_header_raw *);
static CamelMessageContentInfo * content_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageContentInfo * content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp);
static CamelMessageContentInfo * content_info_load(CamelFolderSummary *);
static int		         content_info_save(CamelFolderSummary *, FILE *, CamelMessageContentInfo *);
static void		         content_info_free(CamelFolderSummary *, CamelMessageContentInfo *);

static char *next_uid_string(CamelFolderSummary *s);

static CamelMessageContentInfo * summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp);
static CamelMessageContentInfo * summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object);

static void camel_lite_folder_summary_class_init (CamelFolderSummaryClass *klass);
static void camel_lite_folder_summary_init       (CamelFolderSummary *obj);
static void camel_lite_folder_summary_finalize   (CamelObject *obj);


static void camel_lite_folder_summary_mmap_add(CamelFolderSummary *s, CamelMessageInfo *info);
static void camel_lite_folder_summary_unload_mmap (CamelFolderSummary *s);

static CamelObjectClass *camel_lite_folder_summary_parent;


void
camel_lite_message_info_clear_normal_flags (CamelMessageInfo *min)
{

	CamelMessageInfoBase *mi = (CamelMessageInfoBase*) min;

	mi->flags &= ~CAMEL_MESSAGE_ANSWERED;
	mi->flags &= ~CAMEL_MESSAGE_DELETED;
	mi->flags &= ~CAMEL_MESSAGE_DRAFT;
	mi->flags &= ~CAMEL_MESSAGE_FLAGGED;
	mi->flags &= ~CAMEL_MESSAGE_SEEN;
	mi->flags &= ~CAMEL_MESSAGE_ATTACHMENTS;
	mi->flags &= ~CAMEL_MESSAGE_CACHED;
	mi->flags &= ~CAMEL_MESSAGE_PARTIAL;
	mi->flags &= ~CAMEL_MESSAGE_EXPUNGED;
	mi->flags &= ~CAMEL_MESSAGE_HIGH_PRIORITY;
	mi->flags &= ~CAMEL_MESSAGE_NORMAL_PRIORITY;
	mi->flags &= ~CAMEL_MESSAGE_LOW_PRIORITY;

	mi->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
}

static CamelMessageInfo*
find_message_info_with_uid (CamelFolderSummary *s, const char *uid)
{
	CamelMessageInfo *retval = NULL;
	guint i = 0;
	gboolean check_for = TRUE;

	if (uid == NULL || strlen (uid) <= 0)
		return NULL;

	g_mutex_lock (s->hash_lock);
	if (s->uidhash != NULL) {
		retval = g_hash_table_lookup (s->uidhash, uid);
		check_for = FALSE;
	}
	g_mutex_unlock (s->hash_lock);

	if (retval == NULL && check_for) {
		for (i=0; G_LIKELY (i < s->messages->len) ; i++)
		{
			CamelMessageInfo *info = s->messages->pdata[i];

			/* This can cause cache trashing */
			if (G_UNLIKELY (info->uid[0] == uid[0]) &&
			    G_UNLIKELY (!strcmp (info->uid, uid)))
			{
				retval = info;
				break;
			}
		}
	}

	return retval;
}

int
camel_lite_folder_summary_get_index_for (CamelFolderSummary *s, const char *uid)
{
	int retval = -1, i;

	if (uid == NULL || strlen (uid) == 0)
		return -1;

	for (i=0; G_LIKELY (i < s->messages->len) ; i++)
	{
		CamelMessageInfo *info = s->messages->pdata[i];

		/* This can cause cache trashing */
		if (G_UNLIKELY (info->uid[0] == uid[0]) &&
		    G_UNLIKELY (!strcmp (info->uid, uid)))
		{
			retval = i;
			break;
		}
	}

	return retval;
}

static void do_nothing (CamelFolder *folder, CamelMessageInfoBase *mi) { }

static void
camel_lite_folder_summary_init (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p;

	p = _PRIVATE(s) = g_slice_alloc0 (sizeof (*p));
	s->had_expunges = FALSE;
	p->filter_charset = g_hash_table_new (camel_lite_strcase_hash, camel_lite_strcase_equal);
	s->dump_lock = g_new0 (GStaticRecMutex, 1);
	g_static_rec_mutex_init (s->dump_lock);
	s->message_info_size = sizeof(CamelMessageInfoBase);
	s->content_info_size = sizeof(CamelMessageContentInfo);
	s->set_extra_flags_func = do_nothing;

#if defined (DOESTRV) || defined (DOEPOOLV)
	s->message_info_strings = CAMEL_MESSAGE_INFO_LAST;
#endif

	s->version = CAMEL_FOLDER_SUMMARY_VERSION;
	s->flags = 0;
	s->time = 0;
	s->nextuid = 1;
	s->in_reload = FALSE;

	s->messages = g_ptr_array_new();
	s->expunged = g_ptr_array_new();
	s->uidhash = NULL;
	s->hash_lock = g_mutex_new ();

	p->summary_lock = g_mutex_new();
	p->io_lock = g_mutex_new();
	p->filter_lock = g_mutex_new();
	p->ref_lock = g_mutex_new();
}

void
camel_lite_folder_summary_prepare_hash (CamelFolderSummary *s)
{
	guint i = 0;

	g_mutex_lock (s->hash_lock);

	if (s->uidhash == NULL)
	{
		s->uidhash = g_hash_table_new_full (g_str_hash, g_str_equal,
			(GDestroyNotify)g_free, NULL);

		for (i=0; G_LIKELY (i < s->messages->len) ; i++)
		{
			CamelMessageInfo *info = s->messages->pdata[i];
			g_hash_table_insert(s->uidhash, g_strdup (info->uid), info);
		}
	}

	g_mutex_unlock (s->hash_lock);

}

void
camel_lite_folder_summary_kill_hash (CamelFolderSummary *s)
{
	g_mutex_lock (s->hash_lock);
	if (s->uidhash != NULL)
		g_hash_table_destroy (s->uidhash);
	s->uidhash = NULL;
	g_mutex_unlock (s->hash_lock);
}


static void free_o_name(void *key, void *value, void *data)
{
	camel_lite_object_unref((CamelObject *)value);
	g_free(key);
}

static void
foreach_msginfo (gpointer data, gpointer user_data)
{
	//CamelMessageInfoBase *base = data;
	camel_lite_message_info_free (data);
	//base->summary = NULL;
}

static inline
gboolean always_true (gpointer key, gpointer value, gpointer gp)
{
	return TRUE;
}

static void
camel_lite_folder_summary_unload_mmap (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p;

	p = _PRIVATE(s);

	if (s->file)
		g_mapped_file_free (s->file);
	s->file = NULL;
	s->eof = NULL;

	return;
}

static void
camel_lite_folder_summary_finalize (CamelObject *obj)
{
	struct _CamelFolderSummaryPrivate *p;
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj);

	g_static_rec_mutex_lock (&global_lock);

	g_static_rec_mutex_lock (s->dump_lock);

	g_ptr_array_foreach (s->messages, foreach_msginfo, GSIZE_TO_POINTER (s->message_info_size));
	g_ptr_array_foreach (s->expunged, foreach_msginfo, GSIZE_TO_POINTER (s->message_info_size));

	g_ptr_array_free(s->messages, TRUE);
	g_ptr_array_free(s->expunged, TRUE);

	camel_lite_folder_summary_unload_mmap (s);

	g_static_rec_mutex_unlock (s->dump_lock);

	g_static_rec_mutex_unlock (&global_lock);

	/**/
	g_free (s->dump_lock);

	g_hash_table_foreach(p->filter_charset, free_o_name, NULL);
	g_hash_table_destroy(p->filter_charset);

	if (p->filter_index && CAMEL_IS_OBJECT (p->filter_index))
		camel_lite_object_unref((CamelObject *)p->filter_index);
	if (p->filter_64 && CAMEL_IS_OBJECT (p->filter_64))
		camel_lite_object_unref((CamelObject *)p->filter_64);
	if (p->filter_qp && CAMEL_IS_OBJECT (p->filter_qp))
		camel_lite_object_unref((CamelObject *)p->filter_qp);
	if (p->filter_uu && CAMEL_IS_OBJECT (p->filter_uu))
		camel_lite_object_unref((CamelObject *)p->filter_uu);
	if (p->filter_save && CAMEL_IS_OBJECT (p->filter_save))
		camel_lite_object_unref((CamelObject *)p->filter_save);
	if (p->filter_html && CAMEL_IS_OBJECT (p->filter_html))
		camel_lite_object_unref((CamelObject *)p->filter_html);
	if (p->filter_stream && CAMEL_IS_OBJECT (p->filter_stream))
		camel_lite_object_unref((CamelObject *)p->filter_stream);

	g_free(s->summary_path);

	if (s->uidhash != NULL)
		g_hash_table_destroy (s->uidhash);
	g_mutex_free(s->hash_lock);

	g_mutex_free(p->summary_lock);
	g_mutex_free(p->io_lock);
	g_mutex_free(p->filter_lock);
	g_mutex_free(p->ref_lock);


	g_slice_free1 (sizeof (*p), p);
}

CamelType
camel_lite_folder_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_object_get_type (), "CamelLiteFolderSummary",
					    sizeof (CamelFolderSummary),
					    sizeof (CamelFolderSummaryClass),
					    (CamelObjectClassInitFunc) camel_lite_folder_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_folder_summary_init,
					    (CamelObjectFinalizeFunc) camel_lite_folder_summary_finalize);
	}

	return type;
}


/**
 * camel_lite_folder_summary_new:
 * @folder: parent #CamelFolder object
 *
 * Create a new #CamelFolderSummary object.
 *
 * Returns a new #CamelFolderSummary object
 **/
CamelFolderSummary *
camel_lite_folder_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *new = CAMEL_FOLDER_SUMMARY ( camel_lite_object_new (camel_lite_folder_summary_get_type ()));
	new->folder = folder;
	return new;
}


/**
 * camel_lite_folder_summary_set_filename:
 * @summary: a #CamelFolderSummary object
 * @filename: a filename
 *
 * Set the filename where the summary will be loaded to/saved from.
 **/
void
camel_lite_folder_summary_set_filename(CamelFolderSummary *s, const char *name)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	g_free(s->summary_path);
	s->summary_path = g_strdup(name);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}




/**
 * camel_lite_folder_summary_set_build_content:
 * @summary: a #CamelFolderSummary object
 * @state: to build or not to build the content
 *
 * Set a flag to tell the summary to build the content info summary
 * (#CamelMessageInfo.content).  The default is not to build content
 * info summaries.
 **/
void
camel_lite_folder_summary_set_build_content(CamelFolderSummary *s, gboolean state)
{
	s->build_content = state;
}


/**
 * camel_lite_folder_summary_count:
 * @summary: a #CamelFolderSummary object
 *
 * Get the number of summary items stored in this summary.
 *
 * Returns the number of items in the summary
 **/
int
camel_lite_folder_summary_count(CamelFolderSummary *s)
{
	return s->messages->len;
}


/**
 * camel_lite_folder_summary_index:
 * @summary: a #CamelFolderSummary object
 * @index: item index
 *
 * Retrieve a summary item by index number.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns the summary item, or %NULL if @index is out of range
 **/
CamelMessageInfo *
camel_lite_folder_summary_index(CamelFolderSummary *s, int i)
{
	CamelMessageInfo *info = NULL;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	if (i<s->messages->len)
		info = g_ptr_array_index(s->messages, i);

	if (info)
		info->refcount++;

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return info;
}

#if 0
void
camel_lite_folder_summary_move_up (CamelFolderSummary *s)
{
	guint i = 0;
	gboolean first = TRUE;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	for (i = s->messages->len; i > 0 ; i--)
	{
		if (first) {
			g_ptr_array_add (s->messages, s->messages->pdata[i-1]);
			first = FALSE;
		} else
			s->messages->pdata[i+1] = s->messages->pdata[i];
	}

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

}

#endif
/**
 * camel_lite_folder_summary_array:
 * @summary: a #CamelFolderSummary object
 *
 * Obtain a copy of the summary array.  This is done atomically,
 * so cannot contain empty entries.
 *
 * It must be freed using #camel_lite_folder_summary_array_free.
 *
 * Returns a #GPtrArray of #CamelMessageInfo items
 **/
GPtrArray *
camel_lite_folder_summary_array(CamelFolderSummary *s)
{
	CamelMessageInfo *info;
	GPtrArray *res = g_ptr_array_new();
	int i;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	g_ptr_array_set_size(res, s->messages->len);
	for (i=0;i<s->messages->len;i++) {
		info = res->pdata[i] = g_ptr_array_index(s->messages, i);
		info->refcount++;
	}

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return res;
}


/**
 * camel_lite_folder_summary_array_free:
 * @summary: a #CamelFolderSummary object
 * @array: array of #CamelMessageInfo items as returned from #camel_lite_folder_summary_array
 *
 * Free the folder summary array.
 **/
void
camel_lite_folder_summary_array_free(CamelFolderSummary *s, GPtrArray *array)
{
	int i;

	/* FIXME: do the locking around the whole lot to make it faster */
	for (i=0;i<array->len;i++)
		camel_lite_message_info_free(array->pdata[i]);

	g_ptr_array_free(array, TRUE);
}


/**
 * camel_lite_folder_summary_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Retrieve a summary item by uid.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns the summary item, or %NULL if the uid @uid is not available
 **/
CamelMessageInfo *
camel_lite_folder_summary_uid(CamelFolderSummary *s, const char *uid)
{
	CamelMessageInfo *info;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	info = find_message_info_with_uid (s, uid);

	if (info)
		info->refcount++;

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return info;
}


/**
 * camel_lite_folder_summary_next_uid:
 * @summary: a #CamelFolderSummary object
 *
 * Generate a new unique uid value as an integer.  This
 * may be used to create a unique sequence of numbers.
 *
 * Returns the next unique uid value
 **/
guint32
camel_lite_folder_summary_next_uid(CamelFolderSummary *s)
{
	guint32 uid;


	CAMEL_SUMMARY_LOCK(s, summary_lock);

	uid = s->nextuid++;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	/* FIXME: sync this to disk */
/*	summary_header_save(s);*/
	return uid;
}


/**
 * camel_lite_folder_summary_set_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: The next minimum uid to assign.  To avoid clashing
 * uid's, set this to the uid of a given messages + 1.
 *
 * Set the next minimum uid available.  This can be used to
 * ensure new uid's do not clash with existing uid's.
 **/
void
camel_lite_folder_summary_set_uid(CamelFolderSummary *s, guint32 uid)
{
	s->nextuid = MAX(s->nextuid, uid);
}


/**
 * camel_lite_folder_summary_next_uid_string:
 * @summary: a #CamelFolderSummary object
 *
 * Retrieve the next uid, but as a formatted string.
 *
 * Returns the next uid as an unsigned integer string.
 * This string must be freed by the caller.
 **/
char *
camel_lite_folder_summary_next_uid_string(CamelFolderSummary *s)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->next_uid_string(s);
}

/* loads the content descriptions, recursively */
static CamelMessageContentInfo *
perform_content_info_load(CamelFolderSummary *s)
{
	int i;
	guint32 count;
	CamelMessageContentInfo *ci, *part;
	unsigned char *ptrchr = s->filepos;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_load (s);

	if (ci == NULL)
		return NULL;

	ptrchr = s->filepos;
	if (s->eof < (ptrchr + sizeof (guint32))) {
		d(fprintf (stderr, "Summary file format messed up?"));
		camel_lite_folder_summary_content_info_free (s, ci);
		return NULL;
	}
	ptrchr = camel_lite_file_util_mmap_decode_uint32 ((unsigned char*)ptrchr, &count, FALSE);
	s->filepos = ptrchr;

	if (count > 500)
	{
		camel_lite_folder_summary_content_info_free (s, ci);
		return NULL;
	}

	for (i=0;i<count;i++) {
		part = perform_content_info_load(s);
		if (part) {
			my_list_append((struct _node **)&ci->childs, (struct _node *)part);
			part->parent = ci;
		} else {
			d(fprintf (stderr, "Summary file format messed up?"));
			camel_lite_folder_summary_content_info_free (s, ci);
			return NULL;
		}
	}
	return ci;
}



/**
 * camel_lite_folder_summary_load:
 * @summary: a #CamelFolderSummary object
 *
 * Load the summary from disk.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_folder_summary_load(CamelFolderSummary *s)
{
	int i;
	CamelMessageInfo *mi;
	GError *err = NULL;
	gboolean ul = FALSE;

	if (s->summary_path == NULL || !g_file_test (s->summary_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
		return -1;

	CAMEL_SUMMARY_LOCK(s, io_lock);

	camel_lite_operation_start (NULL, "Opening summary of folder");

	if (!s->file)
	{
		s->file = g_mapped_file_new (s->summary_path, FALSE, &err);
		if (err != NULL)
		{
			g_critical ("Unable to mmap file: %s\n", err->message);
			g_error_free (err);
			goto error;
		}
	}

	s->filepos = (unsigned char*) g_mapped_file_get_contents (s->file);
	s->eof = s->filepos + g_mapped_file_get_length (s->file);

	if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s) == -1)
		goto error;


	if (s->messages && s->messages->len > s->saved_count)
	{
		int r, curlen = s->messages->len;
		for (r = curlen - 1; r >= s->saved_count - 1; r--)
		{
			CamelMessageInfo *ri = g_ptr_array_index (s->messages, r);
			if (ri) {
				((CamelMessageInfoBase *)ri)->flags |= CAMEL_MESSAGE_EXPUNGED;
printf ("Removes %s\n", ri->uid);
				camel_lite_folder_summary_remove (s, ri);
			}
		}
	}

	g_static_rec_mutex_lock (&global_lock);

	/* now read in each message ... */
	for (i=0; i < s->saved_count; i++)
	{
		gboolean must_add = FALSE;
		s->idx = i;

		ul = TRUE;

		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_load(s, &must_add);

		camel_lite_operation_progress (NULL, i , s->saved_count);

		if (mi == NULL)
			goto error;

		if (s->build_content)
		{
			if (((CamelMessageInfoBase *)mi)->content != NULL)
				camel_lite_folder_summary_content_info_free(s, ((CamelMessageInfoBase *)mi)->content);

			((CamelMessageInfoBase *)mi)->content = perform_content_info_load (s);
			if (((CamelMessageInfoBase *)mi)->content == NULL) {
				camel_lite_message_info_free(mi);
				goto error;
			}
		}

		if (must_add)
			camel_lite_folder_summary_mmap_add(s, mi);
	}

	g_static_rec_mutex_unlock (&global_lock);

	ul = FALSE;

	if (s->saved_count <= 0) {
		g_mapped_file_free (s->file);
		s->file = NULL;
		s->eof = NULL;
	}

	camel_lite_operation_end (NULL);

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	s->flags &= ~CAMEL_SUMMARY_DIRTY;


	return 0;

error:
	if (ul)
		g_static_rec_mutex_unlock (&global_lock);

	camel_lite_operation_end (NULL);

	if (errno != EINVAL)
		g_warning ("Cannot load summary file: `%s': %s", s->summary_path, g_strerror (errno));

	CAMEL_SUMMARY_UNLOCK(s, io_lock);
	s->flags |= ~CAMEL_SUMMARY_DIRTY;

	return -1;
}


/* saves the content descriptions, recursively */
static int
perform_content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *part;

	if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->content_info_save (s, out, ci) == -1)
		return -1;

	if (ci == NULL) {
		if (camel_lite_file_util_encode_uint32 (out, 0) == -1)
			return -1;
	} else if (camel_lite_file_util_encode_uint32 (out, my_list_size ((struct _node **)&ci->childs)) == -1)
		return -1;

	if (ci)
	{
		part = ci->childs;
		while (part) {
			if (perform_content_info_save (s, out, part) == -1)
				return -1;
			part = part->next;
		}
	}

	return 0;
}

static void 
flush_for_reload (CamelFolderSummary *s, CamelMessageInfoBase *mi)
{
	if (!(mi->flags & CAMEL_MESSAGE_INFO_NEEDS_FREE)) {
		mi->subject = "...";
		mi->to = "...";
		mi->from = "...";
		mi->cc = "...";
	}
} 

/**
 * camel_lite_folder_summary_save:
 * @summary: a #CamelFolderSummary object
 *
 * Writes the summary to disk.  The summary is only written if changes
 * have occured.
 *
 * Returns %0 on success or %-1 on fail
 **/

#ifdef SAVE_APPEND
static int
camel_lite_folder_summary_save_append (CamelFolderSummary *s, CamelException *ex)
{
	FILE *out;
	int i;
	guint32 count = 0;
	CamelMessageInfo *mi;
	char *path;
	gboolean herr = FALSE;
	gboolean hadhash, create_header = FALSE;;

	g_static_rec_mutex_lock (s->dump_lock);

	hadhash = (s->uidhash != NULL);

	g_assert(s->message_info_size >= sizeof(CamelMessageInfoBase));

	if (s->summary_path == NULL || (s->flags & CAMEL_SUMMARY_DIRTY) == 0) {
		g_static_rec_mutex_unlock (s->dump_lock);
		return 0;
	}

	path = alloca(strlen(s->summary_path)+4);
	sprintf(path, "%s", s->summary_path);

	/* We are going to append to a RO mapped file in WRONLY APPEND.
	 * That's a bit tricky, but fun :) (and we know that we are doing it) */

	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		create_header = TRUE;

	out = fopen(path, "a");
	if (out == NULL) {
		perror ("S");
		i = errno;
		g_unlink(path);
		errno = i;
 		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			"Error storing the summary");
		g_static_rec_mutex_unlock (s->dump_lock);
		return -1;
	}

	io(printf("saving header\n"));

	CAMEL_SUMMARY_LOCK(s, io_lock);

	if (create_header) {
		if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out) == -1)
			goto haerror;
	}

	/* now write out each message ... */
	/* we check ferorr when done for i/o errors */

	count = s->messages->len;

	for (i = 0; i < count; i++) {
		mi = s->messages->pdata[i];

		/* Only the new ones must be written (those have that funny 
		 * internal flag set) */

		if (!create_header && ((CamelMessageInfoBase *)mi)->flags & CAMEL_MESSAGE_INFO_NEEDS_FREE) {

			if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->message_info_save (s, out, mi) == -1) {
				herr = TRUE;
				goto haerror;
			}

			if (s->build_content) {
				if (perform_content_info_save (s, out, ((CamelMessageInfoBase *)mi)->content) == -1) {
					herr = TRUE;
					goto haerror;
				}
			}
		}

	}

	if (fflush (out) != 0 || fsync (fileno (out)) == -1)
		herr = TRUE;

haerror:

	if (s->build_content)
		for (i = 0; i < count; i++) {
			mi = s->messages->pdata[i];
			if (((CamelMessageInfoBase *)mi)->content != NULL)
				camel_lite_folder_summary_content_info_free (s, ((CamelMessageInfoBase *)mi)->content);
			((CamelMessageInfoBase *)mi)->content = NULL;
		}

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	if (herr)
		goto exception;

	fclose (out);
	s->in_reload = TRUE;

	if (!hadhash)
		camel_lite_folder_summary_prepare_hash (s);

	g_static_rec_mutex_lock (&global_lock);
	g_static_mutex_lock (&global_lock2);

	for (i = 0; i < count; i++) {
		mi = s->messages->pdata[i];
		flush_for_reload (s, (CamelMessageInfoBase *) mi);
	}

	/* Remap the appended file :). Moeha! */
	camel_lite_folder_summary_unload_mmap (s);

	out = fopen(path, "r+");
	if (out) {
		((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out);
		fclose (out);
	}

	camel_lite_folder_summary_load (s);

	g_static_mutex_unlock (&global_lock2);
	g_static_rec_mutex_unlock (&global_lock);

	if (!hadhash)
		camel_lite_folder_summary_kill_hash (s);

	s->in_reload = FALSE;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;
	g_static_rec_mutex_unlock (s->dump_lock);

	return 0;

exception:

	camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
		"Error storing the summary");
	i = errno;
	fclose (out);
	g_unlink (path);
	errno = i;
	g_static_rec_mutex_unlock (s->dump_lock);

	return -1;
}
#endif

static int
camel_lite_folder_summary_save_rewrite (CamelFolderSummary *s, CamelException *ex)
{
	FILE *out;
	int fd, i;
	guint32 count = 0;
	CamelMessageInfo *mi;
	char *path;
	gboolean herr = FALSE;
	gboolean hadhash;

	g_static_rec_mutex_lock (s->dump_lock);

	hadhash = (s->uidhash != NULL);

	g_assert(s->message_info_size >= sizeof(CamelMessageInfoBase));

	if (s->summary_path == NULL || (s->flags & CAMEL_SUMMARY_DIRTY) == 0) {
		g_static_rec_mutex_unlock (s->dump_lock);
		return 0;
	}

	path = alloca(strlen(s->summary_path)+4);
	sprintf(path, "%s~", s->summary_path);
	fd = g_open(path, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);

	if (fd == -1) {
	  g_static_rec_mutex_unlock (s->dump_lock);
	  camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
		"Error storing the summary");
	  return -1;
	}

	out = fdopen(fd, "ab");
	if (out == NULL) {
		i = errno;
		g_unlink(path);
		close(fd);
		errno = i;
 		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			"Error storing the summary");
		g_static_rec_mutex_unlock (s->dump_lock);
		return -1;
	}

	io(printf("saving header\n"));

	CAMEL_SUMMARY_LOCK(s, io_lock);

	if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out) == -1)
		goto haerror;

	/* now write out each message ... */
	/* we check ferorr when done for i/o errors */

	count = s->messages->len;

	for (i = 0; i < count; i++) {
		mi = s->messages->pdata[i];

		if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->message_info_save (s, out, mi) == -1) {
			herr = TRUE;
			goto haerror;
		}

		if (s->build_content) {
			if (perform_content_info_save (s, out, ((CamelMessageInfoBase *)mi)->content) == -1) {
				herr = TRUE;
				goto haerror;
			}
		}
	}

	if (fflush (out) != 0 || fsync (fileno (out)) == -1)
		herr = TRUE;

haerror:

	if (s->build_content)
		for (i = 0; i < count; i++) {
			mi = s->messages->pdata[i];
			if (((CamelMessageInfoBase *)mi)->content != NULL)
				camel_lite_folder_summary_content_info_free (s, ((CamelMessageInfoBase *)mi)->content);
			((CamelMessageInfoBase *)mi)->content = NULL;
		}

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	if (herr)
		goto exception;

	fclose (out);
	s->in_reload = TRUE;

	if (!hadhash)
		camel_lite_folder_summary_prepare_hash (s);

	g_static_rec_mutex_lock (&global_lock);
	g_static_mutex_lock (&global_lock2);

	camel_lite_folder_summary_unload_mmap (s);


#ifdef G_OS_WIN32
	g_unlink(s->summary_path); 
#endif

	if (g_rename(path, s->summary_path) == -1) {
		i = errno;
		g_unlink(path);
		errno = i;
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			"Error storing the summary");
		g_static_mutex_unlock (&global_lock2);
		g_static_rec_mutex_unlock (&global_lock);
		g_static_rec_mutex_unlock (s->dump_lock);
		return -1;
	}

	camel_lite_folder_summary_load (s);

	g_static_mutex_unlock (&global_lock2);
	g_static_rec_mutex_unlock (&global_lock);

	if (!hadhash)
		camel_lite_folder_summary_kill_hash (s);

	s->in_reload = FALSE;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;
	g_static_rec_mutex_unlock (s->dump_lock);

	return 0;

exception:

	camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
		"Error storing the summary");
	i = errno;
	fclose (out);
	g_unlink (path);
	errno = i;
	g_static_rec_mutex_unlock (s->dump_lock);

	return -1;
}

int
camel_lite_folder_summary_save (CamelFolderSummary *s, CamelException *ex)
{
	int retval;
#ifdef SAVE_APPEND
	if (s->had_expunges)
#endif
		retval = camel_lite_folder_summary_save_rewrite (s, ex);
#ifdef SAVE_APPEND
	else
		retval = camel_lite_folder_summary_save_append (s, ex);
#endif

	s->had_expunges = FALSE;

	return retval;
}

/**
 * camel_lite_folder_summary_header_load:
 * @summary: a #CamelFolderSummary object
 *
 * Only load the header information from the summary,
 * keep the rest on disk.  This should only be done on
 * a fresh summary object.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_folder_summary_header_load(CamelFolderSummary *s)
{
	int ret;
	GError *err = NULL;

	if (s->summary_path == NULL || !g_file_test (s->summary_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
		return -1;

	CAMEL_SUMMARY_LOCK(s, io_lock);

	if (!s->file)
	{
		s->file = g_mapped_file_new (s->summary_path, FALSE, &err);
		if (err != NULL)
		{
			g_critical ("Unable to mmap file: %s\n", err->message);
			g_error_free (err);
			return -1;
		}
	}

	s->filepos = (unsigned char*) g_mapped_file_get_contents (s->file);
	s->eof = s->filepos + g_mapped_file_get_length (s->file);

	ret = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s);

	s->flags &= ~CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	return ret;
}

static int
summary_assign_uid(CamelFolderSummary *s, CamelMessageInfo *info)
{
	const char *uid;
	CamelMessageInfo *mi;

	uid = camel_lite_message_info_uid (info);

	if (uid == NULL || uid[0] == 0) {
		g_free (info->uid);
		uid = info->uid = camel_lite_folder_summary_next_uid_string(s);
	}

	while ((mi = find_message_info_with_uid (s, uid))) {
		if (mi == info)
			return 0;

		d(printf ("Trying to insert message with clashing uid (%s).  new uid re-assigned", camel_lite_message_info_uid(info)));

		g_free(info->uid);
		uid = info->uid = camel_lite_folder_summary_next_uid_string(s);

		camel_lite_message_info_set_flags(info, CAMEL_MESSAGE_FOLDER_FLAGGED, CAMEL_MESSAGE_FOLDER_FLAGGED);
	}


	return 1;
}


/**
 * camel_lite_folder_summary_add:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * #camel_lite_folder_summary_next_uid_string.
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void
camel_lite_folder_summary_add(CamelFolderSummary *s, CamelMessageInfo *info)
{

	g_static_rec_mutex_lock (s->dump_lock);

	if (info == NULL) {
		g_static_rec_mutex_unlock (s->dump_lock);
		return;
	}

	if (summary_assign_uid(s, info) == 0) {
		g_static_rec_mutex_unlock (s->dump_lock);
		return;
	}

	CAMEL_SUMMARY_LOCK(s, summary_lock);

/* unnecessary for pooled vectors */
#ifdef DOESTRV
	/* this is vitally important, and also if this is ever modified, then
	   the hash table needs to be resynced */
	info->strings = e_strv_pack(info->strings);
#endif

	g_ptr_array_add(s->messages, info);

	g_mutex_lock (s->hash_lock);
	if (s->uidhash != NULL)
		g_hash_table_insert (s->uidhash, g_strdup (info->uid), info);
	g_mutex_unlock (s->hash_lock);

	s->flags |= CAMEL_SUMMARY_DIRTY;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	g_static_rec_mutex_unlock (s->dump_lock);
}

static void
camel_lite_folder_summary_mmap_add(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);

/* unnecessary for pooled vectors */
#ifdef DOESTRV
	/* this is vitally important, and also if this is ever modified, then
	   the hash table needs to be resynced */
	info->strings = e_strv_pack(info->strings);
#endif

	g_ptr_array_add(s->messages, info);

	g_mutex_lock (s->hash_lock);
	if (s->uidhash != NULL)
		g_hash_table_insert (s->uidhash, g_strdup (info->uid), info);
	g_mutex_unlock (s->hash_lock);

	s->flags |= CAMEL_SUMMARY_DIRTY;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_lite_folder_summary_add_from_header:
 * @summary: a #CamelFolderSummary object
 * @headers: rfc822 headers
 *
 * Build a new info record based on a set of headers, and add it to
 * the summary.
 *
 * Note that this function should not be used if build_content_info
 * has been specified for this summary.
 *
 * Returns the newly added record
 **/
CamelMessageInfo *
camel_lite_folder_summary_add_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h, const gchar *uid)
{
	CamelMessageInfo *info = camel_lite_folder_summary_info_new_from_header(s, h);

	if (info->uid)
		g_free (info->uid);
	info->uid = g_strdup (uid);

	camel_lite_folder_summary_add(s, info);

	return info;
}


/**
 * camel_lite_folder_summary_add_from_parser:
 * @summary: a #CamelFolderSummary object
 * @parser: a #CamelMimeParser object
 *
 * Build a new info record based on the current position of a #CamelMimeParser.
 *
 * The parser should be positioned before the start of the message to summarise.
 * This function may be used if build_contnet_info or an index has been
 * specified for the summary.
 *
 * Returns the newly added record
 **/
CamelMessageInfo *
camel_lite_folder_summary_add_from_parser(CamelFolderSummary *s, CamelMimeParser *mp, const gchar *uid)
{
	CamelMessageInfo *info;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (s), NULL);
	g_return_val_if_fail (CAMEL_IS_MIME_PARSER (mp), NULL);

	info = camel_lite_folder_summary_info_new_from_parser(s, mp);

	if (info->uid)
		g_free (info->uid);
	info->uid = g_strdup (uid);

	camel_lite_folder_summary_add(s, info);

	return info;
}


/**
 * camel_lite_folder_summary_add_from_message:
 * @summary: a #CamelFolderSummary object
 * @message: a #CamelMimeMessage object
 *
 * Add a summary item from an existing message.
 *
 * Returns the newly added record
 **/
CamelMessageInfo *
camel_lite_folder_summary_add_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *info = camel_lite_folder_summary_info_new_from_message(s, msg);

	if (!info->uid) 
		info->uid = camel_lite_folder_summary_next_uid_string (s);

	camel_lite_folder_summary_add(s, info);

	return info;
}


/**
 * camel_lite_folder_summary_info_new_from_header:
 * @summary: a #CamelFolderSummary object
 * @headers: rfc822 headers
 *
 * Create a new info record from a header.
 *
 * Returns the newly allocated record which must be freed with
 * #camel_lite_message_info_free
 **/
CamelMessageInfo *
camel_lite_folder_summary_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, h);
}

CamelMessageInfo *
camel_lite_folder_summary_info_new_from_header_with_uid (CamelFolderSummary *s, struct _camel_lite_header_raw *h, const gchar *uid)
{

	CamelMessageInfo *mi;
	CamelMessageInfoBase *bi;

	if (s != NULL)
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, h);
	else
		mi = message_info_new_from_header(s, h);

	bi = (CamelMessageInfoBase *)mi;

	g_static_rec_mutex_lock (&global_lock);
	if (bi->uid)
		g_free (bi->uid);
	((CamelMessageInfoBase *)mi)->uid = g_strdup (uid);
	g_static_rec_mutex_unlock (&global_lock);

	return mi;
}

/**
 * camel_lite_folder_summary_info_new_from_parser:
 * @summary: a #CamelFolderSummary object
 * @parser: a #CamelMimeParser object
 *
 * Create a new info record from a parser.  If the parser cannot
 * determine a uid, then none will be assigned.
 *
 * If indexing is enabled, and the parser cannot determine a new uid, then
 * one is automatically assigned.
 *
 * If indexing is enabled, then the content will be indexed based
 * on this new uid.  In this case, the message info MUST be
 * added using :add().
 *
 * Once complete, the parser will be positioned at the end of
 * the message.
 *
 * Returns the newly allocated record which must be freed with
 * #camel_lite_message_info_free
 **/
CamelMessageInfo *
camel_lite_folder_summary_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *info = NULL;
	char *buffer;
	size_t len;
	off_t start;

	/* should this check the parser is in the right state, or assume it is?? */

	start = camel_lite_mime_parser_tell(mp);
	if (camel_lite_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_parser(s, mp);
		camel_lite_mime_parser_unstep(mp);
		((CamelMessageInfoBase *)info)->size = ( (camel_lite_mime_parser_tell(mp) - start) );
	}
	return info;
}


/**
 * camel_lite_folder_summary_info_new_from_message:
 * @summary: a #CamelFodlerSummary object
 * @message: a #CamelMimeMessage object
 *
 * Create a summary item from a message.
 *
 * Returns the newly allocated record which must be freed using
 * #camel_lite_message_info_free
 **/
CamelMessageInfo *
camel_lite_folder_summary_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *info;

	if (s != NULL)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_message(s, msg);
	else
		info = message_info_new_from_message(s, msg);

	((CamelMessageInfoBase *)info)->content = NULL;

/*
	if (s!=NULL)
		CAMEL_SUMMARY_LOCK(s, filter_lock);

	((CamelMessageInfoBase *)info)->content = summary_build_content_info_message(s, info, (CamelMimePart *)msg);


	if (s!=NULL)
		CAMEL_SUMMARY_UNLOCK(s, filter_lock);
*/

	return info;
}


/**
 * camel_lite_folder_summary_content_info_free:
 * @summary: a #CamelFolderSummary object
 * @ci: a #CamelMessageContentInfo
 *
 * Free the content info @ci, and all associated memory.
 **/
void
camel_lite_folder_summary_content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *pw, *pn;

	pw = ci->childs;

	if (s != NULL)
		((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_free(s, ci);
	else
		content_info_free (NULL, ci);

	while (pw) {
		pn = pw->next;
		camel_lite_folder_summary_content_info_free(s, pw);
		pw = pn;
	}
}


/**
 * camel_lite_folder_summary_touch:
 * @summary: a #CamelFolderSummary object
 *
 * Mark the summary as changed, so that a save will force it to be
 * written back to disk.
 **/
void
camel_lite_folder_summary_touch(CamelFolderSummary *s)
{
	s->flags |= CAMEL_SUMMARY_DIRTY;
}


/**
 * camel_lite_folder_summary_clear:
 * @summary: a #CamelFolderSummary object
 *
 * Empty the summary contents.
 **/
void
camel_lite_folder_summary_clear(CamelFolderSummary *s)
{
	int i;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (camel_lite_folder_summary_count(s) == 0) {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		return;
	}

	for (i=0;i<s->messages->len;i++)
		camel_lite_message_info_free(s->messages->pdata[i]);

	g_ptr_array_set_size(s->messages, 0);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

void
camel_lite_folder_summary_dispose_all (CamelFolderSummary *s)
{
	int i;

	if (s->messages && s->messages->len > 0)
	{
		GPtrArray *items = g_ptr_array_sized_new (s->messages->len);

		CAMEL_SUMMARY_LOCK(s, summary_lock);
		g_static_rec_mutex_lock (&global_lock);

		for (i=0; i<s->messages->len; i++) {
			CamelMessageInfo *info = (CamelMessageInfo *) s->messages->pdata[i];
			CamelMessageInfoBase *mi = (CamelMessageInfoBase *) s->messages->pdata[i];

			mi->flags |= CAMEL_MESSAGE_EXPUNGED;
			mi->flags |= CAMEL_MESSAGE_FREED;
			destroy_possible_pstring_stuff (s, info, FALSE);
			mi->subject = "Expunged";
			mi->to = "Expunged";
			mi->from = "Expunged";
			mi->cc = "Expunged";

			g_ptr_array_add (items, info);
		}

		for (i=0; i<items->len; i++) {
			CamelMessageInfo *info = (CamelMessageInfo *) items->pdata[i];
			g_ptr_array_remove(s->messages, info);
			g_ptr_array_add (s->expunged, info);
		}

		s->had_expunges = TRUE;
		s->flags |= CAMEL_SUMMARY_DIRTY;

		g_static_rec_mutex_unlock (&global_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		g_ptr_array_free (items, TRUE);

	}
}

/**
 * camel_lite_folder_summary_remove:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Remove a specific @info record from the summary.
 **/
void
camel_lite_folder_summary_remove(CamelFolderSummary *s, CamelMessageInfo *info)
{
	g_static_rec_mutex_lock (s->dump_lock);

	if (((CamelMessageInfoBase*)info)->flags & CAMEL_MESSAGE_EXPUNGED)
	{
		CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;

		CAMEL_SUMMARY_LOCK(s, summary_lock);
		g_static_rec_mutex_lock (&global_lock);
		g_ptr_array_remove(s->messages, info);
		g_ptr_array_add (s->expunged, info);
		/* NOTE! XUI */
		destroy_possible_pstring_stuff (s, info, FALSE);
		mi->subject = "Expunged";
		mi->to = "Expunged";
		mi->from = "Expunged";
		mi->cc = "Expunged";
		s->had_expunges = TRUE;
		s->flags |= CAMEL_SUMMARY_DIRTY;
		g_static_rec_mutex_unlock (&global_lock);

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
	} else {
		CAMEL_SUMMARY_LOCK(s, summary_lock);
		g_ptr_array_remove(s->messages, info);
		s->had_expunges = TRUE;
		s->flags |= CAMEL_SUMMARY_DIRTY;
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_lite_message_info_free(info);
	}

	g_static_rec_mutex_unlock (s->dump_lock);
}


/**
 * camel_lite_folder_summary_remove_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Remove a specific info record from the summary, by @uid.
 **/
void
camel_lite_folder_summary_remove_uid(CamelFolderSummary *s, const char *uid)
{
	CamelMessageInfo *oldinfo = NULL;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	oldinfo = find_message_info_with_uid (s, uid);

	if (oldinfo) {
		/* make sure it doesn't vanish while we're removing it */
		oldinfo->refcount++;
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_lite_folder_summary_remove(s, oldinfo);
		camel_lite_message_info_free(oldinfo);
	} else {
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
	}
}


/**
 * camel_lite_folder_summary_remove_index:
 * @summary: a #CamelFolderSummary object
 * @index: record index
 *
 * Remove a specific info record from the summary, by index.
 **/
void
camel_lite_folder_summary_remove_index(CamelFolderSummary *s, int index)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (index < s->messages->len) {
		CamelMessageInfo *info = s->messages->pdata[index];

		s->had_expunges = TRUE;
		g_ptr_array_remove_index(s->messages, index);
		s->flags |= CAMEL_SUMMARY_DIRTY;

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_lite_message_info_free(info);
	} else {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
	}
}


/**
 * camel_lite_folder_summary_remove_range:
 * @summary: a #CamelFolderSummary object
 * @start: initial index
 * @end: last index to remove
 *
 * Removes an indexed range of info records.
 **/
void
camel_lite_folder_summary_remove_range(CamelFolderSummary *s, int start, int end)
{
	if (end < start)
		return;

	g_static_rec_mutex_lock (s->dump_lock);
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (start < s->messages->len) {
		CamelMessageInfo **infos;
		int i;

		end = MIN(end+1, s->messages->len);
		infos = g_malloc((end-start)*sizeof(infos[0]));

		memmove(s->messages->pdata+start, s->messages->pdata+end, (s->messages->len-end)*sizeof(s->messages->pdata[0]));
		g_ptr_array_set_size(s->messages, s->messages->len - (end - start));
		s->flags |= CAMEL_SUMMARY_DIRTY;

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		for (i=start;i<end;i++)
			camel_lite_message_info_free(infos[i-start]);
		g_free(infos);
	} else {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
	}

	g_static_rec_mutex_unlock (s->dump_lock);
}

/* should be sorted, for binary search */
/* This is a tokenisation mechanism for strings written to the
   summary - to save space.
   This list can have at most 31 words. */
static char * tokens[] = {
	"7bit",
	"8bit",
	"alternative",
	"application",
	"base64",
	"boundary",
	"charset",
	"filename",
	"html",
	"image",
	"iso-8859-1",
	"iso-8859-8",
	"message",
	"mixed",
	"multipart",
	"name",
	"octet-stream",
	"parallel",
	"plain",
	"postscript",
	"quoted-printable",
	"related",
	"rfc822",
	"text",
	"us-ascii",		/* 25 words */
};

#define tokens_len (sizeof(tokens)/sizeof(tokens[0]))

guint bytes;
static gchar*
token_add (gchar *str)
{
	int i;

	if (!str)
		return NULL;

	for (i=0; i<tokens_len; i++)
		if (!strcmp (str, tokens[i]))
		{
			g_free (str);
			return tokens[i];
		}

	return (gchar*) camel_lite_pstring_add (str, FALSE);
}


static void
token_free (gchar *token)
{
        gint i;

	if (!token)
		return;

        for (i = 0; i < tokens_len; i ++)
                if (tokens[i] == token)
                        return;

        camel_lite_pstring_free (token);
}

/* baiscally ...
    0 = null
    1-tokens_len == tokens[id-1]
    >=32 string, length = n-32
*/

#ifdef USE_BSEARCH
static int
token_search_cmp(char *key, char **index)
{
	d(printf("comparing '%s' to '%s'\n", key, *index));
	return strcmp(key, *index);
}
#endif

/**
 * camel_lite_folder_summary_encode_token:
 * @out: output FILE pointer
 * @str: string token to encode
 *
 * Encode a string value, but use tokenisation and compression
 * to reduce the size taken for common mailer words.  This
 * can still be used to encode normal strings as well.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_folder_summary_encode_token(FILE *out, const char *str)
{
	io(printf("Encoding token: '%s'\n", str));

	if (str == NULL) {
		return camel_lite_file_util_encode_uint32(out, 0);
	} else {
		int len = strlen(str);
		int i, token=-1;

		if (len <= 16) {
			char lower[32];
			char **match;

			for (i=0;i<len;i++)
				lower[i] = tolower(str[i]);
			lower[i] = 0;
#ifdef USE_BSEARCH
			match = bsearch(lower, tokens, tokens_len, sizeof(char *), (int (*)(const void *, const void *))token_search_cmp);
			if (match)
				token = match-tokens;
#else
			for (i=0;i<tokens_len;i++) {
				if (!strcmp(tokens[i], lower)) {
					token = i;
					break;
				}
			}
#endif
		}
		if (token != -1) {
			return camel_lite_file_util_encode_uint32(out, token+1);
		} else {
			int lena;

			lena = len = strlen (str) + 1;

			if (lena % G_MEM_ALIGN)
				lena += G_MEM_ALIGN - (lena % G_MEM_ALIGN);

			if (camel_lite_file_util_encode_uint32(out, lena+32) == -1)
				return -1;

			if (fwrite (str, len, 1, out) == 1) {
				if (lena > len) {
					if (fwrite ("\0\0\0\0\0\0\0\0", lena-len, 1, out) == 1)
						return 0;
					else return -1;
				}
			} else
				return -1;
		}
	}

	return 0;
}


/**
 * camel_lite_folder_summary_decode_token:
 * @in: input FILE pointer
 * @str: string pointer to hold the decoded result
 *
 * Decode a token value.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_folder_summary_decode_token(CamelFolderSummary *s, char **str)
{
	char *ret;
	guint32 len;
	unsigned char *ptrchr = s->filepos;

	io(printf("Decode token ...\n"));

	if (s->eof < ptrchr + sizeof (guint32)) {
		io(printf ("Got premature EOF"));
		*str = NULL;
		return -1;
	}
	ptrchr = camel_lite_file_util_mmap_decode_uint32 ((unsigned char*)ptrchr, &len, FALSE);

	if (s->eof < ptrchr + len) {
		io(printf ("Got premature EOF"));
		*str = NULL;
		return -1;
	}

	if (len<32) {

		if (len <= 0) {
			ret = NULL;
		} else if (len<= tokens_len) {
			ret = tokens[len-1];
		} else {
			io(printf ("Invalid token encountered: %d", len));
			*str = NULL;
			return -1;
		}
	} else if (len > 10240) {
		io(printf ("Got broken string header length: %d bytes", len));
		*str = NULL;
		return -1;
	} else {

		len -= 32;

		if (len != 0)
			ret = (char*)ptrchr;
		else ret = NULL;

		ptrchr += len;
	}

	s->filepos = ptrchr;

	*str = ret;
	return 0;
}

static struct _node *
my_list_append(struct _node **list, struct _node *n)
{
	struct _node *ln = (struct _node *)list;
	while (ln->next)
		ln = ln->next;
	n->next = NULL;
	ln->next = n;
	return n;
}

static int
my_list_size(struct _node **list)
{
	int len = 0;
	struct _node *ln = (struct _node *)list;
	while (ln->next) {
		ln = ln->next;
		len++;
	}
	return len;
}

static int
summary_header_load(CamelFolderSummary *s)
{
	s->version = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	io(printf("Loading header %d", (s->version&0xff)));

	/* Legacy version check, before version 13 we have no upgrade knowledge */
	if ((s->version > 0xff) && (s->version & 0xff) < 12) {
		io(printf ("Summary header version mismatch"));
		errno = EINVAL;
		return -1;
	}

	/* Check for MMAPable file */
	if (s->version != CAMEL_FOLDER_SUMMARY_VERSION) {
		errno = EINVAL;
		return -1;
	}

	s->flags = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	s->nextuid = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	s->time = (time_t)g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	s->saved_count = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	if (s->version < 0x100 && s->version >= 13) {
		s->unread_count = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
		s->deleted_count = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
		s->junk_count = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
	}

	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	int unread = 0, deleted = 0, junk = 0, count, i;

	fseek(out, 0, SEEK_SET);

	io(printf("Savining header\n"));

	/* we always write out the current version */
	if (camel_lite_file_util_encode_fixed_int32(out, CAMEL_FOLDER_SUMMARY_VERSION) == -1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, s->flags) == -1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, s->nextuid) == -1) return -1;
	if (camel_lite_file_util_encode_time_t(out, s->time) == -1) return -1;

	count = camel_lite_folder_summary_count(s);
	for (i=0; i<count; i++) {
		CamelMessageInfo *info = camel_lite_folder_summary_index(s, i);
		guint32 flags;

		if (info == NULL)
			continue;

		flags = camel_lite_message_info_flags(info);
		if ((flags & CAMEL_MESSAGE_SEEN) == 0)
			unread++;
		if ((flags & CAMEL_MESSAGE_DELETED) != 0)
			deleted++;

		camel_lite_message_info_free(info);
	}

	if (camel_lite_file_util_encode_fixed_int32(out, count) == -1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, unread) == -1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, deleted) == -1) return -1;

	return camel_lite_file_util_encode_fixed_int32(out, junk);
}

/* are these even useful for anything??? */
static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi = NULL;
	int state;

	state = camel_lite_mime_parser_state(mp);
	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, camel_lite_mime_parser_headers_raw(mp));
		break;
	default:
		g_error("Invalid parser state");
	}

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageContentInfo *ci = NULL;

	switch (camel_lite_mime_parser_state(mp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_header(s, camel_lite_mime_parser_headers_raw(mp));
		if (ci) {
			ci->type = camel_lite_mime_parser_content_type(mp);
			camel_lite_content_type_ref(ci->type);
		}
		break;
	default:
		g_error("Invalid parser state");
	}

	return ci;
}

static CamelMessageInfo *
message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *mi;

	if (s != NULL)
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, ((CamelMimePart *)msg)->headers);
	else
		mi = message_info_new_from_header(s, ((CamelMimePart *)msg)->headers);

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp)
{
	CamelMessageContentInfo *ci;

	if (s != NULL)
		ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_header(s, mp->headers);
	else
		ci = content_info_new_from_header(NULL, mp->headers);

	return ci;
}

static char *
summary_format_address(struct _camel_lite_header_raw *h, const char *name, const char *charset)
{
	struct _camel_lite_header_address *addr;
	const char *text;
	char *ret;

	text = camel_lite_header_raw_find (&h, name, NULL);
	addr = camel_lite_header_address_decode (text, charset);
	if (addr) {
		ret = camel_lite_header_address_list_format (addr);
		camel_lite_header_address_list_clear (&addr);
	} else {
		ret = g_strdup (text);
	}

	return ret;
}

static char *
summary_format_string (struct _camel_lite_header_raw *h, const char *name, const char *charset)
{
	const char *text;

	text = camel_lite_header_raw_find (&h, name, NULL);

	if (text) {
		while (isspace ((unsigned) *text))
			text++;
		return camel_lite_header_decode_string (text, charset);
	} else {
		return NULL;
	}
}


/**
 * camel_lite_folder_summary_content_info_new:
 * @summary: a #CamelFolderSummary object
 *
 * Allocate a new #CamelMessageContentInfo, suitable for adding
 * to this summary.
 *
 * Returns a newly allocated #CamelMessageContentInfo
 **/
CamelMessageContentInfo *
camel_lite_folder_summary_content_info_new(CamelFolderSummary *s)
{
	if (s != NULL)
		return g_slice_alloc0(s->content_info_size);
	return g_slice_new0 (CamelMessageContentInfo);
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	CamelMessageInfoBase *mi;
	const char *received;
	guchar digest[16];
	char *msgid = NULL;
	char *subject, *from, *to, *cc;
	CamelContentType *ct = NULL;
	const char *content, *charset = NULL;
	const char *prio = NULL;
	const char *attach = NULL;

	mi = (CamelMessageInfoBase *)camel_lite_message_info_new(s);
	mi->flags |= CAMEL_MESSAGE_INFO_NEEDS_FREE;

	if ((content = camel_lite_header_raw_find(&h, "Content-Type", NULL))
	     && (ct = camel_lite_content_type_decode(content))
	     && (charset = camel_lite_content_type_param(ct, "charset"))
	     && (g_ascii_strcasecmp(charset, "us-ascii") == 0))
		charset = NULL;

	charset = charset ? e_iconv_charset_name (charset) : NULL;

	subject = summary_format_string(h, "subject", charset);
	from = summary_format_address(h, "from", charset);
	to = summary_format_address(h, "to", charset);
	cc = summary_format_address(h, "cc", charset);

	prio = camel_lite_header_raw_find(&h, "X-Priority", NULL);
	if (!prio)
		prio = camel_lite_header_raw_find(&h, "X-MSMail-Priority", NULL);
	if (!prio)
		prio = camel_lite_header_raw_find(&h, "Importance", NULL);

	if (prio)
	{
		if (camel_lite_strstrcase (prio, "high") != NULL)
			mi->flags |= CAMEL_MESSAGE_HIGH_PRIORITY;
		else if (strchr (prio, '1') != NULL)
			mi->flags |= CAMEL_MESSAGE_HIGH_PRIORITY;
		else if (strchr (prio, '2') != NULL)
			mi->flags |= CAMEL_MESSAGE_HIGH_PRIORITY;
		else if (camel_lite_strstrcase (prio, "normal") != NULL)
			mi->flags |= CAMEL_MESSAGE_NORMAL_PRIORITY;
		else if (strchr (prio, '3') != NULL)
			mi->flags |= CAMEL_MESSAGE_NORMAL_PRIORITY;
		else if (camel_lite_strstrcase (prio, "low") != NULL)
			mi->flags |= CAMEL_MESSAGE_LOW_PRIORITY;
		else if (strchr (prio, '4') != NULL)
			mi->flags |= CAMEL_MESSAGE_LOW_PRIORITY;
		else if (strchr (prio, '5') != NULL)
			mi->flags |= CAMEL_MESSAGE_LOW_PRIORITY;
	} else
		mi->flags |= CAMEL_MESSAGE_NORMAL_PRIORITY;

	attach = camel_lite_header_raw_find(&h, "X-MS-Has-Attach", NULL);
	if (attach)
		if (camel_lite_strstrcase (attach, "yes") != NULL)
			mi->flags |= CAMEL_MESSAGE_ATTACHMENTS;


	if (camel_lite_header_raw_find(&h, "X-MSMail-Priority", NULL) &&
		!camel_lite_header_raw_find(&h, "X-MS-Has-Attach", NULL)) {

			mi->flags &= ~CAMEL_MESSAGE_ATTACHMENTS;

	}


	/* else {
		attach = camel_lite_header_raw_find(&h, "Content-Type", NULL);
		if (camel_lite_strstrcase (attach, "multi") != NULL)
			mi->flags |= CAMEL_MESSAGE_ATTACHMENTS;
	} */


	if (ct)
		camel_lite_content_type_unref(ct);

	if (subject)
		mi->subject = camel_lite_pstring_add (subject, TRUE);
	else
		mi->subject = camel_lite_pstring_add (g_strdup (""), TRUE);

	if (from)
		mi->from = camel_lite_pstring_add (from, TRUE);
	else
		mi->from = camel_lite_pstring_add (g_strdup (""), TRUE);

	if (to)
		mi->to = camel_lite_pstring_add (to, TRUE);
	else
		mi->to = camel_lite_pstring_add (g_strdup (""), TRUE);

	if (cc)
		mi->cc = camel_lite_pstring_add (cc, TRUE);
	else
		mi->cc = camel_lite_pstring_add (g_strdup (""), TRUE);

	mi->date_sent = camel_lite_header_decode_date(camel_lite_header_raw_find(&h, "date", NULL), NULL);
	received = camel_lite_header_raw_find(&h, "received", NULL);

	if (received)
		received = strrchr(received, ';');
	if (received)
		mi->date_received = camel_lite_header_decode_date(received + 1, NULL);
	else
		mi->date_received = mi->date_sent;

	if (mi->date_received <= 0)
		mi->date_received = time (NULL);

	if (mi->date_sent <= 0)
		mi->date_sent = time (NULL);

	msgid = camel_lite_header_msgid_decode(camel_lite_header_raw_find(&h, "message-id", NULL));
	if (msgid) {
		md5_get_digest(msgid, strlen(msgid), digest);
		memcpy(mi->message_id.id.hash, digest, sizeof(mi->message_id.id.hash));
		g_free(msgid);
	}

	return (CamelMessageInfo *)mi;
}

/* Just for optimization reasons */
#define GUINT32_SIZE sizeof(guint32)
#define TIME_T_SIZE sizeof(time_t)

/* If the access is outside the boundaries of the mmaped file, then
   show an error and return NULL */
#define CHECK_MMAP_ACCESS(eof,current,length,mi)			\
	if (eof < (current + length)) {					\
		d(printf("%s Premature EOF. Summary file corrupted?\n", __FUNCTION__)); \
		if (mi)							\
			camel_lite_message_info_free ((void *) mi);		\
		return NULL;						\
	}								\

static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, gboolean *must_add)
{
	CamelMessageInfoBase *mi = NULL;
	guint count, len;
	unsigned char *ptrchr = s->filepos;
	unsigned int i;
	gchar *theuid = NULL;

	io(printf("Loading message info\n"));

	/* Try to find the original instance in case we are in reloading */

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
		theuid = (char*)ptrchr;
	}
	ptrchr += len;

	if (!s->in_reload)
	{

		/* We are not reloading, so searching for camel_lite_recoverable
		 * CamelMessageInfo struct instances is avoidable */
		mi = (CamelMessageInfoBase *) camel_lite_message_info_new (s);
		*must_add = TRUE;

		if (mi->uid)
			g_free (mi->uid);
		mi->uid = g_strdup (theuid);

	} else
	{
		CAMEL_SUMMARY_LOCK(s, summary_lock);

		if (s->messages && s->idx >=0 && s->idx < s->messages->len)
		{
			CAMEL_SUMMARY_LOCK(s, ref_lock);

			mi = (CamelMessageInfoBase *) s->messages->pdata[s->idx];
			destroy_possible_pstring_stuff (s, (CamelMessageInfo*) mi, FALSE);
			*must_add = FALSE;


			if (mi->uid)
				g_free (mi->uid);
			mi->uid = g_strdup (theuid);

			CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		} else
			printf ("Problem with %s. Can't find original instance (index=%d)\n", theuid, s->idx);

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
	}

	i = 0;

	if (!mi)
	{
		mi = (CamelMessageInfoBase *) camel_lite_message_info_new(s);
		*must_add = TRUE;

		if (mi->uid)
			g_free (mi->uid);
		mi->uid = g_strdup (theuid);

	}

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &mi->size, FALSE);

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &mi->flags, FALSE);

	mi->flags &= ~CAMEL_MESSAGE_INFO_NEEDS_FREE;
	mi->flags &= ~CAMEL_MESSAGE_FREED;

	s->set_extra_flags_func (s->folder, mi);

	CHECK_MMAP_ACCESS (s->eof, ptrchr, TIME_T_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_time_t (ptrchr, &mi->date_sent);

	CHECK_MMAP_ACCESS (s->eof, ptrchr, TIME_T_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_time_t (ptrchr, &mi->date_received);

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
		mi->subject = (const char*)ptrchr;
	}
	ptrchr += len;

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
		mi->from = (const char*)ptrchr;
	}
	ptrchr += len;

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
		mi->to = (const char*)ptrchr;
	}
	ptrchr += len;

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
		mi->cc = (const char*)ptrchr;
	}
	ptrchr += len;

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);

	if (len) {
		CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
	}
	ptrchr += len;

	s->filepos = ptrchr;

	CHECK_MMAP_ACCESS (s->eof, s->filepos, 8, mi);
	mi->message_id.id.part.hi = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	mi->message_id.id.part.lo = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	ptrchr = (unsigned char*) s->filepos;
	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &count, FALSE);

#ifdef NON_TINYMAIL_FEATURES
	if (mi->references)
		g_free (mi->references);
	mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
	mi->references->size = count;
#endif

	s->filepos = ptrchr;

	CHECK_MMAP_ACCESS (s->eof, s->filepos, count * 8, mi);
#ifdef NON_TINYMAIL_FEATURES
	for (i=0;i<count;i++) {
		mi->references->references[i].id.part.hi = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
		mi->references->references[i].id.part.lo = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
	}
#else
	s->filepos += (count * 8);
#endif

	ptrchr = s->filepos;
	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &count, FALSE);

	for (i=0;i<count;i++)
	{
		char *name = NULL;
		CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);
		if (len) {
			CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
			name = (char*) ptrchr;
		}
		ptrchr += len;
	}

	CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &count, FALSE);
	/* Sergio, how ptrchr could be 0 ? */
	if (!ptrchr) return NULL;

	for (i=0;i<count;i++)
	{
		char *name = NULL, *value = NULL;
		CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);
		if (len) {
			CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
			name = (char*)ptrchr;
		}
		ptrchr += len;

		CHECK_MMAP_ACCESS (s->eof, ptrchr, GUINT32_SIZE, mi);
		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &len, TRUE);
		if (len) {
			CHECK_MMAP_ACCESS (s->eof, ptrchr, len, mi);
			value =(char*) ptrchr;
		}
		ptrchr += len;
	}

	s->filepos = ptrchr;

	return (CamelMessageInfo *)mi;

}

static int
message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	guint32 count;
#ifdef NON_TINYMAIL_FEATURES
	CamelFlag *flag;
	CamelTag *tag;
	int i;
#endif
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	/* if (mi->flags & CAMEL_MESSAGE_INFO_NEEDS_FREE)
	       return -1; */

	io(printf("Saving message info\n"));

	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_uid(mi))== -1) return -1;

	if (camel_lite_file_util_encode_uint32(out, mi->size)== -1) return -1;
	if (camel_lite_file_util_encode_uint32(out, mi->flags)== -1) return -1;

	if (camel_lite_file_util_encode_time_t(out, mi->date_sent)== -1) return -1;
	if (camel_lite_file_util_encode_time_t(out, mi->date_received)== -1) return -1;
	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_subject(mi))== -1) return -1;
	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_from(mi))== -1) return -1;
	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_to(mi))== -1) return -1;
	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_cc(mi))== -1) return -1;

#ifdef NON_TINYMAIL_FEATURES
	if (camel_lite_file_util_encode_string(out, camel_lite_message_info_mlist(mi))== -1) return -1;
#else
	if (camel_lite_file_util_encode_string(out, "")== -1) return -1;
#endif

	if (camel_lite_file_util_encode_fixed_int32(out, mi->message_id.id.part.hi)== -1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, mi->message_id.id.part.lo)== -1) return -1;

#ifdef NON_TINYMAIL_FEATURES
	if (mi->references) {
		if (camel_lite_file_util_encode_uint32(out, mi->references->size)== -1) return -1;
		for (i=0;i<mi->references->size;i++) {
			if (camel_lite_file_util_encode_fixed_int32(out, mi->references->references[i].id.part.hi)== -1) return -1;
			if (camel_lite_file_util_encode_fixed_int32(out, mi->references->references[i].id.part.lo)== -1) return -1;
		}
	} else {
#endif
		if (camel_lite_file_util_encode_uint32(out, 0)== -1) return -1;

#ifdef NON_TINYMAIL_FEATURES
	}
	count = camel_lite_flag_list_size(&mi->user_flags);
#else
	count = 0;
#endif

	if (camel_lite_file_util_encode_uint32(out, count)== -1) return -1;

#ifdef NON_TINYMAIL_FEATURES
	flag = mi->user_flags;
	while (flag) {
		if (camel_lite_file_util_encode_string(out, flag->name)== -1) return -1;
		flag = flag->next;
	}
#endif

#ifdef NON_TINYMAIL_FEATURES
	count = camel_lite_tag_list_size(&mi->user_tags);
#else
	count = 0;
#endif

	if (camel_lite_file_util_encode_uint32(out, count)== -1) return -1;

#ifdef NON_TINYMAIL_FEATURES
	tag = mi->user_tags;
	while (tag) {
		if (camel_lite_file_util_encode_string(out, tag->name)== -1) return -1;
		if (camel_lite_file_util_encode_string(out, tag->value)== -1) return -1;
		tag = tag->next;
	}
#endif

	return ferror(out);
}

#ifdef MEMDEBUG
static int freed=0;
#endif

static void
destroy_possible_pstring_stuff(CamelFolderSummary *s, CamelMessageInfo *info, gboolean freeuid)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

#ifdef MEMDEBUG
	printf ("Freeup %d, %s\n", freed++, (mi->flags & CAMEL_MESSAGE_INFO_NEEDS_FREE)?"YES":"NO");
#endif

	if (mi->flags & CAMEL_MESSAGE_INFO_NEEDS_FREE)
	{
		if (freeuid && mi->uid)
			g_free(mi->uid);

		if (mi->subject)
			camel_lite_pstring_free(mi->subject);
		if (mi->from)
			camel_lite_pstring_free(mi->from);
		if (mi->to)
			camel_lite_pstring_free(mi->to);
		if (mi->cc)
			camel_lite_pstring_free(mi->cc);

#ifdef NON_TINYMAIL_FEATURES
		if (mi->mlist)
			camel_lite_pstring_free(mi->mlist);
		camel_lite_flag_list_free(&mi->user_flags);
		camel_lite_tag_list_free(&mi->user_tags);
		if (mi->headers)
			camel_lite_header_param_list_free (mi->headers);
#endif

		mi->flags &= ~CAMEL_MESSAGE_INFO_NEEDS_FREE;

	} else if (freeuid && mi->uid)
		g_free (mi->uid);

#ifdef NON_TINYMAIL_FEATURES
	g_free(mi->references);
#endif

	return;
}

static void
message_info_free(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	/* NOTE XUI! */
	if (!(mi->flags & CAMEL_MESSAGE_EXPUNGED))
		destroy_possible_pstring_stuff (s, info, TRUE);
	else
		if (mi->uid)
			g_free (mi->uid);

	/* memset: Trash it, makes debugging more easy */

	if (s) {
		memset (info, 0, s->message_info_size);
		g_slice_free1 (s->message_info_size, mi);
	} else {
		memset (info, 0, sizeof (CamelMessageInfoBase));
		g_slice_free (CamelMessageInfoBase, (CamelMessageInfoBase*) mi);
	}
}

static CamelMessageContentInfo *
content_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	CamelMessageContentInfo *ci;
	const char *charset;

	ci = camel_lite_folder_summary_content_info_new (s);
	ci->needs_free = TRUE;

	charset = e_iconv_locale_charset ();

	ci->id = token_add (camel_lite_header_msgid_decode (camel_lite_header_raw_find (&h, "content-id", NULL)));
	ci->description = token_add (camel_lite_header_decode_string (camel_lite_header_raw_find (&h, "content-description", NULL), charset));
	ci->encoding = token_add (camel_lite_content_transfer_encoding_decode (camel_lite_header_raw_find (&h, "content-transfer-encoding", NULL)));
	ci->type = camel_lite_content_type_decode(camel_lite_header_raw_find(&h, "content-type", NULL));

	return ci;
}

static CamelMessageContentInfo *
content_info_load(CamelFolderSummary *s)
{
	CamelMessageContentInfo *ci;
	char *type, *subtype;
	guint32 count, i;
	CamelContentType *ct;
	unsigned char *ptrchr = s->filepos;

	io(printf("Loading content info\n"));

	ci = camel_lite_folder_summary_content_info_new(s);
	ci->needs_free = FALSE;

	camel_lite_folder_summary_decode_token(s, &type);
	camel_lite_folder_summary_decode_token(s, &subtype);

	ct = camel_lite_content_type_new(type, subtype);

	ptrchr = s->filepos;
	if (s->eof < ptrchr + sizeof(guint32)) {
		d(printf("%s Premature EOF. Summary file corrupted?\n", __FUNCTION__));
		camel_lite_content_type_unref (ct);
		camel_lite_folder_summary_content_info_free (s, ci);
		return NULL;
	}
	ptrchr = camel_lite_file_util_mmap_decode_uint32 ((unsigned char*)ptrchr, &count, FALSE);
	s->filepos = ptrchr;

	for (i = 0; i < count; i++) {
		char *name, *value;
		camel_lite_folder_summary_decode_token(s, &name);
		camel_lite_folder_summary_decode_token(s, &value);

		camel_lite_content_type_set_param (ct, name, value);
	}

	ci->type = ct;

	camel_lite_folder_summary_decode_token(s, &ci->id);
	camel_lite_folder_summary_decode_token(s, &ci->description);
	camel_lite_folder_summary_decode_token(s, &ci->encoding);

	ptrchr = s->filepos;
	if (s->eof < ptrchr + sizeof(guint32)) {
		d(printf("%s Premature EOF. Summary file corrupted?\n", __FUNCTION__));
		camel_lite_folder_summary_content_info_free (s, ci);
		return NULL;
	}
	ptrchr = camel_lite_file_util_mmap_decode_uint32 ((unsigned char*)ptrchr, &ci->size, FALSE);
	s->filepos = ptrchr;

	ci->childs = NULL;

	return ci;
}

static int
content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	CamelContentType *ct = NULL;
	struct _camel_lite_header_param *hp = NULL;
	int retval = 0;

	io(printf("Saving content info\n"));

	if (ci)
		ct = ci->type;

	if (ct) {
		if (camel_lite_folder_summary_encode_token(out, ct->type) == -1) return -1;
		if (camel_lite_folder_summary_encode_token(out, ct->subtype)) return -1;
		if (camel_lite_file_util_encode_uint32(out, my_list_size((struct _node **)&ct->params))) return -1;
		hp = ct->params;
		while (hp) {
			if (camel_lite_folder_summary_encode_token(out, hp->name)) return -1;
			if (camel_lite_folder_summary_encode_token(out, hp->value)) return -1;
			hp = hp->next;
		}
	} else {
		if (camel_lite_folder_summary_encode_token(out, NULL)) return -1;
		if (camel_lite_folder_summary_encode_token(out, NULL)) return -1;
		if (camel_lite_file_util_encode_uint32(out, 0)) return -1;
	}

	if (ci) {
		if (camel_lite_folder_summary_encode_token(out, ci->id)) return -1;
		if (camel_lite_folder_summary_encode_token(out, ci->description)) return -1;
		if (camel_lite_folder_summary_encode_token(out, ci->encoding)) return -1;
		retval = camel_lite_file_util_encode_uint32(out, ci->size);
	} else {
		if (camel_lite_folder_summary_encode_token(out, "invalid")) return -1;
		if (camel_lite_folder_summary_encode_token(out, "invalid")) return -1;
		if (camel_lite_folder_summary_encode_token(out, "text/plain")) return -1;
		retval = camel_lite_file_util_encode_uint32(out, 0);
	}

	return retval;
}

static void
content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	camel_lite_content_type_unref(ci->type);

	if (ci->needs_free)
	{
		token_free (ci->id);
		token_free (ci->description);
		token_free (ci->encoding);
	}

	if (s != NULL)
		g_slice_free1(s->content_info_size, ci);
	else
		g_slice_free (CamelMessageContentInfo, ci);
}

static char *
next_uid_string(CamelFolderSummary *s)
{
	return g_strdup_printf("%u", camel_lite_folder_summary_next_uid(s));
}

/*
  OK
  Now this is where all the "smarts" happen, where the content info is built,
  and any indexing and what not is performed
*/

/* must have filter_lock before calling this function */
static CamelMessageContentInfo *
summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp)
{
	int state;
	size_t len;
	char *buffer;
	CamelMessageContentInfo *info = NULL;
	CamelContentType *ct;
	int enc_id = -1, chr_id = -1, html_id = -1, idx_id = -1;
	CamelMessageContentInfo *part;

	d(printf("building content info\n"));

	/* start of this part */
	state = camel_lite_mime_parser_step(mp, &buffer, &len);

	if (s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_parser(s, mp);

	switch(state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		/* check content type for indexing, then read body */
		ct = camel_lite_mime_parser_content_type(mp);
		/* update attachments flag as we go */

/*
		if (camel_lite_content_type_is(ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_lite_content_type_is(ct, "application", "x-pkcs7-signature")
		    || camel_lite_content_type_is(ct, "application", "pkcs7-signature")
#endif
			)
			camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);

*/

		/* and scan/index everything */
		while (camel_lite_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_BODY_END)
			;
		/* and remove the filters */
		camel_lite_mime_parser_filter_remove(mp, enc_id);
		camel_lite_mime_parser_filter_remove(mp, chr_id);
		camel_lite_mime_parser_filter_remove(mp, html_id);
		camel_lite_mime_parser_filter_remove(mp, idx_id);
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		d(printf("Summarising multipart\n"));
		/* update attachments flag as we go */
		ct = camel_lite_mime_parser_content_type(mp);
		if (camel_lite_content_type_is(ct, "multipart", "mixed"))
			camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
		/*if (camel_lite_content_type_is(ct, "multipart", "signed")
		    || camel_lite_content_type_is(ct, "multipart", "encrypted"))
			camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
		*/
		while (camel_lite_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
			camel_lite_mime_parser_unstep(mp);
			part = summary_build_content_info(s, msginfo, mp);
			if (part) {
				part->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)part);
			}
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		d(printf("Summarising message\n"));
		/* update attachments flag as we go */
		camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);

		part = summary_build_content_info(s, msginfo, mp);
		if (part) {
			part->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)part);
		}
		state = camel_lite_mime_parser_step(mp, &buffer, &len);
		if (state != CAMEL_MIME_PARSER_STATE_MESSAGE_END) {
			g_error("Bad parser state: Expecing MESSAGE_END or MESSAGE_EOF, got: %d", state);
			camel_lite_mime_parser_unstep(mp);
		}
		break;
	}

	d(printf("finished building content info\n"));

	return info;
}

/* build the content-info, from a message */
/* this needs the filter lock since it uses filters to perform indexing */
static CamelMessageContentInfo *
summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object)
{
	CamelDataWrapper *containee;
	int parts, i;
	struct _CamelFolderSummaryPrivate *p = NULL;
	CamelMessageContentInfo *info = NULL, *child;
	CamelContentType *ct;

	if (s != NULL && s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_message(s, object);
	else
		info = content_info_new_from_message(NULL, object);

	if (s != NULL)
		p = _PRIVATE(s);

	containee = camel_lite_medium_get_content_object(CAMEL_MEDIUM(object));

	if (containee == NULL)
		return info;

	camel_lite_object_ref (containee);

	/* TODO: I find it odd that get_part and get_content_object do not
	   add a reference, probably need fixing for multithreading */

	/* check for attachments */
	ct = ((CamelDataWrapper *)containee)->mime_type;
	if (camel_lite_content_type_is(ct, "multipart", "*")) {
		if (camel_lite_content_type_is(ct, "multipart", "mixed"))
			camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
/*
		if (camel_lite_content_type_is(ct, "multipart", "signed")
		    || camel_lite_content_type_is(ct, "multipart", "encrypted"))
			camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
*/
	} else if (camel_lite_content_type_is(ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_lite_content_type_is(ct, "application", "x-pkcs7-signature")
		    || camel_lite_content_type_is(ct, "application", "pkcs7-signature")
#endif
		) {
/*		camel_lite_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE); */
	}

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART(containee)) {
		parts = camel_lite_multipart_get_number(CAMEL_MULTIPART(containee));

		for (i=0;i<parts;i++) {
			CamelMimePart *part = camel_lite_multipart_get_part_wref (CAMEL_MULTIPART(containee), i);
			g_assert(part);
			child = summary_build_content_info_message(s, msginfo, part);
			if (child) {
				child->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)child);
			}
			camel_lite_object_unref (part);
		}

	} else if (CAMEL_IS_MIME_MESSAGE(containee)) {
		/* for messages we only look at its contents */
		child = summary_build_content_info_message(s, msginfo, (CamelMimePart *)containee);
		if (child) {
			child->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)child);
		}
	} else if (s != NULL && p && p->filter_stream
		   && camel_lite_content_type_is(ct, "text", "*")) {
		int html_id = -1, idx_id = -1;

		/* pre-attach html filter if required, otherwise just index filter */
		if (camel_lite_content_type_is(ct, "text", "html")) {
			if (p->filter_html == NULL)
				p->filter_html = camel_lite_mime_filter_html_new();
			else
				camel_lite_mime_filter_reset((CamelMimeFilter *)p->filter_html);
			html_id = camel_lite_stream_filter_add(p->filter_stream, (CamelMimeFilter *)p->filter_html);
		}
		idx_id = camel_lite_stream_filter_add(p->filter_stream, (CamelMimeFilter *)p->filter_index);

		camel_lite_data_wrapper_decode_to_stream(containee, (CamelStream *)p->filter_stream);
		camel_lite_stream_flush((CamelStream *)p->filter_stream);

		camel_lite_stream_filter_remove(p->filter_stream, idx_id);
		camel_lite_stream_filter_remove(p->filter_stream, html_id);
	}

	camel_lite_object_unref (containee);

	return info;
}

/**
 * camel_lite_flag_get:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to get
 *
 * Find the state of the flag @name in @list.
 *
 * Returns the state of the flag (%TRUE or %FALSE)
 **/
gboolean
camel_lite_flag_get(CamelFlag **list, const char *name)
{
	CamelFlag *flag;
	flag = *list;
	while (flag) {
		if (!strcmp(flag->name, name))
			return TRUE;
		flag = flag->next;
	}
	return FALSE;
}


/**
 * camel_lite_flag_set:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to set or change
 * @value: the value to set on the flag
 *
 * Set the state of a flag @name in the list @list to @value.
 *
 * Returns %TRUE if the value of the flag has been changed or %FALSE
 * otherwise
 **/
gboolean
camel_lite_flag_set(CamelFlag **list, const char *name, gboolean value)
{
	CamelFlag *flag, *tmp;

	/* this 'trick' works because flag->next is the first element */
	flag = (CamelFlag *)list;
	while (flag->next) {
		tmp = flag->next;
		if (!strcmp(flag->next->name, name)) {
			if (!value) {
				flag->next = tmp->next;
				g_free(tmp);
			}
			return !value;
		}
		flag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp) + strlen(name));
		strcpy(tmp->name, name);
		tmp->next = NULL;
		flag->next = tmp;
	}
	return value;
}


/**
 * camel_lite_flag_list_size:
 * @list: the address of a #CamelFlag list
 *
 * Get the length of the flag list.
 *
 * Returns the number of flags in the list
 **/
int
camel_lite_flag_list_size(CamelFlag **list)
{
	int count=0;
	CamelFlag *flag;

	flag = *list;
	while (flag) {
		count++;
		flag = flag->next;
	}
	return count;
}


/**
 * camel_lite_flag_list_free:
 * @list: the address of a #CamelFlag list
 *
 * Free the memory associated with the flag list @list.
 **/
void
camel_lite_flag_list_free(CamelFlag **list)
{
	CamelFlag *flag, *tmp;
	flag = *list;
	while (flag) {
		tmp = flag->next;
		g_free(flag);
		flag = tmp;
	}
	*list = NULL;
}


/**
 * camel_lite_flag_list_copy:
 * @to: the address of the #CamelFlag list to copy to
 * @from: the address of the #CamelFlag list to copy from
 *
 * Copy a flag list.
 *
 * Returns %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_lite_flag_list_copy(CamelFlag **to, CamelFlag **from)
{
	CamelFlag *flag, *tmp;
	int changed = FALSE;

	if (*to == NULL && from == NULL)
		return FALSE;

	/* Remove any now-missing flags */
	flag = (CamelFlag *)to;
	while (flag->next) {
		tmp = flag->next;
		if (!camel_lite_flag_get(from, tmp->name)) {
			flag->next = tmp->next;
			g_free(tmp);
			changed = TRUE;
		} else {
			flag = tmp;
		}
	}

	/* Add any new flags */
	flag = *from;
	while (flag) {
		changed |= camel_lite_flag_set(to, flag->name, TRUE);
		flag = flag->next;
	}

	return changed;
}


/**
 * camel_lite_tag_get:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to get
 *
 * Find the flag @name in @list and get the value.
 *
 * Returns the value of the flag  or %NULL if unset
 **/
const char *
camel_lite_tag_get(CamelTag **list, const char *name)
{
	CamelTag *tag;

	tag = *list;
	while (tag) {
		if (!strcmp(tag->name, name))
			return (const char *)tag->value;
		tag = tag->next;
	}
	return NULL;
}


/**
 * camel_lite_tag_set:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to set
 * @value: value to set on the tag
 *
 * Set the tag @name in the tag list @list to @value.
 *
 * Returns %TRUE if the value on the tag changed or %FALSE otherwise
 **/
gboolean
camel_lite_tag_set(CamelTag **list, const char *name, const char *value)
{
	CamelTag *tag, *tmp;

	/* this 'trick' works because tag->next is the first element */
	tag = (CamelTag *)list;
	while (tag->next) {
		tmp = tag->next;
		if (!strcmp(tmp->name, name)) {
			if (value == NULL) { /* clear it? */
				tag->next = tmp->next;
				g_free(tmp->value);
				g_free(tmp);
				return TRUE;
			} else if (strcmp(tmp->value, value)) { /* has it changed? */
				g_free(tmp->value);
				tmp->value = g_strdup(value);
				return TRUE;
			}
			return FALSE;
		}
		tag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp)+strlen(name));
		strcpy(tmp->name, name);
		tmp->value = g_strdup(value);
		tmp->next = NULL;
		tag->next = tmp;
		return TRUE;
	}
	return FALSE;
}


/**
 * camel_lite_tag_list_size:
 * @list: the address of a #CamelTag list
 *
 * Get the number of tags present in the tag list @list.
 *
 * Returns the number of tags
 **/
int
camel_lite_tag_list_size(CamelTag **list)
{
	int count=0;
	CamelTag *tag;

	tag = *list;
	while (tag) {
		count++;
		tag = tag->next;
	}
	return count;
}

static void
rem_tag(char *key, char *value, CamelTag **to)
{
	camel_lite_tag_set(to, key, NULL);
}


/**
 * camel_lite_tag_list_copy:
 * @to: the address of the #CamelTag list to copy to
 * @from: the address of the #CamelTag list to copy from
 *
 * Copy a tag list.
 *
 * Returns %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_lite_tag_list_copy(CamelTag **to, CamelTag **from)
{
	int changed = FALSE;
	CamelTag *tag;
	GHashTable *left;

	if (*to == NULL && from == NULL)
		return FALSE;

	left = g_hash_table_new(g_str_hash, g_str_equal);
	tag = *to;
	while (tag) {
		g_hash_table_insert(left, tag->name, tag);
		tag = tag->next;
	}

	tag = *from;
	while (tag) {
		changed |= camel_lite_tag_set(to, tag->name, tag->value);
		g_hash_table_remove(left, tag->name);
		tag = tag->next;
	}

	if (g_hash_table_size(left)>0) {
		g_hash_table_foreach(left, (GHFunc)rem_tag, to);
		changed = TRUE;
	}
	g_hash_table_destroy(left);

	return changed;
}


/**
 * camel_lite_tag_list_free:
 * @list: the address of a #CamelTag list
 *
 * Free the tag list @list.
 **/
void
camel_lite_tag_list_free(CamelTag **list)
{
	CamelTag *tag, *tmp;
	tag = *list;
	while (tag) {
		tmp = tag->next;
		g_free(tag->value);
		g_free(tag);
		tag = tmp;
	}
	*list = NULL;
}

static struct flag_names_t {
	char *name;
	guint32 value;
} flag_names[] = {
	{ "answered", CAMEL_MESSAGE_ANSWERED },
	{ "deleted", CAMEL_MESSAGE_DELETED },
	{ "draft", CAMEL_MESSAGE_DELETED },
	{ "flagged", CAMEL_MESSAGE_FLAGGED },
	{ "seen", CAMEL_MESSAGE_SEEN },
	{ "attachments", CAMEL_MESSAGE_ATTACHMENTS },
/*	{ "secure", CAMEL_MESSAGE_SECURE }, */
	{ NULL, 0 }
};


/**
 * camel_lite_system_flag:
 * @name: name of a system flag
 *
 * Returns the integer value of the system flag string
 **/
guint32
camel_lite_system_flag (const char *name)
{
	struct flag_names_t *flag;

	g_return_val_if_fail (name != NULL, 0);

	for (flag = flag_names; *flag->name; flag++)
		if (!g_ascii_strcasecmp (name, flag->name))
			return flag->value;

	return 0;
}


/**
 * camel_lite_system_flag_get:
 * @flags: bitwise system flags
 * @name: name of the flag to check for
 *
 * Find the state of the flag @name in @flags.
 *
 * Returns %TRUE if the named flag is set or %FALSE otherwise
 **/
gboolean
camel_lite_system_flag_get (guint32 flags, const char *name)
{
	g_return_val_if_fail (name != NULL, FALSE);

	return flags & camel_lite_system_flag (name);
}


/**
 * camel_lite_message_info_new:
 * @summary: a #CamelFolderSummary object or %NULL
 *
 * Create a new #CamelMessageInfo.
 *
 * Returns a new #CamelMessageInfo
 **/
void *
camel_lite_message_info_new (CamelFolderSummary *s)
{
	CamelMessageInfo *info;

	if (s)
		info = g_slice_alloc0(s->message_info_size);
	else
		info = (CamelMessageInfo *) g_slice_new0(CamelMessageInfoBase);

	((CamelMessageInfoBase *)info)->flags = 0;

	info->refcount = 1;
	info->summary = s;

	return info;
}


/**
 * camel_lite_message_info_ref:
 * @info: a #CamelMessageInfo
 *
 * Reference an info.
 **/
void
camel_lite_message_info_ref(void *o)
{
	CamelMessageInfo *mi = o;

	if (mi->summary) {
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
	} else {
		GLOBAL_INFO_LOCK(info);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		GLOBAL_INFO_UNLOCK(info);
	}
}

void *
camel_lite_message_info_new_uid (CamelFolderSummary *summary, const char *uid)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)camel_lite_message_info_new(summary);
	mi->uid = g_strdup (uid);
	mi->flags |= CAMEL_MESSAGE_INFO_NEEDS_FREE;
	return mi;
}


/**
 * camel_lite_message_info_new_from_header:
 * @summary: a #CamelFolderSummary object or %NULL
 * @header: raw header
 *
 * Create a new #CamelMessageInfo pre-populated with info from
 * @header.
 *
 * Returns a new #CamelMessageInfo
 **/
CamelMessageInfo *
camel_lite_message_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *header)
{
	if (s)
		return ((CamelFolderSummaryClass *)((CamelObject *)s)->klass)->message_info_new_from_header(s, header);
	else
		return message_info_new_from_header(NULL, header);
}


/**
 * camel_lite_message_info_free:
 * @info: a #CamelMessageInfo
 *
 * Unref's and potentially frees a #CamelMessageInfo and its contents.
 **/
void
camel_lite_message_info_free(void *o)
{
	CamelMessageInfo *mi = o;

	/* camel_lite_message_info_set_flags(mi, CAMEL_MESSAGE_FREED, CAMEL_MESSAGE_FREED); */

	g_return_if_fail(mi != NULL);

	if (mi->summary) {
	 // if (((CamelObject *)mi->summary)->ref_count > 0) {
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);

		if (mi->refcount >= 1)
			mi->refcount--;
		if (mi->refcount > 0) {
			CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
			return;
		}

		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);

		/* FIXME: this is kinda busted, should really be handled by message info free */
		if (/* mi->summary->build_content
		    && */((CamelMessageInfoBase *)mi)->content) {
			camel_lite_folder_summary_content_info_free(mi->summary, ((CamelMessageInfoBase *)mi)->content);
		}

		((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(mi->summary)))->message_info_free(mi->summary, mi);
	 // }
	} else {
		GLOBAL_INFO_LOCK(info);
		mi->refcount--;
		if (mi->refcount > 0) {
			GLOBAL_INFO_UNLOCK(info);
			return;
		}
		GLOBAL_INFO_UNLOCK(info);

		if (((CamelMessageInfoBase *)mi)->content)
			camel_lite_folder_summary_content_info_free(NULL, ((CamelMessageInfoBase *)mi)->content);

		message_info_free(NULL, mi);
	}
}

static CamelMessageInfo *
message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelMessageInfoBase *to, *from = (CamelMessageInfoBase *)mi;
#ifdef NON_TINYMAIL_FEATURES
	CamelFlag *flag;
	CamelTag *tag;
#endif
	to = (CamelMessageInfoBase *)camel_lite_message_info_new(s);

	to->flags = from->flags;
	to->size = from->size;

	to->date_sent = from->date_sent;
	to->date_received = from->date_received;
	to->refcount = 1;

	to->flags |= CAMEL_MESSAGE_INFO_NEEDS_FREE;

	to->subject = camel_lite_pstring_strdup(from->subject);
	to->from = camel_lite_pstring_strdup(from->from);
	to->to = camel_lite_pstring_strdup(from->to);
	to->cc = camel_lite_pstring_strdup(from->cc);

#ifdef NON_TINYMAIL_FEATURES
	to->mlist = camel_lite_pstring_strdup(from->mlist);
	memcpy(&to->message_id, &from->message_id, sizeof(to->message_id));

	if (from->references) {
		int len = sizeof(*from->references) + ((from->references->size-1) * sizeof(from->references->references[0]));

		to->references = g_malloc(len);
		memcpy(to->references, from->references, len);
	}
#endif

#ifdef NON_TINYMAIL_FEATURES
	flag = from->user_flags;
	while (flag) {
		camel_lite_flag_set(&to->user_flags, flag->name, TRUE);
		flag = flag->next;
	}

	tag = from->user_tags;
	while (tag) {
		camel_lite_tag_set(&to->user_tags, tag->name, tag->value);
		tag = tag->next;
	}
#endif

	return (CamelMessageInfo *)to;
}


/**
 * camel_lite_message_info_clone:
 * @info: a #CamelMessageInfo
 *
 * Duplicate a #CamelMessageInfo.
 *
 * Returns the duplicated #CamelMessageInfo
 **/
void *
camel_lite_message_info_clone(const void *o)
{
	const CamelMessageInfo *mi = o;

	if (mi == NULL || mi->refcount <=0)
		return NULL;

	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->message_info_clone(mi->summary, mi);
	else
		return message_info_clone(NULL, mi);
}


static const void *
info_ptr(const CamelMessageInfo *mi, int id)
{
	const void *retval = NULL;

	g_static_rec_mutex_lock (&global_lock);
	g_static_mutex_lock (&global_lock2);

	if (G_UNLIKELY (mi == NULL || mi->refcount <=0))
		retval = "Invalid refcount";
	if (G_UNLIKELY(((CamelMessageInfoBase*)mi)->flags & CAMEL_MESSAGE_FREED))
		retval = "Invalid";
	else switch (id)
	{
		case CAMEL_MESSAGE_INFO_SUBJECT:
			retval = ((const CamelMessageInfoBase *)mi)->subject;
		break;
		case CAMEL_MESSAGE_INFO_FROM:
			retval = ((const CamelMessageInfoBase *)mi)->from;
		break;
		case CAMEL_MESSAGE_INFO_TO:
			retval = ((const CamelMessageInfoBase *)mi)->to;
		break;
		case CAMEL_MESSAGE_INFO_CC:
			retval = ((const CamelMessageInfoBase *)mi)->cc;
		break;
		case CAMEL_MESSAGE_INFO_MESSAGE_ID:
			retval = &((const CamelMessageInfoBase *)mi)->message_id;
		break;

		default:
			g_warning ("%s: invalid id %d", __FUNCTION__, id);

	}

	g_static_mutex_unlock (&global_lock2);
	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

static guint32
info_uint32(const CamelMessageInfo *mi, int id)
{
	guint32 retval = 0;

	g_static_rec_mutex_lock (&global_lock);

	if (mi == NULL || mi->refcount <=0)
		retval = 0;
	else switch (id)
	{
		case CAMEL_MESSAGE_INFO_FLAGS:
			retval = ((const CamelMessageInfoBase *)mi)->flags;
		break;
		case CAMEL_MESSAGE_INFO_SIZE:
			retval = ((const CamelMessageInfoBase *)mi)->size;
		break;

	        default:
			g_warning ("%s: invalid id %d", __FUNCTION__, id);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

static time_t
info_time(const CamelMessageInfo *mi, int id)
{
	time_t retval = 0;

	g_static_rec_mutex_lock (&global_lock);

	if (mi == NULL || mi->refcount <=0)
		retval = 0;
	else switch (id)
	{
		case CAMEL_MESSAGE_INFO_DATE_SENT:
			retval = ((const CamelMessageInfoBase *)mi)->date_sent;
		break;
		case CAMEL_MESSAGE_INFO_DATE_RECEIVED:
			retval = ((const CamelMessageInfoBase *)mi)->date_received;
		break;
		default:
			g_warning ("%s: invalid id %d", __FUNCTION__, id);

	}

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

static gboolean
info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	return FALSE;
}

static const char *
info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	return NULL;
}


/**
 * camel_lite_message_info_ptr:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting pointer data.
 *
 * Returns the pointer data
 **/
const void *
camel_lite_message_info_ptr(const CamelMessageInfo *mi, int id)
{
	const void * retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi==NULL || mi->refcount <= 0)
		retval = NULL;
	else
	{
		if (mi->summary)
			if (((CamelObject*)mi->summary)->ref_count > 0 && CAMEL_IS_FOLDER_SUMMARY (mi->summary))
				retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_ptr(mi, id);
			else
				retval = NULL;
		else
			retval = info_ptr(mi, id);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}


/**
 * camel_lite_message_info_uint32:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting 32bit int data.
 *
 * Returns the int data
 **/
guint32
camel_lite_message_info_uint32(const CamelMessageInfo *mi, int id)
{
	guint32 retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi == NULL || mi->refcount <=0)
		retval = 0;
	else
	{
		if (mi->summary)
			if (CAMEL_IS_FOLDER_SUMMARY (mi->summary))
				retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_uint32(mi, id);
			else
				retval = 0;
		else
			retval = info_uint32(mi, id);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}


/**
 * camel_lite_message_info_time:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting time_t data.
 *
 * Returns the time_t data
 **/
time_t
camel_lite_message_info_time (const CamelMessageInfo *mi, int id)
{
	time_t retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi == NULL || mi->refcount <=0)
		retval = 0;
	else
	{
		if (mi->summary)
			if (CAMEL_IS_FOLDER_SUMMARY (mi->summary))
				retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_time(mi, id);
			else
				retval = -1;
		else
			retval = info_time(mi, id);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}


/**
 * camel_lite_message_info_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: user flag to get
 *
 * Get the state of a user flag named @id.
 *
 * Returns the state of the user flag
 **/
gboolean
camel_lite_message_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	gboolean retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi->summary)
		if (CAMEL_IS_FOLDER_SUMMARY (mi->summary))
			retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_flag(mi, id);
		else
			retval = FALSE;
	else
		retval = info_user_flag(mi, id);

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}


/**
 * camel_lite_message_info_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: user tag to get
 *
 * Get the value of a user tag named @id.
 *
 * Returns the value of the user tag
 **/
const char *
camel_lite_message_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	const char * retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi->summary)
		if (CAMEL_IS_FOLDER_SUMMARY (mi->summary))
			retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_tag(mi, id);
		else
			retval = NULL;
	else
		retval = info_user_tag(mi, id);

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

static gboolean
info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set)
{
	guint16 old;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	/* TODO: locking? */

	old = mi->flags;
	mi->flags = (old & ~flags) | (set & flags);
	if (old != mi->flags) {
		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		if (mi->summary)
			camel_lite_folder_summary_touch(mi->summary);
	}

	if ((old & ~CAMEL_MESSAGE_SYSTEM_MASK) == (mi->flags & ~CAMEL_MESSAGE_SYSTEM_MASK))
		return FALSE;

	if (mi->summary && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_lite_folder_change_info_new();

		camel_lite_folder_change_info_change_uid(changes, camel_lite_message_info_uid(info));
		camel_lite_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_lite_folder_change_info_free(changes);
	}

	return TRUE;
}


/**
 * camel_lite_message_info_set_flags:
 * @mi: a #CamelMessageInfo
 * @flags: mask of flags to change
 * @set: state the flags should be changed to
 *
 * Change the state of the system flags on the #CamelMessageInfo
 *
 * Returns %TRUE if any of the flags changed or %FALSE otherwise
 **/
gboolean
camel_lite_message_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	gboolean retval;

	/* g_static_rec_mutex_lock (&global_lock); */
	if (mi->summary)
		if (CAMEL_IS_FOLDER_SUMMARY (mi->summary))
			retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_flags(mi, flags, set);
		else
			retval = FALSE;
	else
		retval = info_set_flags(mi, flags, set);

	/* g_static_rec_mutex_unlock (&global_lock); */

	return retval;
}

static gboolean
info_set_user_flag(CamelMessageInfo *info, const char *name, gboolean value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	gboolean res = FALSE;

	g_static_rec_mutex_lock (&global_lock);

#ifdef NON_TINYMAIL_FEATURES
	res = camel_lite_flag_set(&mi->user_flags, name, value);
#endif

	/* TODO: check this item is still in the summary first */
	if (mi->summary && res && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_lite_folder_change_info_new();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		camel_lite_folder_summary_touch(mi->summary);
		camel_lite_folder_change_info_change_uid(changes, camel_lite_message_info_uid(info));
		camel_lite_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_lite_folder_change_info_free(changes);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return res;
}


/**
 * camel_lite_message_info_set_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user flag to set
 * @state: state to set the flag to
 *
 * Set the state of a user flag on a #CamelMessageInfo.
 *
 * Returns %TRUE if the state changed or %FALSE otherwise
 **/
gboolean
camel_lite_message_info_set_user_flag(CamelMessageInfo *mi, const char *id, gboolean state)
{
	gboolean retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi->summary)
		retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_flag(mi, id, state);
	else
		retval = info_set_user_flag(mi, id, state);

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

static gboolean
info_set_user_tag(CamelMessageInfo *info, const char *name, const char *value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	gboolean res = FALSE;

	g_static_rec_mutex_lock (&global_lock);

#ifdef NON_TINYMAIL_FEATURES
	res = camel_lite_tag_set(&mi->user_tags, name, value);
#endif

	if (mi->summary && res && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_lite_folder_change_info_new();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		camel_lite_folder_summary_touch(mi->summary);
		camel_lite_folder_change_info_change_uid(changes, camel_lite_message_info_uid(info));
		camel_lite_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_lite_folder_change_info_free(changes);
	}

	g_static_rec_mutex_unlock (&global_lock);

	return res;
}


/**
 * camel_lite_message_info_set_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user tag to set
 * @val: value to set
 *
 * Set the value of a user tag on a #CamelMessageInfo.
 *
 * Returns %TRUE if the value changed or %FALSE otherwise
 **/
gboolean
camel_lite_message_info_set_user_tag(CamelMessageInfo *mi, const char *id, const char *val)
{
	gboolean retval;

	g_static_rec_mutex_lock (&global_lock);

	if (mi->summary)
		retval = ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_tag(mi, id, val);
	else
		retval = info_set_user_tag(mi, id, val);

	g_static_rec_mutex_unlock (&global_lock);

	return retval;
}

void
camel_lite_content_info_dump (CamelMessageContentInfo *ci, int depth)
{
	char *p;

	p = alloca (depth * 4 + 1);
	memset (p, ' ', depth * 4);
	p[depth * 4] = 0;

	if (ci == NULL) {
		printf ("%s<empty>\n", p);
		return;
	}

	if (ci->type)
		printf ("%scontent-type: %s/%s\n", p, ci->type->type ? ci->type->type : "(null)",
			ci->type->subtype ? ci->type->subtype : "(null)");
	else
		printf ("%scontent-type: <unset>\n", p);
	printf ("%scontent-transfer-encoding: %s\n", p, ci->encoding ? ci->encoding : "(null)");
	printf ("%scontent-description: %s\n", p, ci->description ? ci->description : "(null)");
	printf ("%ssize: %lu\n", p, (unsigned long) ci->size);
	ci = ci->childs;
	while (ci) {
		camel_lite_content_info_dump (ci, depth + 1);
		ci = ci->next;
	}
}

void
camel_lite_message_info_dump (CamelMessageInfo *mi)
{
	if (mi == NULL) {
		printf("No message?\n");
		return;
	}

	printf("Subject: %s\n", camel_lite_message_info_subject(mi));
	printf("To: %s\n", camel_lite_message_info_to(mi));
	printf("Cc: %s\n", camel_lite_message_info_cc(mi));
#ifdef NON_TINYMAIL_FEATURES
	printf("mailing list: %s\n", camel_lite_message_info_mlist(mi));
#endif
	printf("From: %s\n", camel_lite_message_info_from(mi));
	printf("UID: %s\n", camel_lite_message_info_uid(mi));
	printf("Flags: %04x\n", camel_lite_message_info_flags(mi));
	/*camel_lite_content_info_dump(mi->content, 0);*/
}

void
camel_lite_folder_summary_lock ()
{
	g_static_rec_mutex_lock (&global_lock);	
}

void
camel_lite_folder_summary_unlock ()
{
	g_static_rec_mutex_unlock (&global_lock);	
}

static void
camel_lite_folder_summary_class_init (CamelFolderSummaryClass *klass)
{
	camel_lite_folder_summary_parent = camel_lite_type_get_global_classfuncs (camel_lite_object_get_type ());

	klass->summary_header_load = summary_header_load;
	klass->summary_header_save = summary_header_save;

	klass->message_info_new_from_header  = message_info_new_from_header;
	klass->message_info_new_from_parser = message_info_new_from_parser;
	klass->message_info_new_from_message = message_info_new_from_message;
	klass->message_info_load = message_info_load;
	klass->message_info_save = message_info_save;
	klass->message_info_free = message_info_free;
	klass->message_info_clone = message_info_clone;

	klass->content_info_new_from_header  = content_info_new_from_header;
	klass->content_info_new_from_parser = content_info_new_from_parser;
	klass->content_info_new_from_message = content_info_new_from_message;
	klass->content_info_load = content_info_load;
	klass->content_info_save = content_info_save;
	klass->content_info_free = content_info_free;

	klass->next_uid_string = next_uid_string;

	klass->info_ptr = info_ptr;
	klass->info_uint32 = info_uint32;
	klass->info_time = info_time;
	klass->info_user_flag = info_user_flag;
	klass->info_user_tag = info_user_tag;

#if 0
	klass->info_set_string = info_set_string;
	klass->info_set_uint32 = info_set_uint32;
	klass->info_set_time = info_set_time;
	klass->info_set_ptr = info_set_ptr;
#endif
	klass->info_set_user_flag = info_set_user_flag;
	klass->info_set_user_tag = info_set_user_tag;

	klass->info_set_flags = info_set_flags;
}
