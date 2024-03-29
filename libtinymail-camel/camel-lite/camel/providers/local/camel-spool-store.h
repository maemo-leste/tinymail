/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2001 Ximian Inc (www.ximian.com)
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


#ifndef CAMEL_SPOOL_STORE_H
#define CAMEL_SPOOL_STORE_H 1

#include "camel-mbox-store.h"

#define CAMEL_SPOOL_STORE_TYPE     (camel_lite_spool_store_get_type ())
#define CAMEL_SPOOL_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SPOOL_STORE_TYPE, CamelSpoolStore))
#define CAMEL_SPOOL_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SPOOL_STORE_TYPE, CamelSpoolStoreClass))
#define CAMEL_IS_SPOOL_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SPOOL_STORE_TYPE))

G_BEGIN_DECLS

typedef enum _camel_lite_spool_store_t {
	CAMEL_SPOOL_STORE_MBOX,	/* a single mbox */
	CAMEL_SPOOL_STORE_ELM	/* elm/pine/etc tree of mbox files in folders */
} camel_lite_spool_store_t;

typedef struct {
	CamelMboxStore parent_object;

	camel_lite_spool_store_t type;
} CamelSpoolStore;



typedef struct {
	CamelMboxStoreClass parent_class;

} CamelSpoolStoreClass;


/* public methods */

/* Standard Camel function */
CamelType camel_lite_spool_store_get_type (void);

G_END_DECLS

#endif /* CAMEL_SPOOL_STORE_H */


