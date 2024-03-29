/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc.
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

#ifndef CAMEL_MBOX_STORE_H
#define CAMEL_MBOX_STORE_H 1

#include "camel-local-store.h"

#define CAMEL_MBOX_STORE_TYPE     (camel_lite_mbox_store_get_type ())
#define CAMEL_MBOX_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MBOX_STORE_TYPE, CamelMboxStore))
#define CAMEL_MBOX_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MBOX_STORE_TYPE, CamelMboxStoreClass))
#define CAMEL_IS_MBOX_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MBOX_STORE_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelLocalStore parent_object;

} CamelMboxStore;

typedef struct {
	CamelLocalStoreClass parent_class;

} CamelMboxStoreClass;

/* public methods */

/* Standard Camel function */
CamelType camel_lite_mbox_store_get_type (void);

G_END_DECLS

#endif /* CAMEL_MBOX_STORE_H */


