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


#ifndef __CAMEL_CERTDB_H__
#define __CAMEL_CERTDB_H__

#include <stdio.h>
#include <camel/camel-object.h>
#ifdef ENABLE_CST
#include <cst.h>
#endif

#define CAMEL_CERTDB_TYPE         (camel_lite_certdb_get_type ())
#define CAMEL_CERTDB(obj)         (CAMEL_CHECK_CAST (obj, camel_lite_certdb_get_type (), CamelCertDB))
#define CAMEL_CERTDB_CLASS(klass) (CAMEL_CHECK_CLASS_CAST (klass, camel_lite_certdb_get_type (), CamelCertDBClass))
#define CAMEL_IS_CERTDB(obj)      (CAMEL_CHECK_TYPE (obj, camel_lite_certdb_get_type ()))

G_BEGIN_DECLS

typedef struct _CamelCertDB CamelCertDB;
typedef struct _CamelCertDBClass CamelCertDBClass;

enum {
	CAMEL_CERTDB_DIRTY  = (1 << 0)
};

enum {
	CAMEL_CERT_STRING_ISSUER,
	CAMEL_CERT_STRING_SUBJECT,
	CAMEL_CERT_STRING_HOSTNAME,
	CAMEL_CERT_STRING_FINGERPRINT
};

typedef enum {
	CAMEL_CERT_TRUST_UNKNOWN,
	CAMEL_CERT_TRUST_NEVER,
	CAMEL_CERT_TRUST_MARGINAL,
	CAMEL_CERT_TRUST_FULLY,
	CAMEL_CERT_TRUST_ULTIMATE
} CamelCertTrust;

typedef struct {
	guint32 refcount;

	char *issuer;
	char *subject;
	char *hostname;
	char *fingerprint;

	CamelCertTrust trust;
#ifdef ENABLE_CST
	cst_t_seqnum certID;
#endif
	GByteArray *rawcert;
} CamelCert;

struct _CamelCertDB {
	CamelObject parent_object;
	struct _CamelCertDBPrivate *priv;

	char *filename;
	guint32 version;
	guint32 saved_certs;
	guint32 flags;

	guint32 cert_size;

	struct _EMemChunk *cert_chunks;

	GPtrArray *certs;
	GHashTable *cert_hash;
};

struct _CamelCertDBClass {
	CamelObjectClass parent_class;

	int (*header_load) (CamelCertDB *certdb, FILE *istream);
	int (*header_save) (CamelCertDB *certdb, FILE *ostream);

	CamelCert * (*cert_load) (CamelCertDB *certdb, FILE *istream);
	int (*cert_save) (CamelCertDB *certdb, CamelCert *cert, FILE *ostream);

	CamelCert *  (*cert_new) (CamelCertDB *certdb);
	void        (*cert_free) (CamelCertDB *certdb, CamelCert *cert);

	const char * (*cert_get_string) (CamelCertDB *certdb, CamelCert *cert, int string);
	void (*cert_set_string) (CamelCertDB *certdb, CamelCert *cert, int string, const char *value);
};


CamelType camel_lite_certdb_get_type (void);

CamelCertDB *camel_lite_certdb_new (void);

void camel_lite_certdb_set_default (CamelCertDB *certdb);
CamelCertDB *camel_lite_certdb_get_default (void);

void camel_lite_certdb_set_filename (CamelCertDB *certdb, const char *filename);

int camel_lite_certdb_load (CamelCertDB *certdb);
int camel_lite_certdb_save (CamelCertDB *certdb);

void camel_lite_certdb_touch (CamelCertDB *certdb);

CamelCert *camel_lite_certdb_get_cert (CamelCertDB *certdb, const char *fingerprint);

void camel_lite_certdb_add (CamelCertDB *certdb, CamelCert *cert);
void camel_lite_certdb_remove (CamelCertDB *certdb, CamelCert *cert);

CamelCert *camel_lite_certdb_cert_new (CamelCertDB *certdb);
void camel_lite_certdb_cert_ref (CamelCertDB *certdb, CamelCert *cert);
void camel_lite_certdb_cert_unref (CamelCertDB *certdb, CamelCert *cert);

void camel_lite_certdb_clear (CamelCertDB *certdb);


const char *camel_lite_cert_get_string (CamelCertDB *certdb, CamelCert *cert, int string);
void camel_lite_cert_set_string (CamelCertDB *certdb, CamelCert *cert, int string, const char *value);

#define camel_lite_cert_get_issuer(certdb,cert) camel_lite_cert_get_string (certdb, cert, CAMEL_CERT_STRING_ISSUER)
#define camel_lite_cert_get_subject(certdb,cert) camel_lite_cert_get_string (certdb, cert, CAMEL_CERT_STRING_SUBJECT)
#define camel_lite_cert_get_hostname(certdb,cert) camel_lite_cert_get_string (certdb, cert, CAMEL_CERT_STRING_HOSTNAME)
#define camel_lite_cert_get_fingerprint(certdb,cert) camel_lite_cert_get_string (certdb, cert, CAMEL_CERT_STRING_FINGERPRINT)

#define camel_lite_cert_set_issuer(certdb,cert,issuer) camel_lite_cert_set_string (certdb, cert, CAMEL_CERT_STRING_ISSUER, issuer)
#define camel_lite_cert_set_subject(certdb,cert,subject) camel_lite_cert_set_string (certdb, cert, CAMEL_CERT_STRING_SUBJECT, subject)
#define camel_lite_cert_set_hostname(certdb,cert,hostname) camel_lite_cert_set_string (certdb, cert, CAMEL_CERT_STRING_HOSTNAME, hostname)
#define camel_lite_cert_set_fingerprint(certdb,cert,fingerprint) camel_lite_cert_set_string (certdb, cert, CAMEL_CERT_STRING_FINGERPRINT, fingerprint)

CamelCertTrust camel_lite_cert_get_trust (CamelCertDB *certdb, CamelCert *cert);
void camel_lite_cert_set_trust (CamelCertDB *certdb, CamelCert *cert, CamelCertTrust trust);

G_END_DECLS

#endif /* __CAMEL_CERTDB_H__ */
