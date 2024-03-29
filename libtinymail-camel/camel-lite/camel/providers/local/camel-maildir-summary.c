/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/e-lite-memory.h>

#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-stream-null.h"

#include "camel-maildir-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_MAILDIR_SUMMARY_VERSION (0x2000)

static CamelMessageInfo *message_info_load(CamelFolderSummary *s, gboolean *must_add);
static CamelMessageInfo *message_info_new_from_header(CamelFolderSummary *, struct _camel_lite_header_raw *);
static void message_info_free(CamelFolderSummary *, CamelMessageInfo *mi);

static int maildir_summary_load(CamelLocalSummary *cls, int forceindex, CamelException *ex);
static int maildir_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static int maildir_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
static CamelMessageInfo *maildir_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *, CamelException *ex);

static char *maildir_summary_next_uid_string(CamelFolderSummary *s);
static int maildir_summary_decode_x_evolution(CamelLocalSummary *cls, const char *xev, CamelLocalMessageInfo *mi);
static char *maildir_summary_encode_x_evolution(CamelLocalSummary *cls, const CamelLocalMessageInfo *mi);

static void camel_lite_maildir_summary_class_init	(CamelMaildirSummaryClass *class);
static void camel_lite_maildir_summary_init	(CamelMaildirSummary *gspaper);
static void camel_lite_maildir_summary_finalise	(CamelObject *obj);

#define _PRIVATE(x) (((CamelMaildirSummary *)(x))->priv)


struct _CamelMaildirSummaryPrivate {
	char *current_file;
	char *hostname;

	GHashTable *load_map;
};

static CamelLocalSummaryClass *parent_class;

CamelType
camel_lite_maildir_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_lite_type_register(camel_lite_local_summary_get_type (), "CamelLiteMaildirSummary",
					   sizeof(CamelMaildirSummary),
					   sizeof(CamelMaildirSummaryClass),
					   (CamelObjectClassInitFunc)camel_lite_maildir_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_lite_maildir_summary_init,
					   (CamelObjectFinalizeFunc)camel_lite_maildir_summary_finalise);
	}

	return type;
}

static void
camel_lite_maildir_summary_class_init (CamelMaildirSummaryClass *class)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) class;
	CamelLocalSummaryClass *lklass = (CamelLocalSummaryClass *)class;

	parent_class = (CamelLocalSummaryClass *)camel_lite_type_get_global_classfuncs(camel_lite_local_summary_get_type ());

	/* override methods */
	sklass->message_info_load = message_info_load;
	sklass->message_info_new_from_header = message_info_new_from_header;
	sklass->message_info_free = message_info_free;
	sklass->next_uid_string = maildir_summary_next_uid_string;

	lklass->load = maildir_summary_load;
	lklass->check = maildir_summary_check;
	lklass->sync = maildir_summary_sync;
	lklass->add = maildir_summary_add;
	lklass->encode_x_evolution = maildir_summary_encode_x_evolution;
	lklass->decode_x_evolution = maildir_summary_decode_x_evolution;
}

static void
camel_lite_maildir_summary_init (CamelMaildirSummary *o)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *) o;
	char hostname[256];

	o->priv = g_malloc0(sizeof(*o->priv));
	/* set unique file version */
	s->version += CAMEL_MAILDIR_SUMMARY_VERSION;

	s->message_info_size = sizeof(CamelMaildirMessageInfo);
	s->content_info_size = sizeof(CamelMaildirMessageContentInfo);

#if defined (DOEPOOLV) || defined (DOESTRV)
	s->message_info_strings = CAMEL_MAILDIR_INFO_LAST;
#endif

	if (gethostname(hostname, 256) == 0) {
		o->priv->hostname = g_strdup(hostname);
	} else {
		o->priv->hostname = g_strdup("localhost");
	}
}

static void
camel_lite_maildir_summary_finalise(CamelObject *obj)
{
	CamelMaildirSummary *o = (CamelMaildirSummary *)obj;

	g_free(o->priv->hostname);
	g_free(o->priv);
}

