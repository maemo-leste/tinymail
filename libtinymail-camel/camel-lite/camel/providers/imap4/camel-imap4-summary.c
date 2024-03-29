/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2007 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/md5-utils.h>

#include "camel-file-utils.h"
#include "camel-string-utils.h"
#include "camel-offline-journal.h"

#include "camel-imap4-command.h"
#include "camel-imap4-engine.h"
#include "camel-imap4-folder.h"
#include "camel-imap4-journal.h"
#include "camel-imap4-store.h"
#include "camel-imap4-stream.h"
#include "camel-imap4-summary.h"
#include "camel-imap4-utils.h"

#define d(x) x

#define CAMEL_IMAP4_SUMMARY_VERSION  3

static void camel_lite_imap4_summary_class_init (CamelIMAP4SummaryClass *klass);
static void camel_lite_imap4_summary_init (CamelIMAP4Summary *summary, CamelIMAP4SummaryClass *klass);
static void camel_lite_imap4_summary_finalize (CamelObject *object);

static int imap4_header_load (CamelFolderSummary *summary);
static int imap4_header_save (CamelFolderSummary *summary, FILE *fout);
static CamelMessageInfo *imap4_message_info_new_from_header (CamelFolderSummary *summary, struct _camel_lite_header_raw *header);
static CamelMessageInfo *imap4_message_info_load (CamelFolderSummary *summary);
static int imap4_message_info_save (CamelFolderSummary *summary, FILE *fout, CamelMessageInfo *info);
static CamelMessageInfo *imap4_message_info_clone (CamelFolderSummary *summary, const CamelMessageInfo *mi);
static CamelMessageContentInfo *imap4_content_info_load (CamelFolderSummary *summary);
static int imap4_content_info_save (CamelFolderSummary *summary, FILE *out, CamelMessageContentInfo *info);

static CamelFolderSummaryClass *parent_class = NULL;


CamelType
camel_lite_imap4_summary_get_type (void)
{
	static CamelType type = 0;

	if (!type) {
		type = camel_lite_type_register (CAMEL_FOLDER_SUMMARY_TYPE,
					    "CamelLiteIMAP4Summary",
					    sizeof (CamelIMAP4Summary),
					    sizeof (CamelIMAP4SummaryClass),
					    (CamelObjectClassInitFunc) camel_lite_imap4_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_lite_imap4_summary_init,
					    (CamelObjectFinalizeFunc) camel_lite_imap4_summary_finalize);
	}

	return type;
}


static void
camel_lite_imap4_summary_class_init (CamelIMAP4SummaryClass *klass)
{
	CamelFolderSummaryClass *summary_class = (CamelFolderSummaryClass *) klass;

	parent_class = (CamelFolderSummaryClass *) camel_lite_type_get_global_classfuncs (camel_lite_folder_summary_get_type ());

	summary_class->summary_header_load = imap4_header_load;
	summary_class->summary_header_save = imap4_header_save;
	summary_class->message_info_new_from_header = imap4_message_info_new_from_header;
	summary_class->message_info_load = imap4_message_info_load;
	summary_class->message_info_save = imap4_message_info_save;
	summary_class->message_info_clone = imap4_message_info_clone;
	summary_class->content_info_load = imap4_content_info_load;
	summary_class->content_info_save = imap4_content_info_save;
}

static void
camel_lite_imap4_summary_init (CamelIMAP4Summary *summary, CamelIMAP4SummaryClass *klass)
{
	CamelFolderSummary *folder_summary = (CamelFolderSummary *) summary;

	folder_summary->flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder_summary->message_info_size = sizeof (CamelIMAP4MessageInfo);
	folder_summary->content_info_size = sizeof (CamelIMAP4MessageContentInfo);

	((CamelFolderSummary *) summary)->flags |= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;

	summary->update_flags = TRUE;
	summary->uidvalidity_changed = FALSE;
}

static void
camel_lite_imap4_summary_finalize (CamelObject *object)
{
}


CamelFolderSummary *
camel_lite_imap4_summary_new (CamelFolder *folder)
{
	CamelFolderSummary *summary;

	summary = (CamelFolderSummary *) camel_lite_object_new (CAMEL_TYPE_IMAP4_SUMMARY);
	summary->folder = folder;

	return summary;
}

static int
imap4_header_load (CamelFolderSummary *summary)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_load (summary) == -1)
		return -1;

	imap4_summary->version = g_ntohl(get_unaligned_u32(summary->filepos));
	summary->filepos += 4;

	if (imap4_summary->version > CAMEL_IMAP4_SUMMARY_VERSION) {
		g_warning ("Unknown IMAP4 summary version\n");
		errno = EINVAL;
		return -1;
	}

	if (imap4_summary->version == 2) {
		/* check that we have Mailing-List info */
		int have_mlist;

		have_mlist = g_ntohl(get_unaligned_u32(summary->filepos));
		summary->filepos += 4;

		if (have_mlist)
			summary->flags |= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;
		else
			summary->flags ^= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;
	}

	imap4_summary->uidvalidity = g_ntohl(get_unaligned_u32(summary->filepos));
	summary->filepos += 4;

	return 0;
}

static int
imap4_header_save (CamelFolderSummary *summary, FILE *fout)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_save (summary, fout) == -1)
		return -1;

	if (camel_lite_file_util_encode_fixed_int32 (fout, CAMEL_IMAP4_SUMMARY_VERSION) == -1)
		return -1;

	if (camel_lite_file_util_encode_fixed_int32 (fout, imap4_summary->uidvalidity) == -1)
		return -1;

	return 0;
}

