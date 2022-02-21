/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-mime-message.h"
#include "camel-private.h"

#include "camel-mh-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_MH_SUMMARY_VERSION (0x2000)

static int mh_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static int mh_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
/*static int mh_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, CamelMessageInfo *info, CamelFolderChangeInfo *, CamelException *ex);*/

static char *mh_summary_next_uid_string(CamelFolderSummary *s);

static void camel_lite_mh_summary_class_init	(CamelMhSummaryClass *class);
static void camel_lite_mh_summary_init	(CamelMhSummary *gspaper);
static void camel_lite_mh_summary_finalise	(CamelObject *obj);

#define _PRIVATE(x) (((CamelMhSummary *)(x))->priv)

struct _CamelMhSummaryPrivate {
	char *current_uid;
};

static CamelLocalSummaryClass *parent_class;

CamelType
camel_lite_mh_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(camel_lite_local_summary_get_type (), "CamelLiteMhSummary",
					   sizeof(CamelMhSummary),
					   sizeof(CamelMhSummaryClass),
					   (CamelObjectClassInitFunc)camel_lite_mh_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_lite_mh_summary_init,
					   (CamelObjectFinalizeFunc)camel_lite_mh_summary_finalise);
	}

	return type;
}

static void
camel_lite_mh_summary_class_init (CamelMhSummaryClass *class)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) class;
	CamelLocalSummaryClass *lklass = (CamelLocalSummaryClass *)class;

	parent_class = (CamelLocalSummaryClass *)camel_lite_type_get_global_classfuncs(camel_lite_local_summary_get_type ());

	/* override methods */
	sklass->next_uid_string = mh_summary_next_uid_string;

	lklass->check = mh_summary_check;
	lklass->sync = mh_summary_sync;
	/*lklass->add = mh_summary_add;*/
}

static void
camel_lite_mh_summary_init (CamelMhSummary *o)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *) o;

	o->priv = g_malloc0(sizeof(*o->priv));
	/* set unique file version */
	s->version += CAMEL_MH_SUMMARY_VERSION;
}

static void
camel_lite_mh_summary_finalise(CamelObject *obj)
{
	CamelMhSummary *o = (CamelMhSummary *)obj;

	g_free(o->priv);
}

/**
 * camel_lite_mh_summary_new:
 *
 * Create a new CamelMhSummary object.
 *
 * Return value: A new #CamelMhSummary object.
 **/
CamelMhSummary	*camel_lite_mh_summary_new(struct _CamelFolder *folder, const char *filename, const char *mhdir, CamelIndex *index)
{
	CamelMhSummary *o = (CamelMhSummary *)camel_lite_object_new(camel_lite_mh_summary_get_type ());

	((CamelFolderSummary *)o)->folder = folder;

	camel_lite_local_summary_construct((CamelLocalSummary *)o, filename, mhdir, index);
	return o;
}

static char *mh_summary_next_uid_string(CamelFolderSummary *s)
{
	CamelMhSummary *mhs = (CamelMhSummary *)s;
	CamelLocalSummary *cls = (CamelLocalSummary *)s;
	int fd = -1;
	guint32 uid;
	char *name;
	char *uidstr;

	/* if we are working to add an existing file, then use current_uid */
	if (mhs->priv->current_uid) {
		uidstr = g_strdup(mhs->priv->current_uid);
		/* tell the summary of this, so we always append numbers to the end */
		camel_lite_folder_summary_set_uid(s, strtoul(uidstr, NULL, 10)+1);
	} else {
		/* else scan for one - and create it too, to make sure */
		do {
			close(fd);
			uid = camel_lite_folder_summary_next_uid(s);
			name = g_strdup_printf("%s/%u", cls->folder_path, uid);
			/* O_EXCL isn't guaranteed, sigh.  Oh well, bad luck, mh has problems anyway */
			fd = open(name, O_WRONLY|O_CREAT|O_EXCL, 0600);
			g_free(name);
		} while (fd == -1 && errno == EEXIST);

		close(fd);

		uidstr = g_strdup_printf("%u", uid);
	}

	return uidstr;
}

static int camel_lite_mh_summary_add(CamelLocalSummary *cls, const char *name, int forceindex)
{
	CamelMhSummary *mhs = (CamelMhSummary *)cls;
	char *filename = g_strdup_printf("%s/%s", cls->folder_path, name);
	int fd;
	CamelMimeParser *mp;

	d(printf("summarising: %s\n", name));

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		g_warning ("Cannot summarise/index: %s: %s", filename, strerror (errno));
		g_free(filename);
		return -1;
	}
	mp = camel_lite_mime_parser_new();
	camel_lite_mime_parser_scan_from(mp, FALSE);
	camel_lite_mime_parser_init_with_fd(mp, fd);

	mhs->priv->current_uid = (char *)name;
	camel_lite_folder_summary_add_from_parser((CamelFolderSummary *)mhs, mp, name);
	camel_lite_object_unref((CamelObject *)mp);
	mhs->priv->current_uid = NULL;
	g_free(filename);
	return 0;
}