/**
 * camel_lite_maildir_summary_new:
 * @folder: parent folder.
 * @filename: Path to root of this maildir directory (containing new/tmp/cur directories).
 * @index: Index if one is reqiured.
 *
 * Create a new CamelMaildirSummary object.
 *
 * Return value: A new #CamelMaildirSummary object.
 **/
CamelMaildirSummary
*camel_lite_maildir_summary_new(struct _CamelFolder *folder, const char *filename, const char *maildirdir, CamelIndex *index)
{
	CamelMaildirSummary *o = (CamelMaildirSummary *)camel_lite_object_new(camel_lite_maildir_summary_get_type ());
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;

	((CamelFolderSummary *)o)->folder = folder;

	camel_lite_local_summary_construct((CamelLocalSummary *)o, filename, maildirdir, index);

	maildir_summary_load ((CamelLocalSummary *)o, FALSE, &ex);
	maildir_summary_check ((CamelLocalSummary *) o, NULL, &ex);

	return o;
}

/* the 'standard' maildir flags.  should be defined in sorted order. */
static struct {
	char flag;
	guint32 flagbit;
} flagbits[] = {
/* 0 */ { 'D', CAMEL_MESSAGE_DRAFT },
/* 1 */ { 'F', CAMEL_MESSAGE_FLAGGED },
/* -    { 'P', CAMEL_MESSAGE_FORWARDED },*/
/* 2 */ { 'R', CAMEL_MESSAGE_ANSWERED },
/* 3 */ { 'S', CAMEL_MESSAGE_SEEN },
/* 4 */ { 'T', CAMEL_MESSAGE_DELETED },

	/* Non-standard flags */
/* 5 */ { 'A', CAMEL_MESSAGE_ATTACHMENTS },
/* 6 */ { 'I', CAMEL_MESSAGE_PARTIAL },
/* 7 */ { 'O', CAMEL_MESSAGE_SUSPENDED },

	/* Non-standard priority flags */
/* 8 */ { 'H', CAMEL_MESSAGE_HIGH_PRIORITY },
/* 9 */ { 'N', CAMEL_MESSAGE_NORMAL_PRIORITY },
/* 10*/ { 'L', CAMEL_MESSAGE_LOW_PRIORITY }
};

/* convert the uid + flags into a unique:info maildir format */
char *camel_lite_maildir_summary_info_to_name(const CamelMaildirMessageInfo *info)
{
	const char *uid;
	char *p, *buf;
	int i;

	uid = camel_lite_message_info_uid (info);
	/* TNY CHANGE: This used to he ":2,", but VFAT does not allow those characters */
	buf = g_alloca (strlen (uid) + strlen ("!2,") +  (sizeof (flagbits) / sizeof (flagbits[0])) + 1);
	p = buf + sprintf (buf, "%s!2,", uid);

	for (i = 0; i < sizeof (flagbits) / sizeof (flagbits[0]); i++) {
		if (i > 7) {
			int flags = info->info.info.flags;
			flags &= CAMEL_MESSAGE_PRIORITY_MASK;
			if (flags == flagbits[i].flagbit)
				*p++ = flagbits[i].flag;
		} else if (info->info.info.flags & flagbits[i].flagbit)
			*p++ = flagbits[i].flag;
	}

	*p = 0;

	return g_strdup(buf);
}

/* returns 0 if the info matches (or there was none), otherwise we changed it */
int camel_lite_maildir_summary_name_to_info(CamelMaildirMessageInfo *info, const char *name)
{
	char *p, c;
	guint32 set = 0;	/* what we set */
	int i;

	p = strstr (name, "!2,");

	if (!p)
		p = strstr (name, ":2,");

	if (p) {
		p+=3;
		while ((c = *p++)) {
			/* we could assume that the flags are in order, but its just as easy not to require */
			for (i=0; i < sizeof(flagbits)/sizeof(flagbits[0]);i++) {

				if (i > 7) {
					if (c == flagbits[i].flag) {
						set &= ~CAMEL_MESSAGE_PRIORITY_MASK;
						set |= flagbits[i].flagbit;
					}
				} else {
					if (flagbits[i].flag == c && (info->info.info.flags & flagbits[i].flagbit) == 0)
						set |= flagbits[i].flagbit;
				}

			}

		}

		/* changed? */
		/*if ((info->flags & all) != set) {*/
		if ((info->info.info.flags & set) != set) {
			/* ok, they did change, only add the new flags ('merge flags'?) */
			/*info->flags &= all;  if we wanted to set only the new flags, which we probably dont */
			info->info.info.flags |= set;
			return 1;
		}
	}

	return 0;
}

