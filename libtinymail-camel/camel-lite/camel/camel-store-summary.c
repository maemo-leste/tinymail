/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2003 Ximian Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/e-lite-memory.h>
#include <libedataserver/md5-utils.h>

#include "camel-file-utils.h"
#include "camel-private.h"
#include "camel-store-summary.h"
#include "camel-url.h"

#define d(x)
#define io(x)			/* io debug */

/* possible versions, for versioning changes */
#define CAMEL_STORE_SUMMARY_VERSION_0 (1)
#define CAMEL_STORE_SUMMARY_VERSION_2 (2)

/* current version */
#define CAMEL_STORE_SUMMARY_VERSION (2)

#define _PRIVATE(o) (((CamelStoreSummary *)(o))->priv)

static int summary_header_load(CamelStoreSummary *, FILE *);
static int summary_header_save(CamelStoreSummary *, FILE *);

static CamelStoreInfo * store_info_new(CamelStoreSummary *, const char *);
static CamelStoreInfo * store_info_load(CamelStoreSummary *, FILE *);
static int		 store_info_save(CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free(CamelStoreSummary *, CamelStoreInfo *);

static const char *store_info_string(CamelStoreSummary *, const CamelStoreInfo *, int);
static void store_info_set_string(CamelStoreSummary *, CamelStoreInfo *, int, const char *);

static void camel_lite_store_summary_class_init (CamelStoreSummaryClass *klass);
static void camel_lite_store_summary_init       (CamelStoreSummary *obj);
static void camel_lite_store_summary_finalise   (CamelObject *obj);

static CamelObjectClass *camel_lite_store_summary_parent;

static void
camel_lite_store_summary_class_init (CamelStoreSummaryClass *klass)
{
	camel_lite_store_summary_parent = camel_lite_type_get_global_classfuncs (camel_lite_object_get_type ());

	klass->summary_header_load = summary_header_load;
	klass->summary_header_save = summary_header_save;

	klass->store_info_new  = store_info_new;
	klass->store_info_load = store_info_load;
	klass->store_info_save = store_info_save;
	klass->store_info_free = store_info_free;

	klass->store_info_string = store_info_string;
	klass->store_info_set_string = store_info_set_string;
}

static void
camel_lite_store_summary_init (CamelStoreSummary *s)
{
	struct _CamelStoreSummaryPrivate *p;

	p = _PRIVATE(s) = g_malloc0(sizeof(*p));

	s->store_info_size = sizeof(CamelStoreInfo);
	s->version = CAMEL_STORE_SUMMARY_VERSION;
	s->flags = 0;
	s->count = 0;
	s->time = 0;

	s->folders = g_ptr_array_new();
	s->folders_path = g_hash_table_new(g_str_hash, g_str_equal);

	p->summary_lock = g_mutex_new();
	p->io_lock = g_mutex_new();
	p->ref_lock = g_mutex_new();
}

static void
camel_lite_store_summary_finalise (CamelObject *obj)
{
	struct _CamelStoreSummaryPrivate *p;
	CamelStoreSummary *s = (CamelStoreSummary *)obj;
	CamelException nex = CAMEL_EXCEPTION_INITIALISER;

	p = _PRIVATE(obj);

	camel_lite_store_summary_save (s, &nex);

	camel_lite_store_summary_clear(s);
	g_ptr_array_free(s->folders, TRUE);
	g_hash_table_destroy(s->folders_path);

	g_free(s->summary_path);

	if (s->uri_base)
                camel_lite_url_free(s->uri_base);

	g_mutex_free(p->summary_lock);
	g_mutex_free(p->io_lock);
	g_mutex_free(p->ref_lock);

	g_free(p);
}

CamelType
camel_lite_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_object_get_type (), "CamelLiteStoreSummary",
					    sizeof (CamelStoreSummary),
					    sizeof (CamelStoreSummaryClass),
					    (CamelObjectClassInitFunc) camel_lite_store_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_store_summary_init,
					    (CamelObjectFinalizeFunc) camel_lite_store_summary_finalise);
	}

	return type;
}


