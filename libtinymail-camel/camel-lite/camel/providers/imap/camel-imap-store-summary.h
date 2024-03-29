/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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


#ifndef _CAMEL_IMAP_STORE_SUMMARY_H
#define _CAMEL_IMAP_STORE_SUMMARY_H

#include <camel/camel-object.h>
#include <camel/camel-store-summary.h>

#define CAMEL_IMAP_STORE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_lite_imap_store_summary_get_type (), CamelImapStoreSummary)
#define CAMEL_IMAP_STORE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_imap_store_summary_get_type (), CamelImapStoreSummaryClass)
#define CAMEL_IS_IMAP_STORE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_imap_store_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelImapStoreSummary      CamelImapStoreSummary;
typedef struct _CamelImapStoreSummaryClass CamelImapStoreSummaryClass;

typedef struct _CamelImapStoreInfo CamelImapStoreInfo;

enum {
	CAMEL_IMAP_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_IMAP_STORE_INFO_LAST
};

typedef enum _CamelImapStoreNamespaceType {
	CAMEL_IMAP_STORE_NAMESPACE_TYPE_NONE,
	CAMEL_IMAP_STORE_NAMESPACE_TYPE_PERSONAL,
	CAMEL_IMAP_STORE_NAMESPACE_TYPE_OTHER,
	CAMEL_IMAP_STORE_NAMESPACE_TYPE_SHARED
} CamelImapStoreNamespaceType;

struct _CamelImapStoreInfo {
	CamelStoreInfo info;
	char *full_name;
};

typedef struct _CamelImapStoreNamespace CamelImapStoreNamespace;

struct _CamelImapStoreNamespace {
	char *path;		/* display path */
	char *full_name;	/* real name */
	char sep;		/* directory separator */
	CamelImapStoreNamespaceType type; /* type of namespace */
};

struct _CamelImapStoreSummary {
	CamelStoreSummary summary;

	struct _CamelImapStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of base part of file */
	guint32 capabilities;
	GList *namespaces; /*list of CamelImapStoreNamespace */
};

struct _CamelImapStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

CamelType			 camel_lite_imap_store_summary_get_type	(void);
CamelImapStoreSummary      *camel_lite_imap_store_summary_new	(void);

/* TODO: this api needs some more work, needs to support lists */
CamelImapStoreNamespace *camel_lite_imap_store_summary_namespace_new(CamelImapStoreSummary *s, const char *full_name, char dir_sep, CamelImapStoreNamespaceType type);
CamelImapStoreNamespace *camel_lite_imap_store_summary_namespace_add(CamelImapStoreSummary *s, CamelImapStoreNamespace *ns);
CamelImapStoreNamespace *camel_lite_imap_store_summary_namespace_find_path(CamelImapStoreSummary *s, const char *path);
CamelImapStoreNamespace *camel_lite_imap_store_summary_namespace_find_full(CamelImapStoreSummary *s, const char *full_name);

/* converts to/from utf8 canonical nasmes */
char *camel_lite_imap_store_summary_full_to_path(CamelImapStoreSummary *s, const char *full_name, char dir_sep);
char *camel_lite_imap_store_summary_path_to_full(CamelImapStoreSummary *s, const char *path, char dir_sep);

CamelImapStoreInfo *camel_lite_imap_store_summary_full_name(CamelImapStoreSummary *s, const char *full_name);
CamelImapStoreInfo *camel_lite_imap_store_summary_add_from_full(CamelImapStoreSummary *s, const char *full_name, char dir_sep);

/* a convenience lookup function. always use this if path known */
char *camel_lite_imap_store_summary_full_from_path(CamelImapStoreSummary *s, const char *path);

/* helper macro's */
#define camel_lite_imap_store_info_full_name(s, i) (camel_lite_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_IMAP_STORE_INFO_FULL_NAME))

G_END_DECLS

#endif /* ! _CAMEL_IMAP_STORE_SUMMARY_H */
