/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000-2003 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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


#ifndef _CAMEL_MIME_UTILS_H
#define _CAMEL_MIME_UTILS_H

#include <time.h>
#include <glib.h>

/* maximum recommended size of a line from camel_lite_header_fold() */
#define CAMEL_FOLD_SIZE (77)
/* maximum hard size of a line from camel_lite_header_fold() */
#define CAMEL_FOLD_MAX_SIZE (998)

#define CAMEL_UUDECODE_STATE_INIT   (0)
#define CAMEL_UUDECODE_STATE_BEGIN  (1 << 16)
#define CAMEL_UUDECODE_STATE_END    (1 << 17)
#define CAMEL_UUDECODE_STATE_MASK   (CAMEL_UUDECODE_STATE_BEGIN | CAMEL_UUDECODE_STATE_END)

G_BEGIN_DECLS

/* note, if you change this, make sure you change the 'encodings' array in camel-mime-part.c */
typedef enum _CamelTransferEncoding {
	CAMEL_TRANSFER_ENCODING_DEFAULT,
	CAMEL_TRANSFER_ENCODING_7BIT,
	CAMEL_TRANSFER_ENCODING_8BIT,
	CAMEL_TRANSFER_ENCODING_BASE64,
	CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE,
	CAMEL_TRANSFER_ENCODING_BINARY,
	CAMEL_TRANSFER_ENCODING_UUENCODE,
	CAMEL_TRANSFER_NUM_ENCODINGS
} CamelTransferEncoding;

/* a list of references for this message */
struct _camel_lite_header_references {
	struct _camel_lite_header_references *next;
	char *id;
};

struct _camel_lite_header_param {
	struct _camel_lite_header_param *next;
	char *name;
	char *value;
};

/* describes a content-type */
typedef struct {
	char *type;
	char *subtype;
	struct _camel_lite_header_param *params;
	unsigned int refcount;
} CamelContentType;

/* a raw rfc822 header */
/* the value MUST be US-ASCII */
struct _camel_lite_header_raw {
	struct _camel_lite_header_raw *next;
	char *name;
	char *value;
	int offset;		/* in file, if known */
};

typedef struct _CamelContentDisposition {
	char *disposition;
	struct _camel_lite_header_param *params;
	unsigned int refcount;
} CamelContentDisposition;

typedef enum _camel_lite_header_address_t {
	CAMEL_HEADER_ADDRESS_NONE,	/* uninitialised */
	CAMEL_HEADER_ADDRESS_NAME,
	CAMEL_HEADER_ADDRESS_GROUP
} camel_lite_header_address_t;

struct _camel_lite_header_address {
	struct _camel_lite_header_address *next;
	camel_lite_header_address_t type;
	char *name;
	union {
		char *addr;
		struct _camel_lite_header_address *members;
	} v;
	unsigned int refcount;
};

struct _camel_lite_header_newsgroup {
	struct _camel_lite_header_newsgroup *next;

	char *newsgroup;
};

/* Address lists */
struct _camel_lite_header_address *camel_lite_header_address_new (void);
struct _camel_lite_header_address *camel_lite_header_address_new_name (const char *name, const char *addr);
struct _camel_lite_header_address *camel_lite_header_address_new_group (const char *name);
void camel_lite_header_address_ref (struct _camel_lite_header_address *addrlist);
void camel_lite_header_address_unref (struct _camel_lite_header_address *addrlist);
void camel_lite_header_address_set_name (struct _camel_lite_header_address *addrlist, const char *name);
void camel_lite_header_address_set_addr (struct _camel_lite_header_address *addrlist, const char *addr);
void camel_lite_header_address_set_members (struct _camel_lite_header_address *addrlist, struct _camel_lite_header_address *group);
void camel_lite_header_address_add_member (struct _camel_lite_header_address *addrlist, struct _camel_lite_header_address *member);
void camel_lite_header_address_list_append_list (struct _camel_lite_header_address **addrlistp, struct _camel_lite_header_address **addrs);
void camel_lite_header_address_list_append (struct _camel_lite_header_address **addrlistp, struct _camel_lite_header_address *addr);
void camel_lite_header_address_list_clear (struct _camel_lite_header_address **addrlistp);

struct _camel_lite_header_address *camel_lite_header_address_decode (const char *in, const char *charset);
struct _camel_lite_header_address *camel_lite_header_mailbox_decode (const char *in, const char *charset);
/* for mailing */
char *camel_lite_header_address_list_encode (struct _camel_lite_header_address *addrlist);
/* for display */
char *camel_lite_header_address_list_format (struct _camel_lite_header_address *addrlist);

