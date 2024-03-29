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

#ifndef _CAMEL_NNTP_SUMMARY_H
#define _CAMEL_NNTP_SUMMARY_H

#include <camel/camel-folder-summary.h>

#define CAMEL_NNTP_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_lite_nntp_summary_get_type (), CamelNNTPSummary)
#define CAMEL_NNTP_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_nntp_summary_get_type (), CamelNNTPSummaryClass)
#define CAMEL_IS_LOCAL_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_nntp_summary_get_type ())

G_BEGIN_DECLS

struct _CamelNNTPStore;
struct _CamelFolderChangeInfo;
struct _CamelException;

typedef struct _CamelNNTPSummary      CamelNNTPSummary;
typedef struct _CamelNNTPSummaryClass CamelNNTPSummaryClass;

struct _CamelNNTPSummary {
	CamelFolderSummary parent;

	struct _CamelNNTPSummaryPrivate *priv;

	guint32 version;
	guint32 high, low;
};

struct _CamelNNTPSummaryClass {
	CamelFolderSummaryClass parent_class;
};

CamelType	camel_lite_nntp_summary_get_type	(void);
CamelNNTPSummary *camel_lite_nntp_summary_new(struct _CamelFolder *folder, const char *path);

int camel_lite_nntp_summary_check(CamelNNTPSummary *cns, struct _CamelNNTPStore *store, char *line, struct _CamelFolderChangeInfo *changes, struct _CamelException *ex);

G_END_DECLS

#endif /* ! _CAMEL_NNTP_SUMMARY_H */

