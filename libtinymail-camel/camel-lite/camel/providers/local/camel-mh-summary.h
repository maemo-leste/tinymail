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

#ifndef _CAMEL_MH_SUMMARY_H
#define _CAMEL_MH_SUMMARY_H

#include "camel-local-summary.h"
#include <camel/camel-folder.h>
#include <camel/camel-exception.h>
#include <camel/camel-index.h>

#define CAMEL_MH_SUMMARY(obj)	CAMEL_CHECK_CAST (obj, camel_lite_mh_summary_get_type (), CamelMhSummary)
#define CAMEL_MH_SUMMARY_CLASS(klass)	CAMEL_CHECK_CLASS_CAST (klass, camel_lite_mh_summary_get_type (), CamelMhSummaryClass)
#define CAMEL_IS_MH_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_mh_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMhSummary	CamelMhSummary;
typedef struct _CamelMhSummaryClass	CamelMhSummaryClass;

struct _CamelMhSummary {
	CamelLocalSummary parent;
	struct _CamelMhSummaryPrivate *priv;
};

struct _CamelMhSummaryClass {
	CamelLocalSummaryClass parent_class;

	/* virtual methods */

	/* signals */
};

CamelType	 camel_lite_mh_summary_get_type	(void);
CamelMhSummary	*camel_lite_mh_summary_new(struct _CamelFolder *, const char *filename, const char *mhdir, CamelIndex *index);

G_END_DECLS

#endif /* ! _CAMEL_MH_SUMMARY_H */