static int
envelope_decode_address (CamelIMAP4Engine *engine, GString *addrs, CamelException *ex)
{
	char *addr, *name = NULL, *user = NULL;
	struct _camel_lite_header_address *cia;
	unsigned char *literal = NULL;
	camel_lite_imap4_token_t token;
	const char *domain = NULL;
	int part = 0;
	size_t n;

	if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
		return -1;

	if (token.token == CAMEL_IMAP4_TOKEN_NIL) {
		return 0;
	} else if (token.token != '(') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
		return -1;
	}

	if (addrs->len > 0)
		g_string_append (addrs, ", ");

	do {
		if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
			goto exception;

		literal = NULL;
		switch (token.token) {
		case CAMEL_IMAP4_TOKEN_NIL:
			break;
		case CAMEL_IMAP4_TOKEN_ATOM:
		case CAMEL_IMAP4_TOKEN_QSTRING:
			switch (part) {
			case 0:
				name = camel_lite_header_decode_string (token.v.qstring, NULL);
				break;
			case 2:
				user = g_strdup (token.v.qstring);
				break;
			case 3:
				domain = token.v.qstring;
				break;
			}
			break;
		case CAMEL_IMAP4_TOKEN_LITERAL:
			if (camel_lite_imap4_engine_literal (engine, &literal, &n, ex) == -1)
				goto exception;

			switch (part) {
			case 0:
				name = camel_lite_header_decode_string (literal, NULL);
				g_free (literal);
				break;
			case 2:
				user = literal;
				break;
			case 3:
				domain = literal;
				break;
			}
			break;
		default:
			camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
			goto exception;
		}

		part++;
	} while (part < 4);

	addr = g_strdup_printf ("%s@%s", user, domain);
	g_free (literal);
	g_free (user);

	cia = camel_lite_header_address_new_name (name, addr);
	g_free (name);
	g_free (addr);

	addr = camel_lite_header_address_list_format (cia);
	camel_lite_header_address_unref (cia);

	g_string_append (addrs, addr);
	g_free (addr);

	if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
		return -1;

	if (token.token != ')') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
		return -1;
	}

	return 0;

 exception:

	g_free (name);
	g_free (user);

	return -1;
}

static int
envelope_decode_addresses (CamelIMAP4Engine *engine, char **addrlist, CamelException *ex)
{
	camel_lite_imap4_token_t token;
	GString *addrs;

	if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
		return -1;

	if (token.token == CAMEL_IMAP4_TOKEN_NIL) {
		*addrlist = NULL;
		return 0;
	} else if (token.token != '(') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
		return -1;
	}

	addrs = g_string_new ("");

	do {
		if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1) {
			g_string_free (addrs, TRUE);
			return -1;
		}

		if (token.token == '(') {
			camel_lite_imap4_stream_unget_token (engine->istream, &token);

			if (envelope_decode_address (engine, addrs, ex) == -1) {
				g_string_free (addrs, TRUE);
				return -1;
			}
		} else if (token.token == ')') {
			break;
		} else {
			camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
			return -1;
		}
	} while (1);

	*addrlist = addrs->str;
	g_string_free (addrs, FALSE);

	return 0;
}

static int
envelope_decode_date (CamelIMAP4Engine *engine, time_t *date, CamelException *ex)
{
	unsigned char *literal = NULL;
	camel_lite_imap4_token_t token;
	const char *nstring;
	size_t n;

	if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
		return -1;

	switch (token.token) {
	case CAMEL_IMAP4_TOKEN_NIL:
		*date = (time_t) -1;
		return 0;
	case CAMEL_IMAP4_TOKEN_ATOM:
		nstring = token.v.atom;
		break;
	case CAMEL_IMAP4_TOKEN_QSTRING:
		nstring = token.v.qstring;
		break;
	case CAMEL_IMAP4_TOKEN_LITERAL:
		if (camel_lite_imap4_engine_literal (engine, &literal, &n, ex) == -1)
			return -1;

		nstring = literal;
		break;
	default:
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
		return -1;
	}

	*date = camel_lite_header_decode_date (nstring, NULL);

	g_free (literal);

	return 0;
}

static int
envelope_decode_nstring (CamelIMAP4Engine *engine, char **nstring, gboolean rfc2047, CamelException *ex)
{
	camel_lite_imap4_token_t token;
	unsigned char *literal;
	size_t n;

	if (camel_lite_imap4_engine_next_token (engine, &token, ex) == -1)
		return -1;

	switch (token.token) {
	case CAMEL_IMAP4_TOKEN_NIL:
		*nstring = NULL;
		break;
	case CAMEL_IMAP4_TOKEN_ATOM:
		if (rfc2047)
			*nstring = camel_lite_header_decode_string (token.v.atom, NULL);
		else
			*nstring = g_strdup (token.v.atom);
		break;
	case CAMEL_IMAP4_TOKEN_QSTRING:
		if (rfc2047)
			*nstring = camel_lite_header_decode_string (token.v.qstring, NULL);
		else
			*nstring = g_strdup (token.v.qstring);
		break;
	case CAMEL_IMAP4_TOKEN_LITERAL:
		if (camel_lite_imap4_engine_literal (engine, &literal, &n, ex) == -1)
			return -1;

		if (rfc2047) {
			*nstring = camel_lite_header_decode_string (literal, NULL);
			g_free (literal);
		} else
			*nstring = literal;

		break;
	default:
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, &token);
		return -1;
	}

	return 0;
}

