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

#ifndef CAMEL_CIPHER_CONTEXT_H
#define CAMEL_CIPHER_CONTEXT_H

#include <camel/camel-session.h>
#include <camel/camel-exception.h>

/* FIXME: camelise */
#include <libedataserver/e-lite-msgport.h>

#define CAMEL_CIPHER_CONTEXT_TYPE     (camel_lite_cipher_context_get_type ())
#define CAMEL_CIPHER_CONTEXT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_CIPHER_CONTEXT_TYPE, CamelCipherContext))
#define CAMEL_CIPHER_CONTEXT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_CIPHER_CONTEXT_TYPE, CamelCipherContextClass))
#define CAMEL_IS_CIPHER_CONTEXT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_CIPHER_CONTEXT_TYPE))

G_BEGIN_DECLS

struct _CamelStream;
struct _CamelMimePart;

typedef struct _CamelCipherValidity CamelCipherValidity;
typedef struct _CamelCipherCertInfo CamelCipherCertInfo;

typedef enum {
	CAMEL_CIPHER_HASH_DEFAULT,
	CAMEL_CIPHER_HASH_MD2,
	CAMEL_CIPHER_HASH_MD5,
	CAMEL_CIPHER_HASH_SHA1,
	CAMEL_CIPHER_HASH_RIPEMD160,
	CAMEL_CIPHER_HASH_TIGER192,
	CAMEL_CIPHER_HASH_HAVAL5160
} CamelCipherHash;

typedef enum _camel_lite_cipher_validity_sign_t {
	CAMEL_CIPHER_VALIDITY_SIGN_NONE,
	CAMEL_CIPHER_VALIDITY_SIGN_GOOD,
	CAMEL_CIPHER_VALIDITY_SIGN_BAD,
	CAMEL_CIPHER_VALIDITY_SIGN_UNKNOWN,
	CAMEL_CIPHER_VALIDITY_SIGN_NEED_PUBLIC_KEY
} camel_lite_cipher_validity_sign_t;

typedef enum _camel_lite_cipher_validity_encrypt_t {
	CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE,
	CAMEL_CIPHER_VALIDITY_ENCRYPT_WEAK,
	CAMEL_CIPHER_VALIDITY_ENCRYPT_ENCRYPTED, /* encrypted, unknown strenght */
	CAMEL_CIPHER_VALIDITY_ENCRYPT_STRONG
} camel_lite_cipher_validity_encrypt_t;

typedef enum _camel_lite_cipher_validity_mode_t {
	CAMEL_CIPHER_VALIDITY_SIGN,
	CAMEL_CIPHER_VALIDITY_ENCRYPT
} camel_lite_cipher_validity_mode_t;

struct _CamelCipherCertInfo {
	struct _CamelCipherCertInfo *next;
	struct _CamelCipherCertInfo *prev;

	char *name;		/* common name */
	char *email;
};

struct _CamelCipherValidity {
	struct _CamelCipherValidity *next;
	struct _CamelCipherValidity *prev;
	EDList children;

	struct {
		enum _camel_lite_cipher_validity_sign_t status;
		char *description;
		EDList signers;	/* CamelCipherCertInfo's */
	} sign;
	struct {
		enum _camel_lite_cipher_validity_encrypt_t status;
		char *description;
		EDList encrypters;	/* CamelCipherCertInfo's */
	} encrypt;
};

typedef struct _CamelCipherContext {
	CamelObject parent_object;

	struct _CamelCipherContextPrivate *priv;

	CamelSession *session;

	/* these MUST be set by implementors */
	const char *sign_protocol;
	const char *encrypt_protocol;
	const char *key_protocol;
} CamelCipherContext;