/**
 * camel_lite_store_summary_new:
 *
 * Create a new #CamelStoreSummary object.
 *
 * Returns a new #CamelStoreSummary object
 **/
CamelStoreSummary *
camel_lite_store_summary_new (void)
{
	CamelStoreSummary *new = CAMEL_STORE_SUMMARY ( camel_lite_object_new (camel_lite_store_summary_get_type ()));	return new;
}


/**
 * camel_lite_store_summary_set_filename:
 * @summary: a #CamelStoreSummary
 * @filename: a filename
 *
 * Set the filename where the summary will be loaded to/saved from.
 **/
void
camel_lite_store_summary_set_filename(CamelStoreSummary *s, const char *name)
{
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	g_free(s->summary_path);
	s->summary_path = g_strdup(name);

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
}


/**
 * camel_lite_store_summary_set_uri_base:
 * @summary: a #CamelStoreSummary object
 * @base: a #CamelURL
 *
 * Sets the base URI for the summary.
 **/
void
camel_lite_store_summary_set_uri_base(CamelStoreSummary *s, CamelURL *base)
{
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	if (s->uri_base)
		camel_lite_url_free(s->uri_base);
	s->uri_base = camel_lite_url_new_with_base(base, "");

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
}


/**
 * camel_lite_store_summary_count:
 * @summary: a #CamelStoreSummary object
 *
 * Get the number of summary items stored in this summary.
 *
 * Returns the number of items int he summary.
 **/
int
camel_lite_store_summary_count(CamelStoreSummary *s)
{
	return s->folders->len;
}


/**
 * camel_lite_store_summary_index:
 * @summary: a #CamelStoreSummary object
 * @index: record index
 *
 * Retrieve a summary item by index number.
 *
 * A referenced to the summary item is returned, which may be ref'd or
 * free'd as appropriate.
 *
 * It must be freed using #camel_lite_store_summary_info_free.
 *
 * Returns the summary item, or %NULL if @index is out of range
 **/
CamelStoreInfo *
camel_lite_store_summary_index(CamelStoreSummary *s, int i)
{
	CamelStoreInfo *info = NULL;

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	if (i<s->folders->len)
		info = g_ptr_array_index(s->folders, i);

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);

	return info;
}


/**
 * camel_lite_store_summary_array:
 * @summary: a #CamelStoreSummary object
 *
 * Obtain a copy of the summary array.  This is done atomically,
 * so cannot contain empty entries.
 *
 * It must be freed using #camel_lite_store_summary_array_free.
 *
 * Returns the summary array
 **/
GPtrArray *
camel_lite_store_summary_array(CamelStoreSummary *s)
{
	CamelStoreInfo *info;
	GPtrArray *res = g_ptr_array_new();
	int i;

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	g_ptr_array_set_size(res, s->folders->len);
	for (i=0;i<s->folders->len;i++) {
		info = res->pdata[i] = g_ptr_array_index(s->folders, i);
		info->refcount++;
	}

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
	CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);

	return res;
}


/**
 * camel_lite_store_summary_array_free:
 * @summary: a #CamelStoreSummary object
 * @array: the summary array as gotten from #camel_lite_store_summary_array
 *
 * Free the folder summary array.
 **/
void
camel_lite_store_summary_array_free(CamelStoreSummary *s, GPtrArray *array)
{
	int i;

	for (i=0;i<array->len;i++)
		camel_lite_store_summary_info_free(s, array->pdata[i]);

	g_ptr_array_free(array, TRUE);
}


/**
 * camel_lite_store_summary_path:
 * @summary: a #CamelStoreSummary object
 * @path: path to the item
 *
 * Retrieve a summary item by path name.
 *
 * A referenced to the summary item is returned, which may be ref'd or
 * free'd as appropriate.
 *
 * It must be freed using #camel_lite_store_summary_info_free.
 *
 * Returns the summary item, or %NULL if the @path name is not
 * available
 **/