/* for maildir, x-evolution isn't used, so dont try and get anything out of it */
static int maildir_summary_decode_x_evolution(CamelLocalSummary *cls, const char *xev, CamelLocalMessageInfo *mi)
{
	return -1;
}

static char *maildir_summary_encode_x_evolution(CamelLocalSummary *cls, const CamelLocalMessageInfo *mi)
{
	return NULL;
}

/* FIXME:
   both 'new' and 'add' will try and set the filename, this is not ideal ...
*/


static CamelMessageInfo *
adapted_local_summary_add (CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, CamelException *ex)
{
	CamelLocalMessageInfo *mi;
	char *xev;

	d(printf("Adding message to summary\n"));

	mi = (CamelLocalMessageInfo *)camel_lite_folder_summary_add_from_message((CamelFolderSummary *)cls, msg);
	if (mi) {
		d(printf("Added, uid = %s\n", mi->uid));
		if (info) {
#ifdef NON_TINYMAIL_FEATURES

			const CamelTag *tag = camel_lite_message_info_user_tags(info);
			const CamelFlag *flag = camel_lite_message_info_user_flags(info);

			while (flag) {
				camel_lite_message_info_set_user_flag((CamelMessageInfo *)mi, flag->name, TRUE);
				flag = flag->next;
			}

			while (tag) {
				camel_lite_message_info_set_user_tag((CamelMessageInfo *)mi, tag->name, tag->value);
				tag = tag->next;
			}
#endif
			mi->info.flags = camel_lite_message_info_flags(info);
			/*mi->info.flags |= (camel_lite_message_info_flags(info) & 0x1fff);*/
			mi->info.size = ((CamelMessageInfoBase*)info)->size;
		}

		/* we need to calculate the size ourselves */

		/* this is terribly slow, of course!

		if (mi->info.size == 0) {
			CamelStreamNull *sn = (CamelStreamNull *)camel_lite_stream_null_new();
			camel_lite_data_wrapper_write_to_stream((CamelDataWrapper *)msg, (CamelStream *)sn);
			mi->info.size = (sn->written);
			camel_lite_object_unref((CamelObject *)sn);
		} */

		mi->info.flags &= ~(CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED);
		xev = camel_lite_local_summary_encode_x_evolution(cls, mi);
		camel_lite_medium_set_header((CamelMedium *)msg, "X-Evolution", xev);
		g_free(xev);
		if (camel_lite_message_info_uid(mi))
			camel_lite_folder_change_info_add_uid(ci, camel_lite_message_info_uid(mi));
	} else {
		d(printf("Failed!\n"));
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_SYSTEM_MEMORY,
				     _("Unable to add message to summary: unknown reason"));
	}
	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *maildir_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelMaildirMessageInfo *mi;

	/* mi = (CamelMaildirMessageInfo *)((CamelLocalSummaryClass *) parent_class)->add(cls, msg, info, changes, ex); */

	mi = (CamelMaildirMessageInfo *) adapted_local_summary_add  (cls, msg, info, changes, ex);

	if (mi) {
		if (camel_lite_message_info_uid(mi))
		{
			/* struct stat sbuf; */
			CamelMessageInfoBase *mii = (CamelMessageInfoBase *)mi;
			/* gchar *name = NULL; */

			camel_lite_maildir_info_set_filename(mi, camel_lite_maildir_summary_info_to_name(mi));
			d(printf("Setting filename to %s\n", camel_lite_maildir_info_filename(mi)));


			if (mii->size == 0) {
				CamelStreamNull *sn = (CamelStreamNull *)camel_lite_stream_null_new();
				camel_lite_data_wrapper_write_to_stream((CamelDataWrapper *)msg, (CamelStream *)sn);
				mii->size = (sn->written);
				camel_lite_object_unref((CamelObject *)sn);
			} 

			/* name = g_strdup_printf("%s/cur/%s", cls->folder_path, camel_lite_maildir_info_filename (mi));
			if (stat (name, &sbuf) == 0)
				mii->size = sbuf.st_size;
			g_free (name); */
		}
	}

	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *message_info_new_from_header(CamelFolderSummary * s, struct _camel_lite_header_raw *h)
{
	CamelMessageInfo *mi, *info;
	CamelMaildirSummary *mds = (CamelMaildirSummary *)s;

	mi = ((CamelFolderSummaryClass *) parent_class)->message_info_new_from_header(s, h);

	/* assign the uid and new filename */

	/* with maildir we know the real received date, from the filename
	   Bulls. We just use what we have in the header of the message!
	mdi->info.info.date_received = strtoul(camel_lite_message_info_uid(mi), NULL, 10);  */

	if (mi) {
		CamelMaildirMessageInfo *mdi = (CamelMaildirMessageInfo *)mi;
		const gchar *uid;

		uid = camel_lite_message_info_uid(mi);
		if (uid == NULL || uid[0] == 0)
			mdi->info.info.uid = camel_lite_folder_summary_next_uid_string (s);

		/* handle 'duplicates' */
		info = camel_lite_folder_summary_uid(s, uid);
		if (info) {
			d(printf("already seen uid '%s', just summarising instead\n", uid));
			camel_lite_message_info_free(mi);
			mdi = (CamelMaildirMessageInfo *)(mi = info);
		}


		if (mds->priv->current_file) {
			/* if setting from a file, grab the flags from it */
			camel_lite_maildir_info_set_filename(mi, g_strdup(mds->priv->current_file));
			camel_lite_maildir_summary_name_to_info(mdi, mds->priv->current_file);

		} else {
			/* if creating a file, set its name from the flags we have */
			camel_lite_maildir_info_set_filename(mdi, camel_lite_maildir_summary_info_to_name(mdi));
			d(printf("Setting filename to %s\n", camel_lite_maildir_info_filename(mi)));
		}
	}

	return mi;
}


