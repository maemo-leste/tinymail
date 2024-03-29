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
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-file-utils.h"
#include "camel-mime-message.h"
#include "camel-stream-null.h"

#include "camel-local-summary.h"

#define w(x)
#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_LOCAL_SUMMARY_VERSION (1)

static int summary_header_load (CamelFolderSummary *);
static int summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new_from_header (CamelFolderSummary *, struct _camel_lite_header_raw *);

static int local_summary_decode_x_evolution(CamelLocalSummary *cls, const char *xev, CamelLocalMessageInfo *mi);
static char *local_summary_encode_x_evolution(CamelLocalSummary *cls, const CamelLocalMessageInfo *mi);

static int local_summary_load(CamelLocalSummary *cls, int forceindex, CamelException *ex);
static int local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static int local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static CamelMessageInfo *local_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *, CamelException *ex);

static void camel_lite_local_summary_class_init (CamelLocalSummaryClass *klass);
static void camel_lite_local_summary_init       (CamelLocalSummary *obj);
static void camel_lite_local_summary_finalise   (CamelObject *obj);
static CamelFolderSummaryClass *camel_lite_local_summary_parent;

CamelType
camel_lite_local_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(camel_lite_folder_summary_get_type(), "CamelLiteLocalSummary",
					   sizeof (CamelLocalSummary),
					   sizeof (CamelLocalSummaryClass),
					   (CamelObjectClassInitFunc) camel_lite_local_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_lite_local_summary_init,
					   (CamelObjectFinalizeFunc) camel_lite_local_summary_finalise);
	}

	return type;
}

static void
camel_lite_local_summary_class_init(CamelLocalSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) klass;

	camel_lite_local_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS(camel_lite_type_get_global_classfuncs(camel_lite_folder_summary_get_type()));

	sklass->summary_header_load = summary_header_load;
	sklass->summary_header_save = summary_header_save;

	sklass->message_info_new_from_header  = message_info_new_from_header;

	klass->load = local_summary_load;
	klass->check = local_summary_check;
	klass->sync = local_summary_sync;
	klass->add = local_summary_add;

	klass->encode_x_evolution = local_summary_encode_x_evolution;
	klass->decode_x_evolution = local_summary_decode_x_evolution;
}

static void
camel_lite_local_summary_set_extra_flags (CamelFolder *folder, CamelMessageInfoBase *mi)
{
	mi->flags |= CAMEL_MESSAGE_CACHED;
}

static void
camel_lite_local_summary_init(CamelLocalSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s->set_extra_flags_func = camel_lite_local_summary_set_extra_flags;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelLocalMessageInfo);
	s->content_info_size = sizeof(CamelMessageContentInfo);

	/* and a unique file version */
	s->version += CAMEL_LOCAL_SUMMARY_VERSION;
}

static void
camel_lite_local_summary_finalise(CamelObject *obj)
{
	CamelLocalSummary *mbs = CAMEL_LOCAL_SUMMARY(obj);

	if (mbs->index)
		camel_lite_object_unref((CamelObject *)mbs->index);
	g_free(mbs->folder_path);
}

void
camel_lite_local_summary_construct(CamelLocalSummary *new, const char *filename, const char *local_name, CamelIndex *index)
{
	camel_lite_folder_summary_set_build_content(CAMEL_FOLDER_SUMMARY(new), FALSE);
	camel_lite_folder_summary_set_filename(CAMEL_FOLDER_SUMMARY(new), filename);
	new->folder_path = g_strdup(local_name);
	new->index = index;
	if (index)
		camel_lite_object_ref((CamelObject *)index);
}

static int
local_summary_load(CamelLocalSummary *cls, int forceindex, CamelException *ex)
{
	return camel_lite_folder_summary_load((CamelFolderSummary *)cls);
}

/* load/check the summary */
int
camel_lite_local_summary_load(CamelLocalSummary *cls, int forceindex, CamelException *ex)
{
	d(printf("Loading summary ...\n"));

 	if (((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->load(cls, forceindex, ex) == -1) {
		w(g_warning("Could not load summary: flags may be reset"));
		return -1;
	}

	return 0;
}

void camel_lite_local_summary_check_force(CamelLocalSummary *cls)
{
	cls->check_force = 1;
}

char *
camel_lite_local_summary_encode_x_evolution(CamelLocalSummary *cls, const CamelLocalMessageInfo *info)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->encode_x_evolution(cls, info);
}

int
camel_lite_local_summary_decode_x_evolution(CamelLocalSummary *cls, const char *xev, CamelLocalMessageInfo *info)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->decode_x_evolution(cls, xev, info);
}

