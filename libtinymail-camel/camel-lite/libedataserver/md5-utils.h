/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * md5_context structure, pass it to rpmMD5Init, call rpmMD5Update as
 * needed on buffers full of bytes, and then call rpmMD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

/* parts of this file are :
 * Written March 1993 by Branko Lankester
 * Modified June 1993 by Colin Plumb for altered md5.c.
 * Modified October 1995 by Erik Troan for RPM
 */


#ifndef MD5_UTILS_H
#define MD5_UTILS_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * md5_context:
 *
 * A buffer structure used for md5 calculation.
 **/
typedef struct _md5_context {
	/*< private >*/
	guint32 buf[4];
	guint32 bits[2];
	guchar in[64];
} md5_context;


void md5_get_digest (const gchar *buffer, gint buffer_size, guchar digest[16]);

/* use this one when speed is needed */
/* for use in provider code only */
void md5_get_digest_from_file (const gchar *filename, guchar digest[16]);

/* raw routines */
void md5_init (md5_context *ctx);
void md5_update (md5_context *ctx, const guchar *buf, guint32 len);
void md5_final (md5_context *ctx, guchar digest[16]);

G_END_DECLS

#endif	/* MD5_UTILS_H */
