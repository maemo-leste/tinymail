/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000-2003 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_CHARSET_MAP_H
#define _CAMEL_CHARSET_MAP_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CamelCharset CamelCharset;

struct _CamelCharset {
	unsigned int mask;
	int level;
};

void camel_lite_charset_init(CamelCharset *);
void camel_lite_charset_step(CamelCharset *, const char *in, int len);

const char *camel_lite_charset_best_name (CamelCharset *);

/* helper function */
const char *camel_lite_charset_best(const char *in, int len);

const char *camel_lite_charset_iso_to_windows (const char *isocharset);

G_END_DECLS

#endif /* ! _CAMEL_CHARSET_MAP_H */