static void
message_info_free(CamelFolderSummary *s, CamelMessageInfo *mi)
{
#if !defined (DOEPOOLV) && !defined (DOESTRV)
	CamelMaildirMessageInfo *mdi = (CamelMaildirMessageInfo *)mi;

	g_free(mdi->filename);
#endif
	((CamelFolderSummaryClass *) parent_class)->message_info_free(s, mi);
}


static char *maildir_summary_next_uid_string(CamelFolderSummary *s)
{
	CamelMaildirSummary *mds = (CamelMaildirSummary *)s;

	d(printf("next uid string called?\n"));

	/* if we have a current file, then use that to get the uid */
	if (mds->priv->current_file) {
		char *cln;

		cln = strchr(mds->priv->current_file, '!');

		if (!cln)
			cln = strchr(mds->priv->current_file, ':');

		if (cln)
			return g_strndup(mds->priv->current_file, cln-mds->priv->current_file);
		else
			return g_strdup(mds->priv->current_file);
	} else {
		CamelLocalSummary *cls = (CamelLocalSummary *)s;
		char *name = NULL, *uid = NULL;
		struct stat st;
		int retry = 0;
		guint32 nextuid = camel_lite_folder_summary_next_uid(s);

		/* we use time.pid_count.hostname */
		do {
			if (retry > 0) {
				g_free(name);
				g_free(uid);
				sleep(2);
			}
			uid = g_strdup_printf("%ld.%d_%u.%s", time(NULL), getpid(), nextuid, mds->priv->hostname);
			name = g_strdup_printf("%s/tmp/%s", cls->folder_path, uid);
			retry++;
		} while (stat(name, &st) == 0 && retry<3);

		/* I dont know what we're supposed to do if it fails to find a unique name?? */

		g_free(name);
		return uid;
	}
}

