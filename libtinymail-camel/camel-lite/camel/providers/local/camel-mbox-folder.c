/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999, 2003 Ximian Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _LARGEFILE64_SOURCE
#define O_LARGEFILE 0
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel/camel-data-wrapper.h"
#include "camel/camel-exception.h"
#include "camel/camel-mime-filter-from.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-private.h"
#include "camel/camel-stream-filter.h"
#include "camel/camel-stream-fs.h"

#include "camel-mbox-folder.h"
#include "camel-mbox-store.h"
#include "camel-mbox-summary.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMboxFolder */
#define CMBOXF_CLASS(so) CAMEL_MBOX_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMBOXS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static int mbox_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex);
static void mbox_unlock(CamelLocalFolder *lf);

static void mbox_append_message(CamelFolder *folder, CamelMimeMessage * message, const CamelMessageInfo * info,	char **appended_uid, CamelException *ex);
static CamelMimeMessage *mbox_get_message(CamelFolder *folder, const gchar * uid, CamelFolderReceiveType type, gint param, CamelException *ex);
static CamelLocalSummary *mbox_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index);

static void mbox_finalise(CamelObject * object);

static void
camel_lite_mbox_folder_class_init(CamelMboxFolderClass * camel_lite_mbox_folder_class)
{
	CamelFolderClass *camel_lite_folder_class = CAMEL_FOLDER_CLASS(camel_lite_mbox_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_lite_mbox_folder_class;

	parent_class = (CamelLocalFolderClass *)camel_lite_type_get_global_classfuncs(camel_lite_local_folder_get_type());

	/* virtual method definition */

	/* virtual method overload */
	camel_lite_folder_class->append_message = mbox_append_message;
	camel_lite_folder_class->get_message = mbox_get_message;

	lclass->create_summary = mbox_create_summary;
	lclass->lock = mbox_lock;
	lclass->unlock = mbox_unlock;
}

static void
mbox_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;*/
	CamelMboxFolder *mbox_folder = object;

	mbox_folder->lockfd = -1;
}

static void
mbox_finalise(CamelObject * object)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)object;

	g_assert(mbox_folder->lockfd == -1);
}

CamelType camel_lite_mbox_folder_get_type(void)
{
	static CamelType camel_lite_mbox_folder_type = CAMEL_INVALID_TYPE;

	if (camel_lite_mbox_folder_type == CAMEL_INVALID_TYPE) {
		camel_lite_mbox_folder_type = camel_lite_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelLiteMboxFolder",
							     sizeof(CamelMboxFolder),
							     sizeof(CamelMboxFolderClass),
							     (CamelObjectClassInitFunc) camel_lite_mbox_folder_class_init,
							     NULL,
							     (CamelObjectInitFunc) mbox_init,
							     (CamelObjectFinalizeFunc) mbox_finalise);
	}

	return camel_lite_mbox_folder_type;
}

CamelFolder *
camel_lite_mbox_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating mbox folder: %s in %s\n", full_name, camel_lite_local_store_get_toplevel_dir((CamelLocalStore *)parent_store)));

	folder = (CamelFolder *)camel_lite_object_new(CAMEL_MBOX_FOLDER_TYPE);
	folder = (CamelFolder *)camel_lite_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *mbox_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_lite_mbox_summary_new((CamelFolder *)lf, path, folder, index);
}

static int mbox_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex)
{
#ifndef G_OS_WIN32
	CamelMboxFolder *mf = (CamelMboxFolder *)lf;

	/* make sure we have matching unlocks for locks, camel-local-folder class should enforce this */
	g_assert(mf->lockfd == -1);

	mf->lockfd = open(lf->folder_path, O_RDWR, 0);
	if (mf->lockfd == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder lock on %s: %s"),
				      lf->folder_path, g_strerror (errno));
		return -1;
	}

	if (camel_lite_lock_folder(lf->folder_path, mf->lockfd, type, ex) == -1) {
		close(mf->lockfd);
		mf->lockfd = -1;
		return -1;
	}
#endif
	return 0;
}

static void mbox_unlock(CamelLocalFolder *lf)
{
#ifndef G_OS_WIN32
	CamelMboxFolder *mf = (CamelMboxFolder *)lf;

	g_assert(mf->lockfd != -1);
	camel_lite_unlock_folder(lf->folder_path, mf->lockfd);
	close(mf->lockfd);
	mf->lockfd = -1;
#endif
}