/* structured header prameters */
struct _camel_lite_header_param *camel_lite_header_param_list_decode (const char *in, const char *default_encoding);
char *camel_lite_header_param (struct _camel_lite_header_param *params, const char *name);
struct _camel_lite_header_param *camel_lite_header_set_param (struct _camel_lite_header_param **paramsp, const char *name, const char *value);
void camel_lite_header_param_list_format_append (GString *out, struct _camel_lite_header_param *params);
char *camel_lite_header_param_list_format (struct _camel_lite_header_param *params);
void camel_lite_header_param_list_free (struct _camel_lite_header_param *params);

/* Content-Type header */
CamelContentType *camel_lite_content_type_new (const char *type, const char *subtype);
CamelContentType *camel_lite_content_type_decode (const char *in);
void camel_lite_content_type_unref (CamelContentType *content_type);
void camel_lite_content_type_ref (CamelContentType *content_type);
const char *camel_lite_content_type_param (CamelContentType *content_type, const char *name);
void camel_lite_content_type_set_param (CamelContentType *content_type, const char *name, const char *value);
int camel_lite_content_type_is (CamelContentType *content_type, const char *type, const char *subtype);
char *camel_lite_content_type_format (CamelContentType *content_type);
char *camel_lite_content_type_simple (CamelContentType *content_type);

/* DEBUGGING function */
void camel_lite_content_type_dump (CamelContentType *content_type);

/* Content-Disposition header */
CamelContentDisposition *camel_lite_content_disposition_decode (const char *in, const char *default_encoding);
void camel_lite_content_disposition_ref (CamelContentDisposition *disposition);
void camel_lite_content_disposition_unref (CamelContentDisposition *disposition);
char *camel_lite_content_disposition_format (CamelContentDisposition *disposition);

/* decode the contents of a content-encoding header */
char *camel_lite_content_transfer_encoding_decode (const char *in);

/* raw headers */
void camel_lite_header_raw_append (struct _camel_lite_header_raw **list, const char *name, const char *value, int offset);
void camel_lite_header_raw_append_parse (struct _camel_lite_header_raw **list, const char *header, int offset);
const char *camel_lite_header_raw_find (struct _camel_lite_header_raw **list, const char *name, int *offset);
const char *camel_lite_header_raw_find_next (struct _camel_lite_header_raw **list, const char *name, int *offset, const char *last);
void camel_lite_header_raw_replace (struct _camel_lite_header_raw **list, const char *name, const char *value, int offset);
void camel_lite_header_raw_remove (struct _camel_lite_header_raw **list, const char *name);
void camel_lite_header_raw_fold (struct _camel_lite_header_raw **list);
void camel_lite_header_raw_clear (struct _camel_lite_header_raw **list);

char *camel_lite_header_raw_check_mailing_list (struct _camel_lite_header_raw **list);

/* fold a header */
char *camel_lite_header_address_fold (const char *in, size_t headerlen);
char *camel_lite_header_fold (const char *in, size_t headerlen);
char *camel_lite_header_unfold (const char *in);

/* decode a header which is a simple token */
char *camel_lite_header_token_decode (const char *in);

int camel_lite_header_decode_int (const char **in);

/* decode/encode a string type, like a subject line */
char *camel_lite_header_decode_string (const char *in, const char *default_charset);
char *camel_lite_header_encode_string (const unsigned char *in);

/* decode (text | comment) - a one-way op */
char *camel_lite_header_format_ctext (const char *in, const char *default_charset);

/* encode a phrase, like the real name of an address */
char *camel_lite_header_encode_phrase (const unsigned char *in);

/* FIXME: these are the only 2 functions in this header which are ch_(action)_type
   rather than ch_type_(action) */

/* decode an email date field into a GMT time, + optional offset */
time_t camel_lite_header_decode_date (const char *in, int *saveoffset);
char *camel_lite_header_format_date (time_t time, int offset);

/* decode a message id */
char *camel_lite_header_msgid_decode (const char *in);
char *camel_lite_header_contentid_decode (const char *in);

/* generate msg id */
char *camel_lite_header_msgid_generate (void);

/* decode a References or In-Reply-To header */
struct _camel_lite_header_references *camel_lite_header_references_inreplyto_decode (const char *in);
struct _camel_lite_header_references *camel_lite_header_references_decode (const char *in);
void camel_lite_header_references_list_clear (struct _camel_lite_header_references **list);
void camel_lite_header_references_list_append_asis (struct _camel_lite_header_references **list, char *ref);
int camel_lite_header_references_list_size (struct _camel_lite_header_references **list);
struct _camel_lite_header_references *camel_lite_header_references_dup (const struct _camel_lite_header_references *list);