static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, gboolean *must_add)
{
	CamelMessageInfo *mi;
	CamelMaildirSummary *mds = (CamelMaildirSummary *)s;

	mi = ((CamelFolderSummaryClass *) parent_class)->message_info_load(s, must_add);
	if (mi) {
		char *name;

		if (mds->priv->load_map && camel_lite_message_info_uid(mi)
		    && (name = g_hash_table_lookup(mds->priv->load_map, camel_lite_message_info_uid(mi)))) {
			d(printf("Setting filename of %s to %s\n", camel_lite_message_info_uid(mi), name));
			camel_lite_maildir_info_set_filename(mi, g_strdup(name));
			camel_lite_maildir_summary_name_to_info((CamelMaildirMessageInfo *)mi, name);
		}
	}

	return mi;
}

static int maildir_summary_load(CamelLocalSummary *cls, int forceindex, CamelException *ex)
{
	char *cur;
	DIR *dir;
	struct dirent *d;
	CamelMaildirSummary *mds = (CamelMaildirSummary *)cls;
	char *uid;
	EMemPool *pool;
	int ret;

	cur = g_strdup_printf("%s/cur", cls->folder_path);

	d(printf("pre-loading uid <> filename map\n"));

	dir = opendir(cur);
	if (dir == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM_IO_READ,
			_("Cannot open maildir directory path: %s: %s"),
			cls->folder_path, g_strerror (errno));
		g_free(cur);
		return -1;
	}

	mds->priv->load_map = g_hash_table_new(g_str_hash, g_str_equal);
	pool = e_mempool_new(1024, 512, E_MEMPOOL_ALIGN_BYTE);

	while ( (d = readdir(dir)) ) {
		if (d->d_name[0] == '.')
			continue;

		if (!strcmp (d->d_name, "core"))
			continue;

		uid = strchr(d->d_name, '!');

		if (!uid)
			uid = strchr(d->d_name, ':');

		if (uid) {
			int len = uid-d->d_name;
			uid = e_mempool_alloc(pool, len+1);
			memcpy(uid, d->d_name, len);
			uid[len] = 0;
			g_hash_table_insert(mds->priv->load_map, uid, e_mempool_strdup(pool, d->d_name));
		} else {
			uid = e_mempool_strdup(pool, d->d_name);
			g_hash_table_insert(mds->priv->load_map, uid, uid);
		}

	}
	closedir(dir);
	g_free(cur);

	ret = ((CamelLocalSummaryClass *) parent_class)->load(cls, forceindex, ex);

	g_hash_table_destroy(mds->priv->load_map);
	mds->priv->load_map = NULL;
	e_mempool_destroy(pool);

	return ret;
}

static int
camel_lite_maildir_summary_add (CamelLocalSummary *cls, const char *name, char *uid)
{
	CamelMaildirSummary *maildirs = (CamelMaildirSummary *)cls;
	char *filename = g_strdup_printf("%s/cur/%s", cls->folder_path, name);
	int fd;
	CamelMimeParser *mp;
	CamelMessageInfo *info;
	struct stat sbuf;

	d(printf("summarising: %s\n", name));

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		g_warning ("Cannot summarise/index: %s: %s", filename, strerror (errno));
		g_free(filename);
		return -1;
	}
	mp = camel_lite_mime_parser_new();
	camel_lite_mime_parser_scan_from(mp, FALSE);
	camel_lite_mime_parser_init_with_fd(mp, fd);
	maildirs->priv->current_file = (char *)name;
	info = camel_lite_folder_summary_add_from_parser((CamelFolderSummary *)maildirs, mp, uid);

	if (stat (filename, &sbuf) == 0)
		((CamelMessageInfoBase *)info)->size = sbuf.st_size;

	camel_lite_object_unref((CamelObject *)mp);
	maildirs->priv->current_file = NULL;
	g_free(filename);
	return 0;
}