static void
mbox_append_message(CamelFolder *folder, CamelMimeMessage * message, const CamelMessageInfo * info, char **appended_uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream = NULL, *filter_stream = NULL;
	CamelMimeFilter *filter_from;
	CamelMboxSummary *mbs = (CamelMboxSummary *)folder->summary;
	CamelMessageInfo *mi;
	char *fromline = NULL;
	struct stat st;
	int retval;
#if 0
	char *xev;
#endif
	/* If we can't lock, dont do anything */
	if (camel_lite_local_folder_lock(lf, CAMEL_LOCK_WRITE, ex) == -1)
		return;

	d(printf("Appending message\n"));

	/* first, check the summary is correct (updates folder_size too) */
	retval = camel_lite_local_summary_check ((CamelLocalSummary *)folder->summary, lf->changes, ex);
	if (retval == -1)
		goto fail;

	/* add it to the summary/assign the uid, etc */
	mi = camel_lite_local_summary_add((CamelLocalSummary *)folder->summary, message, info, lf->changes, ex);
	if (mi == NULL)
		goto fail;

	d(printf("Appending message: uid is %s\n", camel_lite_message_info_uid(mi)));

	output_stream = camel_lite_stream_fs_new_with_name(lf->folder_path, O_WRONLY | O_APPEND | O_LARGEFILE, 0666);
	if (output_stream == NULL) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot open mailbox: %s: %s\n"),
				      lf->folder_path, g_strerror (errno));
		goto fail;
	}

	/* and we need to set the frompos/XEV explicitly */
	((CamelMboxMessageInfo *)mi)->frompos = mbs->folder_size;
#if 0
	xev = camel_lite_local_summary_encode_x_evolution((CamelLocalSummary *)folder->summary, mi);
	if (xev) {
		/* the x-ev header should match the 'current' flags, no problem, so store as much */
		camel_lite_medium_set_header((CamelMedium *)message, "X-Evolution", xev);
		mi->flags &= ~ CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED;
		g_free(xev);
	}
#endif

	/* we must write this to the non-filtered stream ... */
	fromline = camel_lite_mime_message_build_mbox_from(message);
	if (camel_lite_stream_write(output_stream, fromline, strlen(fromline)) == -1)
		goto fail_write;

	/* and write the content to the filtering stream, that translates '\nFrom' into '\n>From' */
	filter_stream = (CamelStream *) camel_lite_stream_filter_new_with_stream(output_stream);
	filter_from = (CamelMimeFilter *) camel_lite_mime_filter_from_new();
	camel_lite_stream_filter_add((CamelStreamFilter *) filter_stream, filter_from);
	camel_lite_object_unref (filter_from);

	if (camel_lite_data_wrapper_write_to_stream ((CamelDataWrapper *) message, filter_stream) == -1 ||
	    camel_lite_stream_write (filter_stream, "\n", 1) == -1 ||
	    camel_lite_stream_flush (filter_stream) == -1)
		goto fail_write;

	/* filter stream ref's the output stream itself, so we need to unref it too */
	camel_lite_object_unref (filter_stream);
	camel_lite_object_unref (output_stream);
	g_free(fromline);

	/* now we 'fudge' the summary  to tell it its uptodate, because its idea of uptodate has just changed */
	/* the stat really shouldn't fail, we just wrote to it */
	if (g_stat (lf->folder_path, &st) == 0) {
		((CamelFolderSummary *) mbs)->time = st.st_mtime;
		mbs->folder_size = st.st_size;
	}

	/* unlock as soon as we can */
	camel_lite_local_folder_unlock(lf);

	if (camel_lite_folder_change_info_changed(lf->changes)) {
		camel_lite_object_trigger_event((CamelObject *)folder, "folder_changed", lf->changes);
		camel_lite_folder_change_info_clear(lf->changes);
	}

	if (appended_uid)
		*appended_uid = g_strdup(camel_lite_message_info_uid(mi));

	return;

fail_write:
	if (errno == EINTR)
		camel_lite_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Mail append canceled"));
	else
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message to mbox file: %s: %s"),
				      lf->folder_path, g_strerror (errno));

	if (output_stream) {
		/* reset the file to original size */
		do {
			retval = ftruncate (((CamelStreamFs *) output_stream)->fd, mbs->folder_size);
		} while (retval == -1 && errno == EINTR);

		camel_lite_object_unref (output_stream);
	}

	if (filter_stream)
		camel_lite_object_unref (filter_stream);

	g_free(fromline);

	/* remove the summary info so we are not out-of-sync with the mbox */
	camel_lite_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (mbs), camel_lite_message_info_uid (mi));

	/* and tell the summary it's up-to-date */
	if (g_stat (lf->folder_path, &st) == 0) {
		((CamelFolderSummary *) mbs)->time = st.st_mtime;
		mbs->folder_size = st.st_size;
	}

