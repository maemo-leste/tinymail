/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#ifndef __CAMEL_STRING_UTILS_H__
#define __CAMEL_STRING_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

void camel_lite_du (char *name, int *my_size);
void camel_lite_rm (char *name);

int   camel_lite_strcase_equal (gconstpointer a, gconstpointer b);
guint camel_lite_strcase_hash  (gconstpointer v);

void camel_lite_string_list_free (GList *string_list);

char *camel_lite_strstrcase (const char *haystack, const char *needle);

const char *camel_lite_strdown (char *str);
char camel_lite_tolower(char c);
char camel_lite_toupper(char c);

const char *camel_lite_pstring_add (char *str, gboolean own);
const char *camel_lite_pstring_strdup(const char *s);
void camel_lite_pstring_free(const char *s);

G_END_DECLS

#endif /* __CAMEL_STRING_UTILS_H__ */
