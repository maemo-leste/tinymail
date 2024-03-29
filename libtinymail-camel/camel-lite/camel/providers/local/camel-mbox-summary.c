/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _LARGEFILE64_SOURCE
#define O_LARGEFILE 0
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-file-utils.h"
#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-private.h"

#include "camel-mbox-summary.h"

#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_MBOX_SUMMARY_VERSION (1)

static int summary_header_load (CamelFolderSummary *);
static int summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new_from_header(CamelFolderSummary *, struct _camel_lite_header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_load (CamelFolderSummary *, gboolean *);
static int		  message_info_save (CamelFolderSummary *, FILE *, CamelMessageInfo *);
static int 		  meta_message_info_save(CamelFolderSummary *s, FILE *out_meta, FILE *out, CamelMessageInfo *mi);
/*static void		  message_info_free (CamelFolderSummary *, CamelMessageInfo *);*/

static char *mbox_summary_encode_x_evolution (CamelLocalSummary *cls, const CamelLocalMessageInfo *mi);

static int mbox_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static int mbox_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
#ifdef STATUS_PINE
static CamelMessageInfo *mbox_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, CamelException *ex);
#endif

static int mbox_summary_sync_quick(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static int mbox_summary_sync_full(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);

static void camel_lite_mbox_summary_class_init (CamelMboxSummaryClass *klass);
static void camel_lite_mbox_summary_init       (CamelMboxSummary *obj);
static void camel_lite_mbox_summary_finalise   (CamelObject *obj);

#ifdef STATUS_PINE
/* Which status flags are stored in each separate header */
#define STATUS_XSTATUS (CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_DELETED)
#define STATUS_STATUS (CAMEL_MESSAGE_SEEN)

static void encode_status(guint32 flags, char status[8]);
static guint32 decode_status(const char *status);
#endif

static CamelLocalSummaryClass *camel_lite_mbox_summary_parent;

CamelType
camel_lite_mbox_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(camel_lite_local_summary_get_type(), "CamelLiteMboxSummary",
					   sizeof (CamelMboxSummary),
					   sizeof (CamelMboxSummaryClass),
					   (CamelObjectClassInitFunc) camel_lite_mbox_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_lite_mbox_summary_init,
					   (CamelObjectFinalizeFunc) camel_lite_mbox_summary_finalise);
	}

	return type;
}
static gboolean
mbox_info_set_user_flag(CamelMessageInfo *mi, const char *name, gboolean value)
{
	int res;

	res = ((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->info_set_user_flag(mi, name, value);
	if (res)
		((CamelLocalMessageInfo *)mi)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

static gboolean
mbox_info_set_user_tag(CamelMessageInfo *mi, const char *name, const char *value)
{
	int res;

	res = ((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->info_set_user_tag(mi, name, value);
	if (res)
		((CamelLocalMessageInfo *)mi)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

#ifdef STATUS_PINE
static gboolean
mbox_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	/* Basically, if anything could change the Status line, presume it does */
	if (((CamelMboxSummary *)mi->summary)->xstatus
	    && (flags & (CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_DELETED))) {
		flags |= CAMEL_MESSAGE_FOLDER_XEVCHANGE|CAMEL_MESSAGE_FOLDER_FLAGGED;
		set |= CAMEL_MESSAGE_FOLDER_XEVCHANGE|CAMEL_MESSAGE_FOLDER_FLAGGED;
	}

	return ((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->info_set_flags(mi, flags, set);
}
#endif

static void
camel_lite_mbox_summary_class_init(CamelMboxSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *)klass;
	CamelLocalSummaryClass *lklass = (CamelLocalSummaryClass *)klass;

	camel_lite_mbox_summary_parent = (CamelLocalSummaryClass *)camel_lite_type_get_global_classfuncs(camel_lite_local_summary_get_type());

	sklass->summary_header_load = summary_header_load;
	sklass->summary_header_save = summary_header_save;

	sklass->message_info_new_from_header  = message_info_new_from_header;
	sklass->message_info_new_from_parser = message_info_new_from_parser;
	sklass->message_info_load = message_info_load;
	sklass->message_info_save = message_info_save;
	sklass->meta_message_info_save = meta_message_info_save;
	/*sklass->message_info_free = message_info_free;*/

	sklass->info_set_user_flag = mbox_info_set_user_flag;
	sklass->info_set_user_tag = mbox_info_set_user_tag;
#ifdef STATUS_PINE
	sklass->info_set_flags = mbox_info_set_flags;
#endif

	lklass->encode_x_evolution = mbox_summary_encode_x_evolution;
	lklass->check = mbox_summary_check;
	lklass->sync = mbox_summary_sync;
#ifdef STATUS_PINE
	lklass->add = mbox_summary_add;
#endif

	klass->sync_quick = mbox_summary_sync_quick;
	klass->sync_full = mbox_summary_sync_full;
}

static void
camel_lite_mbox_summary_init(CamelMboxSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelMboxMessageInfo);
	s->content_info_size = sizeof(CamelMboxMessageContentInfo);

	/* and a unique file version */
	s->version += CAMEL_MBOX_SUMMARY_VERSION;
}

static void
camel_lite_mbox_summary_finalise(CamelObject *obj)
{
	/*CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(obj);*/
}

/**
 * camel_lite_mbox_summary_new:
 *
 * Create a new CamelMboxSummary object.
 *
 * Return value: A new CamelMboxSummary widget.
 **/
CamelMboxSummary *
camel_lite_mbox_summary_new(struct _CamelFolder *folder, const char *filename, const char *mbox_name, CamelIndex *index)
{
	CamelMboxSummary *new = (CamelMboxSummary *)camel_lite_object_new(camel_lite_mbox_summary_get_type());

	((CamelFolderSummary *)new)->folder = folder;

	camel_lite_local_summary_construct((CamelLocalSummary *)new, filename, mbox_name, index);
	return new;
}

void camel_lite_mbox_summary_xstatus(CamelMboxSummary *mbs, int state)
{
	mbs->xstatus = state;
}

static char *
mbox_summary_encode_x_evolution (CamelLocalSummary *cls, const CamelLocalMessageInfo *mi)
{
	const char *p, *uidstr;
	guint32 uid;

	/* This is busted, it is supposed to encode ALL DATA */
	p = uidstr = camel_lite_message_info_uid(mi);
	while (*p && isdigit(*p))
		p++;

	if (*p == 0 && sscanf(uidstr, "%u", &uid) == 1) {
		return g_strdup_printf("%08x-%04x", uid, mi->info.flags & 0x1fff);
	} else {
		return g_strdup_printf("%s-%04x", uidstr, mi->info.flags & 0x1fff);
	}
}

static int
summary_header_load(CamelFolderSummary *s)
{
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);
	unsigned char *ptrchr;

	if (((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->summary_header_load(s) == -1)
		return -1;

	/* legacy version */
	if (s->version == 0x120c) {
		unsigned char* ptrchr = s->filepos;
		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &mbs->folder_size, FALSE);
		s->filepos = ptrchr;
	}

	/* version 1 */
	mbs->version = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	ptrchr = s->filepos;
	ptrchr = camel_lite_file_util_mmap_decode_size_t (ptrchr, &mbs->folder_size);
	s->filepos = ptrchr;

	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);

	if (((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->summary_header_save(s, out) == -1)
		return -1;

	if (camel_lite_file_util_encode_fixed_int32(out, CAMEL_MBOX_SUMMARY_VERSION) == -1) return -1;

	return camel_lite_file_util_encode_size_t(out, mbs->folder_size);
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	CamelMboxMessageInfo *mi;
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	mi = (CamelMboxMessageInfo *)((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->message_info_new_from_header(s, h);
	if (mi) {
		const char *xev, *uid;
		CamelMboxMessageInfo *info = NULL;
		int add = 0;	/* bitmask of things to add, 1 assign uid, 2, just add as new, 4 = recent */
#ifdef STATUS_PINE
		const char *status = NULL, *xstatus = NULL;
		guint32 flags = 0;

		if (mbs->xstatus) {
			/* check for existance of status & x-status headers */
			status = camel_lite_header_raw_find(&h, "Status", NULL);
			if (status)
				flags = decode_status(status);
			xstatus = camel_lite_header_raw_find(&h, "X-Status", NULL);
			if (xstatus)
				flags |= decode_status(xstatus);
		}
#endif
		/* if we have an xev header, use it, else assign a new one */
		xev = camel_lite_header_raw_find(&h, "X-Evolution", NULL);
		if (xev != NULL
		    && camel_lite_local_summary_decode_x_evolution((CamelLocalSummary *)s, xev, &mi->info) == 0) {
			uid = camel_lite_message_info_uid(mi);
			d(printf("found valid x-evolution: %s\n", uid));
			info = (CamelMboxMessageInfo *)camel_lite_folder_summary_uid(s, uid);
			if (info) {
				if ((info->info.info.flags & CAMEL_MESSAGE_FOLDER_NOTSEEN)) {
					info->info.info.flags &= ~CAMEL_MESSAGE_FOLDER_NOTSEEN;
					camel_lite_message_info_free(mi);
					mi = info;
				} else {
					add = 7;
					d(printf("seen '%s' before, adding anew\n", uid));
					camel_lite_message_info_free(info);
				}
			} else {
				add = 2;
				d(printf("but isn't present in summary\n"));
			}
		} else {
			d(printf("didn't find x-evolution\n"));
			add = 7;
		}

		if (add&1) {
			mi->info.info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED | CAMEL_MESSAGE_FOLDER_NOXEV;
			g_free (mi->info.info.uid);
			mi->info.info.uid = camel_lite_folder_summary_next_uid_string(s);
		} else {
			camel_lite_folder_summary_set_uid(s, strtoul(camel_lite_message_info_uid(mi), NULL, 10));
		}
#ifdef STATUS_PINE
		if (mbs->xstatus && add&2) {
			/* use the status as the flags when we read it the first time */
			if (status)
				mi->info.info.flags = (mi->info.info.flags & ~(STATUS_STATUS)) | (flags & STATUS_STATUS);
			if (xstatus)
				mi->info.info.flags = (mi->info.info.flags & ~(STATUS_XSTATUS)) | (flags & STATUS_XSTATUS);
		}
#endif
		if (mbs->changes) {
			if (add&2)
				camel_lite_folder_change_info_add_uid(mbs->changes, camel_lite_message_info_uid(mi));
			if ((add&4) && status == NULL)
				camel_lite_folder_change_info_recent_uid(mbs->changes, camel_lite_message_info_uid(mi));
		}

		mi->frompos = -1;
	}

	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi;

	mi = ((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->message_info_new_from_parser(s, mp);
	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		mbi->frompos = camel_lite_mime_parser_tell_start_from(mp);
	}

	return mi;
}


static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, gboolean *must_add)
{
	CamelMessageInfo *mi;

	io(printf("loading mbox message info\n"));

	mi = ((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->message_info_load(s, must_add);
	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		unsigned char *ptrchr = s->filepos;
		ptrchr = camel_lite_file_util_mmap_decode_off_t (ptrchr, &mbi->frompos);
		s->filepos = ptrchr;
	}

	return mi;
}

static int
meta_message_info_save(CamelFolderSummary *s, FILE *out_meta, FILE *out, CamelMessageInfo *mi)
{
	CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

	io(printf("saving mbox message info\n"));

	if (((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->meta_message_info_save(s, out_meta, out, mi) == -1
	    || camel_lite_file_util_encode_off_t(out_meta, mbi->frompos) == -1)
		return -1;

	return 0;
}

static int
message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *mi)
{
	CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

	io(printf("saving mbox message info\n"));

	if (((CamelFolderSummaryClass *)camel_lite_mbox_summary_parent)->message_info_save(s, out, mi) == -1
	    || camel_lite_file_util_encode_off_t (out, mbi->frompos) == -1)
		return -1;

	return 0;
}

/* like summary_rebuild, but also do changeinfo stuff (if supplied) */
static int
summary_update(CamelLocalSummary *cls, off_t offset, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	int i, count;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelMimeParser *mp;
	CamelMboxMessageInfo *mi;
	int fd;
	int ok = 0;
	struct stat st;
	off_t size = 0;

	d(printf("Calling summary update, from pos %d\n", (int)offset));

	cls->index_force = FALSE;

	camel_lite_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE | O_RDONLY | O_BINARY, 0);
	if (fd == -1) {
		d(printf("%s failed to open: %s\n", cls->folder_path, strerror (errno)));
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not open folder: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		camel_lite_operation_end(NULL);
		return -1;
	}

	if (fstat(fd, &st) == 0)
		size = st.st_size;

	mp = camel_lite_mime_parser_new();
	camel_lite_mime_parser_init_with_fd(mp, fd);
	camel_lite_mime_parser_scan_from(mp, TRUE);
	camel_lite_mime_parser_seek(mp, offset, SEEK_SET);

	if (offset > 0) {
		if (camel_lite_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM
		    && camel_lite_mime_parser_tell_start_from(mp) == offset) {
			camel_lite_mime_parser_unstep(mp);
		} else {
			g_warning("The next message didn't start where I expected, building summary from start");
			camel_lite_mime_parser_drop_step(mp);
			offset = 0;
			camel_lite_mime_parser_seek(mp, offset, SEEK_SET);
		}
	}

	/* we mark messages as to whether we've seen them or not.
	   If we're not starting from the start, we must be starting
	   from the old end, so everything must be treated as new */
	count = camel_lite_folder_summary_count(s);
	for (i=0;i<count;i++) {
		mi = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);
		if (mi) {
			if (offset == 0)
				mi->info.info.flags |= CAMEL_MESSAGE_FOLDER_NOTSEEN;
			else
				mi->info.info.flags &= ~CAMEL_MESSAGE_FOLDER_NOTSEEN;
			camel_lite_message_info_free(mi);
		}
	}
	mbs->changes = changeinfo;

	while (camel_lite_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMessageInfo *info;
		off_t pc = camel_lite_mime_parser_tell_start_from (mp) + 1;
		gchar *nuid;

		camel_lite_operation_progress (NULL, pc, size);

		nuid = camel_lite_folder_summary_next_uid_string (s);
		info = camel_lite_folder_summary_add_from_parser(s, mp, nuid);
		g_free (nuid);

		if (info == NULL) {
			camel_lite_exception_setv(ex, 1, _("Fatal mail parser error near position %ld in folder %s"),
					     camel_lite_mime_parser_tell(mp), cls->folder_path);
			ok = -1;
			break;
		}

		g_assert(camel_lite_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM_END);
	}

	camel_lite_object_unref(CAMEL_OBJECT (mp));

	count = camel_lite_folder_summary_count(s);
	for (i=0;i<count;i++) {
		mi = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);
		/* must've dissapeared from the file? */
		if (mi && mi->info.info.flags & CAMEL_MESSAGE_FOLDER_NOTSEEN) {
			d(printf("uid '%s' vanished, removing", camel_lite_message_info_uid(mi)));
			if (changeinfo)
				camel_lite_folder_change_info_remove_uid(changeinfo, camel_lite_message_info_uid(mi));
			camel_lite_folder_summary_remove(s, (CamelMessageInfo *)mi);
			count--;
			i--;
		}
		camel_lite_message_info_free(mi);
	}
	mbs->changes = NULL;

	/* update the file size/mtime in the summary */
	if (ok != -1) {
		if (g_stat(cls->folder_path, &st) == 0) {
			camel_lite_folder_summary_touch(s);
			mbs->folder_size = st.st_size;
			s->time = st.st_mtime;
		}
	}

	camel_lite_operation_end(NULL);

	return ok;
}

static int
mbox_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	struct stat st;
	int ret = 0;
	int i, count;

	d(printf("Checking summary\n"));

	/* check if the summary is up-to-date */
	if (g_stat(cls->folder_path, &st) == -1) {
		camel_lite_folder_summary_dispose_all (s);
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot check folder: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		return -1;
	}

	if (cls->check_force)
		mbs->folder_size = 0;
	cls->check_force = 0;

	if (st.st_size == 0) {
		/* empty?  No need to scan at all */
		d(printf("Empty mbox, clearing summary\n"));
		count= camel_lite_folder_summary_count(s);
		for (i=0;i<count;i++) {
			CamelMessageInfo *info = camel_lite_folder_summary_index(s, i);

			if (info) {
				camel_lite_folder_change_info_remove_uid(changes, camel_lite_message_info_uid(info));
				camel_lite_message_info_free(info);
			}
		}
		camel_lite_folder_summary_dispose_all (s);
		ret = 0;
	} else {
		/* is the summary uptodate? */
		if (st.st_size != mbs->folder_size || st.st_mtime != s->time) {
			if (mbs->folder_size < st.st_size) {
				/* this will automatically rescan from 0 if there is a problem */
				d(printf("folder grew, attempting to rebuild from %d\n", mbs->folder_size));
				ret = summary_update(cls, mbs->folder_size, changes, ex);
			} else {
				d(printf("folder shrank!  rebuilding from start\n"));
				ret = summary_update(cls, 0, changes, ex);
			}
		} else {
			d(printf("Folder unchanged, do nothing\n"));
		}
	}

	/* FIXME: move upstream? */

	if (ret != -1) {
		if (mbs->folder_size != st.st_size || s->time != st.st_mtime) {
			mbs->folder_size = st.st_size;
			s->time = st.st_mtime;
			camel_lite_folder_summary_touch(s);
		}
	}

	return ret;
}

/* perform a full sync */
static int
mbox_summary_sync_full(CamelMboxSummary *mbs, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	int fd = -1, fdout = -1;
	char *tmpname = NULL;
	guint32 flags = (expunge?1:0);

	d(printf("performing full summary/sync\n"));

	camel_lite_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE | O_RDONLY | O_BINARY, 0);
	if (fd == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not open file: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		camel_lite_operation_end(NULL);
		return -1;
	}

	tmpname = g_alloca (strlen (cls->folder_path) + 5);
	sprintf (tmpname, "%s.tmp", cls->folder_path);
	d(printf("Writing temporary file to %s\n", tmpname));
	fdout = g_open(tmpname, O_LARGEFILE|O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fdout == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot open temporary mailbox: %s"),
				      g_strerror (errno));
		goto error;
	}

	if (camel_lite_mbox_summary_sync_mbox((CamelMboxSummary *)cls, flags, changeinfo, fd, fdout, ex) == -1)
		goto error;

	d(printf("Closing folders\n"));

	if (close(fd) == -1) {
		g_warning("Cannot close source folder: %s", strerror (errno));
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not close source folder %s: %s"),
				      cls->folder_path, g_strerror (errno));
		fd = -1;
		goto error;
	}

	if (close(fdout) == -1) {
		g_warning("Cannot close temporary folder: %s", strerror (errno));
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not close temporary folder: %s"),
				      g_strerror (errno));
		fdout = -1;
		goto error;
	}

	/* this should probably either use unlink/link/unlink, or recopy over
	   the original mailbox, for various locking reasons/etc */
#ifdef G_OS_WIN32
	if (g_file_test(cls->folder_path,G_FILE_TEST_IS_REGULAR) && g_remove(cls->folder_path) == -1)
		g_warning ("Cannot remove %s: %s", cls->folder_path, g_strerror (errno));
#endif
	if (g_rename(tmpname, cls->folder_path) == -1) {
		g_warning("Cannot rename folder: %s", strerror (errno));
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not rename folder: %s"),
				      g_strerror (errno));
		goto error;
	}
	tmpname = NULL;

	camel_lite_operation_end(NULL);

	return 0;
 error:
	if (fd != -1)
		close(fd);

	if (fdout != -1)
		close(fdout);

	if (tmpname)
		g_unlink(tmpname);

	camel_lite_operation_end(NULL);

	return -1;
}

/* perform a quick sync - only system flags have changed */
static int
mbox_summary_sync_quick(CamelMboxSummary *mbs, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	int i, count;
	CamelMboxMessageInfo *info = NULL;
	int fd = -1, pfd;
	char *xevnew, *xevtmp;
	const char *xev;
	int len;
	off_t lastpos;

	d(printf("Performing quick summary sync\n"));

	camel_lite_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE|O_RDWR|O_BINARY, 0);
	if (fd == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not open file: %s: %s"),
				      cls->folder_path, g_strerror (errno));

		camel_lite_operation_end(NULL);
		return -1;
	}

	/* need to dup since mime parser closes its fd once it is finalised */
	pfd = dup(fd);
	if (pfd == -1) {
		camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not store folder: %s"),
				     g_strerror(errno));
		close(fd);
		return -1;
	}

	mp = camel_lite_mime_parser_new();
	camel_lite_mime_parser_scan_from(mp, TRUE);
	camel_lite_mime_parser_scan_pre_from(mp, TRUE);
	camel_lite_mime_parser_init_with_fd(mp, pfd);

	count = camel_lite_folder_summary_count(s);
	for (i = 0; i < count; i++) {
		int xevoffset;

		camel_lite_operation_progress(NULL, i+1, count);

		info = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);

		g_assert(info);

		d(printf("Checking message %s %08x\n", camel_lite_message_info_uid(info), info->info.flags));

		if ((info->info.info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED) == 0) {
			camel_lite_message_info_free((CamelMessageInfo *)info);
			info = NULL;
			continue;
		}

		d(printf("Updating message %s\n", camel_lite_message_info_uid(info)));

		camel_lite_mime_parser_seek(mp, info->frompos, SEEK_SET);

		if (camel_lite_mime_parser_step(mp, NULL, NULL) != CAMEL_MIME_PARSER_STATE_FROM) {
			g_warning("Expected a From line here, didn't get it");
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		if (camel_lite_mime_parser_tell_start_from(mp) != info->frompos) {
			g_warning("Didn't get the next message where I expected (%d) got %d instead",
				  (int)info->frompos, (int)camel_lite_mime_parser_tell_start_from(mp));
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		if (camel_lite_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM_END) {
			g_warning("camel_lite_mime_parser_step failed (2)");
			goto error;
		}

		xev = camel_lite_mime_parser_header(mp, "X-Evolution", &xevoffset);
		if (xev == NULL || camel_lite_local_summary_decode_x_evolution(cls, xev, NULL) == -1) {
			g_warning("We're supposed to have a valid x-ev header, but we dont");
			goto error;
		}
		xevnew = camel_lite_local_summary_encode_x_evolution(cls, &info->info);
		/* SIGH: encode_param_list is about the only function which folds headers by itself.
		   This should be fixed somehow differently (either parser doesn't fold headers,
		   or param_list doesn't, or something */
		xevtmp = camel_lite_header_unfold(xevnew);
		/* the raw header contains a leading ' ', so (dis)count that too */
		if (strlen(xev)-1 != strlen(xevtmp)) {
			g_free(xevnew);
			g_free(xevtmp);
			g_warning("Hmm, the xev headers shouldn't have changed size, but they did");
			goto error;
		}
		g_free(xevtmp);

		/* we write out the xevnew string, assuming its been folded identically to the original too! */

		lastpos = lseek(fd, 0, SEEK_CUR);
		lseek(fd, xevoffset+strlen("X-Evolution: "), SEEK_SET);
		do {
			len = write(fd, xevnew, strlen(xevnew));
		} while (len == -1 && errno == EINTR);
		lseek(fd, lastpos, SEEK_SET);
		g_free(xevnew);

		camel_lite_mime_parser_drop_step(mp);
		camel_lite_mime_parser_drop_step(mp);

		info->info.info.flags &= 0x1fff;
		camel_lite_message_info_free((CamelMessageInfo *)info);
	}

	d(printf("Closing folders\n"));

	if (close(fd) == -1) {
		g_warning ("Cannot close source folder: %s", strerror (errno));
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not close source folder %s: %s"),
				      cls->folder_path, g_strerror (errno));
		fd = -1;
		goto error;
	}

	camel_lite_object_unref((CamelObject *)mp);

	camel_lite_operation_end(NULL);

	return 0;
 error:
	if (fd != -1)
		close(fd);
	if (mp)
		camel_lite_object_unref((CamelObject *)mp);
	if (info)
		camel_lite_message_info_free((CamelMessageInfo *)info);

	camel_lite_operation_end(NULL);

	return -1;
}