static void
remove_summary(char *key, CamelMessageInfo *info, CamelLocalSummary *cls)
{
	d(printf("removing message %s from summary\n", key));
	if (cls->index)
		camel_lite_index_delete_name(cls->index, camel_lite_message_info_uid(info));

	((CamelMessageInfoBase*)info)->flags |= CAMEL_MESSAGE_EXPUNGED;
	camel_lite_folder_summary_remove((CamelFolderSummary *)cls, info);
	camel_lite_message_info_free(info);
}

static int
sort_uid_cmp(const void *ap, const void *bp)
{
	const CamelMessageInfo
		*a = *((CamelMessageInfo **)ap),
		*b = *((CamelMessageInfo **)bp);
	const char
		*auid = camel_lite_message_info_uid(a),
		*buid = camel_lite_message_info_uid(b);
	int aval = atoi(auid), bval = atoi(buid);

	return (aval < bval) ? -1 : (aval > bval) ? 1 : 0;
}

static int
mh_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	DIR *dir;
	struct dirent *d;
	char *p, c;
	CamelMessageInfo *info;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	GHashTable *left;
	int i, count;
	int forceindex;

	/* FIXME: Handle changeinfo */

	d(printf("checking summary ...\n"));

	/* scan the directory, check for mail files not in the index, or index entries that
	   no longer exist */
	dir = opendir(cls->folder_path);
	if (dir == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot open MH directory path: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		return -1;
	}

	/* keeps track of all uid's that have not been processed */
	left = g_hash_table_new(g_str_hash, g_str_equal);
	count = camel_lite_folder_summary_count((CamelFolderSummary *)cls);
	forceindex = count == 0;
	for (i=0;i<count;i++) {
		info = camel_lite_folder_summary_index((CamelFolderSummary *)cls, i);
		if (info) {
			g_hash_table_insert(left, (char *)camel_lite_message_info_uid(info), info);
		}
	}

	camel_lite_folder_summary_prepare_hash ((CamelFolderSummary *)cls);
	while ( (d = readdir(dir)) ) {
		/* FIXME: also run stat to check for regular file */
		p = d->d_name;
		while ( (c = *p++) ) {
			if (!isdigit(c))
				break;
		}
		if (c==0) {
			info = camel_lite_folder_summary_uid((CamelFolderSummary *)cls, d->d_name);
			if (info == NULL || (cls->index && (!camel_lite_index_has_name(cls->index, d->d_name)))) {
				/* need to add this file to the summary */
				if (info != NULL) {
					g_hash_table_remove(left, camel_lite_message_info_uid(info));
					((CamelMessageInfoBase*)info)->flags |= CAMEL_MESSAGE_EXPUNGED;
					camel_lite_folder_summary_remove((CamelFolderSummary *)cls, info);
					camel_lite_message_info_free(info);
				}
				camel_lite_mh_summary_add(cls, d->d_name, forceindex);
			} else {
				const char *uid = camel_lite_message_info_uid(info);
				CamelMessageInfo *old = g_hash_table_lookup(left, uid);

				if (old) {
					camel_lite_message_info_free(old);
					g_hash_table_remove(left, uid);
				}
				camel_lite_message_info_free(info);
			}
		}
	}
	closedir(dir);
	camel_lite_folder_summary_kill_hash ((CamelFolderSummary *)cls);

	g_hash_table_foreach(left, (GHFunc)remove_summary, cls);
	g_hash_table_destroy(left);

	/* sort the summary based on message number (uid), since the directory order is not useful */
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	qsort(s->messages->pdata, s->messages->len, sizeof(CamelMessageInfo *), sort_uid_cmp);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return 0;
}

/* sync the summary file with the ondisk files */
static int
mh_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changes, CamelException *ex)
{
	int count, i;
	CamelLocalMessageInfo *info;
	char *name;
	const char *uid;

	d(printf("summary_sync(expunge=%s)\n", expunge?"true":"false"));

	/* we could probably get away without this ... but why not use it, esp if we're going to
	   be doing any significant io already */
	if (camel_lite_local_summary_check(cls, changes, ex) == -1)
		return -1;

	/* FIXME: need to update/honour .mh_sequences or whatever it is */

	count = camel_lite_folder_summary_count((CamelFolderSummary *)cls);
	for (i=count-1;i>=0;i--) {
		info = (CamelLocalMessageInfo *)camel_lite_folder_summary_index((CamelFolderSummary *)cls, i);
		g_assert(info);
		if (expunge && (info->info.flags & CAMEL_MESSAGE_DELETED)) {
			uid = camel_lite_message_info_uid(info);
			name = g_strdup_printf("%s/%s", cls->folder_path, uid);
			d(printf("deleting %s\n", name));
			if (unlink(name) == 0 || errno==ENOENT) {

				/* FIXME: put this in folder_summary::remove()? */
				if (cls->index)
					camel_lite_index_delete_name(cls->index, (char *)uid);

				camel_lite_folder_change_info_remove_uid(changes, uid);
				((CamelMessageInfoBase*)info)->flags |= CAMEL_MESSAGE_EXPUNGED;
				camel_lite_folder_summary_remove((CamelFolderSummary *)cls, (CamelMessageInfo *)info);
			}
			g_free(name);
		} else if (info->info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			info->info.flags &= 0x1fff;
		}
		camel_lite_message_info_free(info);
	}

	return ((CamelLocalSummaryClass *)parent_class)->sync(cls, expunge, changes, ex);
}