/*#define DOSTATS*/
#ifdef DOSTATS
struct _stat_info {
	int mitotal;
	int micount;
	int citotal;
	int cicount;
	int msgid;
	int msgcount;
};

static void
do_stat_ci(CamelLocalSummary *cls, struct _stat_info *info, CamelMessageContentInfo *ci)
{
	info->cicount++;
	info->citotal += ((CamelFolderSummary *)cls)->content_info_size /*+ 4 memchunks are 1/4 byte overhead per mi */;
	if (ci->id)
		info->citotal += strlen(ci->id) + 4;
	if (ci->description)
		info->citotal += strlen(ci->description) + 4;
	if (ci->encoding)
		info->citotal += strlen(ci->encoding) + 4;
	if (ci->type) {
		CamelContentType *ct = ci->type;
		struct _camel_lite_header_param *param;

		info->citotal += sizeof(*ct) + 4;
		if (ct->type)
			info->citotal += strlen(ct->type) + 4;
		if (ct->subtype)
			info->citotal += strlen(ct->subtype) + 4;
		param = ct->params;
		while (param) {
			info->citotal += sizeof(*param) + 4;
			if (param->name)
				info->citotal += strlen(param->name)+4;
			if (param->value)
				info->citotal += strlen(param->value)+4;
			param = param->next;
		}
	}
	ci = ci->childs;
	while (ci) {
		do_stat_ci(cls, info, ci);
		ci = ci->next;
	}
}

static void
do_stat_mi(CamelLocalSummary *cls, struct _stat_info *info, CamelMessageInfo *mi)
{
	info->micount++;
	info->mitotal += ((CamelFolderSummary *)cls)->content_info_size /*+ 4*/;

	if (mi->subject)
		info->mitotal += strlen(mi->subject) + 4;
	if (mi->to)
		info->mitotal += strlen(mi->to) + 4;
	if (mi->from)
		info->mitotal += strlen(mi->from) + 4;
	if (mi->cc)
		info->mitotal += strlen(mi->cc) + 4;
	if (mi->uid)
		info->mitotal += strlen(mi->uid) + 4;

	if (mi->references) {
		info->mitotal += (mi->references->size-1) * sizeof(CamelSummaryMessageID) + sizeof(CamelSummaryReferences) + 4;
		info->msgid += (mi->references->size) * sizeof(CamelSummaryMessageID);
		info->msgcount += mi->references->size;
	}

	/* dont have any user flags yet */

	if (mi->content) {
		do_stat_ci(cls, info, mi->content);
	}
}

#endif

int
camel_lite_local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	int ret;

	ret = ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->check(cls, changeinfo, ex);

#ifdef DOSTATS
	if (ret != -1) {
		int i;
		CamelFolderSummary *s = (CamelFolderSummary *)cls;
		struct _stat_info stats = { 0 };

		for (i=0;i<camel_lite_folder_summary_count(s);i++) {
			CamelMessageInfo *info = camel_lite_folder_summary_index(s, i);
			do_stat_mi(cls, &stats, info);
			camel_lite_message_info_free(info);
		}

		printf("\nMemory used by summary:\n\n");
		printf("Total of %d messages\n", camel_lite_folder_summary_count(s));
		printf("Total: %d bytes (ave %f)\n", stats.citotal + stats.mitotal,
		       (double)(stats.citotal+stats.mitotal)/(double)camel_lite_folder_summary_count(s));
		printf("Message Info: %d (ave %f)\n", stats.mitotal, (double)stats.mitotal/(double)stats.micount);
		printf("Content Info; %d (ave %f) count %d\n", stats.citotal, (double)stats.citotal/(double)stats.cicount, stats.cicount);
		printf("message id's: %d (ave %f) count %d\n", stats.msgid, (double)stats.msgid/(double)stats.msgcount, stats.msgcount);
	}
#endif
	return ret;
}

int
camel_lite_local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->sync(cls, expunge, changeinfo, ex);
}

CamelMessageInfo *
camel_lite_local_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, CamelException *ex)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->add(cls, msg, info, ci, ex);
}

/**
 * camel_lite_local_summary_write_headers:
 * @fd:
 * @header:
 * @xevline:
 * @status:
 * @xstatus:
 *
 * Write a bunch of headers to the file @fd.  IF xevline is non NULL, then
 * an X-Evolution header line is created at the end of all of the headers.
 * If @status is non NULL, then a Status header line is also written.
 * The headers written are termianted with a blank line.
 *
 * Return value: -1 on error, otherwise the number of bytes written.
 **/