static int
maildir_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changes, CamelException *ex)
{
	DIR *dir;
	struct dirent *d;
	char *p;
	CamelMessageInfo *info;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	char *new, *cur;
	char *uid;
	int count, total;

	new = g_strdup_printf("%s/new", cls->folder_path);
	cur = g_strdup_printf("%s/cur", cls->folder_path);

	camel_lite_operation_start(NULL, _("Checking folder consistency"));

	/* scan the directory, check for mail files not in the index, or index entries that
	   no longer exist */
	dir = opendir(cur);
	if (dir == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM_IO_READ,
			_("Cannot open maildir directory path: %s: %s"),
			cls->folder_path, g_strerror (errno));
		g_free(cur);
		g_free(new);
		camel_lite_operation_end(NULL);
		return -1;
	}

	total = 0;
	count = 0;
	while (readdir(dir))
		total++;
	rewinddir(dir);

	camel_lite_folder_summary_prepare_hash ((CamelFolderSummary *)cls);

	while (d = readdir(dir)) {
		gboolean mfree = FALSE;

		camel_lite_operation_progress(NULL, count, total);
		count++;

		/* FIXME: also run stat to check for regular file */
		p = d->d_name;
		if (p[0] == '.')
			continue;

		if (!strcmp (d->d_name, "core"))
			continue;

		/* map the filename -> uid */
		uid = strchr(d->d_name, '!');

		if (!uid)
			uid = strchr(d->d_name, ':');

		if (uid) {
			uid =  g_strndup(d->d_name, uid - d->d_name);
			mfree = TRUE;
		} else
			uid = d->d_name;


		info = camel_lite_folder_summary_uid ((CamelFolderSummary *)cls, uid);
		if (info == NULL) {
			/* must be a message incorporated by another client, this is not a 'recent' uid */
			if (camel_lite_maildir_summary_add (cls, d->d_name, uid) == 0)
				if (changes)
					camel_lite_folder_change_info_add_uid(changes, uid);


		} else {
			// CamelMaildirMessageInfo *mdi = (CamelMaildirMessageInfo *) info;
			// mdi->filename = g_strdup (d->d_name);
			camel_lite_message_info_free(info);
		}

		if (mfree)
			g_free(uid);
	}
	closedir(dir);

	camel_lite_operation_end(NULL);

	camel_lite_operation_start(NULL, _("Checking for new messages"));

	/* now, scan new for new messages, and copy them to cur, and so forth */
	dir = opendir(new);
	if (dir != NULL) {
		total = 0;
		count = 0;
		while (readdir(dir))
			total++;
		rewinddir(dir);

		while (d = readdir(dir)) {
			char *name, *newname, *destname, *destfilename;
			char *src, *dest;

			camel_lite_operation_progress(NULL, count, total);
			count++;

			name = d->d_name;
			if (name[0] == '.')
				continue;

			if (!strcmp (d->d_name, "core"))
				continue;

			/* already in summary?  shouldn't happen, but just incase ... */
			if ((info = camel_lite_folder_summary_uid((CamelFolderSummary *)cls, name))) {
				camel_lite_message_info_free(info);
				newname = destname = camel_lite_folder_summary_next_uid_string(s);
			} else {
				newname = NULL;
				destname = name;
			}

			/* copy this to the destination folder, use 'standard' semantics for maildir info field */
			src = g_strdup_printf("%s/%s", new, name);
			destfilename = g_strdup_printf("%s!2,", destname);
			dest = g_strdup_printf("%s/%s", cur, destfilename);

			/* FIXME: This should probably use link/unlink */

			if (rename(src, dest) == 0) {
				camel_lite_maildir_summary_add (cls, destfilename, destname);
				if (changes) {
					camel_lite_folder_change_info_add_uid(changes, destname);
					camel_lite_folder_change_info_recent_uid(changes, destname);
				}
			} else {
				/* else?  we should probably care about failures, but wont */
				g_warning("Failed to move new maildir message %s to cur %s", src, dest);
			}

			/* c strings are painful to work with ... */
			g_free(destfilename);
			g_free(newname);
			g_free(src);
			g_free(dest);
		}

		camel_lite_operation_end(NULL);
		closedir(dir);
	}

	g_free(new);
	g_free(cur);

	camel_lite_folder_summary_kill_hash ((CamelFolderSummary *)cls);

	camel_lite_folder_summary_save ((CamelFolderSummary *) cls, ex);

	return 0;
}



