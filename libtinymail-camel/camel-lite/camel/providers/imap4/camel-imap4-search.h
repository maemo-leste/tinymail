/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2007 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *           Michael Zucchi <notzed@novell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __CAMEL_IMAP4_SEARCH_H__
#define __CAMEL_IMAP4_SEARCH_H__

#include <libedataserver/e-lite-msgport.h>

#include <camel/camel-data-cache.h>
#include <camel/camel-folder-search.h>

#define CAMEL_IMAP4_SEARCH_TYPE         (camel_lite_imap4_search_get_type ())
#define CAMEL_IMAP4_SEARCH(obj)         CAMEL_CHECK_CAST (obj, camel_lite_imap4_search_get_type (), CamelIMAP4Search)
#define CAMEL_IMAP4_SEARCH_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_imap4_search_get_type (), CamelIMAP4SearchClass)
#define CAMEL_IS_IMAP4_SEARCH(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_imap4_search_get_type ())

G_BEGIN_DECLS

typedef struct _CamelIMAP4Search CamelIMAP4Search;
typedef struct _CamelIMAP4SearchClass CamelIMAP4SearchClass;

struct _CamelIMAP4Engine;

struct _CamelIMAP4Search {
	CamelFolderSearch parent_object;

	struct _CamelIMAP4Engine *engine;

	guint32 lastuid;	/* current 'last uid' for the folder */
	guint32 validity;	/* validity of the current folder */

	CamelDataCache *cache;	/* disk-cache for searches */

	/* cache of body search matches */
	EDList matches;
	GHashTable *matches_hash;
	unsigned int matches_count;
};

struct _CamelIMAP4SearchClass {
	CamelFolderSearchClass parent_class;

};


CamelType camel_lite_imap4_search_get_type (void);

CamelFolderSearch *camel_lite_imap4_search_new (struct _CamelIMAP4Engine *engine, const char *cachedir);

G_END_DECLS

#endif /* __CAMEL_IMAP4_SEARCH_H__ */
