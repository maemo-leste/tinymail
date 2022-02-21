/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors:
 *   Jeffrey Stedfast <fejj@ximian.com>
 *   Philip Van Hoof <pvanhoof@gnome.org>
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

/* #error "The Tinymail OpenSSL implementation is unfinished and hence unsupported" */

#include <config.h>

#ifndef HAVE_NSS
#ifdef HAVE_OPENSSL

#include <glib/gi18n-lib.h>

#include "camel-tcp-stream-ssl.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "camel-certdb.h"
#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

#include "camel-tcp-stream-ssl.h"

#define d(x)

static CamelTcpStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStreamSSL */
#define CTSR_CLASS(so) CAMEL_TCP_STREAM_SSL_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static ssize_t stream_read_nb (CamelTcpStream *stream, char *buffer, size_t n);
static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write (CamelStream *stream, const char *buffer, size_t n);
static int stream_flush  (CamelStream *stream);
static int stream_close  (CamelStream *stream);

static int stream_connect (CamelTcpStream *stream, struct addrinfo *host);
static int stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static int stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *stream_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *stream_get_remote_address (CamelTcpStream *stream, socklen_t *len);
static int stream_gettimeout (CamelTcpStream *stream);

static SSL *open_ssl_connection (CamelSession *session, int sockfd, CamelTcpStreamSSL *openssl);

struct _CamelTcpStreamSSLPrivate {
	int sockfd;
	SSL *ssl;

	CamelSession *session;
	char *expected_host;
	gboolean ssl_mode;
	guint32 flags;
	CamelService *service;
};

struct CRYPTO_dynlock_value {
	pthread_mutex_t mutex;
};

static pthread_mutex_t *mutex_buf = NULL;


static void 
openssl_enable_compress (CamelTcpStream *stream)
{
	g_print ("OpenSSL COMPRESS enable request: not yet supported");
	/* TODO */ 
	return;
}

static void locking_function(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&mutex_buf[n]);
	else
		pthread_mutex_unlock(&mutex_buf[n]);
}

static unsigned long id_function(void)
{
	return ((unsigned long) pthread_self());
}

static struct CRYPTO_dynlock_value *dyn_create_function(const char *file, int line)
{
	struct CRYPTO_dynlock_value *value;

	value = (struct CRYPTO_dynlock_value *)
		malloc(sizeof(struct CRYPTO_dynlock_value));
	if (!value)
		goto err;
	pthread_mutex_init(&value->mutex, NULL);

	return value;

err:
	return (NULL);
}

static void dyn_lock_function (int mode, struct CRYPTO_dynlock_value *l, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&l->mutex);
	else
		pthread_mutex_unlock(&l->mutex);
}


static void
dyn_destroy_function(struct CRYPTO_dynlock_value *l, const char *file, int line)
{
	pthread_mutex_destroy(&l->mutex);
	free(l);
}


static int
tls_init(void)
{
	int i;

	mutex_buf = malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
	if (mutex_buf == NULL)
		return (-1);
	for (i = 0; i < CRYPTO_num_locks(); i++)
		pthread_mutex_init(&mutex_buf[i], NULL);
	CRYPTO_set_locking_callback(locking_function);
	CRYPTO_set_id_callback(id_function);
	CRYPTO_set_dynlock_create_callback(dyn_create_function);
	CRYPTO_set_dynlock_lock_callback(dyn_lock_function);
	CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);
	SSL_load_error_strings();
	SSLeay_add_ssl_algorithms();
	RAND_load_file("/dev/urandom", 1024);

	return (0);
}


static int
tls_cleanup(void)
{
	int i;

	if (mutex_buf == NULL)
		return (0);

	CRYPTO_set_dynlock_create_callback(NULL);
	CRYPTO_set_dynlock_lock_callback(NULL);
	CRYPTO_set_dynlock_destroy_callback(NULL);

	CRYPTO_set_locking_callback(NULL);
	CRYPTO_set_id_callback(NULL);

	for (i = 0; i < CRYPTO_num_locks(); i++)
		pthread_mutex_destroy(&mutex_buf[i]);
	free(mutex_buf);
	mutex_buf = NULL;

	return (0);
}


