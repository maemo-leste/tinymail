/*
 *  Copyright (C) 2000 Ximian Inc.
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

#ifndef _CAMEL_IMAPP_SUMMARY_H
#define _CAMEL_IMAPP_SUMMARY_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>

#define CAMEL_IMAPP_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_lite_imapp_summary_get_type (), CamelIMAPPSummary)
#define CAMEL_IMAPP_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_imapp_summary_get_type (), CamelIMAPPSummaryClass)
#define CAMEL_IS_IMAPP_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_imapp_summary_get_type ())

#define CAMEL_IMAPP_SERVER_FLAGS (CAMEL_MESSAGE_ANSWERED | \
				 CAMEL_MESSAGE_DELETED | \
				 CAMEL_MESSAGE_DRAFT | \
				 CAMEL_MESSAGE_FLAGGED | \
				 CAMEL_MESSAGE_SEEN)

#define CAMEL_IMAPP_MESSAGE_RECENT (1 << 8)

G_BEGIN_DECLS

typedef struct _CamelIMAPPSummaryClass CamelIMAPPSummaryClass;
typedef struct _CamelIMAPPSummary CamelIMAPPSummary;

typedef struct _CamelIMAPPMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} CamelIMAPPMessageInfo;

struct _CamelIMAPPSummary {
	CamelFolderSummary parent;

	guint32 version;
	guint32 uidvalidity;
};

struct _CamelIMAPPSummaryClass {
	CamelFolderSummaryClass parent_class;

};

CamelType               camel_lite_imapp_summary_get_type     (void);
CamelFolderSummary *camel_lite_imapp_summary_new          (void);

G_END_DECLS

#endif /* ! _CAMEL_IMAPP_SUMMARY_H */
