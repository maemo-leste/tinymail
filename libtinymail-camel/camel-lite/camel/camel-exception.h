/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-execpetion.h : exception utils */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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



#ifndef CAMEL_EXCEPTION_H
#define CAMEL_EXCEPTION_H 1

#include <camel/camel-types.h>

G_BEGIN_DECLS

typedef enum {
#include "camel-exception-list.def"

} ExceptionId;

struct _CamelException {
	/* do not access the fields directly */
	ExceptionId id;
	char *desc;
};

#define CAMEL_EXCEPTION_INITIALISER { 0, NULL }

/* creation and destruction functions */
CamelException *          camel_lite_exception_new           (void);
void                      camel_lite_exception_free          (CamelException *ex);
void                      camel_lite_exception_init          (CamelException *ex);


/* exception content manipulation */
void                      camel_lite_exception_clear         (CamelException *ex);
void                      camel_lite_exception_set           (CamelException *ex,
							 ExceptionId id,
							 const char *desc);
void                      camel_lite_exception_setv          (CamelException *ex,
							 ExceptionId id,
							 const char *format,
							 ...);

/* exception content transfer */
void                      camel_lite_exception_xfer          (CamelException *ex_dst,
							 CamelException *ex_src);


/* exception content retrieval */
ExceptionId               camel_lite_exception_get_id        (CamelException *ex);
const char *             camel_lite_exception_get_description (CamelException *ex);

#define camel_lite_exception_is_set(ex) (camel_lite_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)

G_END_DECLS

#endif /* CAMEL_EXCEPTION_H */

