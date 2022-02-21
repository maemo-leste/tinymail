/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-tcp-stream.h"

#define w(x)

static CamelStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStream */
#define CTS_CLASS(so) CAMEL_TCP_STREAM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static int tcp_connect    (CamelTcpStream *stream, struct addrinfo *host);
static int tcp_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static int tcp_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *tcp_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *tcp_get_remote_address (CamelTcpStream *stream, socklen_t *len);
static ssize_t tcp_read_nb (CamelTcpStream *stream, char *buffer, size_t n);
static int tcp_gettimeout (CamelTcpStream *stream);

static void 
tcp_enable_compress (CamelTcpStream *stream)
{
	g_warning ("Compress not supported on this stream");
	return;
}


static void
camel_lite_tcp_stream_class_init (CamelTcpStreamClass *camel_lite_tcp_stream_class)
{
	/*CamelStreamClass *camel_lite_stream_class = CAMEL_STREAM_CLASS (camel_lite_tcp_stream_class);*/

	parent_class = CAMEL_STREAM_CLASS (camel_lite_type_get_global_classfuncs (CAMEL_STREAM_TYPE));

	/* tcp stream methods */
	camel_lite_tcp_stream_class->enable_compress     = tcp_enable_compress;
	camel_lite_tcp_stream_class->gettimeout         = tcp_gettimeout;
	camel_lite_tcp_stream_class->read_nb            = tcp_read_nb;
	camel_lite_tcp_stream_class->connect            = tcp_connect;
	camel_lite_tcp_stream_class->getsockopt         = tcp_getsockopt;
	camel_lite_tcp_stream_class->setsockopt         = tcp_setsockopt;
	camel_lite_tcp_stream_class->get_local_address  = tcp_get_local_address;
	camel_lite_tcp_stream_class->get_remote_address = tcp_get_remote_address;
}

static void
camel_lite_tcp_stream_init (void *o)
{
	;
}

CamelType
camel_lite_tcp_stream_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (CAMEL_STREAM_TYPE,
					    "CamelLiteTcpStream",
					    sizeof (CamelTcpStream),
					    sizeof (CamelTcpStreamClass),
					    (CamelObjectClassInitFunc) camel_lite_tcp_stream_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_tcp_stream_init,
					    NULL);
	}

	return type;
}

static ssize_t
tcp_read_nb (CamelTcpStream *stream, char *buffer, size_t n)
{
	w(g_warning ("CamelLiteTcpStream::read_nb called on default implementation"));
	return -1;
}

static int
tcp_connect (CamelTcpStream *stream, struct addrinfo *host)
{
	w(g_warning ("CamelLiteTcpStream::connect called on default implementation"));
	return -1;
}

/**
 * camel_lite_tcp_stream_connect:
 * @stream: a #CamelTcpStream object
 * @host: a linked list of addrinfo structures to try to connect, in
 * the order of most likely to least likely to work.
 *
 * Create a socket and connect based upon the data provided.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_tcp_stream_connect (CamelTcpStream *stream, struct addrinfo *host)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->connect (stream, host);
}


void 
camel_lite_tcp_stream_enable_compress (CamelTcpStream *stream)
{
	g_return_if_fail (CAMEL_IS_TCP_STREAM (stream));

	CTS_CLASS (stream)->enable_compress (stream);
}

static int
tcp_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	w(g_warning ("CamelLiteTcpStream::getsockopt called on default implementation"));
	return -1;
}

/**
 * camel_lite_tcp_stream_getsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Get the socket options set on the stream and populate @data.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_tcp_stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->getsockopt (stream, data);
}

static int
tcp_gettimeout (CamelTcpStream *stream)
{
	w(g_warning ("CamelLiteTcpStream::gettimeout called on default implementation"));
	return 330;
}

int
camel_lite_tcp_stream_gettimeout (CamelTcpStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->gettimeout (stream);
}

static int
tcp_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	w(g_warning ("CamelLiteTcpStream::setsockopt called on default implementation"));
	return -1;
}

/**
 * camel_lite_tcp_stream_setsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Set the socket options contained in @data on the stream.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
camel_lite_tcp_stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->setsockopt (stream, data);
}

static struct sockaddr *
tcp_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	w(g_warning ("CamelLiteTcpStream::get_local_address called on default implementation"));
	return NULL;
}

/**
 * camel_lite_tcp_stream_get_local_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length which must be supplied
 *
 * Get the local address of @stream.
 *
 * Returns the stream's local address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not
 **/
struct sockaddr *
camel_lite_tcp_stream_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail(len != NULL, NULL);

	return CTS_CLASS (stream)->get_local_address (stream, len);
}

int
camel_lite_tcp_stream_read_nb (CamelTcpStream *stream, char *buffer, size_t n)
{
	return CTS_CLASS (stream)->read_nb (stream, buffer, n);
}

static struct sockaddr *
tcp_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	w(g_warning ("CamelLiteTcpStream::get_remote_address called on default implementation"));
	return NULL;
}

/**
 * camel_lite_tcp_stream_get_remote_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length, which must be supplied
 *
 * Get the remote address of @stream.
 *
 * Returns the stream's remote address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not.
 **/
struct sockaddr *
camel_lite_tcp_stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail(len != NULL, NULL);

	return CTS_CLASS (stream)->get_remote_address (stream, len);
}