static CamelSummaryReferences *
decode_references (const char *refstr, const char *irtstr)
{
	struct _camel_lite_header_references *refs, *irt, *r;
	CamelSummaryReferences *references;
	unsigned char md5sum[16];
	guint32 i, n;

	refs = camel_lite_header_references_decode (refstr);
	irt = camel_lite_header_references_inreplyto_decode (irtstr);

	if (!refs && !irt)
		return NULL;

	if (irt) {
		/* The References field is populated from the `References' and/or `In-Reply-To'
		   headers. If both headers exist, take the first thing in the In-Reply-To header
		   that looks like a Message-ID, and append it to the References header. */

		if (refs) {
			r = irt;
			while (r->next != NULL)
				r = r->next;
			r->next = refs;
		}

		refs = irt;
	}

	n = camel_lite_header_references_list_size (&refs);
	references = g_malloc (sizeof (CamelSummaryReferences) + (sizeof (CamelSummaryMessageID) * (n - 1)));
	references->size = n;

	for (i = 0, r = refs; r != NULL; i++, r = r->next) {
		md5_get_digest (r->id, strlen (r->id), md5sum);
		memcpy (references->references[i].id.hash, md5sum, sizeof (CamelSummaryMessageID));
	}

	camel_lite_header_references_list_clear (&refs);

	return references;
}

static int
decode_envelope (CamelIMAP4Engine *engine, CamelMessageInfo *info, camel_lite_imap4_token_t *token, CamelException *ex)
{
	CamelIMAP4MessageInfo *iinfo = (CamelIMAP4MessageInfo *) info;
	unsigned char md5sum[16];
	char *nstring, *msgid;

	if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
		return -1;

	if (token->token != '(') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, token);
		return -1;
	}

	if (envelope_decode_date (engine, &iinfo->info.date_sent, ex) == -1)
		goto exception;

	/* subject */
	if (envelope_decode_nstring (engine, &nstring, TRUE, ex) == -1)
		goto exception;

	iinfo->info.needs_free = TRUE;

	iinfo->info.subject = camel_lite_pstring_strdup (nstring);
	g_free(nstring);

	/* from */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	iinfo->info.from = camel_lite_pstring_strdup (nstring);
	g_free(nstring);

	/* sender */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	g_free (nstring);

	/* reply-to */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	g_free (nstring);

	/* to */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	iinfo->info.to = camel_lite_pstring_strdup (nstring);
	g_free(nstring);

	/* cc */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	iinfo->info.cc = camel_lite_pstring_strdup (nstring);
	g_free(nstring);

	/* bcc */
	if (envelope_decode_addresses (engine, &nstring, ex) == -1)
		goto exception;
	g_free (nstring);

	/* in-reply-to */
	if (envelope_decode_nstring (engine, &nstring, FALSE, ex) == -1)
		goto exception;

	if (nstring != NULL) {
		if (!iinfo->info.references)
			iinfo->info.references = decode_references (NULL, nstring);

		g_free (nstring);
	}

	/* message-id */
	if (envelope_decode_nstring (engine, &nstring, FALSE, ex) == -1)
		goto exception;

	if (nstring != NULL) {
		if ((msgid = camel_lite_header_msgid_decode (nstring))) {
			md5_get_digest (msgid, strlen (msgid), md5sum);
			memcpy (iinfo->info.message_id.id.hash, md5sum, sizeof (CamelSummaryMessageID));
			g_free (msgid);
		}
		g_free (nstring);
	}

	if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
		return -1;

	if (token->token != ')') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, token);
		goto exception;
	}

	return 0;

 exception:

	return -1;
}

static char *tm_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static gboolean
decode_time (const char **in, int *hour, int *min, int *sec)
{
	register const unsigned char *inptr = (const unsigned char *) *in;
	int *val, colons = 0;

	*hour = *min = *sec = 0;

	val = hour;
	for ( ; *inptr && !isspace ((int) *inptr); inptr++) {
		if (*inptr == ':') {
			colons++;
			switch (colons) {
			case 1:
				val = min;
				break;
			case 2:
				val = sec;
				break;
			default:
				return FALSE;
			}
		} else if (!isdigit ((int) *inptr))
			return FALSE;
		else
			*val = (*val * 10) + (*inptr - '0');
	}

	*in = inptr;

	return TRUE;
}

static time_t
mktime_utc (struct tm *tm)
{
	time_t tt;

	tm->tm_isdst = -1;
	tt = mktime (tm);

#if defined (HAVE_TM_GMTOFF)
	tt += tm->tm_gmtoff;
#elif defined (HAVE_TIMEZONE)
	if (tm->tm_isdst > 0) {
#if defined (HAVE_ALTZONE)
		tt -= altzone;
#else /* !defined (HAVE_ALTZONE) */
		tt -= (timezone - 3600);
#endif
	} else
		tt -= timezone;
#endif

	return tt;
}

static time_t
decode_internaldate (const char *in)
{
	const char *inptr = in;
	int hour, min, sec, n;
	struct tm tm;
	time_t date;
	char *buf;

	memset ((void *) &tm, 0, sizeof (struct tm));

	tm.tm_mday = strtoul (inptr, &buf, 10);
	if (buf == inptr || *buf != '-')
		return (time_t) -1;

	inptr = buf + 1;
	if (inptr[3] != '-')
		return (time_t) -1;

	for (n = 0; n < 12; n++) {
		if (!g_ascii_strncasecmp (inptr, tm_months[n], 3))
			break;
	}

	if (n >= 12)
		return (time_t) -1;

	tm.tm_mon = n;

	inptr += 4;

	n = strtoul (inptr, &buf, 10);
	if (buf == inptr || *buf != ' ')
		return (time_t) -1;

	tm.tm_year = n - 1900;

	inptr = buf + 1;
	if (!decode_time (&inptr, &hour, &min, &sec))
		return (time_t) -1;

	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;

	n = strtol (inptr, NULL, 10);

	date = mktime_utc (&tm);

	/* date is now GMT of the time we want, but not offset by the timezone ... */

	/* this should convert the time to the GMT equiv time */
	date -= ((n / 100) * 60 * 60) + (n % 100) * 60;

	return date;
}

