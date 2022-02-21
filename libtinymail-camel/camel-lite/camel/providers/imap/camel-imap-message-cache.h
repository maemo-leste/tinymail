/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-message-cache.h: Class for an IMAP message cache */

/*
 * Author:
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */


#ifndef CAMEL_IMAP_MESSAGE_CACHE_H
#define CAMEL_IMAP_MESSAGE_CACHE_H 1

#include "camel-imap-types.h"
#include "camel-folder.h"
#include <camel/camel-folder-search.h>

#define CAMEL_IMAP_MESSAGE_CACHE_TYPE     (camel_lite_imap_message_cache_get_type ())
#define CAMEL_IMAP_MESSAGE_CACHE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_MESSAGE_CACHE_TYPE, CamelImapFolder))
#define CAMEL_IMAP_MESSAGE_CACHE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_MESSAGE_CACHE_TYPE, CamelImapFolderClass))
#define CAMEL_IS_IMAP_MESSAGE_CACHE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_MESSAGE_CACHE_TYPE))

G_BEGIN_DECLS

struct _CamelImapMessageCache {
	CamelObject parent_object;

	char *path;
	GHashTable *parts, *cached;
	guint32 max_uid;
};


typedef struct {
	CamelFolderClass parent_class;

	/* Virtual methods */

} CamelImapMessageCacheClass;


/* public methods */
CamelImapMessageCache *camel_lite_imap_message_cache_new (const char *path,
						     CamelFolderSummary *summ,
						     CamelException *ex);

void camel_lite_imap_message_cache_set_path (CamelImapMessageCache *cache,
					const char *path);

guint32     camel_lite_imap_message_cache_max_uid (CamelImapMessageCache *cache);

CamelStream *camel_lite_imap_message_cache_insert (CamelImapMessageCache *cache,
					      const char *uid,
					      const char *part_spec,
					      const char *data,
					      int len,
					      CamelException *ex);
void camel_lite_imap_message_cache_insert_stream  (CamelImapMessageCache *cache,
					      const char *uid,
					      const char *part_spec,
					      CamelStream *data_stream,
					      CamelException *ex);
void camel_lite_imap_message_cache_insert_wrapper (CamelImapMessageCache *cache,
					      const char *uid,
					      const char *part_spec,
					      CamelDataWrapper *wrapper,
					      CamelException *ex);

CamelStream *camel_lite_imap_message_cache_get    (CamelImapMessageCache *cache,
					      const char *uid,
					      const char *part_spec,
					      CamelException *ex);

gboolean     camel_lite_imap_message_cache_is_partial (CamelImapMessageCache *cache,
					      const char *uid);

void         camel_lite_imap_message_cache_set_partial (CamelImapMessageCache *cache,
					      const char *uid, gboolean partial);

void         camel_lite_imap_message_cache_remove (CamelImapMessageCache *cache,
					      const char *uid);

void         camel_lite_imap_message_cache_clear  (CamelImapMessageCache *cache);

void         camel_lite_imap_message_cache_copy   (CamelImapMessageCache *source,
					      const char *source_uid,
					      CamelImapMessageCache *dest,
					      const char *dest_uid,
					      CamelException *ex);

void camel_lite_imap_message_cache_set_flags (const gchar *folder_dir, CamelMessageInfoBase *mi);

void camel_lite_imap_message_cache_delete_attachments (CamelImapMessageCache *cache, const char *uid);

void camel_lite_imap_message_cache_replace_cache (CamelImapMessageCache *cache, const char *uid, const char *part_spec,
					     const char *dest_uid, const char *dest_part_spec);

gboolean camel_lite_imap_message_cache_get_allow_external_images (CamelImapMessageCache *cache, const char *uid);
void camel_lite_imap_message_cache_set_allow_external_images (CamelImapMessageCache *cache, const char *uid, gboolean allow);

void
camel_lite_imap_message_cache_replace_with_wrapper (CamelImapMessageCache *cache,
					       const char *uid,
					       CamelDataWrapper *wrapper, CamelException *ex);

/* Standard Camel function */
CamelType camel_lite_imap_message_cache_get_type (void);

G_END_DECLS

#endif /* CAMEL_IMAP_MESSAGE_CACHE_H */