static void
camel_lite_tcp_stream_ssl_class_init (CamelTcpStreamSSLClass *camel_lite_tcp_stream_ssl_class)
{
	CamelTcpStreamClass *camel_lite_tcp_stream_class =
		CAMEL_TCP_STREAM_CLASS (camel_lite_tcp_stream_ssl_class);
	CamelStreamClass *camel_lite_stream_class =
		CAMEL_STREAM_CLASS (camel_lite_tcp_stream_ssl_class);

	g_warning ("tinymail: Using openssl, which is not fully tested or supported. NSS is recommended instead.");


	parent_class = CAMEL_TCP_STREAM_CLASS (camel_lite_type_get_global_classfuncs (camel_lite_tcp_stream_get_type ()));

	camel_lite_stream_class->read = stream_read;
	camel_lite_stream_class->write = stream_write;
	camel_lite_stream_class->flush = stream_flush;
	camel_lite_stream_class->close = stream_close;

	camel_lite_tcp_stream_class->enable_compress = openssl_enable_compress;
	camel_lite_tcp_stream_class->gettimeout = stream_gettimeout;
	camel_lite_tcp_stream_class->read_nb = stream_read_nb;
	camel_lite_tcp_stream_class->connect = stream_connect;
	camel_lite_tcp_stream_class->getsockopt = stream_getsockopt;
	camel_lite_tcp_stream_class->setsockopt = stream_setsockopt;
	camel_lite_tcp_stream_class->get_local_address  = stream_get_local_address;
	camel_lite_tcp_stream_class->get_remote_address = stream_get_remote_address;

	/* init OpenSSL stuff */

	tls_init ();
}

static void
camel_lite_tcp_stream_ssl_init (gpointer object, gpointer klass)
{
	CamelTcpStreamSSL *stream = CAMEL_TCP_STREAM_SSL (object);

	stream->priv = g_new0 (struct _CamelTcpStreamSSLPrivate, 1);
	stream->priv->sockfd = -1;
}

static void
camel_lite_tcp_stream_ssl_finalize (CamelObject *object)
{
	CamelTcpStreamSSL *stream = CAMEL_TCP_STREAM_SSL (object);

	if (stream->priv->ssl) {
		SSL_shutdown (stream->priv->ssl);

		if (stream->priv->ssl->ctx) {
			SSL_CTX_free (stream->priv->ssl->ctx);
		}

		SSL_free (stream->priv->ssl);
	}

	if (stream->priv->sockfd != -1)
		close (stream->priv->sockfd);

	g_free (stream->priv->expected_host);

	g_free (stream->priv);
}


CamelType
camel_lite_tcp_stream_ssl_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register (camel_lite_tcp_stream_get_type (),
					    "CamelLiteTcpStreamSSL",
					    sizeof (CamelTcpStreamSSL),
					    sizeof (CamelTcpStreamSSLClass),
					    (CamelObjectClassInitFunc) camel_lite_tcp_stream_ssl_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_tcp_stream_ssl_init,
					    (CamelObjectFinalizeFunc) camel_lite_tcp_stream_ssl_finalize);
	}

	return type;
}



/**
 * camel_lite_tcp_stream_ssl_new:
 * @service: camel service
 * @expected_host: host that the stream is expecting to connect with.
 * @flags: flags
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a CamelService is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Return value: a ssl stream (in ssl mode)
 **/
CamelStream *
camel_lite_tcp_stream_ssl_new (CamelService *service, const char *expected_host, guint32 flags)
{
	CamelTcpStreamSSL *stream;

	stream = CAMEL_TCP_STREAM_SSL (camel_lite_object_new (camel_lite_tcp_stream_ssl_get_type ()));

	stream->priv->session = service->session;
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = TRUE;
	stream->priv->flags = flags;
	stream->priv->service = service;

	return CAMEL_STREAM (stream);
}