static int
mbox_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	struct stat st;
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	int i, count;
	int quick = TRUE, work=FALSE;
	int ret;

	/* first, sync ourselves up, just to make sure */
	if (camel_lite_local_summary_check(cls, changeinfo, ex) == -1)
		return -1;

	count = camel_lite_folder_summary_count(s);
	if (count == 0)
		return 0;

	/* check what work we have to do, if any */
	for (i=0;quick && i<count; i++) {
		CamelMboxMessageInfo *info = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);

		g_assert(info);
		if ((expunge && (info->info.info.flags & CAMEL_MESSAGE_DELETED)) ||
		    (info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_XEVCHANGE)))
			quick = FALSE;
		else
			work |= (info->info.info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0;
		camel_lite_message_info_free(info);
	}

	/* yuck i hate this logic, but its to simplify the 'all ok, update summary' and failover cases */
	ret = -1;
	if (quick) {
		if (work) {
			ret = ((CamelMboxSummaryClass *)((CamelObject *)cls)->klass)->sync_quick(mbs, expunge, changeinfo, ex);
			if (ret == -1) {
				g_warning("failed a quick-sync, trying a full sync");
				camel_lite_exception_clear(ex);
			}
		} else {
			ret = 0;
		}
	}

	if (ret == -1)
		ret = ((CamelMboxSummaryClass *)((CamelObject *)cls)->klass)->sync_full(mbs, expunge, changeinfo, ex);
	if (ret == -1)
		return -1;

	if (g_stat(cls->folder_path, &st) == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Unknown error: %s"), g_strerror (errno));
		return -1;
	}

	if (mbs->folder_size != st.st_size || s->time != st.st_mtime) {
		s->time = st.st_mtime;
		mbs->folder_size = st.st_size;
		camel_lite_folder_summary_touch(s);
	}

	return ((CamelLocalSummaryClass *)camel_lite_mbox_summary_parent)->sync(cls, expunge, changeinfo, ex);
}

