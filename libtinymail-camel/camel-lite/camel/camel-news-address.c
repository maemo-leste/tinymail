/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors:
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

#include "camel-news-address.h"


static void camel_lite_news_address_class_init (CamelNewsAddressClass *klass);

static CamelAddressClass *camel_lite_news_address_parent;

static void
camel_lite_news_address_class_init (CamelNewsAddressClass *klass)
{
	camel_lite_news_address_parent = CAMEL_ADDRESS_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_address_get_type ()));
}


CamelType
camel_lite_news_address_get_type (void)
{
	static guint type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_address_get_type (), "CamelLiteNewsAddress",
					    sizeof (CamelNewsAddress),
					    sizeof (CamelNewsAddressClass),
					    (CamelObjectClassInitFunc) camel_lite_news_address_class_init,
					    NULL,
					    NULL,
					    NULL);
	}

	return type;
}

/**
 * camel_lite_news_address_new:
 *
 * Create a new CamelNewsAddress object.
 *
 * Return value: A new CamelNewsAddress widget.
 **/
CamelNewsAddress *
camel_lite_news_address_new (void)
{
	CamelNewsAddress *new = CAMEL_NEWS_ADDRESS ( camel_lite_object_new (camel_lite_news_address_get_type ()));
	return new;
}