/**
 * camel_lite_tcp_stream_ssl_new_raw:
 * @service: camel service
 * @expected_host: host that the stream is expecting to connect with.
 * @flags: flags
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a CamelService is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Return value: a ssl-capable stream (in non ssl mode)
 **/
CamelStream *
camel_lite_tcp_stream_ssl_new_raw (CamelService *service, const char *expected_host, guint32 flags)
{
	CamelTcpStreamSSL *stream;

	stream = CAMEL_TCP_STREAM_SSL (camel_lite_object_new (camel_lite_tcp_stream_ssl_get_type ()));

	stream->priv->session = service->session;
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = FALSE;
	stream->priv->flags = flags;
	stream->priv->service = service;

	return CAMEL_STREAM (stream);
}


static int
ssl_errno (SSL *ssl, int ret)
{
	switch (SSL_get_error (ssl, ret)) {
	case SSL_ERROR_NONE:
		return 0;
	case SSL_ERROR_ZERO_RETURN:
		/* this one does not map well at all */
		d(printf ("ssl_errno: SSL_ERROR_ZERO_RETURN\n"));
		return EINVAL;
	case SSL_ERROR_WANT_READ:   /* non-fatal; retry */
	case SSL_ERROR_WANT_WRITE:  /* non-fatal; retry */
		d(printf ("ssl_errno: SSL_ERROR_WANT_[READ,WRITE]\n"));
		return EAGAIN;
	case SSL_ERROR_SYSCALL:
		d(printf ("ssl_errno: SSL_ERROR_SYSCALL\n"));
		return EINTR;
	case SSL_ERROR_SSL:
		d(printf ("ssl_errno: SSL_ERROR_SSL  <-- very useful error...riiiiight\n"));
		return EINTR;
	default:
		d(printf ("ssl_errno: default error\n"));
		return EINTR;
	}
}


/**
 * camel_lite_tcp_stream_ssl_enable_ssl:
 * @stream: ssl stream
 *
 * Toggles an ssl-capable stream into ssl mode (if it isn't already).
 *
 * Returns 0 on success or -1 on fail.
 **/
int
camel_lite_tcp_stream_ssl_enable_ssl (CamelTcpStreamSSL *stream)
{
	SSL *ssl;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM_SSL (stream), -1);

	if (stream->priv->sockfd != -1 && !stream->priv->ssl_mode) {
		ssl = open_ssl_connection (stream->priv->session, stream->priv->sockfd, stream);
		if (ssl == NULL) {
			stream->priv->sockfd = -1;
			return -1;
		}

		stream->priv->ssl = ssl;
	}

	stream->priv->ssl_mode = TRUE;

	return 0;
}

static int
stream_gettimeout (CamelTcpStream *stream)
{
	CamelTcpStreamSSL *openssl = CAMEL_TCP_STREAM_SSL (stream);
	SSL *ssl = openssl->priv->ssl;

	return SSL_get_default_timeout ((const SSL*) ssl);
}

static ssize_t
stream_read_nb (CamelTcpStream *stream, char *buffer, size_t n)
{
	CamelTcpStreamSSL *openssl = CAMEL_TCP_STREAM_SSL (stream);
	SSL *ssl = openssl->priv->ssl;
	ssize_t nread;

	int error, flags, fdmax;
	struct timeval timeout;
	fd_set rdset;
	int res;

	flags = fcntl (openssl->priv->sockfd, F_GETFL);
	fcntl (openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);

	fdmax = openssl->priv->sockfd + 1;

	do {
		FD_ZERO (&rdset);
		FD_SET (openssl->priv->sockfd, &rdset);
		nread = -1;
		timeout.tv_sec = NONBLOCKING_READ_TIMEOUT;
		timeout.tv_usec = 0;
		res = select (fdmax, &rdset, 0, 0, &timeout);

		if (res == -1)
			;
		else if (res == 0)
			errno = ETIMEDOUT;
		else {

		  do {
			if (ssl) {
				nread = SSL_read (ssl, buffer, n);
				if (nread < 0)
					errno = ssl_errno (ssl, nread);
			} else {
				nread = read (openssl->priv->sockfd, buffer, n);
			}
		  } while (0 && (nread < 0 && errno == EINTR));
		}
	} while (0 && (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)));

	error = errno;
	fcntl (openssl->priv->sockfd, F_SETFL, flags);
	errno = error;


	return nread;
}