fail:
	/* make sure we unlock the folder - before we start triggering events into appland */
	camel_lite_local_folder_unlock(lf);

	/* cascade the changes through, anyway, if there are any outstanding */
	if (camel_lite_folder_change_info_changed(lf->changes)) {
		camel_lite_object_trigger_event((CamelObject *)folder, "folder_changed", lf->changes);
		camel_lite_folder_change_info_clear(lf->changes);
	}
}

static CamelMimeMessage *
mbox_get_message(CamelFolder *folder, const gchar * uid, CamelFolderReceiveType type, gint param, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelMimeMessage *message = NULL;
	CamelMboxMessageInfo *info;
	CamelMimeParser *parser = NULL;
	int fd, retval;
	int retried = FALSE;
	off_t frompos;

	d(printf("Getting message %s\n", uid));

	/* TNY TODO: Implement partial message retrieval if full==TRUE
	   maybe remove the attachments if it happens to be FALSE in this case?
	 */

	/* lock the folder first, burn if we can't, need write lock for summary check */
	if (camel_lite_local_folder_lock(lf, CAMEL_LOCK_WRITE, ex) == -1)
		return NULL;

	/* check for new messages always */
	if (camel_lite_local_summary_check((CamelLocalSummary *)folder->summary, lf->changes, ex) == -1) {
		camel_lite_local_folder_unlock(lf);
		return NULL;
	}

retry:
	/* get the message summary info */
	info = (CamelMboxMessageInfo *) camel_lite_folder_summary_uid(folder->summary, uid);

	if (info == NULL) {
		camel_lite_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, lf->folder_path, _("No such message"));
		goto fail;
	}

	/* no frompos, its an error in the library (and we can't do anything with it) */
	g_assert(info->frompos != -1);

	frompos = info->frompos;
	camel_lite_message_info_free((CamelMessageInfo *)info);

	/* we use an fd instead of a normal stream here - the reason is subtle, camel_lite_mime_part will cache
	   the whole message in memory if the stream is non-seekable (which it is when built from a parser
	   with no stream).  This means we dont have to lock the mbox for the life of the message, but only
	   while it is being created. */

	fd = g_open(lf->folder_path, O_LARGEFILE | O_RDONLY | O_BINARY, 0);
	if (fd == -1) {
		camel_lite_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot get message: %s from folder %s\n  %s"),
				      uid, lf->folder_path, g_strerror (errno));
		goto fail;
	}

	/* we use a parser to verify the message is correct, and in the correct position */
	parser = camel_lite_mime_parser_new();
	camel_lite_mime_parser_init_with_fd(parser, fd);
	camel_lite_mime_parser_scan_from(parser, TRUE);

	camel_lite_mime_parser_seek(parser, frompos, SEEK_SET);
	if (camel_lite_mime_parser_step(parser, NULL, NULL) != CAMEL_MIME_PARSER_STATE_FROM
	    || camel_lite_mime_parser_tell_start_from(parser) != frompos) {

		g_warning("Summary doesn't match the folder contents!  eek!\n"
			  "  expecting offset %ld got %ld, state = %d", (long int)frompos,
			  (long int)camel_lite_mime_parser_tell_start_from(parser),
			  camel_lite_mime_parser_state(parser));

		camel_lite_object_unref((CamelObject *)parser);
		parser = NULL;

		if (!retried) {
			retried = TRUE;
			camel_lite_local_summary_check_force((CamelLocalSummary *)folder->summary);
			retval = camel_lite_local_summary_check((CamelLocalSummary *)folder->summary, lf->changes, ex);
			if (retval != -1)
				goto retry;
		}

		camel_lite_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID,
				     _("Cannot get message: %s from folder %s\n  %s"), uid, lf->folder_path,
				     _("The folder appears to be ircamel_lite_recoverably corrupted."));
		goto fail;
	}

	message = camel_lite_mime_message_new();
	if (camel_lite_mime_part_construct_from_parser((CamelMimePart *)message, parser) == -1) {
		camel_lite_exception_setv(ex, errno==EINTR?CAMEL_EXCEPTION_USER_CANCEL:CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot get message: %s from folder %s\n  %s"), uid, lf->folder_path,
				     _("Message construction failed."));
		camel_lite_object_unref((CamelObject *)message);
		message = NULL;
		goto fail;
	}

	camel_lite_medium_remove_header((CamelMedium *)message, "X-Evolution");
fail:
	/* and unlock now we're finished with it */
	camel_lite_local_folder_unlock(lf);

	if (parser)
		camel_lite_object_unref((CamelObject *)parser);

	/* use the opportunity to notify of changes (particularly if we had a rebuild) */
	if (camel_lite_folder_change_info_changed(lf->changes)) {
		camel_lite_object_trigger_event((CamelObject *)folder, "folder_changed", lf->changes);
		camel_lite_folder_change_info_clear(lf->changes);
	}

	return message;
}
