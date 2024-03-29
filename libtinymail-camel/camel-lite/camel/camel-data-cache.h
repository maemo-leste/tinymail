/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-data-cache.h: Class for a Camel filesystem cache
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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


#ifndef CAMEL_DATA_CACHE_H
#define CAMEL_DATA_CACHE_H 1

#include <glib.h>

#include <camel/camel-stream.h>
#include <camel/camel-exception.h>
#include <camel/camel-folder-summary.h>

#define CAMEL_DATA_CACHE_TYPE     (camel_lite_data_cache_get_type ())
#define CAMEL_DATA_CACHE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_DATA_CACHE_TYPE, CamelFolder))
#define CAMEL_DATA_CACHE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_DATA_CACHE_TYPE, CamelFolderClass))
#define CAMEL_IS_DATA_CACHE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_DATA_CACHE_TYPE))

G_BEGIN_DECLS

typedef struct _CamelDataCache CamelDataCache;
typedef struct _CamelDataCacheClass CamelDataCacheClass;

struct _CamelDataCache {
	CamelObject parent_object;

	struct _CamelDataCachePrivate *priv;

	char *path;
	guint32 flags;

	time_t expire_age;
	time_t expire_access;
};

struct _CamelDataCacheClass {
	CamelObjectClass parent_class;

	/* None are virtual yet */
#if 0
	/* Virtual methods */
	CamelStream *(*add)(CamelDataCache *cmc, const char *path, const char *key, CamelException *ex);
	CamelStream *(*get)(CamelDataCache *cmc, const char *path, const char *key, CamelException *ex);
	int (*close)(CamelDataCache *cmc, CamelStream *stream, CamelException *ex);
	int (*remove)(CamelDataCache *cmc, const char *path, const char *key, CamelException *ex);

	int (*clear)(CamelDataCache *cmc, const char *path, CamelException *ex);
#endif
};

/* public methods */
CamelDataCache *camel_lite_data_cache_new(const char *path, guint32 flags, CamelException *ex);

void camel_lite_data_cache_set_expire_age(CamelDataCache *cache, time_t when);
void camel_lite_data_cache_set_expire_access(CamelDataCache *cdc, time_t when);

int             camel_lite_data_cache_rename(CamelDataCache *cache,
					const char *old, const char *new, CamelException *ex);

CamelStream    *camel_lite_data_cache_add(CamelDataCache *cache,
				     const char *path, const char *key, CamelException *ex);
CamelStream    *camel_lite_data_cache_get(CamelDataCache *cache,
				     const char *path, const char *key, CamelException *ex);
gboolean       camel_lite_data_cache_exists (CamelDataCache *cache,
				     const char *path, const char *key, CamelException *ex);

int             camel_lite_data_cache_remove(CamelDataCache *cache,
					const char *path, const char *key, CamelException *ex);

int             camel_lite_data_cache_clear(CamelDataCache *cache,
				       const char *path, CamelException *ex);


gboolean     camel_lite_data_cache_is_partial (CamelDataCache *cache, const char *path,
					      const char *uid);

void         camel_lite_data_cache_set_partial (CamelDataCache *cache, const char *path,
					      const char *uid, gboolean partial);
gboolean     camel_lite_data_cache_get_allow_external_images (CamelDataCache *cache, const char *path,
							 const char *uid);
void         camel_lite_data_cache_set_allow_external_images (CamelDataCache *cache, const char *path,
							 const char *uid, gboolean allow);
void         camel_lite_data_cache_delete_attachments (CamelDataCache *cdc, const char *path,
					      const char *key);

void camel_lite_data_cache_set_flags (CamelDataCache *cdc, const char *path, CamelMessageInfoBase *mi);

/* Standard Camel function */
CamelType camel_lite_data_cache_get_type (void);

G_END_DECLS

#endif /* CAMEL_DATA_CACHE_H */