enum {
	IMAP4_FETCH_ENVELOPE     = (1 << 1),
	IMAP4_FETCH_FLAGS        = (1 << 2),
	IMAP4_FETCH_INTERNALDATE = (1 << 3),
	IMAP4_FETCH_RFC822SIZE   = (1 << 4),
	IMAP4_FETCH_UID          = (1 << 5),
};

#define IMAP4_FETCH_ALL (IMAP4_FETCH_ENVELOPE | IMAP4_FETCH_FLAGS | IMAP4_FETCH_INTERNALDATE | IMAP4_FETCH_RFC822SIZE | IMAP4_FETCH_UID)

struct imap4_envelope_t {
	CamelMessageInfo *info;
	guint changed;
};

struct imap4_fetch_all_t {
	CamelFolderChangeInfo *changes;
	CamelFolderSummary *summary;
	GHashTable *uid_hash;
	GPtrArray *added;
	guint32 first;
	guint32 need;
	int count;
	int total;
};

static void
imap4_fetch_all_free (struct imap4_fetch_all_t *fetch)
{
	struct imap4_envelope_t *envelope;
	int i;

	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i]))
			continue;

		camel_lite_message_info_free (envelope->info);
		g_free (envelope);
	}

	g_ptr_array_free (fetch->added, TRUE);
	g_hash_table_destroy (fetch->uid_hash);
	camel_lite_folder_change_info_free (fetch->changes);
	g_free (fetch);
}

static void
courier_imap_is_a_piece_of_shit (CamelFolderSummary *summary, guint32 msg)
{
	CamelSession *session = ((CamelService *) summary->folder->parent_store)->session;
	char *warning;

	warning = g_strdup_printf ("IMAP server did not respond with an untagged FETCH response "
				   "for message #%lu. This is illegal according to rfc3501 (and "
				   "the older rfc2060). You will need to contact your\n"
				   "Administrator(s) (or ISP) and have them resolve this issue.\n\n"
				   "Hint: If your IMAP server is Courier-IMAP, it is likely that this "
				   "message is simply unreadable by the IMAP server and will need "
				   "to be given read permissions.", msg);

	CamelService *service = NULL; /* TODO: Is there a CamelService that we can use? */
	camel_lite_session_alert_user_generic (session, CAMEL_SESSION_ALERT_WARNING, warning, FALSE, service);
	g_free (warning);
}

static void
imap4_fetch_all_add (struct imap4_fetch_all_t *fetch)
{
	CamelFolderChangeInfo *changes = NULL;
	struct imap4_envelope_t *envelope;
	CamelMessageInfo *info;
	int i;

	changes = fetch->changes;

	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i])) {
			courier_imap_is_a_piece_of_shit (fetch->summary, i + fetch->first);
			break;
		}

		if (envelope->changed != IMAP4_FETCH_ALL) {
			d(fprintf (stderr, "Hmmm, IMAP4 server didn't give us everything for message %d\n", i + 1));
			camel_lite_message_info_free (envelope->info);
			g_free (envelope);
			continue;
		}

		if ((info = camel_lite_folder_summary_uid (fetch->summary, camel_lite_message_info_uid (envelope->info)))) {
			camel_lite_message_info_free (envelope->info);
			camel_lite_message_info_free (info);
			g_free (envelope);
			continue;
		}

		camel_lite_folder_change_info_add_uid (changes, camel_lite_message_info_uid (envelope->info));

		if ((((CamelMessageInfoBase *) envelope->info)->flags & CAMEL_IMAP4_MESSAGE_RECENT))
			camel_lite_folder_change_info_recent_uid (changes, camel_lite_message_info_uid (envelope->info));

		camel_lite_folder_summary_add (fetch->summary, envelope->info);
		g_free (envelope);
	}

	g_ptr_array_free (fetch->added, TRUE);
	g_hash_table_destroy (fetch->uid_hash);

	if (camel_lite_folder_change_info_changed (changes))
		camel_lite_object_trigger_event (fetch->summary->folder, "folder_changed", changes);
	camel_lite_folder_change_info_free (changes);

	g_free (fetch);
}

static guint32
imap4_fetch_all_update (struct imap4_fetch_all_t *fetch)
{
	CamelIMAP4MessageInfo *iinfo, *new_iinfo;
	CamelFolderChangeInfo *changes = NULL;
	struct imap4_envelope_t *envelope;
	CamelMessageInfo *info;
	guint32 first = 0;
	guint32 flags;
	int total, i;

	changes = fetch->changes;

	total = camel_lite_folder_summary_count (fetch->summary);
	for (i = 0; i < total; i++) {
		info = camel_lite_folder_summary_index (fetch->summary, i);
		if (!(envelope = g_hash_table_lookup (fetch->uid_hash, camel_lite_message_info_uid (info)))) {
			/* remove it */
			camel_lite_folder_change_info_remove_uid (changes, camel_lite_message_info_uid (info));
			camel_lite_folder_summary_remove (fetch->summary, info);
			total--;
			i--;
		} else if (envelope->changed & IMAP4_FETCH_FLAGS) {
			/* update it with the new flags */
			new_iinfo = (CamelIMAP4MessageInfo *) envelope->info;
			iinfo = (CamelIMAP4MessageInfo *) info;

			flags = iinfo->info.flags;
			iinfo->info.flags = camel_lite_imap4_merge_flags (iinfo->server_flags, iinfo->info.flags, new_iinfo->server_flags);
			iinfo->server_flags = new_iinfo->server_flags;
			if (iinfo->info.flags != flags)
				camel_lite_folder_change_info_change_uid (changes, camel_lite_message_info_uid (info));
		}

		camel_lite_message_info_free (info);
	}

	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i])) {
			courier_imap_is_a_piece_of_shit (fetch->summary, i + fetch->first);
			break;
		}

		info = envelope->info;
		if (!first && camel_lite_message_info_uid (info)) {
			if ((info = camel_lite_folder_summary_uid (fetch->summary, camel_lite_message_info_uid (info)))) {
				camel_lite_message_info_free(info);
			} else {
				first = i + fetch->first;
			}
		}

		camel_lite_message_info_free(envelope->info);
		g_free (envelope);
	}

	g_ptr_array_free (fetch->added, TRUE);
	g_hash_table_destroy (fetch->uid_hash);

	if (camel_lite_folder_change_info_changed (changes))
		camel_lite_object_trigger_event (fetch->summary->folder, "folder_changed", changes);
	camel_lite_folder_change_info_free (changes);

	g_free (fetch);

	return first;
}