static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelTcpStreamSSL *openssl = CAMEL_TCP_STREAM_SSL (stream);
	SSL *ssl = openssl->priv->ssl;
	ssize_t nread;
	int cancel_fd;

	if (camel_lite_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_lite_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {

		int error, flags, fdmax;
		struct timeval timeout;
		fd_set rdset;

		flags = fcntl (openssl->priv->sockfd, F_GETFL);
		fcntl (openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);

		fdmax = openssl->priv->sockfd + 1;

		do {
			FD_ZERO (&rdset);
			FD_SET (openssl->priv->sockfd, &rdset);

			timeout.tv_sec = 0;
			timeout.tv_usec = 0;

			select (fdmax, &rdset, 0, 0, &timeout);

			do {
				if (ssl) {
					nread = SSL_read (ssl, buffer, n);
					if (nread < 0)
						errno = ssl_errno (ssl, nread);
				} else {
					nread = read (openssl->priv->sockfd, buffer, n);
				}
			} while (nread < 0 && errno == EINTR);
		} while (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

		error = errno;
		fcntl (openssl->priv->sockfd, F_SETFL, flags);
		errno = error;

	} else {
		int error, flags, fdmax;
		struct timeval timeout;
		fd_set rdset;

		flags = fcntl (openssl->priv->sockfd, F_GETFL);
		fcntl (openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);

		fdmax = MAX (openssl->priv->sockfd, cancel_fd) + 1;

		do {
			FD_ZERO (&rdset);
			FD_SET (openssl->priv->sockfd, &rdset);
			FD_SET (cancel_fd, &rdset);

			timeout.tv_sec = 0;
			timeout.tv_usec = 0;

			select (fdmax, &rdset, 0, 0, &timeout);
			if (FD_ISSET (cancel_fd, &rdset)) {
				fcntl (openssl->priv->sockfd, F_SETFL, flags);
				errno = EINTR;
				return -1;
			}

			do {
				if (ssl) {
					nread = SSL_read (ssl, buffer, n);
					if (nread < 0)
						errno = ssl_errno (ssl, nread);
				} else {
					nread = read (openssl->priv->sockfd, buffer, n);
				}
			} while (nread < 0 && errno == EINTR);
		} while (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

		error = errno;
		fcntl (openssl->priv->sockfd, F_SETFL, flags);
		errno = error;
	}

	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelTcpStreamSSL *openssl = CAMEL_TCP_STREAM_SSL (stream);
	SSL *ssl = openssl->priv->ssl;
	ssize_t w, written = 0;
	int cancel_fd;

	if (camel_lite_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_lite_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {


		int error, flags, fdmax;
		struct timeval timeout;
		fd_set rdset, wrset;

		flags = fcntl (openssl->priv->sockfd, F_GETFL);
		fcntl (openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);

		fdmax = openssl->priv->sockfd + 1;
		do {
			/* Write in chunks of max WRITE_CHUNK_SIZE bytes */
			ssize_t actual = MIN (n - written, WRITE_CHUNK_SIZE);

			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (openssl->priv->sockfd, &wrset);

			timeout.tv_sec = BLOCKING_WRITE_TIMEOUT;
			timeout.tv_usec = 0;
			select (fdmax, &rdset, &wrset, 0, &timeout);


			do {
				if (ssl) {
					w = SSL_write (ssl, buffer + written, actual /* n - written */);
					if (w < 0)
						errno = ssl_errno (ssl, w);
				} else {
					w = write (openssl->priv->sockfd, buffer + written, actual /* n - written */);
				}
			} while (w < 0 && errno == EINTR);

			if (w < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					w = 0;
				} else {
					error = errno;
					fcntl (openssl->priv->sockfd, F_SETFL, flags);
					errno = error;
					return -1;
				}
			} else
				written += w;
		} while (w >= 0 && written < n);

		fcntl (openssl->priv->sockfd, F_SETFL, flags);

	} else {
		int error, flags, fdmax;
		struct timeval timeout;
		fd_set rdset, wrset;

		flags = fcntl (openssl->priv->sockfd, F_GETFL);
		fcntl (openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);

		fdmax = MAX (openssl->priv->sockfd, cancel_fd) + 1;
		do {
			/* Write in chunks of max WRITE_CHUNK_SIZE bytes */
			ssize_t actual = MIN (n - written, WRITE_CHUNK_SIZE);

			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (openssl->priv->sockfd, &wrset);
			FD_SET (cancel_fd, &rdset);

			timeout.tv_sec = BLOCKING_WRITE_TIMEOUT;
			timeout.tv_usec = 0;
			select (fdmax, &rdset, &wrset, 0, &timeout);
			if (FD_ISSET (cancel_fd, &rdset)) {
				fcntl (openssl->priv->sockfd, F_SETFL, flags);
				errno = EINTR;
				return -1;
			}

			do {
				if (ssl) {
					w = SSL_write (ssl, buffer + written, actual /* n - written */);
					if (w < 0)
						errno = ssl_errno (ssl, w);
				} else {
					w = write (openssl->priv->sockfd, buffer + written, actual /* n - written */);
				}
			} while (w < 0 && errno == EINTR);

			if (w < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					w = 0;
				} else {
					error = errno;
					fcntl (openssl->priv->sockfd, F_SETFL, flags);
					errno = error;
					return -1;
				}
			} else
				written += w;
		} while (w >= 0 && written < n);

		fcntl (openssl->priv->sockfd, F_SETFL, flags);
	}

	return written;
}

static int
stream_flush (CamelStream *stream)
{
	return 0;
}


static void
close_ssl_connection (SSL *ssl)
{
	if (ssl) {
		SSL_shutdown (ssl);

		if (ssl->ctx)
			SSL_CTX_free (ssl->ctx);

		SSL_free (ssl);
	}
}

static int
stream_close (CamelStream *stream)
{
	if( ((CamelTcpStreamSSL *)stream)->priv->ssl ){
		close_ssl_connection (((CamelTcpStreamSSL *)stream)->priv->ssl);
		((CamelTcpStreamSSL *)stream)->priv->ssl = NULL;
	}

	if( ((CamelTcpStreamSSL *)stream)->priv->sockfd != -1 ){
		if (close (((CamelTcpStreamSSL *)stream)->priv->sockfd) == -1)
			return -1;

		((CamelTcpStreamSSL *)stream)->priv->sockfd = -1;
	}

	return 0;
}

/* this is a 'cancellable' connect, cancellable from camel_lite_operation_cancel etc */
/* returns -1 & errno == EINTR if the connection was cancelled */
static int
socket_connect (struct addrinfo *host)
{
	struct sockaddr *saddr = host->ai_addr;
	struct timeval tv;
	int cancel_fd;
	int ret, fd;

	/* see if we're cancelled yet */
	if (camel_lite_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	fd = socket (host->ai_family, SOCK_STREAM, 0);

	cancel_fd = camel_lite_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		ret = connect (fd, saddr, sizeof (struct sockaddr));
		if (ret == -1) {
			close (fd);
			return -1;
		}

		return fd;
	} else {
		fd_set rdset, wrset;
		int flags, fdmax;

		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);

		ret = connect (fd, saddr, sizeof (struct sockaddr));
		if (ret == 0) {
			fcntl (fd, F_SETFL, flags);
			return fd;
		}

		if (errno != EINPROGRESS) {
			close (fd);
			return -1;
		}

		FD_ZERO (&rdset);
		FD_ZERO (&wrset);
		FD_SET (fd, &wrset);
		FD_SET (cancel_fd, &rdset);
		fdmax = MAX (fd, cancel_fd) + 1;
		tv.tv_usec = 0;
		tv.tv_sec = CONNECT_TIMEOUT;

		if (select (fdmax, &rdset, &wrset, 0, &tv) == 0) {
			close (fd);
			errno = ETIMEDOUT;
			return -1;
		}

		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &rdset)) {
			close (fd);
			errno = EINTR;
			return -1;
		} else {
			socklen_t len = sizeof (int);

			if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &len) == -1) {
				close (fd);
				return -1;
			}

			if (ret != 0) {
				close (fd);
				errno = ret;
				return -1;
			}
		}

		fcntl (fd, F_SETFL, flags);
	}

	return fd;
}