CamelStoreInfo *
camel_lite_store_summary_path(CamelStoreSummary *s, const char *path)
{
	CamelStoreInfo *info;

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	info = g_hash_table_lookup(s->folders_path, path);

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);

	return info;
}

/**
 * camel_lite_store_summary_load:
 * @summary: a #CamelStoreSummary object
 *
 * Load the summary off disk.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_store_summary_load(CamelStoreSummary *s)
{
	FILE *in;
	int i;
	CamelStoreInfo *info;

	g_assert(s->summary_path);

	CAMEL_STORE_SUMMARY_LOCK(s, io_lock);

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL) {
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		return -1;
	}

	if ( ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in) == -1)
		goto error;

	/* now read in each message ... */
	for (i=0;i<s->count;i++) {
		info = ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_load(s, in);

		if (info == NULL)
			goto error;

		camel_lite_store_summary_add(s, info);
	}

	if (fclose (in) != 0) {
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		return -1;
	}

	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;

	CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);

	return 0;

error:
	i = ferror (in);
	g_warning ("Cannot load summary file: %s", strerror (ferror (in)));
	CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
	fclose (in);
	s->flags |= ~CAMEL_STORE_SUMMARY_DIRTY;
	errno = i;

	return -1;
}


/**
 * camel_lite_store_summary_save:
 * @summary: a #CamelStoreSummary object
 *
 * Writes the summary to disk.  The summary is only written if changes
 * have occured.
 *
 * Returns %0 on succes or %-1 on fail
 **/
int
camel_lite_store_summary_save(CamelStoreSummary *s, CamelException *ex)
{
	FILE *out;
	int fd;
	int i;
	guint32 count;
	CamelStoreInfo *info;
	gchar *tmp_path;
	g_assert(s->summary_path);

	tmp_path = g_strdup_printf ("%s~", s->summary_path);

	io(printf("** saving summary\n"));

	CAMEL_STORE_SUMMARY_LOCK(s, io_lock);

	if ((s->flags & CAMEL_STORE_SUMMARY_DIRTY) == 0) {
		io(printf("**  summary clean no save\n"));
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return 0;
	}

	fd = g_open(tmp_path, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);

	if (fd == -1) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_READ,
			_("Error storing the store summary"));
		io(printf("**  open error: %s\n", strerror (errno)));
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return -1;
	}

	out = fdopen(fd, "wb");
	if ( out == NULL ) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_READ,
			_("Error storing the store summary"));
		i = errno;
		printf("**  fdopen error: %s\n", strerror (errno));
		close(fd);
		errno = i;
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return -1;
	}

	io(printf("saving header\n"));


	if ( ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out) == -1) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			_("Error storing the store summary"));
		i = errno;
		fclose(out);
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		errno = i;
		g_free (tmp_path);
		return -1;
	}

	count = s->folders->len;
	for (i=0;i<count;i++) {
		info = s->folders->pdata[i];
		((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_save(s, out, info);
	}

	if (fflush (out) != 0) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			_("Error storing the store summary"));
		i = errno;
		fclose (out);
		errno = i;
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return -1;
	}

	if (fclose (out) != 0) {
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			_("Error storing the store summary"));
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return -1;
	}

	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;

	if (g_rename(tmp_path, s->summary_path) == -1) {
		i = errno;
		g_unlink(tmp_path);
		errno = i;
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
			_("Error storing the store summary"));
		CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);
		g_free (tmp_path);
		return -1;
	}

	CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);

	if (tmp_path)
		g_free (tmp_path);

	return 0;
}