static int
untagged_fetch_all (CamelIMAP4Engine *engine, CamelIMAP4Command *ic, guint32 index, camel_lite_imap4_token_t *token, CamelException *ex)
{
	struct imap4_fetch_all_t *fetch = ic->user_data;
	CamelFolderSummary *summary = fetch->summary;
	struct imap4_envelope_t *envelope = NULL;
	GPtrArray *added = fetch->added;
	CamelIMAP4MessageInfo *iinfo;
	CamelMessageInfo *info;
	guint32 changed = 0;
	const char *iuid;
	char uid[12];

	if (index < fetch->first) {
		/* we already have this message envelope cached -
		 * server is probably notifying us of a FLAGS change
		 * by another client? */
		g_assert (index < summary->messages->len);
		iinfo = (CamelIMAP4MessageInfo *)(info = summary->messages->pdata[index - 1]);
		g_assert (info != NULL);
	} else {
		if (index > (added->len + fetch->first - 1))
			g_ptr_array_set_size (added, index - fetch->first + 1);

		if (!(envelope = added->pdata[index - fetch->first])) {
			iinfo = (CamelIMAP4MessageInfo *) (info = camel_lite_message_info_new (summary));
			envelope = g_new (struct imap4_envelope_t, 1);
			added->pdata[index - fetch->first] = envelope;
			envelope->info = info;
			envelope->changed = 0;
		} else {
			iinfo = (CamelIMAP4MessageInfo *) (info = envelope->info);
		}
	}

	if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
		return -1;

	/* parse the FETCH response list */
	if (token->token != '(') {
		camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, token);
		return -1;
	}

	do {
		if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
			goto exception;

		if (token->token == ')' || token->token == '\n')
			break;

		if (token->token != CAMEL_IMAP4_TOKEN_ATOM)
			goto unexpected;

		if (!strcmp (token->v.atom, "ENVELOPE")) {
			if (envelope) {
				if (decode_envelope (engine, info, token, ex) == -1)
					goto exception;

				changed |= IMAP4_FETCH_ENVELOPE;
			} else {
				CamelMessageInfo *tmp;
				int rv;

				g_warning ("Hmmm, server is sending us ENVELOPE data for a message we didn't ask for (message %lu)\n",
					   index);
				tmp = camel_lite_message_info_new (summary);
				rv = decode_envelope (engine, tmp, token, ex);
				camel_lite_message_info_free(tmp);

				if (rv == -1)
					goto exception;
			}
		} else if (!strcmp (token->v.atom, "FLAGS")) {
			guint32 server_flags = 0;

			if (camel_lite_imap4_parse_flags_list (engine, &server_flags, ex) == -1)
				return -1;

			iinfo->info.flags = camel_lite_imap4_merge_flags (iinfo->server_flags, iinfo->info.flags, server_flags);
			iinfo->server_flags = server_flags;

			changed |= IMAP4_FETCH_FLAGS;
		} else if (!strcmp (token->v.atom, "INTERNALDATE")) {
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			switch (token->token) {
			case CAMEL_IMAP4_TOKEN_NIL:
				iinfo->info.date_received = (time_t) -1;
				break;
			case CAMEL_IMAP4_TOKEN_ATOM:
			case CAMEL_IMAP4_TOKEN_QSTRING:
				iinfo->info.date_received = decode_internaldate (token->v.qstring);
				break;
			default:
				goto unexpected;
			}

			changed |= IMAP4_FETCH_INTERNALDATE;
		} else if (!strcmp (token->v.atom, "RFC822.SIZE")) {
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			if (token->token != CAMEL_IMAP4_TOKEN_NUMBER)
				goto unexpected;

			iinfo->info.size = token->v.number;

			changed |= IMAP4_FETCH_RFC822SIZE;
		} else if (!strcmp (token->v.atom, "UID")) {
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			if (token->token != CAMEL_IMAP4_TOKEN_NUMBER || token->v.number == 0)
				goto unexpected;

			sprintf (uid, "%lu", token->v.number);
			iuid = camel_lite_message_info_uid (info);
			if (iuid != NULL && iuid[0] != '\0') {
				if (strcmp (iuid, uid) != 0) {
					d(fprintf (stderr, "Hmmm, UID mismatch for message %u\n", index));
					g_assert_not_reached ();
				}
			} else {
				if (((CamelMessageInfoBase*)info)->uid_needs_free)
					g_free (info->uid);
				info->uid = g_strdup (uid);
				((CamelMessageInfoBase*)info)->uid_needs_free = TRUE;

				g_hash_table_insert (fetch->uid_hash, (void *) camel_lite_message_info_uid (info), envelope);
				changed |= IMAP4_FETCH_UID;
			}
		} else if (!strcmp (token->v.atom, "BODY[HEADER.FIELDS")) {
			/* References, Content-Type, and Mailing-List headers... */
			CamelContentType *content_type;
			struct _camel_lite_header_raw *h;
			CamelMimeParser *parser;
			unsigned char *literal;
			const char *refs, *str;
			char *mlist;
			size_t n;

			/* '(' */
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			if (token->token != '(')
				goto unexpected;

			/* header name list */
			do {
				if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
					goto exception;

				if (token->token == ')')
					break;

				switch (token->token) {
				case CAMEL_IMAP4_TOKEN_ATOM:
				case CAMEL_IMAP4_TOKEN_QSTRING:
					break;
				case CAMEL_IMAP4_TOKEN_LITERAL:
					if (camel_lite_imap4_engine_literal (engine, &literal, &n, ex) == -1)
						return -1;

					g_free (literal);
					break;
				default:
					goto unexpected;
				}

				/* we don't care what the list was... */
			} while (1);

			/* ']' */
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			if (token->token != ']')
				goto unexpected;

			/* literal */
			if (camel_lite_imap4_engine_next_token (engine, token, ex) == -1)
				goto exception;

			if (token->token != CAMEL_IMAP4_TOKEN_LITERAL)
				goto unexpected;

			parser = camel_lite_mime_parser_new ();
			camel_lite_mime_parser_init_with_stream (parser, (CamelStream *) engine->istream);

			switch (camel_lite_mime_parser_step (parser, NULL, NULL)) {
			case CAMEL_MIME_PARSER_STATE_HEADER:
			case CAMEL_MIME_PARSER_STATE_MESSAGE:
			case CAMEL_MIME_PARSER_STATE_MULTIPART:
				h = camel_lite_mime_parser_headers_raw (parser);

				/* find our mailing-list header */

				/* This introduces a problem for the mmap() implementation */

				// mlist = camel_lite_header_raw_check_mailing_list (&h);
				// iinfo->info.mlist = camel_lite_pstring_strdup (mlist);
				// g_free (mlist);

				/* check if we possibly have attachments */
				if ((str = camel_lite_header_raw_find (&h, "Content-Type", NULL))) {
					content_type = camel_lite_content_type_decode (str);
					if (camel_lite_content_type_is (content_type, "multipart", "*")
					    && !camel_lite_content_type_is (content_type, "multipart", "alternative"))
						iinfo->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;
					camel_lite_content_type_unref (content_type);
				}

				/* check for References: */
				g_free (iinfo->info.references);
				refs = camel_lite_header_raw_find (&h, "References", NULL);
				str = camel_lite_header_raw_find (&h, "In-Reply-To", NULL);
				iinfo->info.references = decode_references (refs, str);
			default:
				break;
			}

			camel_lite_object_unref (parser);
		} else {
			/* wtf? */
			d(fprintf (stderr, "huh? %s?...\n", token->v.atom));
		}
	} while (1);

	if (envelope) {
		envelope->changed |= changed;
		if ((envelope->changed & fetch->need) == fetch->need)
			camel_lite_operation_progress (NULL, (++fetch->count * 100.0f) / fetch->total);
	} else if (changed & IMAP4_FETCH_FLAGS) {
		camel_lite_folder_change_info_change_uid (fetch->changes, camel_lite_message_info_uid (info));
	}

	if (token->token != ')')
		goto unexpected;

	return 0;

 unexpected:

	camel_lite_imap4_utils_set_unexpected_token_error (ex, engine, token);

 exception:

	return -1;
}