/* sync the summary with the ondisk files. */
static int
maildir_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changes, CamelException *ex)
{
	int count, i;
	CamelMessageInfo *info;
	CamelMaildirMessageInfo *mdi;
	char *name;
	struct stat st;

	d(printf("summary_sync(expunge=%s)\n", expunge?"true":"false"));

	if (camel_lite_local_summary_check(cls, changes, ex) == -1)
		return -1;

	camel_lite_operation_start(NULL, _("Storing folder"));

	count = camel_lite_folder_summary_count((CamelFolderSummary *)cls);
	for (i=count-1;i>=0;i--) {
		camel_lite_operation_progress(NULL, (count-i), count);

		info = camel_lite_folder_summary_index((CamelFolderSummary *)cls, i);
		mdi = (CamelMaildirMessageInfo *)info;
		if (mdi && (mdi->info.info.flags & CAMEL_MESSAGE_DELETED) && expunge) {
			name = camel_lite_maildir_get_filename (cls->folder_path, mdi, info->uid);
			d(printf("deleting %s\n", name));
			if (unlink(name) == 0 || errno==ENOENT) {

				/* FIXME: put this in folder_summary::remove()? */
				if (cls->index)
					camel_lite_index_delete_name(cls->index, camel_lite_message_info_uid(info));

				camel_lite_folder_change_info_remove_uid(changes, camel_lite_message_info_uid(info));
				((CamelMessageInfoBase*)info)->flags |= CAMEL_MESSAGE_EXPUNGED;
				camel_lite_folder_summary_remove((CamelFolderSummary *)cls, info);
			}
			g_free(name);
		} else if (mdi && (mdi->info.info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			char *newname = camel_lite_maildir_summary_info_to_name(mdi);
			char *dest;

			/* do we care about additional metainfo stored inside the message? */
			/* probably should all go in the filename? */

			/* have our flags/ i.e. name changed? */
			if (newname && strcmp(newname, camel_lite_maildir_info_filename (mdi))) {
				name = g_strdup_printf("%s/cur/%s", cls->folder_path, camel_lite_maildir_info_filename(mdi));
				dest = g_strdup_printf("%s/cur/%s", cls->folder_path, newname);
				rename(name, dest);
				if (stat(dest, &st) == -1) {
					/* we'll assume it didn't work, but dont change anything else */
					g_free(newname);
					newname = NULL;
				}
				g_free(name);
				g_free(dest);
			} 

			if (newname)
				g_free(newname);

			/* strip FOLDER_MESSAGE_FLAGED, etc */
			mdi->info.info.flags &= 0x1fff;
		}
		camel_lite_message_info_free(info);
	}

	camel_lite_operation_end(NULL);

	return ((CamelLocalSummaryClass *)parent_class)->sync(cls, expunge, changes, ex);
}


char *camel_lite_maildir_get_filename (const gchar *fpath, CamelMaildirMessageInfo *mdi, const gchar *uid)
{
	char *filen = mdi->filename;
	gboolean nfree = FALSE;
	char *name;

	if (!filen || strlen (filen) == 0) {
		filen = camel_lite_maildir_summary_info_to_name (mdi);
		if (filen)
			nfree = TRUE;
	}

	if (filen)
		name = g_strdup_printf("%s/cur/%s", fpath, filen);
	else 
		name = (char *) uid;

	if (nfree)
		g_free (filen);

	if (!g_file_test (name, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		gint len = strlen (name);
		gchar *cur = g_strdup_printf("%s/cur", fpath);
		DIR *dir; struct dirent *d;
		dir = opendir(cur);
		
		if (dir) {
			while ( (d = readdir(dir)) ) {
				char *nname = g_strdup_printf ("%s/%s", cur, d->d_name);
				char *ptr = strstr (nname, "!");

				if (!ptr)
					strstr (nname, ":");
				if (ptr)
					len = (ptr - nname);

				if (!g_ascii_strncasecmp (nname, name, len)) {
					g_free (name);
					name = nname;
					break;
				}
				g_free (nname);
			}
			closedir(dir);
		}
		g_free (cur);
	}

	return name;
}