/**
 * camel_lite_store_summary_header_load:
 * @summary: a #CamelStoreSummary object
 *
 * Only load the header information from the summary,
 * keep the rest on disk.  This should only be done on
 * a fresh summary object.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_store_summary_header_load(CamelStoreSummary *s)
{
	FILE *in;
	int ret;

	g_assert(s->summary_path);

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL)
		return -1;

	CAMEL_STORE_SUMMARY_LOCK(s, io_lock);
	ret = ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in);
	CAMEL_STORE_SUMMARY_UNLOCK(s, io_lock);

	fclose(in);
	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;
	return ret;
}


/**
 * camel_lite_store_summary_add:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * #camel_lite_store_summary_next_uid_string.
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void
camel_lite_store_summary_add(CamelStoreSummary *s, CamelStoreInfo *info)
{
	if (info == NULL)
		return;

	if (camel_lite_store_info_path(s, info) == NULL) {
		g_warning("Trying to add a folder info with missing required path name\n");
		return;
	}

	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	g_ptr_array_add(s->folders, info);
	g_hash_table_insert(s->folders_path, (char *)camel_lite_store_info_path(s, info), info);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
}


/**
 * camel_lite_store_summary_add_from_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Build a new info record based on the name, and add it to the summary.
 *
 * Returns the newly added record
 **/
CamelStoreInfo *
camel_lite_store_summary_add_from_path(CamelStoreSummary *s, const char *path)
{
	CamelStoreInfo *info;

	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);

	info = g_hash_table_lookup(s->folders_path, path);
	if (info != NULL) {
		/* g_warning("Trying to add folder '%s' to summary that already has it", path); */
		info = NULL;
	} else {
		info = camel_lite_store_summary_info_new_from_path(s, path);
		g_ptr_array_add(s->folders, info);
		g_hash_table_insert(s->folders_path, (char *)camel_lite_store_info_path(s, info), info);
		s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	}

	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);

	return info;
}


/**
 * camel_lite_store_summary_info_new_from_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Create a new info record from a name.
 *
 * This info record MUST be freed using
 * #camel_lite_store_summary_info_free, #camel_lite_store_info_free will not
 * work.
 *
 * Returns the #CamelStoreInfo associated with @path
 **/
CamelStoreInfo *
camel_lite_store_summary_info_new_from_path(CamelStoreSummary *s, const char *path)
{
	return ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_new(s, path);
}


/**
 * camel_lite_store_summary_info_free:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Unref and potentially free @info, and all associated memory.
 **/
void
camel_lite_store_summary_info_free(CamelStoreSummary *s, CamelStoreInfo *info)
{
	g_assert(info);
	g_assert(s);

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);

	g_assert(info->refcount >= 1);

	info->refcount--;
	if (info->refcount > 0) {
		CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);
		return;
	}

	CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);

	((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_free(s, info);
}


/**
 * camel_lite_store_summary_info_ref:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Add an extra reference to @info.
 **/
void
camel_lite_store_summary_info_ref(CamelStoreSummary *s, CamelStoreInfo *info)
{
	g_assert(info);
	g_assert(s);

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);
	g_assert(info->refcount >= 1);
	info->refcount++;
	CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);
}


/**
 * camel_lite_store_summary_touch:
 * @summary: a #CamelStoreSummary object
 *
 * Mark the summary as changed, so that a save will force it to be
 * written back to disk.
 **/
void
camel_lite_store_summary_touch(CamelStoreSummary *s)
{
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
}


/**
 * camel_lite_store_summary_clear:
 * @summary: a #CamelStoreSummary object
 *
 * Empty the summary contents.
 **/
void
camel_lite_store_summary_clear(CamelStoreSummary *s)
{
	int i;

	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
	if (camel_lite_store_summary_count(s) == 0) {
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		return;
	}

	for (i=0;i<s->folders->len;i++)
		camel_lite_store_summary_info_free(s, s->folders->pdata[i]);

	g_ptr_array_set_size(s->folders, 0);
	g_hash_table_destroy(s->folders_path);
	s->folders_path = g_hash_table_new(g_str_hash, g_str_equal);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
}


/**
 * camel_lite_store_summary_remove:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Remove a specific @info record from the summary.
 **/
void
camel_lite_store_summary_remove(CamelStoreSummary *s, CamelStoreInfo *info)
{
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
	g_hash_table_remove(s->folders_path, camel_lite_store_info_path(s, info));
	g_ptr_array_remove(s->folders, info);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);

	camel_lite_store_summary_info_free(s, info);
}


/**
 * camel_lite_store_summary_remove_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Remove a specific info record from the summary, by @path.
 **/
