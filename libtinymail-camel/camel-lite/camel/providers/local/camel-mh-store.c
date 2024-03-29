/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>

#include "camel-exception.h"
#include "camel-private.h"
#include "camel-stream-buffer.h"
#include "camel-stream-fs.h"
#include "camel-url.h"

#include "camel-mh-folder.h"
#include "camel-mh-store.h"
#include "camel-mh-summary.h"
#include "camel-string-utils.h"
#include "camel-file-utils.h"

static CamelLocalStoreClass *parent_class = NULL;

#define d(x)

/* Returns the class for a CamelMhStore */
#define CMHS_CLASS(so) CAMEL_MH_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMHF_CLASS(so) CAMEL_MH_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static void construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex);
static CamelFolder *get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex);
static CamelFolder *get_inbox (CamelStore *store, CamelException *ex);
static void delete_folder(CamelStore * store, const char *folder_name, CamelException * ex);
static void rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex);
static CamelFolderInfo * get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex);

static void camel_lite_mh_store_class_init(CamelObjectClass * camel_lite_mh_store_class)
{
	CamelStoreClass *camel_lite_store_class = CAMEL_STORE_CLASS(camel_lite_mh_store_class);
	CamelServiceClass *camel_lite_service_class = CAMEL_SERVICE_CLASS(camel_lite_mh_store_class);

	parent_class = (CamelLocalStoreClass *)camel_lite_type_get_global_classfuncs(camel_lite_local_store_get_type());

	/* virtual method overload, use defaults for most */
	camel_lite_service_class->construct = construct;

	camel_lite_store_class->get_folder = get_folder;
	camel_lite_store_class->get_inbox = get_inbox;
	camel_lite_store_class->delete_folder = delete_folder;
	camel_lite_store_class->rename_folder = rename_folder;
	camel_lite_store_class->get_folder_info = get_folder_info;
}

CamelType camel_lite_mh_store_get_type(void)
{
	static CamelType camel_lite_mh_store_type = CAMEL_INVALID_TYPE;

	if (camel_lite_mh_store_type == CAMEL_INVALID_TYPE) {
		camel_lite_mh_store_type = camel_lite_type_register(CAMEL_LOCAL_STORE_TYPE, "CamelLiteMhStore",
							  sizeof(CamelMhStore),
							  sizeof(CamelMhStoreClass),
							  (CamelObjectClassInitFunc) camel_lite_mh_store_class_init,
							  NULL,
							  NULL,
							  NULL);
	}

	return camel_lite_mh_store_type;
}

static void
construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	CamelMhStore *mh_store = (CamelMhStore *)service;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_lite_exception_is_set (ex))
		return;

	if (camel_lite_url_get_param(url, "dotfolders"))
		mh_store->flags |= CAMEL_MH_DOTFOLDERS;
}

enum {
	UPDATE_NONE,
	UPDATE_ADD,
	UPDATE_REMOVE,
	UPDATE_RENAME
};