static const char *
x509_strerror (int err)
{
	switch (err) {
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
		return _("Unable to get issuer's certificate");
	case X509_V_ERR_UNABLE_TO_GET_CRL:
		return _("Unable to get Certificate Revocation List");
	case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
		return _("Unable to decrypt certificate signature");
	case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
		return _("Unable to decrypt Certificate Revocation List signature");
	case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
		return _("Unable to decode issuer's public key");
	case X509_V_ERR_CERT_SIGNATURE_FAILURE:
		return _("Certificate signature failure");
	case X509_V_ERR_CRL_SIGNATURE_FAILURE:
		return _("Certificate Revocation List signature failure");
	case X509_V_ERR_CERT_NOT_YET_VALID:
		return _("Certificate not yet valid");
	case X509_V_ERR_CERT_HAS_EXPIRED:
		return _("Certificate has expired");
	case X509_V_ERR_CRL_NOT_YET_VALID:
		return _("CRL not yet valid");
	case X509_V_ERR_CRL_HAS_EXPIRED:
		return _("CRL has expired");
	case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
	case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
		return _("Error in CRL");
	case X509_V_ERR_OUT_OF_MEM:
		return _("Out of memory");
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
		return _("Zero-depth self-signed certificate");
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
		return _("Self-signed certificate in chain");
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
		return _("Unable to get issuer's certificate locally");
	case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
		return _("Unable to verify leaf signature");
	case X509_V_ERR_CERT_CHAIN_TOO_LONG:
		return _("Certificate chain too long");
	case X509_V_ERR_CERT_REVOKED:
		return _("Certificate Revoked");
	case X509_V_ERR_INVALID_CA:
		return _("Invalid Certificate Authority (CA)");
	case X509_V_ERR_PATH_LENGTH_EXCEEDED:
		return _("Path length exceeded");
	case X509_V_ERR_INVALID_PURPOSE:
		return _("Invalid purpose");
	case X509_V_ERR_CERT_UNTRUSTED:
		return _("Certificate untrusted");
	case X509_V_ERR_CERT_REJECTED:
		return _("Certificate rejected");
		/* These are 'informational' when looking for issuer cert */
	case X509_V_ERR_SUBJECT_ISSUER_MISMATCH:
		return _("Subject/Issuer mismatch");
	case X509_V_ERR_AKID_SKID_MISMATCH:
		return _("AKID/SKID mismatch");
	case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH:
		return _("AKID/Issuer serial mismatch");
	case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
		return _("Key usage does not support certificate signing");
		/* The application is not happy */
	case X509_V_ERR_APPLICATION_VERIFICATION:
		return _("Error in application verification");
	default:
		return _("Unknown");
	}
}

