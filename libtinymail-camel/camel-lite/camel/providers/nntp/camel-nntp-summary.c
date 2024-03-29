/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>

#include "camel/camel-data-cache.h"
#include "camel/camel-debug.h"
#include "camel/camel-file-utils.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-operation.h"
#include "camel/camel-stream-null.h"

#include "camel-nntp-folder.h"
#include "camel-nntp-store.h"
#include "camel-nntp-stream.h"
#include "camel-nntp-summary.h"

#define w(x)
#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/
#define dd(x) (camel_lite_debug("nntp")?(x):0)

#define CAMEL_NNTP_SUMMARY_VERSION (1)

struct _CamelNNTPSummaryPrivate {
	char *uid;

	struct _xover_header *xover; /* xoverview format */
	int xover_setup;
};

#define _PRIVATE(o) (((CamelNNTPSummary *)(o))->priv)

static CamelMessageInfo * message_info_new_from_header (CamelFolderSummary *, struct _camel_lite_header_raw *);
static int summary_header_load(CamelFolderSummary *);
static int summary_header_save(CamelFolderSummary *, FILE *);

static void camel_lite_nntp_summary_class_init (CamelNNTPSummaryClass *klass);
static void camel_lite_nntp_summary_init       (CamelNNTPSummary *obj);
static void camel_lite_nntp_summary_finalise   (CamelObject *obj);
static CamelFolderSummaryClass *camel_lite_nntp_summary_parent;

CamelType
camel_lite_nntp_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(camel_lite_folder_summary_get_type(), "CamelLiteNNTPSummary",
					   sizeof (CamelNNTPSummary),
					   sizeof (CamelNNTPSummaryClass),
					   (CamelObjectClassInitFunc) camel_lite_nntp_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_lite_nntp_summary_init,
					   (CamelObjectFinalizeFunc) camel_lite_nntp_summary_finalise);
	}

	return type;
}

static void
camel_lite_nntp_summary_class_init(CamelNNTPSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) klass;

	camel_lite_nntp_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS(camel_lite_type_get_global_classfuncs(camel_lite_folder_summary_get_type()));

	sklass->message_info_new_from_header  = message_info_new_from_header;
	sklass->summary_header_load = summary_header_load;
	sklass->summary_header_save = summary_header_save;
}

static void
camel_lite_nntp_summary_init(CamelNNTPSummary *obj)
{
	struct _CamelNNTPSummaryPrivate *p;
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelMessageInfoBase);
	s->content_info_size = sizeof(CamelMessageContentInfo);

	/* and a unique file version */
	s->version += CAMEL_NNTP_SUMMARY_VERSION;
}

static void
camel_lite_nntp_summary_finalise(CamelObject *obj)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(obj);

	g_free(cns->priv);
}

CamelNNTPSummary *
camel_lite_nntp_summary_new(struct _CamelFolder *folder, const char *path)
{
	CamelNNTPSummary *cns = (CamelNNTPSummary *)camel_lite_object_new(camel_lite_nntp_summary_get_type());

	((CamelFolderSummary *)cns)->folder = folder;

	camel_lite_folder_summary_set_filename((CamelFolderSummary *)cns, path);
	camel_lite_folder_summary_set_build_content((CamelFolderSummary *)cns, FALSE);

	return cns;
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	CamelMessageInfoBase *mi;
	CamelNNTPSummary *cns = (CamelNNTPSummary *)s;

	/* error to call without this setup */
	if (cns->priv->uid == NULL)
		return NULL;

	/* we shouldn't be here if we already have this uid */
	g_assert(camel_lite_folder_summary_uid(s, cns->priv->uid) == NULL);

	mi = (CamelMessageInfoBase *)((CamelFolderSummaryClass *)camel_lite_nntp_summary_parent)->message_info_new_from_header(s, h);
	if (mi) {
		g_free(mi->uid);
		mi->uid = cns->priv->uid;
		cns->priv->uid = NULL;
	}

	return (CamelMessageInfo *)mi;
}

static int
summary_header_load(CamelFolderSummary *s)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(s);

	if (((CamelFolderSummaryClass *)camel_lite_nntp_summary_parent)->summary_header_load(s) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x20c) {
		cns->high = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;
		cns->low = g_ntohl(get_unaligned_u32(s->filepos));
		s->filepos += 4;

		return 0;
	}

	cns->version = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	if (cns->version > CAMEL_NNTP_SUMMARY_VERSION) {
		g_warning("Unknown NNTP summary version");
		errno = EINVAL;
		return -1;
	}

	cns->high = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	cns->low = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(s);

	if (((CamelFolderSummaryClass *)camel_lite_nntp_summary_parent)->summary_header_save(s, out) == -1
	    || camel_lite_file_util_encode_fixed_int32(out, CAMEL_NNTP_SUMMARY_VERSION) == -1
	    || camel_lite_file_util_encode_fixed_int32(out, cns->high) == -1
	    || camel_lite_file_util_encode_fixed_int32(out, cns->low) == -1)
		return -1;

	return 0;
}