typedef struct _CamelCipherContextClass {
	CamelObjectClass parent_class;

	CamelCipherHash	      (*id_to_hash)(CamelCipherContext *context, const char *id);
	const char *	      (*hash_to_id)(CamelCipherContext *context, CamelCipherHash hash);

	int                   (*sign)      (CamelCipherContext *context, const char *userid, CamelCipherHash hash,
					    struct _CamelMimePart *ipart, struct _CamelMimePart *opart, CamelException *ex);

	CamelCipherValidity * (*verify)    (CamelCipherContext *context, struct _CamelMimePart *ipart, CamelException *ex);

	int                   (*encrypt)   (CamelCipherContext *context, const char *userid,
					    GPtrArray *recipients, struct _CamelMimePart *ipart, struct _CamelMimePart *opart,
					    CamelException *ex);

	CamelCipherValidity  *(*decrypt)  (CamelCipherContext *context, struct _CamelMimePart *ipart, struct _CamelMimePart *opart,
					   CamelException *ex);

	int                   (*import_keys) (CamelCipherContext *context, struct _CamelStream *istream,
					      CamelException *ex);

	int                   (*export_keys) (CamelCipherContext *context, GPtrArray *keys,
					      struct _CamelStream *ostream, CamelException *ex);
} CamelCipherContextClass;

CamelType            camel_lite_cipher_context_get_type (void);

CamelCipherContext  *camel_lite_cipher_context_new (CamelSession *session);

void                 camel_lite_cipher_context_construct (CamelCipherContext *context, CamelSession *session);

/* cipher context util routines */
CamelCipherHash	     camel_lite_cipher_id_to_hash (CamelCipherContext *context, const char *id);
const char *	     camel_lite_cipher_hash_to_id (CamelCipherContext *context, CamelCipherHash hash);

/* FIXME:
   There are some inconsistencies here, the api's should probably handle CamelMimePart's as input/outputs,
   Something that might generate a multipart/signed should do it as part of that processing, internally
   to the cipher, etc etc. */

/* cipher routines */
int                  camel_lite_cipher_sign (CamelCipherContext *context, const char *userid, CamelCipherHash hash,
					struct _CamelMimePart *ipart, struct _CamelMimePart *opart, CamelException *ex);
CamelCipherValidity *camel_lite_cipher_verify (CamelCipherContext *context, struct _CamelMimePart *ipart, CamelException *ex);
int                  camel_lite_cipher_encrypt (CamelCipherContext *context, const char *userid,
					   GPtrArray *recipients, struct _CamelMimePart *ipart, struct _CamelMimePart *opart,
					   CamelException *ex);
CamelCipherValidity *camel_lite_cipher_decrypt (CamelCipherContext *context, struct _CamelMimePart *ipart, struct _CamelMimePart *opart,
					   CamelException *ex);

/* key/certificate routines */
int                  camel_lite_cipher_import_keys (CamelCipherContext *context, struct _CamelStream *istream,
					       CamelException *ex);
int                  camel_lite_cipher_export_keys (CamelCipherContext *context, GPtrArray *keys,
					       struct _CamelStream *ostream, CamelException *ex);

/* CamelCipherValidity utility functions */
CamelCipherValidity *camel_lite_cipher_validity_new (void);
void                 camel_lite_cipher_validity_init (CamelCipherValidity *validity);
gboolean             camel_lite_cipher_validity_get_valid (CamelCipherValidity *validity);
void                 camel_lite_cipher_validity_set_valid (CamelCipherValidity *validity, gboolean valid);
char                *camel_lite_cipher_validity_get_description (CamelCipherValidity *validity);
void                 camel_lite_cipher_validity_set_description (CamelCipherValidity *validity, const char *description);
void                 camel_lite_cipher_validity_clear (CamelCipherValidity *validity);
CamelCipherValidity *camel_lite_cipher_validity_clone(CamelCipherValidity *vin);
void		     camel_lite_cipher_validity_add_certinfo(CamelCipherValidity *vin, camel_lite_cipher_validity_mode_t mode, const char *name, const char *email);
void		     camel_lite_cipher_validity_envelope(CamelCipherValidity *valid, CamelCipherValidity *outer);
void                 camel_lite_cipher_validity_free (CamelCipherValidity *validity);

/* utility functions */
int		     camel_lite_cipher_canonical_to_stream(CamelMimePart *part, guint32 flags, CamelStream *ostream);

G_END_DECLS

#endif /* CAMEL_CIPHER_CONTEXT_H */