int
camel_lite_mbox_summary_sync_mbox(CamelMboxSummary *cls, guint32 flags, CamelFolderChangeInfo *changeinfo, int fd, int fdout, CamelException *ex)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	int i, count;
	CamelMboxMessageInfo *info = NULL;
	char *buffer, *xevnew = NULL;
	size_t len;
	const char *fromline;
	int lastdel = FALSE;
#ifdef STATUS_PINE
	char statnew[8], xstatnew[8];
#endif

	d(printf("performing full summary/sync\n"));

	/* need to dup this because the mime-parser owns the fd after we give it to it */
	fd = dup(fd);
	if (fd == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not store folder: %s"),
				      g_strerror (errno));
		return -1;
	}

	mp = camel_lite_mime_parser_new();
	camel_lite_mime_parser_scan_from(mp, TRUE);
	camel_lite_mime_parser_scan_pre_from(mp, TRUE);
	camel_lite_mime_parser_init_with_fd(mp, fd);

	count = camel_lite_folder_summary_count(s);
	for (i = 0; i < count; i++) {

		camel_lite_operation_progress(NULL, i + 1, count);

		info = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);

		g_assert(info);

		d(printf("Looking at message %s\n", camel_lite_message_info_uid(info)));

		/* only need to seek past deleted messages, otherwise we should be at the right spot/state already */
		if (lastdel) {
			d(printf("seeking to %d\n", (int)info->frompos));
			camel_lite_mime_parser_seek(mp, info->frompos, SEEK_SET);
		}

		if (camel_lite_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_FROM) {
			g_warning("Expected a From line here, didn't get it");
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		if (camel_lite_mime_parser_tell_start_from(mp) != info->frompos) {
			g_warning("Didn't get the next message where I expected (%d) got %d instead",
				  (int)info->frompos, (int)camel_lite_mime_parser_tell_start_from(mp));
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		lastdel = FALSE;
		if ((flags&1) && info->info.info.flags & CAMEL_MESSAGE_DELETED) {
			const char *uid = camel_lite_message_info_uid(info);

			d(printf("Deleting %s\n", uid));

			if (((CamelLocalSummary *)cls)->index)
				camel_lite_index_delete_name(((CamelLocalSummary *)cls)->index, uid);

			/* remove it from the change list */
			camel_lite_folder_change_info_remove_uid(changeinfo, uid);
			((CamelMessageInfoBase*)info)->flags |= CAMEL_MESSAGE_EXPUNGED;
			camel_lite_folder_summary_remove(s, (CamelMessageInfo *)info);
			camel_lite_message_info_free((CamelMessageInfo *)info);
			count--;
			i--;
			info = NULL;
			lastdel = TRUE;
		} else {
			/* otherwise, the message is staying, copy its From_ line across */
#if 0
			if (i>0)
				write(fdout, "\n", 1);
#endif
			info->frompos = lseek(fdout, 0, SEEK_CUR);
			fromline = camel_lite_mime_parser_from_line(mp);
			write(fdout, fromline, strlen(fromline));
		}

		if (info && info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV | CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			d(printf("Updating header for %s flags = %08x\n", camel_lite_message_info_uid(info), info->info.flags));

			if (camel_lite_mime_parser_step(mp, &buffer, &len) == CAMEL_MIME_PARSER_STATE_FROM_END) {
				g_warning("camel_lite_mime_parser_step failed (2)");
				goto error;
			}

			xevnew = camel_lite_local_summary_encode_x_evolution((CamelLocalSummary *)cls, &info->info);
#ifdef STATUS_PINE
			if (mbs->xstatus) {
				encode_status(info->info.info.flags & STATUS_STATUS, statnew);
				encode_status(info->info.info.flags & STATUS_XSTATUS, xstatnew);
				len = camel_lite_local_summary_write_headers(fdout, camel_lite_mime_parser_headers_raw(mp), xevnew, statnew, xstatnew);
			} else {
#endif
				len = camel_lite_local_summary_write_headers(fdout, camel_lite_mime_parser_headers_raw(mp), xevnew, NULL, NULL);
#ifdef STATUS_PINE
			}
#endif
			if (len == -1) {
				d(printf("Error writing to temporary mailbox\n"));
				camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
						      _("Writing to temporary mailbox failed: %s"),
						      g_strerror (errno));
				goto error;
			}
			info->info.info.flags &= 0x1fff;
			g_free(xevnew);
			xevnew = NULL;
			camel_lite_mime_parser_drop_step(mp);
		}

		camel_lite_mime_parser_drop_step(mp);
		if (info) {
			d(printf("looking for message content to copy across from %d\n", (int)camel_lite_mime_parser_tell(mp)));
			while (camel_lite_mime_parser_step(mp, &buffer, &len) == CAMEL_MIME_PARSER_STATE_PRE_FROM) {
				/*d(printf("copying mbox contents to temporary: '%.*s'\n", len, buffer));*/
				if (write(fdout, buffer, len) != len) {
					camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
							      _("Writing to temporary mailbox failed: %s: %s"),
							      ((CamelLocalSummary *)cls)->folder_path,
							      g_strerror (errno));
					goto error;
				}
			}

			if (write(fdout, "\n", 1) != 1) {
				camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
						      _("Writing to temporary mailbox failed: %s"),
						      g_strerror (errno));
				goto error;
			}

			d(printf("we are now at %d, from = %d\n", (int)camel_lite_mime_parser_tell(mp),
				 (int)camel_lite_mime_parser_tell_start_from(mp)));
			camel_lite_mime_parser_unstep(mp);
			camel_lite_message_info_free((CamelMessageInfo *)info);
			info = NULL;
		}
	}