static int
ssl_verify (int ok, X509_STORE_CTX *ctx)
{
	unsigned char md5sum[16], fingerprint[40], *f;
	CamelTcpStreamSSL *stream;
	CamelSession *session;
	CamelCertDB *certdb = NULL;
	CamelCert *ccert = NULL;
	char *prompt, *cert_str;
	int err, i;
	unsigned int md5len;
	char buf[257];
	X509 *cert;
	SSL *ssl;

	if (ok)
		return TRUE;

	ssl = X509_STORE_CTX_get_ex_data (ctx, SSL_get_ex_data_X509_STORE_CTX_idx ());

	stream = SSL_CTX_get_app_data (ssl->ctx);
	if (!stream)
		return FALSE;

	session = stream->priv->session;

	cert = X509_STORE_CTX_get_current_cert (ctx);
	err = X509_STORE_CTX_get_error (ctx);

	/* calculate the MD5 hash of the raw certificate */
	md5len = sizeof (md5sum);
	X509_digest (cert, EVP_md5 (), md5sum, &md5len);
	for (i = 0, f = fingerprint; i < 16; i++, f += 3)
		sprintf ((char *) f, "%.2x%c", md5sum[i], i != 15 ? ':' : '\0');

#define GET_STRING(name) X509_NAME_oneline (name, buf, 256)

	certdb = camel_lite_certdb_get_default ();
	if (certdb) {
		ccert = camel_lite_certdb_get_cert (certdb, (const char *) fingerprint);
		if (ccert) {
			if (ccert->trust != CAMEL_CERT_TRUST_UNKNOWN) {
				ok = ccert->trust != CAMEL_CERT_TRUST_NEVER;
				camel_lite_certdb_cert_unref (certdb, ccert);
				camel_lite_object_unref (certdb);

				return ok;
			}
		} else {
			/* create a new camel-cert */
			ccert = camel_lite_certdb_cert_new (certdb);
			camel_lite_cert_set_issuer (certdb, ccert, GET_STRING (X509_get_issuer_name (cert)));
			camel_lite_cert_set_subject (certdb, ccert, GET_STRING (X509_get_subject_name (cert)));
			camel_lite_cert_set_hostname (certdb, ccert, (const char *) stream->priv->expected_host);
			camel_lite_cert_set_fingerprint (certdb, ccert, (const char *) fingerprint);
			camel_lite_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);

			/* Add the certificate to our db */
			camel_lite_certdb_add (certdb, ccert);
		}
	}

	cert_str = g_strdup_printf (_("Issuer:            %s\n"
				      "Subject:           %s\n"
				      "Fingerprint:       %s\n"
				      "Signature:         %s"),
				    GET_STRING (X509_get_issuer_name (cert)),
				    GET_STRING (X509_get_subject_name (cert)),
				    fingerprint, cert->valid ? _("GOOD") : _("BAD"));

	prompt = g_strdup_printf (_("SSL Certificate check:\n\n%s\n\n%s\n"),
				    cert_str, x509_strerror (err));

	ok = camel_lite_session_alert_user_with_id (session, CAMEL_SESSION_ALERT_WARNING,
		CAMEL_EXCEPTION_SERVICE_CERTIFICATE, prompt, TRUE, stream->priv->service);
	g_free (prompt);

	if (ok && ccert) {
		camel_lite_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_FULLY);
		camel_lite_certdb_touch (certdb);
	}

	if (certdb) {
		camel_lite_certdb_cert_unref (certdb, ccert);
		camel_lite_object_unref (certdb);
	}

	return ok;
}