#define IMAP4_ALL "FLAGS INTERNALDATE RFC822.SIZE ENVELOPE"
#define MAILING_LIST_HEADERS "List-Post List-Id Mailing-List Originator X-Mailing-List X-Loop X-List Sender Delivered-To Return-Path X-BeenThere List-Unsubscribe"

#define BASE_HEADER_FIELDS "Content-Type References In-Reply-To"
#define MORE_HEADER_FIELDS BASE_HEADER_FIELDS " " MAILING_LIST_HEADERS

static CamelIMAP4Command *
imap4_summary_fetch_all (CamelFolderSummary *summary, guint32 first, guint32 last)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;
	CamelFolder *folder = summary->folder;
	struct imap4_fetch_all_t *fetch;
	CamelIMAP4Engine *engine;
	CamelIMAP4Command *ic;
	const char *query;
	int total;

	engine = ((CamelIMAP4Store *) folder->parent_store)->engine;

	total = last ? (last - first) + 1 : (imap4_summary->exists - first) + 1;
	fetch = g_new (struct imap4_fetch_all_t, 1);
	fetch->uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	fetch->changes = camel_lite_folder_change_info_new ();
	fetch->added = g_ptr_array_sized_new (total);
	fetch->summary = summary;
	fetch->first = first;
	fetch->need = IMAP4_FETCH_ALL;
	fetch->total = total;
	fetch->count = 0;

	if (last != 0) {
		if (((CamelIMAP4Folder *) folder)->enable_mlist)
			query = "FETCH %u:%u (UID " IMAP4_ALL " BODY.PEEK[HEADER.FIELDS (" MORE_HEADER_FIELDS ")])\r\n";
		else
			query = "FETCH %u:%u (UID " IMAP4_ALL " BODY.PEEK[HEADER.FIELDS (" BASE_HEADER_FIELDS ")])\r\n";

		ic = camel_lite_imap4_engine_queue (engine, folder, query, first, last);
	} else {
		if (((CamelIMAP4Folder *) folder)->enable_mlist)
			query = "FETCH %u:* (UID " IMAP4_ALL " BODY.PEEK[HEADER.FIELDS (" MORE_HEADER_FIELDS ")])\r\n";
		else
			query = "FETCH %u:* (UID " IMAP4_ALL " BODY.PEEK[HEADER.FIELDS (" BASE_HEADER_FIELDS ")])\r\n";

		ic = camel_lite_imap4_engine_queue (engine, folder, query, first);
	}

	camel_lite_imap4_command_register_untagged (ic, "FETCH", untagged_fetch_all);
	ic->user_data = fetch;

	return ic;
}