int
camel_lite_local_summary_write_headers(int fd, struct _camel_lite_header_raw *header, const char *xevline, const char *status, const char *xstatus)
{
	int outlen = 0, len;
	int newfd;
	FILE *out;

	/* dum de dum, maybe the whole sync function should just use stdio for output */
	newfd = dup(fd);
	if (newfd == -1)
		return -1;

	out = fdopen(newfd, "w");
	if (out == NULL) {
		close(newfd);
		errno = EINVAL;
		return -1;
	}

	while (header) {
		if (strcmp(header->name, "X-Evolution") != 0
		    && (status == NULL || strcmp(header->name, "Status") != 0)
		    && (xstatus == NULL || strcmp(header->name, "X-Status") != 0)) {
			len = fprintf(out, "%s:%s\n", header->name, header->value);
			if (len == -1) {
				fclose(out);
				return -1;
			}
			outlen += len;
		}
		header = header->next;
	}

	if (status) {
		len = fprintf(out, "Status: %s\n", status);
		if (len == -1) {
			fclose(out);
			return -1;
		}
		outlen += len;
	}

	if (xstatus) {
		len = fprintf(out, "X-Status: %s\n", xstatus);
		if (len == -1) {
			fclose(out);
			return -1;
		}
		outlen += len;
	}

	if (xevline) {
		len = fprintf(out, "X-Evolution: %s\n", xevline);
		if (len == -1) {
			fclose(out);
			return -1;
		}
		outlen += len;
	}

	len = fprintf(out, "\n");
	if (len == -1) {
		fclose(out);
		return -1;
	}
	outlen += len;

	if (fclose(out) == -1)
		return -1;

	return outlen;
}

static int
local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	/* FIXME: sync index here ? */
	return 0;
}

static int
local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	int ret = 0;

	ret = camel_lite_folder_summary_save((CamelFolderSummary *)cls, ex);
	if (ret == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM_IO_WRITE,
				      _("Could not save summary: %s: %s"),
				      cls->folder_path, g_strerror (errno));

		g_warning ("Could not save summary for %s: %s", cls->folder_path, strerror (errno));
	}

	if (cls->index && camel_lite_index_sync(cls->index) == -1)
		g_warning ("Could not sync index for %s: %s", cls->folder_path, strerror (errno));

	return ret;
}

static CamelMessageInfo *
local_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, CamelException *ex)
{
	CamelLocalMessageInfo *mi;
	char *xev;

	d(printf("Adding message to summary\n"));

	mi = (CamelLocalMessageInfo *)camel_lite_folder_summary_add_from_message((CamelFolderSummary *)cls, msg);
	if (mi) {
		d(printf("Added, uid = %s\n", mi->uid));
		if (info) {

#ifdef NON_TINYMAIL_FEATURES

			const CamelTag *tag = camel_lite_message_info_user_tags(info);
			const CamelFlag *flag = camel_lite_message_info_user_flags(info);

			while (flag) {
				camel_lite_message_info_set_user_flag((CamelMessageInfo *)mi, flag->name, TRUE);
				flag = flag->next;
			}

			while (tag) {
				camel_lite_message_info_set_user_tag((CamelMessageInfo *)mi, tag->name, tag->value);
				tag = tag->next;
			}
#endif
			mi->info.flags = camel_lite_message_info_flags(info);
			/*mi->info.flags |= (camel_lite_message_info_flags(info) & 0x1fff);*/
			mi->info.size = ((CamelMessageInfoBase*)info)->size;
		}

		/* we need to calculate the size ourselves */
		if (mi->info.size == 0) {
			CamelStreamNull *sn = (CamelStreamNull *)camel_lite_stream_null_new();

			camel_lite_data_wrapper_write_to_stream((CamelDataWrapper *)msg, (CamelStream *)sn);
			mi->info.size = sn->written;
			camel_lite_object_unref((CamelObject *)sn);
		}

		mi->info.flags &= ~(CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED);
		xev = camel_lite_local_summary_encode_x_evolution(cls, mi);
		camel_lite_medium_set_header((CamelMedium *)msg, "X-Evolution", xev);
		g_free(xev);
		if (camel_lite_message_info_uid(mi))
			camel_lite_folder_change_info_add_uid(ci, camel_lite_message_info_uid(mi));
	} else {
		d(printf("Failed!\n"));
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_MEMORY,
				     _("Unable to add message to summary: unknown reason"));
	}
	return (CamelMessageInfo *)mi;
}