/* ********************************************************************** */

/* Note: This will be called from camel_lite_nntp_command, so only use camel_lite_nntp_raw_command */
static int
add_range_xover(CamelNNTPSummary *cns, CamelNNTPStore *store, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelFolderSummary *s;
	CamelMessageInfoBase *mi;
	struct _camel_lite_header_raw *headers = NULL;
	char *line, *tab;
	int len, ret;
	unsigned int n, count, total, size, acnt=0;
	struct _xover_header *xover;

	s = (CamelFolderSummary *)cns;

	camel_lite_operation_start(NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	ret = camel_lite_nntp_raw_command_auth(store, ex, &line, "xover %r", low, high);
	if (ret != 224) {
		camel_lite_operation_end(NULL);
		if (ret != -1)
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Unexpected server response from xover: %s"), line);
		return -1;
	}

	count = 0;
	total = high-low+1;

	camel_lite_folder_summary_prepare_hash (s);

	while ((ret = camel_lite_nntp_stream_line(store->stream, (unsigned char **)&line, (unsigned int *) &len)) > 0) {
		camel_lite_operation_progress(NULL, (count) , total);
		count++;
		n = strtoul(line, &tab, 10);
		if (*tab != '\t')
			continue;
		tab++;
		xover = store->xover;
		size = 0;
		for (;tab[0] && xover;xover = xover->next) {
			line = tab;
			tab = strchr(line, '\t');
			if (tab)
				*tab++ = 0;
			else
				tab = line+strlen(line);

			/* do we care about this column? */
			if (xover->name) {
				line += xover->skip;
				if (line < tab) {
					camel_lite_header_raw_append(&headers, xover->name, line, -1);
					switch(xover->type) {
					case XOVER_STRING:
						break;
					case XOVER_MSGID:
						cns->priv->uid = g_strdup_printf("%u,%s", n, line);
						break;
					case XOVER_SIZE:
						size = strtoul(line, NULL, 10);
						break;
					}
				}
			}
		}

		/* skip headers we don't care about, incase the server doesn't actually send some it said it would. */
		while (xover && xover->name == NULL)
			xover = xover->next;

		/* truncated line? ignore? */
		if (xover == NULL) {
			mi = (CamelMessageInfoBase *)camel_lite_folder_summary_uid(s, cns->priv->uid);
			if (mi == NULL) {
				gchar *nuid;

				if (acnt > 1000) {
					camel_lite_folder_summary_save (s, ex);
					acnt = 0;
				}
				acnt++;
				nuid = camel_lite_folder_summary_next_uid_string (s);
				mi = (CamelMessageInfoBase *)camel_lite_folder_summary_add_from_header(s, headers, nuid);
				g_free (nuid);
				if (mi) {
					mi->size = (size);
					cns->high = n;
					camel_lite_folder_change_info_add_uid(changes, camel_lite_message_info_uid(mi));
				}
			} else {
				camel_lite_message_info_free(mi);
			}
		}

		if (cns->priv->uid) {
			g_free(cns->priv->uid);
			cns->priv->uid = NULL;
		}

		camel_lite_header_raw_clear(&headers);
	}

	camel_lite_folder_summary_kill_hash (s);

	camel_lite_operation_end(NULL);

	return ret;
}

/* Note: This will be called from camel_lite_nntp_command, so only use camel_lite_nntp_raw_command */
static int
add_range_head(CamelNNTPSummary *cns, CamelNNTPStore *store, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelFolderSummary *s;
	int ret = -1;
	char *line, *msgid;
	unsigned int i, n, count, total, acnt=0;
	CamelMessageInfo *mi;
	CamelMimeParser *mp;

	s = (CamelFolderSummary *)cns;

	mp = camel_lite_mime_parser_new();

	camel_lite_operation_start(NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	count = 0;
	total = high-low+1;

	camel_lite_folder_summary_prepare_hash (s);

	for (i=low;i<high+1;i++) {
		camel_lite_operation_progress(NULL, (count) , total);
		count++;
		ret = camel_lite_nntp_raw_command_auth(store, ex, &line, "head %u", i);
		/* unknown article, ignore */
		if (ret == 423)
			continue;
		else if (ret == -1)
			goto ioerror;
		else if (ret != 221) {
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Unexpected server response from head: %s"), line);
			goto ioerror;
		}
		line += 3;
		n = strtoul(line, &line, 10);
		if (n != i)
			g_warning("retrieved message '%u' when i expected '%u'?\n", n, i);

		/* FIXME: use camel-mime-utils.c function for parsing msgid? */
		if ((msgid = strchr(line, '<')) && (line = strchr(msgid+1, '>'))){
			line[1] = 0;
			cns->priv->uid = g_strdup_printf("%u,%s\n", n, msgid);

			mi = camel_lite_folder_summary_uid(s, cns->priv->uid);
			if (mi == NULL) {
				if (camel_lite_mime_parser_init_with_stream(mp, (CamelStream *)store->stream) == -1)
					goto error;
				if (acnt > 1000) {
					camel_lite_folder_summary_save (s, ex);
					acnt = 0;
				}
				acnt++;
				mi = camel_lite_folder_summary_add_from_parser(s, mp, cns->priv->uid);
				while (camel_lite_mime_parser_step(mp, NULL, NULL) != CAMEL_MIME_PARSER_STATE_EOF)
					;
				if (mi == NULL) {
					goto error;
				}
				cns->high = i;
				camel_lite_folder_change_info_add_uid(changes, camel_lite_message_info_uid(mi));
			} else {
				/* already have, ignore */
				camel_lite_message_info_free(mi);
			}
			if (cns->priv->uid) {
				g_free(cns->priv->uid);
				cns->priv->uid = NULL;
			}
		}
	}

	ret = 0;
error:

	if (ret == -1) {
		if (errno == EINTR)
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Use cancel"));
		else
			camel_lite_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Operation failed: %s"), strerror(errno));
	}
ioerror:

	camel_lite_folder_summary_kill_hash (s);

	if (cns->priv->uid) {
		g_free(cns->priv->uid);
		cns->priv->uid = NULL;
	}
	camel_lite_object_unref((CamelObject *)mp);

	camel_lite_operation_end(NULL);

	return ret;
}

/* Assumes we have the stream */
/* Note: This will be called from camel_lite_nntp_command, so only use camel_lite_nntp_raw_command */
int
camel_lite_nntp_summary_check(CamelNNTPSummary *cns, CamelNNTPStore *store, char *line, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelFolderSummary *s;
	int ret = 0, i;
	unsigned int n, f, l;
	int count;
	char *folder = NULL;
	CamelNNTPStoreInfo *si;

	s = (CamelFolderSummary *)cns;

	line +=3;
	n = strtoul(line, &line, 10);
	f = strtoul(line, &line, 10);
	l = strtoul(line, &line, 10);
	if (line[0] == ' ') {
		char *tmp;

		folder = line+1;
		tmp = strchr(folder, ' ');
		if (tmp)
			*tmp = 0;
		tmp = g_alloca(strlen(folder)+1);
		strcpy(tmp, folder);
		folder = tmp;
	}

	if (cns->low == f && cns->high == l) {
		dd(printf("nntp_summary: no work to do!\n"));
		goto update;
	}

	/* Need to work out what to do with our messages */

	/* Check for messages no longer on the server */
	if (cns->low != f) {
		count = camel_lite_folder_summary_count(s);
		for (i = 0; i < count; i++) {
			CamelMessageInfo *mi = camel_lite_folder_summary_index(s, i);

			if (mi) {
				const char *uid = camel_lite_message_info_uid(mi);
				const char *msgid;

				n = strtoul(uid, NULL, 10);
				if (n < f || n > l) {
					dd(printf("nntp_summary: %u is lower/higher than lowest/highest article, removed\n", n));
					/* Since we use a global cache this could prematurely remove
					   a cached message that might be in another folder - not that important as
					   it is a true cache */
					msgid = strchr(uid, ',');
					if (msgid)
						camel_lite_data_cache_remove(store->cache, "cache", msgid+1, NULL);
					camel_lite_folder_change_info_remove_uid(changes, uid);
					camel_lite_folder_summary_remove(s, mi);
					count--;
					i--;
				}

				camel_lite_message_info_free(mi);
			}
		}
		cns->low = f;
	}

	if (cns->high < l) {
		if (cns->high < f)
			cns->high = f-1;

		if (store->xover) {
			ret = add_range_xover(cns, store, l, cns->high+1, changes, ex);
		} else {
			ret = add_range_head(cns, store, l, cns->high+1, changes, ex);
		}
	}

	/* TODO: not from here */
	camel_lite_folder_summary_touch(s);
	camel_lite_folder_summary_save(s, ex);
update:
	/* update store summary if we have it */
	if (folder
	    && (si = (CamelNNTPStoreInfo *)camel_lite_store_summary_path((CamelStoreSummary *)store->summary, folder))) {
		int unread = 0;

		count = camel_lite_folder_summary_count(s);
		for (i = 0; i < count; i++) {
			CamelMessageInfoBase *mi = (CamelMessageInfoBase *)camel_lite_folder_summary_index(s, i);

			if (mi) {
				if ((mi->flags & CAMEL_MESSAGE_SEEN) == 0)
					unread++;
				camel_lite_message_info_free(mi);
			}
		}

		if (si->info.unread != unread
		    || si->info.total != count
		    || si->first != f
		    || si->last != l) {
			si->info.unread = unread;
			si->info.total = count;
			si->first = f;
			si->last = l;
			camel_lite_store_summary_touch((CamelStoreSummary *)store->summary);
			camel_lite_store_summary_save((CamelStoreSummary *)store->summary, ex);
		}
		camel_lite_store_summary_info_free ((CamelStoreSummary *)store->summary, (CamelStoreInfo *)si);
	} else {
		if (folder)
			g_warning("Group '%s' not present in summary", folder);
		else
			g_warning("Missing group from group response");
	}

	return ret;
}
