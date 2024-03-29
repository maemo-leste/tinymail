/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 * 	    Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2004 Ximian, Inc.
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

#ifndef CAMEL_NET_UTILS_H
#define CAMEL_NET_UTILS_H 1

#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

G_BEGIN_DECLS

struct _CamelException;

#ifndef _WIN32
#ifdef NEED_ADDRINFO
/* Some of this is copied from GNU's netdb.h

  Copyright (C) 1996-2002, 2003, 2004 Free Software Foundation, Inc.
  This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
*/
struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	size_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

#define AI_CANONNAME	0x0002	/* Request for canonical name.  */
#define AI_NUMERICHOST	0x0004	/* Don't use name resolution.  */

/* Error values for `getaddrinfo' function.  */
#define EAI_BADFLAGS	  -1	/* Invalid value for `ai_flags' field.  */
#define EAI_NONAME	  -2	/* NAME or SERVICE is unknown.  */
#define EAI_AGAIN	  -3	/* Temporary failure in name resolution.  */
#define EAI_FAIL	  -4	/* Non-camel_lite_recoverable failure in name res.  */
#define EAI_NODATA	  -5	/* No address associated with NAME.  */
#define EAI_FAMILY	  -6	/* `ai_family' not supported.  */
#define EAI_SOCKTYPE	  -7	/* `ai_socktype' not supported.  */
#define EAI_SERVICE	  -8	/* SERVICE not supported for `ai_socktype'.  */
#define EAI_ADDRFAMILY	  -9	/* Address family for NAME not supported.  */
#define EAI_MEMORY	  -10	/* Memory allocation failure.  */
#define EAI_SYSTEM	  -11	/* System error returned in `errno'.  */
#define EAI_OVERFLOW	  -12	/* Argument buffer overflow.  */

#define NI_NUMERICHOST	1	/* Don't try to look up hostname.  */
#define NI_NUMERICSERV	2	/* Don't convert port number to name.  */
#define NI_NOFQDN	4	/* Only return nodename portion.  */
#define NI_NAMEREQD	8	/* Don't return numeric addresses.  */
#define NI_DGRAM	16	/* Look up UDP service rather than TCP.  */
#endif
#endif

struct addrinfo *camel_lite_getaddrinfo(const char *name, const char *service,
				   const struct addrinfo *hints, struct _CamelException *ex);
void camel_lite_freeaddrinfo(struct addrinfo *host);
int camel_lite_getnameinfo(const struct sockaddr *sa, socklen_t salen, char **host, char **serv,
		      int flags, struct _CamelException *ex);

G_END_DECLS

#endif /* CAMEL_NET_UTILS_H */