static char *
local_summary_encode_x_evolution(CamelLocalSummary *cls, const CamelLocalMessageInfo *mi)
{
	GString *out = g_string_new("");

#ifdef NON_TINYMAIL_FEATURES
	struct _camel_lite_header_param *params = NULL;
	GString *val = g_string_new("");
	CamelFlag *flag = mi->info.user_flags;
	CamelTag *tag = mi->info.user_tags;
#endif
	char *ret;
	const char *p, *uidstr;
	guint32 uid;

	/* FIXME: work out what to do with uid's that aren't stored here? */
	/* FIXME: perhaps make that a mbox folder only issue?? */
	p = uidstr = camel_lite_message_info_uid(mi);
	while (*p && isdigit(*p))
		p++;
	if (*p == 0 && sscanf (uidstr, "%u", &uid) == 1) {
		g_string_printf (out, "%08x-%04x", uid, mi->info.flags & 0x1fff);
	} else {
		g_string_printf (out, "%s-%04x", uidstr, mi->info.flags & 0x1fff);
	}

#ifdef NON_TINYMAIL_FEATURES
	if (flag || tag) {
		val = g_string_new ("");

		if (flag) {
			while (flag) {
				g_string_append (val, flag->name);
				if (flag->next)
					g_string_append_c (val, ',');
				flag = flag->next;
			}
			camel_lite_header_set_param (&params, "flags", val->str);
			g_string_truncate (val, 0);
		}
		if (tag) {
			while (tag) {
				g_string_append (val, tag->name);
				g_string_append_c (val, '=');
				g_string_append (val, tag->value);
				if (tag->next)
					g_string_append_c (val, ',');
				tag = tag->next;
			}
			camel_lite_header_set_param (&params, "tags", val->str);
		}
		g_string_free (val, TRUE);
		camel_lite_header_param_list_format_append (out, params);
		camel_lite_header_param_list_free (params);
	}
#endif

	ret = out->str;
	g_string_free (out, FALSE);

	return ret;
}

static int
local_summary_decode_x_evolution(CamelLocalSummary *cls, const char *xev, CamelLocalMessageInfo *mi)
{
	struct _camel_lite_header_param *params, *scan;
	guint32 uid, flags;
	char *header;
	int i;
	char uidstr[20];

	uidstr[0] = 0;

	/* check for uid/flags */
	header = camel_lite_header_token_decode(xev);
	if (header && strlen(header) == strlen("00000000-0000")
	    && sscanf(header, "%08x-%04x", &uid, &flags) == 2) {
		if (mi)
			sprintf(uidstr, "%u", uid);
	} else {
		g_free(header);
		return -1;
	}
	g_free(header);

	if (mi == NULL)
		return 0;

	/* check for additional data */
	header = strchr(xev, ';');
	if (header) {
		params = camel_lite_header_param_list_decode(header+1, NULL);
		scan = params;
		while (scan) {
			if (!g_ascii_strcasecmp(scan->name, "flags")) {
				char **flagv = g_strsplit(scan->value, ",", 1000);

				for (i=0;flagv[i];i++)
					camel_lite_message_info_set_user_flag((CamelMessageInfo *)mi, flagv[i], TRUE);
				g_strfreev(flagv);
			} else if (!g_ascii_strcasecmp(scan->name, "tags")) {
				char **tagv = g_strsplit(scan->value, ",", 10000);
				char *val;

				for (i=0;tagv[i];i++) {
					val = strchr(tagv[i], '=');
					if (val) {
						*val++ = 0;
						camel_lite_message_info_set_user_tag((CamelMessageInfo *)mi, tagv[i], val);
						val[-1]='=';
					}
				}
				g_strfreev(tagv);
			}
			scan = scan->next;
		}
		camel_lite_header_param_list_free(params);
	}

	mi->info.uid = g_strdup(uidstr);
	mi->info.flags = flags;

	return 0;
}

static int
summary_header_load(CamelFolderSummary *s)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)s;

	/* We dont actually add our own headers, but version that we don't anyway */

	if (((CamelFolderSummaryClass *)camel_lite_local_summary_parent)->summary_header_load(s) == -1)
		return -1;

	/* Legacy version, version is in summary only */
	if ((s->version & 0xfff) == 0x20c)
		return 0;

	/* otherwise load the version number */

	cls->version = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	/*CamelLocalSummary *cls = (CamelLocalSummary *)s;*/

	if (((CamelFolderSummaryClass *)camel_lite_local_summary_parent)->summary_header_save(s, out) == -1)
		return -1;

	return camel_lite_file_util_encode_fixed_int32(out, CAMEL_LOCAL_SUMMARY_VERSION);
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_lite_header_raw *h)
{
	CamelLocalMessageInfo *mi;

	mi = (CamelLocalMessageInfo *)((CamelFolderSummaryClass *)camel_lite_local_summary_parent)->message_info_new_from_header(s, h);
	return (CamelMessageInfo *)mi;
}
