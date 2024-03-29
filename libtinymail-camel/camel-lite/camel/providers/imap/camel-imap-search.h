/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-search.h: IMAP folder search */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *
 *  Copyright 2000 Ximian, Inc. (www.ximian.com)
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
 *
 */

#ifndef _CAMEL_IMAP_SEARCH_H
#define _CAMEL_IMAP_SEARCH_H

#include <camel/camel-folder-search.h>
#include <libedataserver/e-lite-msgport.h>
#include <camel/camel-data-cache.h>

#define CAMEL_IMAP_SEARCH_TYPE         (camel_lite_imap_search_get_type ())
#define CAMEL_IMAP_SEARCH(obj)         CAMEL_CHECK_CAST (obj, camel_lite_imap_search_get_type (), CamelImapSearch)
#define CAMEL_IMAP_SEARCH_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_imap_search_get_type (), CamelImapSearchClass)
#define CAMEL_IS_IMAP_SEARCH(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_imap_search_get_type ())

G_BEGIN_DECLS

typedef struct _CamelImapSearchClass CamelImapSearchClass;

struct _CamelImapSearch {
	CamelFolderSearch parent;

	guint32 lastuid;	/* current 'last uid' for the folder */
	guint32 validity;	/* validity of the current folder */

	CamelDataCache *cache;	/* disk-cache for searches */

	/* cache of body search matches */
	unsigned int matches_count;
	EDList matches;
	GHashTable *matches_hash;
};

struct _CamelImapSearchClass {
	CamelFolderSearchClass parent_class;

};

CamelType              camel_lite_imap_search_get_type (void);
CamelFolderSearch *camel_lite_imap_search_new      (const char *cachedir);

G_END_DECLS

#endif /* ! _CAMEL_IMAP_SEARCH_H */