static SSL *
open_ssl_connection (CamelSession *session, int sockfd, CamelTcpStreamSSL *openssl)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int n;

	/* SSLv23_client_method will negotiate with SSL v2, v3, or TLS v1 */
	ssl_ctx = SSL_CTX_new (SSLv23_client_method ());
	g_return_val_if_fail (ssl_ctx != NULL, NULL);

	SSL_CTX_set_default_verify_paths (ssl_ctx);
	SSL_CTX_set_verify (ssl_ctx, SSL_VERIFY_PEER, &ssl_verify);
	ssl = SSL_new (ssl_ctx);
	SSL_set_fd (ssl, sockfd);

	SSL_CTX_set_app_data (ssl_ctx, openssl);

	n = SSL_connect (ssl);
	if (n != 1) {
		int errnosave = ssl_errno (ssl, n);

		SSL_shutdown (ssl);

		if (ssl->ctx)
			SSL_CTX_free (ssl->ctx);

		SSL_free (ssl);
		ssl = NULL;

		close (sockfd);

		errno = errnosave;
	}

	return ssl;
}

static int stream_connect_do (CamelTcpStream *stream, struct addrinfo *host)
{
	CamelTcpStreamSSL *openssl = CAMEL_TCP_STREAM_SSL (stream);
	SSL *ssl = NULL;
	int fd;

	g_return_val_if_fail (host != NULL, -1);

	fd = socket_connect (host);
	if (fd == -1)
		return -1;

	if (openssl->priv->ssl_mode) {
		ssl = open_ssl_connection (openssl->priv->session, fd, openssl);
		if (!ssl)
			return -1;
	}

	openssl->priv->sockfd = fd;
	openssl->priv->ssl = ssl;

	return 0;
}