/* update the .folders file if it exists, or create it if it doesn't */
static void
folders_update(const char *root, int mode, const char *folder, const char *new)
{
	char *tmp, *tmpnew, *line = NULL;
	CamelStream *stream, *in = NULL, *out = NULL;
	int flen = strlen(folder);

	tmpnew = g_alloca (strlen (root) + 16);
	sprintf (tmpnew, "%s.folders~", root);

	out = camel_lite_stream_fs_new_with_name(tmpnew, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (out == NULL)
		goto fail;

	tmp = g_alloca (strlen (root) + 16);
	sprintf (tmp, "%s.folders", root);
	stream = camel_lite_stream_fs_new_with_name(tmp, O_RDONLY, 0);
	if (stream) {
		in = camel_lite_stream_buffer_new(stream, CAMEL_STREAM_BUFFER_READ);
		camel_lite_object_unref(stream);
	}
	if (in == NULL || stream == NULL) {
		if (mode == UPDATE_ADD && camel_lite_stream_printf(out, "%s\n", folder) == -1)
			goto fail;
		goto done;
	}

	while ((line = camel_lite_stream_buffer_read_line((CamelStreamBuffer *)in))) {
		int copy = TRUE;

		switch (mode) {
		case UPDATE_REMOVE:
			if (strcmp(line, folder) == 0)
				copy = FALSE;
			break;
		case UPDATE_RENAME:
			if (strncmp(line, folder, flen) == 0
			    && (line[flen] == 0 || line[flen] == '/')) {
				if (camel_lite_stream_write(out, new, strlen(new)) == -1
				    || camel_lite_stream_write(out, line+flen, strlen(line)-flen) == -1
				    || camel_lite_stream_write(out, "\n", 1) == -1)
					goto fail;
				copy = FALSE;
			}
			break;
		case UPDATE_ADD: {
			int cmp = strcmp(line, folder);

			if (cmp > 0) {
				/* found insertion point */
				if (camel_lite_stream_printf(out, "%s\n", folder) == -1)
					goto fail;
				mode = UPDATE_NONE;
			} else if (tmp == NULL) {
				/* already there */
				mode = UPDATE_NONE;
			}
			break; }
		case UPDATE_NONE:
			break;
		}

		if (copy && camel_lite_stream_printf(out, "%s\n", line) == -1)
			goto fail;

		g_free(line);
		line = NULL;
	}

	/* add to end? */
	if (mode == UPDATE_ADD && camel_lite_stream_printf(out, "%s\n", folder) == -1)
		goto fail;

	if (camel_lite_stream_close(out) == -1)
		goto fail;

done:
	/* should we care if this fails?  I suppose so ... */
	rename(tmpnew, tmp);
fail:
	unlink(tmpnew);		/* remove it if its there */
	g_free(line);
	if (in)
		camel_lite_object_unref(in);
	if (out)
		camel_lite_object_unref(out);
}

static CamelFolder *
get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex)
{
	char *name;
	struct stat st;

	if (!((CamelStoreClass *)parent_class)->get_folder(store, folder_name, flags, ex))
		return NULL;

	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);

	if (stat(name, &st) == -1) {
		if (errno != ENOENT) {
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Cannot get folder `%s': %s"),
					     folder_name, g_strerror (errno));
			g_free (name);
			return NULL;
		}
		if ((flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
					     _("Cannot get folder `%s': folder does not exist."),
					     folder_name);
			g_free (name);
			return NULL;
		}

		if (mkdir(name, 0777) != 0) {
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Could not create folder `%s': %s"),
					     folder_name, g_strerror (errno));
			g_free (name);
			return NULL;
		}

		/* add to .folders if we are supposed to */
		/* FIXME: throw exception on error */
		if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS)
			folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_ADD, folder_name, NULL);
	} else if (!S_ISDIR(st.st_mode)) {
		camel_lite_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("Cannot get folder `%s': not a directory."), folder_name);
		g_free (name);
		return NULL;
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder `%s': folder exists."), folder_name);
		g_free (name);
		return NULL;
	}

	g_free(name);

	return camel_lite_mh_folder_new(store, folder_name, flags, ex);
}

static CamelFolder *
get_inbox (CamelStore *store, CamelException *ex)
{
	return get_folder (store, "inbox", 0, ex);
}

static void delete_folder(CamelStore * store, const char *folder_name, CamelException * ex)
{
	char *name;

	/* remove folder directory - will fail if not empty */
	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	if (rmdir(name) == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not delete folder `%s': %s"),
				      folder_name, g_strerror (errno));
		g_free(name);
		return;
	}
	g_free(name);

	/* remove from .folders if we are supposed to */
	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS)
		folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_REMOVE, folder_name, NULL);

	/* and remove metadata */
	((CamelStoreClass *)parent_class)->delete_folder(store, folder_name, ex);
}

static void
rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex)
{
	CamelException e;

	camel_lite_exception_init(&e);
	((CamelStoreClass *)parent_class)->rename_folder(store, old, new, &e);
	if (camel_lite_exception_is_set(&e)) {
		camel_lite_exception_xfer(ex, &e);
		return;
	}
	camel_lite_exception_clear(&e);

	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS) {
		/* yeah this is messy, but so is mh! */
		folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_RENAME, old, new);
	}
}

