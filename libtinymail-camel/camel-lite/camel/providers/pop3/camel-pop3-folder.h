/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.h : Class for a POP3 folder */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
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

#ifndef CAMEL_POP3_FOLDER_H
#define CAMEL_POP3_FOLDER_H 1

#include <camel/camel-folder.h>
#include <camel/camel-disco-folder.h>

#define CAMEL_POP3_FOLDER_TYPE     (camel_lite_pop3_folder_get_type ())
#define CAMEL_POP3_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_POP3_FOLDER_TYPE, CamelPOP3Folder))
#define CAMEL_POP3_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_POP3_FOLDER_TYPE, CamelPOP3FolderClass))
#define CAMEL_IS_POP3_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_POP3_FOLDER_TYPE))

G_BEGIN_DECLS

typedef struct {
	guint32 id;
	guint32 size;
	guint32 flags;
	guint32 index;		/* index of request */
	char *uid;
	int err;
	struct _CamelPOP3Command *cmd;
	struct _CamelStream *stream;
	gboolean has_attachments;
} CamelPOP3FolderInfo;

typedef struct {
	CamelDiscoFolder parent_object;
} CamelPOP3Folder;

typedef struct {
	CamelDiscoFolderClass parent_class;
	/* Virtual methods */
} CamelPOP3FolderClass;

/* public methods */
CamelFolder *camel_lite_pop3_folder_new (CamelStore *parent, CamelException *ex);

/* Standard Camel function */
CamelType camel_lite_pop3_folder_get_type (void);

int camel_lite_pop3_delete_old(CamelFolder *folder, int days_to_delete, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_POP3_FOLDER_H */
