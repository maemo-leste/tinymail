/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright(C) 2000 Ximian Inc.
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "camel-file-utils.h"

#include "camel-imapp-summary.h"

#define CAMEL_IMAPP_SUMMARY_VERSION (1)

static int summary_header_load(CamelFolderSummary *);
static int summary_header_save(CamelFolderSummary *, FILE *);

static CamelMessageInfo *message_info_load(CamelFolderSummary *s, gboolean *must_add);
static int message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *info);

static void camel_lite_imapp_summary_class_init(CamelIMAPPSummaryClass *klass);
static void camel_lite_imapp_summary_init      (CamelIMAPPSummary *obj);

static CamelFolderSummaryClass *camel_lite_imapp_summary_parent;

CamelType
camel_lite_imapp_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(
			camel_lite_folder_summary_get_type(), "CamelLiteIMAPPSummary",
			sizeof(CamelIMAPPSummary),
			sizeof(CamelIMAPPSummaryClass),
			(CamelObjectClassInitFunc) camel_lite_imapp_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_lite_imapp_summary_init,
			NULL);
	}

	return type;
}

static void
camel_lite_imapp_summary_class_init(CamelIMAPPSummaryClass *klass)
{
	CamelFolderSummaryClass *cfs_class =(CamelFolderSummaryClass *) klass;

	camel_lite_imapp_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS(camel_lite_type_get_global_classfuncs(camel_lite_folder_summary_get_type()));

	cfs_class->summary_header_load = summary_header_load;
	cfs_class->summary_header_save = summary_header_save;
	cfs_class->message_info_load = message_info_load;
	cfs_class->message_info_save = message_info_save;
}

static void
camel_lite_imapp_summary_init(CamelIMAPPSummary *obj)
{
	CamelFolderSummary *s =(CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelIMAPPMessageInfo);
	s->content_info_size = sizeof(CamelMessageContentInfo);

	/* and a unique file version */
	s->version += CAMEL_IMAPP_SUMMARY_VERSION;
}

/**
 * camel_lite_imapp_summary_new:
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelIMAPPSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelIMAPPSummary object.
 **/
CamelFolderSummary *
camel_lite_imapp_summary_new(void)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY(camel_lite_object_new(camel_lite_imapp_summary_get_type()));

	return summary;
}


static int
summary_header_load(CamelFolderSummary *s)
{
	CamelIMAPPSummary *ims = CAMEL_IMAPP_SUMMARY(s);
	unsigned char *ptrchr = s->filepos;

	if (camel_lite_imapp_summary_parent->summary_header_load(s) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x100c) {
		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &ims->uidvalidity, FALSE);
		s->filepos = ptrchr;

		return 0;
	}

	ims->version = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;
	ims->uidvalidity = g_ntohl(get_unaligned_u32(s->filepos));
	s->filepos += 4;

	if (ims->version > CAMEL_IMAPP_SUMMARY_VERSION) {
		g_warning("Unkown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelIMAPPSummary *ims = CAMEL_IMAPP_SUMMARY(s);

	if (camel_lite_imapp_summary_parent->summary_header_save(s, out) == -1)
		return -1;

	if (camel_lite_file_util_encode_fixed_int32(out, CAMEL_IMAPP_SUMMARY_VERSION) == -1
	    || camel_lite_file_util_encode_fixed_int32(out, ims->uidvalidity) == -1)
		return -1;

	return 0;
}


static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, gboolean *must_add)
{
	CamelMessageInfo *info;
	CamelIMAPPMessageInfo *iinfo;

	info = camel_lite_imapp_summary_parent->message_info_load(s, must_add);
	if (info) {
		unsigned char *ptrchr = s->filepos;
		iinfo =(CamelIMAPPMessageInfo *)info;

		ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &iinfo->server_flags, FALSE);
		s->filepos = ptrchr;
	}

	return info;
}

static int
message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	CamelIMAPPMessageInfo *iinfo =(CamelIMAPPMessageInfo *)info;

	if (camel_lite_imapp_summary_parent->message_info_save(s, out, info) == -1)
		return -1;

	return camel_lite_file_util_encode_uint32(out, iinfo->server_flags);
}