void
camel_lite_store_summary_remove_path(CamelStoreSummary *s, const char *path)
{
        CamelStoreInfo *oldinfo;
        char *oldpath;

	CAMEL_STORE_SUMMARY_LOCK(s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
        if (g_hash_table_lookup_extended(s->folders_path, path, (void *)&oldpath, (void *)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		oldinfo->refcount++;
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);
		camel_lite_store_summary_remove(s, oldinfo);
		camel_lite_store_summary_info_free(s, oldinfo);
        } else {
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_STORE_SUMMARY_UNLOCK(s, ref_lock);
	}
}


/**
 * camel_lite_store_summary_remove_index:
 * @summary: a #CamelStoreSummary object
 * @index: item index
 *
 * Remove a specific info record from the summary, by index.
 **/
void
camel_lite_store_summary_remove_index(CamelStoreSummary *s, int index)
{
	CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
	if (index < s->folders->len) {
		CamelStoreInfo *info = s->folders->pdata[index];

		g_hash_table_remove(s->folders_path, camel_lite_store_info_path(s, info));
		g_ptr_array_remove_index(s->folders, index);
		s->flags |= CAMEL_STORE_SUMMARY_DIRTY;

		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		camel_lite_store_summary_info_free(s, info);
	} else {
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
	}
}

static int
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	gint32 version, flags, count;
	time_t time;

	fseek(in, 0, SEEK_SET);

	io(printf("Loading header\n"));

	if (camel_lite_file_util_decode_fixed_int32(in, &version) == -1
	    || camel_lite_file_util_decode_fixed_int32(in, &flags) == -1
	    || camel_lite_file_util_decode_time_t(in, &time) == -1
	    || camel_lite_file_util_decode_fixed_int32(in, &count) == -1) {
		return -1;
	}

	s->flags = flags;
	s->time = time;
	s->count = count;
	s->version = version;

	if (version < CAMEL_STORE_SUMMARY_VERSION_0) {
		g_warning("Store summary header version too low");
		return -1;
	}

	return 0;
}

static int
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	fseek(out, 0, SEEK_SET);

	io(printf("Savining header\n"));

	/* always write latest version */
	if (camel_lite_file_util_encode_fixed_int32(out, CAMEL_STORE_SUMMARY_VERSION)==-1) return -1;
	if (camel_lite_file_util_encode_fixed_int32(out, s->flags)==-1) return -1;
	if (camel_lite_file_util_encode_time_t(out, s->time)==-1) return -1;
	return camel_lite_file_util_encode_fixed_int32(out, camel_lite_store_summary_count(s));
}


/**
 * camel_lite_store_summary_info_new:
 * @summary: a #CamelStoreSummary object
 *
 * Allocate a new #CamelStoreInfo, suitable for adding to this
 * summary.
 *
 * Returns the newly allocated #CamelStoreInfo
 **/
CamelStoreInfo *
camel_lite_store_summary_info_new(CamelStoreSummary *s)
{
	CamelStoreInfo *info;

	info = g_slice_alloc0(s->store_info_size);
	info->refcount = 1;
	return info;
}


/**
 * camel_lite_store_info_string:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 * @type: specific string being requested
 *
 * Get a specific string from the @info.
 *
 * Returns the string value
 **/