static int
stream_connect (CamelTcpStream *stream, struct addrinfo *host)
{
	while (host) {
		if (stream_connect_do (stream, host) == 0)
			return 0;
		host = host->ai_next;
	}

	return -1;
}


static int
get_sockopt_level (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
	case CAMEL_SOCKOPT_NODELAY:
		return IPPROTO_TCP;
	default:
		return SOL_SOCKET;
	}
}

static int
get_sockopt_optname (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
		return TCP_MAXSEG;
	case CAMEL_SOCKOPT_NODELAY:
		return TCP_NODELAY;
	case CAMEL_SOCKOPT_BROADCAST:
		return SO_BROADCAST;
	case CAMEL_SOCKOPT_KEEPALIVE:
		return SO_KEEPALIVE;
	case CAMEL_SOCKOPT_LINGER:
		return SO_LINGER;
	case CAMEL_SOCKOPT_RECVBUFFERSIZE:
		return SO_RCVBUF;
	case CAMEL_SOCKOPT_SENDBUFFERSIZE:
		return SO_SNDBUF;
	case CAMEL_SOCKOPT_REUSEADDR:
		return SO_REUSEADDR;
	case CAMEL_SOCKOPT_IPTYPEOFSERVICE:
		return SO_TYPE;
	default:
		return -1;
	}
}

static int
stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	int optname, optlen;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
		int flags;

		flags = fcntl (((CamelTcpStreamSSL *) stream)->priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		data->value.non_blocking = flags & O_NONBLOCK ? TRUE : FALSE;

		return 0;
	}

	return getsockopt (((CamelTcpStreamSSL *) stream)->priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (void *) &data->value,
			   (socklen_t *) &optlen);
}

static int
stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	int optname;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
		int flags, set;

		flags = fcntl (((CamelTcpStreamSSL *) stream)->priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		set = data->value.non_blocking ? O_NONBLOCK : 0;
		flags = (flags & ~O_NONBLOCK) | set;

		if (fcntl (((CamelTcpStreamSSL *) stream)->priv->sockfd, F_SETFL, flags) == -1)
			return -1;

		return 0;
	}

	return setsockopt (((CamelTcpStreamSSL *) stream)->priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (void *) &data->value,
			   sizeof (data->value));
}

static struct sockaddr *
stream_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	struct sockaddr *saddr = (struct sockaddr *) (malloc (*len));

	if (getsockname (CAMEL_TCP_STREAM_SSL (stream)->priv->sockfd, saddr, len) == -1) {
		free (saddr);
		return NULL;
	}

	return saddr;
}

static struct sockaddr *
stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	struct sockaddr *saddr = (struct sockaddr *) (malloc (*len));

	if (getpeername (CAMEL_TCP_STREAM_SSL (stream)->priv->sockfd, saddr, len) == -1) {
		free (saddr);
		return NULL;
	}

	return saddr;
}

#endif /* HAVE_OPENSSL */
#endif /* HAVE_NSS */