/* decode content-location */
char *camel_lite_header_location_decode (const char *in);

/* nntp stuff */
struct _camel_lite_header_newsgroup *camel_lite_header_newsgroups_decode(const char *in);
void camel_lite_header_newsgroups_free(struct _camel_lite_header_newsgroup *ng);

const char *camel_lite_transfer_encoding_to_string (CamelTransferEncoding encoding);
CamelTransferEncoding camel_lite_transfer_encoding_from_string (const char *string);

/* decode the mime-type header */
void camel_lite_header_mime_decode (const char *in, int *maj, int *min);

#ifndef CAMEL_DISABLE_DEPRECATED
/* do incremental base64/quoted-printable (de/en)coding */
size_t camel_lite_base64_decode_step (unsigned char *in, size_t len, unsigned char *out, int *state, unsigned int *save);

size_t camel_lite_base64_encode_step (unsigned char *in, size_t len, gboolean break_lines, unsigned char *out, int *state, int *save);
size_t camel_lite_base64_encode_close (unsigned char *in, size_t len, gboolean break_lines, unsigned char *out, int *state, int *save);
#endif

size_t camel_lite_uudecode_step (unsigned char *in, size_t len, unsigned char *out, int *state, guint32 *save);

size_t camel_lite_uuencode_step (unsigned char *in, size_t len, unsigned char *out, unsigned char *uubuf, int *state,
		      guint32 *save);
size_t camel_lite_uuencode_close (unsigned char *in, size_t len, unsigned char *out, unsigned char *uubuf, int *state,
		       guint32 *save);

size_t camel_lite_quoted_decode_step (unsigned char *in, size_t len, unsigned char *out, int *savestate, int *saveme);

size_t camel_lite_quoted_encode_step (unsigned char *in, size_t len, unsigned char *out, int *state, int *save);
size_t camel_lite_quoted_encode_close (unsigned char *in, size_t len, unsigned char *out, int *state, int *save);

#ifndef CAMEL_DISABLE_DEPRECATED
char *camel_lite_base64_encode_simple (const char *data, size_t len);
size_t camel_lite_base64_decode_simple (char *data, size_t len);
#endif

/* camel ctype type functions for rfc822/rfc2047/other, which are non-locale specific */
enum {
	CAMEL_MIME_IS_CTRL		= 1<<0,
	CAMEL_MIME_IS_LWSP		= 1<<1,
	CAMEL_MIME_IS_TSPECIAL	= 1<<2,
	CAMEL_MIME_IS_SPECIAL	= 1<<3,
	CAMEL_MIME_IS_SPACE	= 1<<4,
	CAMEL_MIME_IS_DSPECIAL	= 1<<5,
	CAMEL_MIME_IS_QPSAFE	= 1<<6,
	CAMEL_MIME_IS_ESAFE	= 1<<7,	/* encoded word safe */
	CAMEL_MIME_IS_PSAFE	= 1<<8,	/* encoded word in phrase safe */
	CAMEL_MIME_IS_ATTRCHAR  = 1<<9	/* attribute-char safe (rfc2184) */
};

extern unsigned short camel_lite_mime_special_table[256];

#define camel_lite_mime_is_ctrl(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_CTRL) != 0)
#define camel_lite_mime_is_lwsp(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_LWSP) != 0)
#define camel_lite_mime_is_tspecial(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_TSPECIAL) != 0)
#define camel_lite_mime_is_type(x, t) ((camel_lite_mime_special_table[(unsigned char)(x)] & (t)) != 0)
#define camel_lite_mime_is_ttoken(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & (CAMEL_MIME_IS_TSPECIAL|CAMEL_MIME_IS_LWSP|CAMEL_MIME_IS_CTRL)) == 0)
#define camel_lite_mime_is_atom(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & (CAMEL_MIME_IS_SPECIAL|CAMEL_MIME_IS_SPACE|CAMEL_MIME_IS_CTRL)) == 0)
#define camel_lite_mime_is_dtext(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_DSPECIAL) == 0)
#define camel_lite_mime_is_fieldname(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & (CAMEL_MIME_IS_CTRL|CAMEL_MIME_IS_SPACE)) == 0)
#define camel_lite_mime_is_qpsafe(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_QPSAFE) != 0)
#define camel_lite_mime_is_especial(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_ESPECIAL) != 0)
#define camel_lite_mime_is_psafe(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_PSAFE) != 0)
#define camel_lite_mime_is_attrchar(x) ((camel_lite_mime_special_table[(unsigned char)(x)] & CAMEL_MIME_IS_ATTRCHAR) != 0)

G_END_DECLS

#endif /* ! _CAMEL_MIME_UTILS_H */