static void
fill_fi(CamelStore *store, CamelFolderInfo *fi, guint32 flags)
{
	CamelFolder *folder;
	char *path, *folderpath;
	const char *root;

	folder = camel_lite_object_bag_get(store->folders, fi->full_name);

	if (folder == NULL
	    && (flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
		folder = camel_lite_store_get_folder(store, fi->full_name, 0, NULL);

	/* This should be fast enough not to have to test for INFO_FAST */
	/* We could: if we have no folder, and FAST isn't specified, perform a full
	   scan of all messages for their status flags.  But its probably not worth
	   it as we need to read the top of every file, i.e. very very slow */
	root = camel_lite_local_store_get_toplevel_dir((CamelLocalStore *)store);
	path = g_strdup_printf("%s/%s.ev-summary.mmap", root, fi->full_name);
	folderpath = g_strdup_printf("%s/%s", root, fi->full_name);

	if (folder) {
		if ((flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
			camel_lite_folder_refresh_info(folder, NULL);
		fi->unread = camel_lite_folder_get_unread_message_count(folder);
		fi->total = camel_lite_folder_get_message_count(folder);
		camel_lite_object_unref(folder);
	} else {
		fi->total = -1;
		fi->unread = -1;
		camel_lite_file_util_read_counts (path, fi);
	}

	if (folderpath) {
		fi->local_size = 0;
		camel_lite_du (folderpath, (int *) &fi->local_size);
	}

	g_free(folderpath);
	g_free(path);

}

static CamelFolderInfo *
folder_info_new (CamelStore *store, CamelURL *url, const char *root, const char *path, guint32 flags)
{
	/* FIXME: need to set fi->flags = CAMEL_FOLDER_NOSELECT (and possibly others) when appropriate */
	CamelFolderInfo *fi;
	char *base;

	base = strrchr(path, '/');

	camel_lite_url_set_fragment (url, path);

	/* Build the folder info structure. */
	fi = camel_lite_folder_info_new ();
	fi->uri = camel_lite_url_to_string (url, 0);
	fi->full_name = g_strdup(path);
	fi->name = g_strdup(base?base+1:path);
	fill_fi(store, fi, flags);

	d(printf("New folderinfo:\n '%s'\n '%s'\n '%s'\n", fi->full_name, fi->uri, fi->path));

	return fi;
}

/* used to find out where we've visited already */
struct _inode {
	dev_t dnode;
	ino_t inode;
};

/* Scan path, under root, for directories to add folders for.  Both
 * root and path should have a trailing "/" if they aren't empty. */
static void
recursive_scan (CamelStore *store, CamelURL *url, CamelFolderInfo **fip, CamelFolderInfo *parent,
		GHashTable *visited, const char *root, const char *path, guint32 flags)
{
	char *fullpath, *tmp;
	DIR *dp;
	struct dirent *d;
	struct stat st;
	CamelFolderInfo *fi;
	struct _inode in, *inew;

	/* Open the specified directory. */
	if (path[0]) {
		fullpath = alloca (strlen (root) + strlen (path) + 2);
		sprintf (fullpath, "%s/%s", root, path);
	} else
		fullpath = (char *)root;

	if (stat(fullpath, &st) == -1 || !S_ISDIR(st.st_mode))
		return;

	in.dnode = st.st_dev;
	in.inode = st.st_ino;

	/* see if we've visited already */
	if (g_hash_table_lookup(visited, &in) != NULL)
		return;

	inew = g_malloc(sizeof(*inew));
	*inew = in;
	g_hash_table_insert(visited, inew, inew);

	/* link in ... */
	fi = folder_info_new(store, url, root, path, flags);
	fi->parent = parent;
	fi->next = *fip;
	*fip = fi;

	if (((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) || parent == NULL)) {
		/* now check content for possible other directories */
		dp = opendir(fullpath);
		if (dp == NULL)
			return;

		/* Look for subdirectories to add and scan. */
		while ((d = readdir(dp)) != NULL) {
			/* Skip current and parent directory. */
			if (strcmp(d->d_name, ".") == 0
			    || strcmp(d->d_name, "..") == 0)
				continue;

			/* skip fully-numerical entries (i.e. mh messages) */
			strtoul(d->d_name, &tmp, 10);
			if (*tmp == 0)
				continue;

			/* otherwise, treat at potential node, and recurse, a bit more expensive than needed, but tough! */
			if (path[0]) {
				tmp = g_strdup_printf("%s/%s", path, d->d_name);
				recursive_scan(store, url, &fi->child, fi, visited, root, tmp, flags);
				g_free(tmp);
			} else {
				recursive_scan(store, url, &fi->child, fi, visited, root, d->d_name, flags);
			}
		}

		closedir(dp);
	}
}

/* scan a .folders file */
static void
folders_scan(CamelStore *store, CamelURL *url, const char *root, const char *top, CamelFolderInfo **fip, guint32 flags)
{
	CamelFolderInfo *fi;
	char  line[512], *path, *tmp;
	CamelStream *stream, *in;
	struct stat st;
	GPtrArray *folders;
	GHashTable *visited;
	int len;

	tmp = g_alloca (strlen (root) + 16);
	sprintf (tmp, "%s/.folders", root);
	stream = camel_lite_stream_fs_new_with_name(tmp, 0, O_RDONLY);
	if (stream == NULL)
		return;

	in = camel_lite_stream_buffer_new(stream, CAMEL_STREAM_BUFFER_READ);
	camel_lite_object_unref(stream);
	if (in == NULL)
		return;

	visited = g_hash_table_new(g_str_hash, g_str_equal);
	folders = g_ptr_array_new();

	while ( (len = camel_lite_stream_buffer_gets((CamelStreamBuffer *)in, line, sizeof(line))) > 0) {
		/* ignore blank lines */
		if (len <= 1)
			continue;
		/* check for invalidly long lines, we abort evreything and fallback */
		if (line[len-1] != '\n') {
			int i;

			for (i=0;i<folders->len;i++)
				camel_lite_folder_info_free(folders->pdata[i]);
			g_ptr_array_set_size(folders, 0);
			break;
		}
		line[len-1] = 0;

		/* check for \r ? */

		if (top && top[0]) {
			int toplen = strlen(top);

			/* check is dir or subdir */
			if (strncmp(top, line, toplen) != 0
			    || (line[toplen] != 0 && line[toplen] != '/'))
				continue;

			/* check is not sub-subdir if not recursive */
			if ((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) == 0
			    && (tmp = strrchr(line, '/'))
			    && tmp > line+toplen)
				continue;
		}

		if (g_hash_table_lookup(visited, line) != NULL)
			continue;

		tmp = g_strdup(line);
		g_hash_table_insert(visited, tmp, tmp);

		path = g_strdup_printf("%s/%s", root, line);
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			fi = folder_info_new(store, url, root, line, flags);
			g_ptr_array_add(folders, fi);
		}
		g_free(path);
	}

	if (folders->len)
		*fip = camel_lite_folder_info_build(folders, top, '/', TRUE);
	g_ptr_array_free(folders, TRUE);

	g_hash_table_foreach(visited, (GHFunc)g_free, NULL);
	g_hash_table_destroy(visited);

	camel_lite_object_unref(in);
}

/* FIXME: move to camel-local, this is shared with maildir code */
static guint inode_hash(const void *d)
{
	const struct _inode *v = d;

	return v->inode ^ v->dnode;
}

static gboolean inode_equal(const void *a, const void *b)
{
	const struct _inode *v1 = a, *v2 = b;

	return v1->inode == v2->inode && v1->dnode == v2->dnode;
}

static void inode_free(void *k, void *v, void *d)
{
	g_free(k);
}

static CamelFolderInfo *
get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelFolderInfo *fi = NULL;
	CamelURL *url;
	char *root;

	root = ((CamelService *)store)->url->path;

	url = camel_lite_url_copy (((CamelService *) store)->url);

	/* use .folders if we are supposed to */
	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS) {
		folders_scan(store, url, root, top, &fi, flags);
	} else {
		GHashTable *visited = g_hash_table_new(inode_hash, inode_equal);

		if (top == NULL)
			top = "";

		recursive_scan(store, url, &fi, NULL, visited, root, top, flags);

		/* if we actually scanned from root, we have a "" root node we dont want */
		if (fi != NULL && top[0] == 0) {
			CamelFolderInfo *rfi;

			rfi = fi;
			fi = rfi->child;
			rfi->child = NULL;
			camel_lite_folder_info_free(rfi);
		}

		g_hash_table_foreach(visited, inode_free, NULL);
		g_hash_table_destroy(visited);
	}

	camel_lite_url_free (url);

	return fi;
}