static CamelIMAP4Command *
imap4_summary_fetch_flags (CamelFolderSummary *summary, guint32 first, guint32 last)
{
	CamelFolder *folder = summary->folder;
	struct imap4_fetch_all_t *fetch;
	CamelIMAP4Engine *engine;
	CamelIMAP4Command *ic;
	int total;

	engine = ((CamelIMAP4Store *) folder->parent_store)->engine;

	total = (last - first) + 1;
	fetch = g_new (struct imap4_fetch_all_t, 1);
	fetch->uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	fetch->changes = camel_lite_folder_change_info_new ();
	fetch->added = g_ptr_array_sized_new (total);
	fetch->summary = summary;
	fetch->first = first;
	fetch->need = IMAP4_FETCH_UID | IMAP4_FETCH_FLAGS;
	fetch->total = total;
	fetch->count = 0;

	if (last != 0)
		ic = camel_lite_imap4_engine_queue (engine, folder, "FETCH %u:%u (UID FLAGS)\r\n", first, last);
	else
		ic = camel_lite_imap4_engine_queue (engine, folder, "FETCH %u:* (UID FLAGS)\r\n", first);

	camel_lite_imap4_command_register_untagged (ic, "FETCH", untagged_fetch_all);
	ic->user_data = fetch;

	return ic;
}

#if 0
static int
imap4_build_summary (CamelFolderSummary *summary, guint32 first, guint32 last)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;
	CamelFolder *folder = summary->folder;
	struct imap4_fetch_all_t *fetch;
	CamelIMAP4Engine *engine;
	CamelIMAP4Command *ic;
	int id;

	engine = ((CamelIMAP4Store *) folder->store)->engine;

	ic = imap4_summary_fetch_all (summary, first, last);
	while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
		;

	fetch = ic->user_data;

	if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
		camel_lite_imap4_command_unref (ic);
		imap4_fetch_all_free (fetch);
		return -1;
	}

	imap4_fetch_all_add (fetch);

	camel_lite_imap4_command_unref (ic);

	return 0;
}
#endif

static CamelMessageInfo *
imap4_message_info_new_from_header (CamelFolderSummary *summary, struct _camel_lite_header_raw *header)
{
	CamelMessageInfo *info;

	info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_new_from_header (summary, header);

	((CamelIMAP4MessageInfo *) info)->server_flags = 0;

	return info;
}

static CamelMessageInfo *
imap4_message_info_load (CamelFolderSummary *summary)
{
	CamelIMAP4MessageInfo *minfo;
	CamelMessageInfo *info;
	unsigned char *ptrchr = summary->filepos;

	if (!(info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_load (summary)))
		return NULL;

	minfo = (CamelIMAP4MessageInfo *) info;

	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &minfo->server_flags, FALSE);
	summary->filepos = ptrchr;

	return info;
}

static int
imap4_message_info_save (CamelFolderSummary *summary, FILE *fout, CamelMessageInfo *info)
{
	CamelIMAP4MessageInfo *minfo = (CamelIMAP4MessageInfo *) info;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_save (summary, fout, info) == -1)
		return -1;

	if (camel_lite_file_util_encode_uint32 (fout, minfo->server_flags) == -1)
		return -1;

	return 0;
}

static CamelMessageInfo *
imap4_message_info_clone (CamelFolderSummary *summary, const CamelMessageInfo *mi)
{
	const CamelIMAP4MessageInfo *src = (const CamelIMAP4MessageInfo *) mi;
	CamelIMAP4MessageInfo *dest;

	dest = (CamelIMAP4MessageInfo *) CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_clone (summary, mi);
	dest->server_flags = src->server_flags;

	/* FIXME: parent clone should do this */
	dest->info.content = camel_lite_folder_summary_content_info_new (summary);

	return (CamelMessageInfo *) dest;
}

static CamelMessageContentInfo *
imap4_content_info_load (CamelFolderSummary *summary)
{
	guint32 doit;
	unsigned char* ptrchr = summary->filepos;

	ptrchr = camel_lite_file_util_mmap_decode_uint32 (ptrchr, &doit, FALSE);
	summary->filepos = ptrchr;

	if (doit == 1)
		return CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->content_info_load (summary);
	else
		return camel_lite_folder_summary_content_info_new (summary);
}

static int
imap4_content_info_save (CamelFolderSummary *summary, FILE *out, CamelMessageContentInfo *info)
{
	if (info->type) {
		fputc (1, out);
		return CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->content_info_save (summary, out, info);
	} else
		return fputc (0, out);
}


void
camel_lite_imap4_summary_set_exists (CamelFolderSummary *summary, guint32 exists)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	imap4_summary->exists = exists;
}

void
camel_lite_imap4_summary_set_recent (CamelFolderSummary *summary, guint32 recent)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	imap4_summary->recent = recent;
}

void
camel_lite_imap4_summary_set_unseen (CamelFolderSummary *summary, guint32 unseen)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	imap4_summary->unseen = unseen;
}

void
camel_lite_imap4_summary_set_uidnext (CamelFolderSummary *summary, guint32 uidnext)
{
	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	summary->nextuid = uidnext;
}

