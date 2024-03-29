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

#ifndef _CAMEL_MAILDIR_SUMMARY_H
#define _CAMEL_MAILDIR_SUMMARY_H

#include "camel-local-summary.h"
#include <camel/camel-folder.h>
#include <camel/camel-exception.h>
#include <camel/camel-index.h>

#define CAMEL_MAILDIR_SUMMARY(obj)	CAMEL_CHECK_CAST (obj, camel_lite_maildir_summary_get_type (), CamelMaildirSummary)
#define CAMEL_MAILDIR_SUMMARY_CLASS(klass)	CAMEL_CHECK_CLASS_CAST (klass, camel_lite_maildir_summary_get_type (), CamelMaildirSummaryClass)
#define CAMEL_IS_MAILDIR_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_maildir_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMaildirSummary	CamelMaildirSummary;
typedef struct _CamelMaildirSummaryClass	CamelMaildirSummaryClass;

typedef struct _CamelMaildirMessageContentInfo {
	CamelMessageContentInfo info;
} CamelMaildirMessageContentInfo;

enum {
	CAMEL_MAILDIR_INFO_FILENAME = CAMEL_MESSAGE_INFO_LAST,
	CAMEL_MAILDIR_INFO_LAST
};

typedef struct _CamelMaildirMessageInfo {
	CamelLocalMessageInfo info;

	char *filename;		/* maildir has this annoying status shit on the end of the filename, use this to get the real message id */
} CamelMaildirMessageInfo;

struct _CamelMaildirSummary {
	CamelLocalSummary parent;
	struct _CamelMaildirSummaryPrivate *priv;
};

struct _CamelMaildirSummaryClass {
	CamelLocalSummaryClass parent_class;

	/* virtual methods */

	/* signals */
};

CamelType	 camel_lite_maildir_summary_get_type	(void);
CamelMaildirSummary	*camel_lite_maildir_summary_new	(struct _CamelFolder *folder, const char *filename, const char *maildirdir, CamelIndex *index);

/* convert some info->flags to/from the messageinfo */
char *camel_lite_maildir_summary_info_to_name(const CamelMaildirMessageInfo *info);
int camel_lite_maildir_summary_name_to_info(CamelMaildirMessageInfo *info, const char *name);

/* TODO: could proably use get_string stuff */
#define camel_lite_maildir_info_filename(x) (((CamelMaildirMessageInfo *)x)->filename?((CamelMaildirMessageInfo *)x)->filename:"")
#define camel_lite_maildir_info_set_filename(x, s) (g_free(((CamelMaildirMessageInfo *)x)->filename),((CamelMaildirMessageInfo *)x)->filename = s)

char *camel_lite_maildir_get_filename (const gchar *fpath, CamelMaildirMessageInfo *mdi, const gchar *uid);

G_END_DECLS

#endif /* ! _CAMEL_MAILDIR_SUMMARY_H */