#if 0
	/* if last was deleted, append the \n we removed */
	if (lastdel && count > 0)
		write(fdout, "\n", 1);
#endif

	camel_lite_object_unref((CamelObject *)mp);

	/* clear working flags */
	for (i=0; i<count; i++) {
		info = (CamelMboxMessageInfo *)camel_lite_folder_summary_index(s, i);
		if (info) {
			if (info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED|CAMEL_MESSAGE_FOLDER_XEVCHANGE)) {
				info->info.info.flags &= ~(CAMEL_MESSAGE_FOLDER_NOXEV
							   |CAMEL_MESSAGE_FOLDER_FLAGGED
							   |CAMEL_MESSAGE_FOLDER_XEVCHANGE);
				camel_lite_folder_summary_touch(s);
			}
			camel_lite_message_info_free((CamelMessageInfo *)info);
		}
	}

	return 0;
 error:
	g_free(xevnew);

	if (mp)
		camel_lite_object_unref((CamelObject *)mp);
	if (info)
		camel_lite_message_info_free((CamelMessageInfo *)info);

	return -1;
}

#ifdef STATUS_PINE
static CamelMessageInfo *
mbox_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, CamelException *ex)
{
	CamelMboxMessageInfo *mi;

	mi = (CamelMboxMessageInfo *)((CamelLocalSummaryClass *)camel_lite_mbox_summary_parent)->add(cls, msg, info, ci, ex);
	if (mi && ((CamelMboxSummary *)cls)->xstatus) {
		char status[8];

		/* we snoop and add status/x-status headers to suit */
		encode_status(mi->info.info.flags & STATUS_STATUS, status);
		camel_lite_medium_set_header((CamelMedium *)msg, "Status", status);
		encode_status(mi->info.info.flags & STATUS_XSTATUS, status);
		camel_lite_medium_set_header((CamelMedium *)msg, "X-Status", status);
	}

	return (CamelMessageInfo *)mi;
}

static struct {
	char tag;
	guint32 flag;
} status_flags[] = {
	{ 'F', CAMEL_MESSAGE_FLAGGED },
	{ 'A', CAMEL_MESSAGE_ANSWERED },
	{ 'D', CAMEL_MESSAGE_DELETED },
	{ 'R', CAMEL_MESSAGE_SEEN },
};

static void
encode_status(guint32 flags, char status[8])
{
	size_t i;
	char *p;

	p = status;
	for (i = 0; i < G_N_ELEMENTS (status_flags); i++)
		if (status_flags[i].flag & flags)
			*p++ = status_flags[i].tag;
	*p++ = 'O';
	*p = '\0';
}

static guint32
decode_status(const char *status)
{
	const char *p;
	guint32 flags = 0;
	size_t i;
	char c;

	p = status;
	while ((c = *p++)) {
		for (i = 0; i < G_N_ELEMENTS (status_flags); i++)
			if (status_flags[i].tag == c)
				flags |= status_flags[i].flag;
	}

	return flags;
}

#endif /* STATUS_PINE */