static void
imap4_summary_clear (CamelFolderSummary *summary, gboolean uncache)
{
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	int i, count;

	changes = camel_lite_folder_change_info_new ();
	count = camel_lite_folder_summary_count (summary);
	for (i = 0; i < count; i++) {
		if (!(info = camel_lite_folder_summary_index (summary, i)))
			continue;

		camel_lite_folder_change_info_remove_uid (changes, camel_lite_message_info_uid (info));
		camel_lite_message_info_free(info);
	}

	camel_lite_folder_summary_clear (summary);

	if (uncache)
		camel_lite_data_cache_clear (((CamelIMAP4Folder *) summary->folder)->cache, "cache", NULL);

	if (camel_lite_folder_change_info_changed (changes))
		camel_lite_object_trigger_event (summary->folder, "folder_changed", changes);
	camel_lite_folder_change_info_free (changes);
}

void
camel_lite_imap4_summary_set_uidvalidity (CamelFolderSummary *summary, guint32 uidvalidity)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;

	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	if (imap4_summary->uidvalidity == uidvalidity)
		return;

	imap4_summary_clear (summary, TRUE);

	imap4_summary->uidvalidity = uidvalidity;

	imap4_summary->uidvalidity_changed = TRUE;
}

void
camel_lite_imap4_summary_expunge (CamelFolderSummary *summary, int seqid)
{
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	const char *uid;

	g_return_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary));

	seqid--;
	if (!(info = camel_lite_folder_summary_index (summary, seqid)))
		return;

	imap4_summary->exists--;

	uid = camel_lite_message_info_uid (info);
	camel_lite_data_cache_remove (((CamelIMAP4Folder *) summary->folder)->cache, "cache", uid, NULL);

	changes = camel_lite_folder_change_info_new ();
	camel_lite_folder_change_info_remove_uid (changes, uid);

	camel_lite_message_info_free(info);
	camel_lite_folder_summary_remove_index (summary, seqid);

	camel_lite_object_trigger_event (summary->folder, "folder_changed", changes);
	camel_lite_folder_change_info_free (changes);
}

#if 0
static int
info_uid_sort (const CamelMessageInfo **info0, const CamelMessageInfo **info1)
{
	guint32 uid0, uid1;

	uid0 = strtoul (camel_lite_message_info_uid (*info0), NULL, 10);
	uid1 = strtoul (camel_lite_message_info_uid (*info1), NULL, 10);

	if (uid0 == uid1)
		return 0;

	return uid0 < uid1 ? -1 : 1;
}
#endif

int
camel_lite_imap4_summary_flush_updates (CamelFolderSummary *summary, CamelException *ex)
{
	CamelIMAP4Folder *imap4_folder = (CamelIMAP4Folder *) summary->folder;
	CamelIMAP4Summary *imap4_summary = (CamelIMAP4Summary *) summary;
	CamelOfflineJournal *journal = imap4_folder->journal;
	CamelIMAP4Engine *engine;
	CamelIMAP4Command *ic;
	guint32 first = 0;
	int scount, id;

	g_return_val_if_fail (CAMEL_IS_IMAP4_SUMMARY (summary), -1);

	/* FIXME: what do we do if replaying the journal fails? */
	camel_lite_offline_journal_replay (journal, NULL);

	if (imap4_folder->enable_mlist && !(summary->flags & CAMEL_IMAP4_SUMMARY_HAVE_MLIST)) {
		/* need to refetch all summary info to get info->mlist */
		imap4_summary_clear (summary, FALSE);
	}

	summary->flags = (summary->flags & ~CAMEL_IMAP4_SUMMARY_HAVE_MLIST);
	if (imap4_folder->enable_mlist)
		summary->flags |= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;
	else
		summary->flags ^= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;

	engine = ((CamelIMAP4Store *) summary->folder->parent_store)->engine;
	scount = camel_lite_folder_summary_count (summary);

	if (imap4_summary->uidvalidity_changed) {
		first = 1;
	} else if (imap4_summary->update_flags || imap4_summary->exists < scount) {
		/* this both updates flags and removes messages which
		 * have since been expunged from the server by another
		 * client */
		ic = imap4_summary_fetch_flags (summary, 1, scount);

		camel_lite_operation_start (NULL, _("Scanning for changed messages"));
		while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
			;

		if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
			camel_lite_imap4_journal_readd_failed ((CamelIMAP4Journal *) journal);
			imap4_fetch_all_free (ic->user_data);
			camel_lite_exception_xfer (ex, &ic->ex);
			camel_lite_imap4_command_unref (ic);
			camel_lite_operation_end (NULL);
			return -1;
		}

		if (!(first = imap4_fetch_all_update (ic->user_data)) && imap4_summary->exists > scount)
			first = scount + 1;

		camel_lite_imap4_command_unref (ic);
		camel_lite_operation_end (NULL);
	} else {
		first = scount + 1;
	}

	if (first != 0 && first <= imap4_summary->exists) {
		ic = imap4_summary_fetch_all (summary, first, 0);

		camel_lite_operation_start (NULL, _("Fetching envelopes for new messages"));
		while ((id = camel_lite_imap4_engine_iterate (engine)) < ic->id && id != -1)
			;

		if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
			camel_lite_imap4_journal_readd_failed ((CamelIMAP4Journal *) journal);
			imap4_fetch_all_free (ic->user_data);
			camel_lite_exception_xfer (ex, &ic->ex);
			camel_lite_imap4_command_unref (ic);
			camel_lite_operation_end (NULL);
			return -1;
		}

		imap4_fetch_all_add (ic->user_data);
		camel_lite_imap4_command_unref (ic);
		camel_lite_operation_end (NULL);
	}

	imap4_summary->update_flags = FALSE;
	imap4_summary->uidvalidity_changed = FALSE;

	camel_lite_imap4_journal_readd_failed ((CamelIMAP4Journal *) journal);

	return 0;
}
