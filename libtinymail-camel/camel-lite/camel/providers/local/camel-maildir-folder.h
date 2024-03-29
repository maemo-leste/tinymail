/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors:
 * 	Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999 Ximian Inc.
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

#ifndef CAMEL_MAILDIR_FOLDER_H
#define CAMEL_MAILDIR_FOLDER_H 1

#include "camel-local-folder.h"

#define CAMEL_MAILDIR_FOLDER_TYPE     (camel_lite_maildir_folder_get_type ())
#define CAMEL_MAILDIR_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAILDIR_FOLDER_TYPE, CamelMaildirFolder))
#define CAMEL_MAILDIR_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAILDIR_FOLDER_TYPE, CamelMaildirFolderClass))
#define CAMEL_IS_MAILDIR_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAILDIR_FOLDER_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelLocalFolder parent_object;

} CamelMaildirFolder;

typedef struct {
	CamelLocalFolderClass parent_class;

	/* Virtual methods */

} CamelMaildirFolderClass;

/* public methods */
CamelFolder *camel_lite_maildir_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex);

/* Standard Camel function */
CamelType camel_lite_maildir_folder_get_type(void);


G_END_DECLS

#endif /* CAMEL_MAILDIR_FOLDER_H */