const char *
camel_lite_store_info_string(CamelStoreSummary *s, const CamelStoreInfo *info, int type)
{
	return ((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_string(s, info, type);
}


/**
 * camel_lite_store_info_set_string:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 * @type: specific string being set
 * @value: string value to set
 *
 * Set a specific string on the @info.
 **/
void
camel_lite_store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *info, int type, const char *value)
{
	((CamelStoreSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->store_info_set_string(s, info, type, value);
}

static CamelStoreInfo *
store_info_new(CamelStoreSummary *s, const char *f)
{
	CamelStoreInfo *info;

	info = camel_lite_store_summary_info_new(s);

	info->path = g_strdup(f);
	info->unread = CAMEL_STORE_INFO_FOLDER_UNKNOWN;
	info->total = CAMEL_STORE_INFO_FOLDER_UNKNOWN;

	return info;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelStoreInfo *info;

	info = camel_lite_store_summary_info_new(s);

	io(printf("Loading folder info\n"));

	if (camel_lite_file_util_decode_string(in, &info->path) == -1)
		goto error;
	if (camel_lite_file_util_decode_uint32(in, &info->flags) == -1)
		goto error;
	if (camel_lite_file_util_decode_uint32(in, &info->unread) == -1)
		goto error;
	if (camel_lite_file_util_decode_uint32(in, &info->total) == -1)
		goto error;

	/* Ok, brown paper bag bug - prior to version 2 of the file, flags are
	   stored using the bit number, not the bit. Try to camel_lite_recover as best we can */
	if (s->version < CAMEL_STORE_SUMMARY_VERSION_2) {
		guint32 flags = 0;

		if (info->flags & 1)
			flags |= CAMEL_STORE_INFO_FOLDER_NOSELECT;
		if (info->flags & 2)
			flags |= CAMEL_STORE_INFO_FOLDER_READONLY;
		if (info->flags & 3)
			flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
		if (info->flags & 4)
			flags |= CAMEL_STORE_INFO_FOLDER_FLAGGED;

		info->flags = flags;
	}

	if (!ferror(in))
		return info;
error:
	camel_lite_store_summary_info_free(s, info);

	return NULL;
}

static int
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *info)
{
	io(printf("Saving folder info\n"));

	if( camel_lite_file_util_encode_string(out, camel_lite_store_info_path(s, info)) == -1)
		return -1;
	if( camel_lite_file_util_encode_uint32(out, info->flags) == -1)
		return -1;
	if( camel_lite_file_util_encode_uint32(out, info->unread) == -1)
		return -1;
	if( camel_lite_file_util_encode_uint32(out, info->total) == -1)
		return -1;

	return ferror(out);
}

static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *info)
{
	g_free(info->path);
	g_free(info->uri);
	g_slice_free1(s->store_info_size, info);
}

static const char *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *info, int type)
{
	const char *p;

	/* FIXME: Locks? */

	g_assert (info != NULL);

	switch (type) {
	case CAMEL_STORE_INFO_PATH:
		return info->path;
	case CAMEL_STORE_INFO_NAME:
		p = strrchr(info->path, '/');
		if (p)
			return p+1;
		else
			return info->path;
	case CAMEL_STORE_INFO_URI:
		if (info->uri == NULL) {
			CamelURL *uri;

			uri = camel_lite_url_new_with_base(s->uri_base, info->path);
			((CamelStoreInfo *)info)->uri = camel_lite_url_to_string(uri, 0);
			camel_lite_url_free(uri);
		}
		return info->uri;
	}

	return "";
}

static void
store_info_set_string (CamelStoreSummary *s, CamelStoreInfo *info, int type, const char *str)
{
	const char *p;
	char *v;
	int len;

	g_assert (info != NULL);

	switch(type) {
	case CAMEL_STORE_INFO_PATH:
		CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
		g_hash_table_remove(s->folders_path, (char *)camel_lite_store_info_path(s, info));
		g_free(info->path);
		g_free(info->uri);
		info->path = g_strdup(str);
		g_hash_table_insert(s->folders_path, (char *)camel_lite_store_info_path(s, info), info);
		s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		break;
	case CAMEL_STORE_INFO_NAME:
		CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
		g_hash_table_remove(s->folders_path, (char *)camel_lite_store_info_path(s, info));
		p = strrchr(info->path, '/');
		if (p) {
			len = p-info->path+1;
			v = g_malloc(len+strlen(str)+1);
			memcpy(v, info->path, len);
			strcpy(v+len, str);
		} else {
			v = g_strdup(str);
		}
		g_free(info->path);
		info->path = v;
		g_hash_table_insert(s->folders_path, (char *)camel_lite_store_info_path(s, info), info);
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		break;
	case CAMEL_STORE_INFO_URI:
		g_warning("Cannot set store info uri, aborting");
		abort();
		break;
	}
}
