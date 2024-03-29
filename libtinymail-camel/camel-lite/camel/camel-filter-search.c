/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	     Michael Zucchi <NotZed@Ximian.com>
 *
 *  Copyright 2000 Ximian, Inc. (www.ximian.com)
 *  Copyright 2001 Ximian Inc. (www.ximian.com)
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

/* POSIX requires <sys/types.h> be included before <regex.h> */
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifndef G_OS_WIN32
#include <sys/wait.h>
#endif

#include <libedataserver/e-lite-iconv.h>
#include <libedataserver/e-lite-sexp.h>

#include "camel-debug.h"
#include "camel-exception.h"
#include "camel-filter-search.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-provider.h"
#include "camel-search-private.h"
#include "camel-session.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"
#include "camel-string-utils.h"
#include "camel-url.h"

#define d(x)

typedef struct {
	CamelSession *session;
	CamelFilterSearchGetMessageFunc get_message;
	void *get_message_data;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	const char *source;
	CamelException *ex;
} FilterMessageSearch;

/* ESExp callbacks */
static ESExpResult *header_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_matches (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_starts_with (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_ends_with (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_exists (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_soundex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_full_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *match_all (struct _ESExp *f, int argc, struct _ESExpTerm **argv, FilterMessageSearch *fms);
static ESExpResult *body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *body_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *user_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *user_tag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *system_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_sent_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_received_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_current_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *header_source (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_size (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *pipe_message (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);

#ifdef NON_TINYMAIL_FEATURES
static ESExpResult *junk_test (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
#endif

/* builtin functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "match-all",          (ESExpFunc *) match_all,          1 },
	{ "body-contains",      (ESExpFunc *) body_contains,      0 },
	{ "body-regex",         (ESExpFunc *) body_regex,         0 },
	{ "header-contains",    (ESExpFunc *) header_contains,    0 },
	{ "header-matches",     (ESExpFunc *) header_matches,     0 },
	{ "header-starts-with", (ESExpFunc *) header_starts_with, 0 },
	{ "header-ends-with",   (ESExpFunc *) header_ends_with,   0 },
	{ "header-exists",      (ESExpFunc *) header_exists,      0 },
	{ "header-soundex",     (ESExpFunc *) header_soundex,     0 },
	{ "header-regex",       (ESExpFunc *) header_regex,       0 },
	{ "header-full-regex",  (ESExpFunc *) header_full_regex,  0 },
	{ "user-tag",           (ESExpFunc *) user_tag,           0 },
	{ "user-flag",          (ESExpFunc *) user_flag,          0 },
	{ "system-flag",        (ESExpFunc *) system_flag,        0 },
	{ "get-sent-date",      (ESExpFunc *) get_sent_date,      0 },
	{ "get-received-date",  (ESExpFunc *) get_received_date,  0 },
	{ "get-current-date",   (ESExpFunc *) get_current_date,   0 },
	{ "header-source",      (ESExpFunc *) header_source,      0 },
	{ "get-size",           (ESExpFunc *) get_size,           0 },
	{ "pipe-message",       (ESExpFunc *) pipe_message,       0 },
#ifdef NON_TINYMAIL_FEATURES
	{ "junk-test",          (ESExpFunc *) junk_test,          0 },
#endif
};


static CamelMimeMessage *
camel_lite_filter_search_get_message (FilterMessageSearch *fms, struct _ESExp *sexp)
{
	if (fms->message)
		return fms->message;

	fms->message = fms->get_message (fms->get_message_data, fms->ex);

	if (fms->message == NULL)
		e_sexp_fatal_error (sexp, _("Failed to retrieve message"));

	return fms->message;
}

static ESExpResult *
check_header (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms, camel_lite_search_match_t how)
{
	gboolean matched = FALSE;
	ESExpResult *r;
	int i;

	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		char *name = argv[0]->value.string;

		/* shortcut: a match for "" against any header always matches */
		for (i=1; i<argc && !matched; i++)
			matched = argv[i]->type == ESEXP_RES_STRING && argv[i]->value.string[0] == 0;

#if NON_TINYMAIL_FEATURES
		if (g_ascii_strcasecmp(name, "x-camel-mlist") == 0) {
			const char *list = camel_lite_message_info_mlist(fms->info);

			if (list) {
				for (i=1; i<argc && !matched; i++) {
					if (argv[i]->type == ESEXP_RES_STRING)
						matched = camel_lite_search_header_match(list, argv[i]->value.string, how, CAMEL_SEARCH_TYPE_MLIST, NULL);
				}
			}
		} else {
#endif
			{
			CamelMimeMessage *message;
			struct _camel_lite_header_raw *header;
			const char *charset = NULL;
			camel_lite_search_t type = CAMEL_SEARCH_TYPE_ENCODED;
			CamelContentType *ct;

			message = camel_lite_filter_search_get_message (fms, f);

			/* FIXME: what about Resent-To, Resent-Cc and Resent-From? */
			if (g_ascii_strcasecmp("to", name) == 0 || g_ascii_strcasecmp("cc", name) == 0 || g_ascii_strcasecmp("from", name) == 0)
				type = CAMEL_SEARCH_TYPE_ADDRESS_ENCODED;
			else if (message) {
				ct = camel_lite_mime_part_get_content_type (CAMEL_MIME_PART (message));
				if (ct) {
					charset = camel_lite_content_type_param (ct, "charset");
					charset = e_iconv_charset_name (charset);
				}
			}

			for (header = ((CamelMimePart *)message)->headers; header && !matched; header = header->next) {
				if (!g_ascii_strcasecmp(header->name, name)) {
					for (i=1; i<argc && !matched; i++) {
						if (argv[i]->type == ESEXP_RES_STRING)
							matched = camel_lite_search_header_match(header->value, argv[i]->value.string, how, type, charset);
					}
				}
			}
#ifdef NON_TINYMAIL_FEATURES
		}
#endif
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = matched;

	return r;
}

static ESExpResult *
header_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	return check_header (f, argc, argv, fms, CAMEL_SEARCH_MATCH_CONTAINS);
}


static ESExpResult *
header_matches (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	return check_header (f, argc, argv, fms, CAMEL_SEARCH_MATCH_EXACT);
}

static ESExpResult *
header_starts_with (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	return check_header (f, argc, argv, fms, CAMEL_SEARCH_MATCH_STARTS);
}

static ESExpResult *
header_ends_with (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	return check_header (f, argc, argv, fms, CAMEL_SEARCH_MATCH_ENDS);
}

static ESExpResult *
header_soundex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	return check_header (f, argc, argv, fms, CAMEL_SEARCH_MATCH_SOUNDEX);
}

static ESExpResult *
header_exists (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	CamelMimeMessage *message;
	gboolean matched = FALSE;
	ESExpResult *r;
	int i;

	message = camel_lite_filter_search_get_message (fms, f);

	for (i = 0; i < argc && !matched; i++) {
		if (argv[i]->type == ESEXP_RES_STRING)
			matched = camel_lite_medium_get_header (CAMEL_MEDIUM (message), argv[i]->value.string) != NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = matched;

	return r;
}

static ESExpResult *
header_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	CamelMimeMessage *message;
	regex_t pattern;
	const char *contents;

	message = camel_lite_filter_search_get_message (fms, f);

	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING
	    && (contents = camel_lite_medium_get_header (CAMEL_MEDIUM (message), argv[0]->value.string))
	    && camel_lite_search_build_match_regex(&pattern, CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_ICASE, argc-1, argv+1, fms->ex) == 0) {
		r->value.bool = regexec (&pattern, contents, 0, NULL, 0) == 0;
		regfree (&pattern);
	} else
		r->value.bool = FALSE;

	return r;
}

static gchar *
get_full_header (CamelMimeMessage *message)
{
	CamelMimePart *mp = CAMEL_MIME_PART (message);
	GString *str = g_string_new ("");
	char   *ret;
	struct _camel_lite_header_raw *h;

	for (h = mp->headers; h; h = h->next) {
		if (h->value != NULL) {
			g_string_append (str, h->name);
			if (isspace (h->value[0]))
				g_string_append (str, ":");
			else
				g_string_append (str, ": ");
			g_string_append (str, h->value);
			g_string_append_c(str, '\n');
		}
	}

	ret = str->str;
	g_string_free (str, FALSE);

	return ret;
}

static ESExpResult *
header_full_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	CamelMimeMessage *message;
	regex_t pattern;
	char *contents;

	if (camel_lite_search_build_match_regex(&pattern, CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_ICASE|CAMEL_SEARCH_MATCH_NEWLINE,
					   argc, argv, fms->ex) == 0) {
		message = camel_lite_filter_search_get_message (fms, f);
		contents = get_full_header (message);
		r->value.bool = regexec (&pattern, contents, 0, NULL, 0) == 0;
		g_free (contents);
		regfree (&pattern);
	} else
		r->value.bool = FALSE;

	return r;
}

static ESExpResult *
match_all (struct _ESExp *f, int argc, struct _ESExpTerm **argv, FilterMessageSearch *fms)
{
	/* match-all: when dealing with single messages is a no-op */
	ESExpResult *r;

	if (argc > 0)
		return e_sexp_term_eval (f, argv[0]);

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = TRUE;

	return r;
}

static ESExpResult *
body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	CamelMimeMessage *message;
	regex_t pattern;

	if (camel_lite_search_build_match_regex (&pattern, CAMEL_SEARCH_MATCH_ICASE, argc, argv, fms->ex) == 0) {
		message = camel_lite_filter_search_get_message (fms, f);
		r->value.bool = camel_lite_search_message_body_contains ((CamelDataWrapper *) message, &pattern);
		regfree (&pattern);
	} else
		r->value.bool = FALSE;

	return r;
}

static ESExpResult *
body_regex (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	CamelMimeMessage *message;
	regex_t pattern;

	if (camel_lite_search_build_match_regex(&pattern, CAMEL_SEARCH_MATCH_ICASE|CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_NEWLINE,
					   argc, argv, fms->ex) == 0) {
		message = camel_lite_filter_search_get_message (fms, f);
		r->value.bool = camel_lite_search_message_body_contains ((CamelDataWrapper *) message, &pattern);
		regfree (&pattern);
	} else
		r->value.bool = FALSE;

	return r;
}

static ESExpResult *
user_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	gboolean truth = FALSE;
	int i;

	/* performs an OR of all words */
	for (i = 0; i < argc && !truth; i++) {
		if (argv[i]->type == ESEXP_RES_STRING
		    && camel_lite_message_info_user_flag(fms->info, argv[i]->value.string)) {
			truth = TRUE;
			break;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

static ESExpResult *
system_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_STRING)
		e_sexp_fatal_error(f, _("Invalid arguments to (system-flag)"));

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = camel_lite_system_flag_get (camel_lite_message_info_flags(fms->info), argv[0]->value.string);

	return r;
}

static ESExpResult *
user_tag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	const char *tag;

	if (argc != 1 || argv[0]->type != ESEXP_RES_STRING)
		e_sexp_fatal_error(f, _("Invalid arguments to (user-tag)"));

	tag = camel_lite_message_info_user_tag(fms->info, argv[0]->value.string);

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = g_strdup (tag ? tag : "");

	return r;
}

static ESExpResult *
get_sent_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	CamelMimeMessage *message;
	ESExpResult *r;

	message = camel_lite_filter_search_get_message (fms, f);
	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = camel_lite_mime_message_get_date (message, NULL);

	return r;
}

static ESExpResult *
get_received_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	CamelMimeMessage *message;
	ESExpResult *r;

	message = camel_lite_filter_search_get_message (fms, f);
	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = camel_lite_mime_message_get_date_received (message, NULL);

	return r;
}

static ESExpResult *
get_current_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = time (NULL);

	return r;
}

static ESExpResult *
header_source (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	CamelMimeMessage *message;
	ESExpResult *r;
	const char *src;
	int truth = FALSE, i;
	CamelProvider *provider;
	CamelURL *uria, *urib;

	if (fms->source) {
		src = fms->source;
	} else {
		message = camel_lite_filter_search_get_message(fms, f);
		src = camel_lite_mime_message_get_source(message);
	}

	if (src
	    && (provider = camel_lite_provider_get(src, NULL))
	    && provider->url_equal) {
		uria = camel_lite_url_new(src, NULL);
		if (uria) {
			for (i=0;i<argc && !truth;i++) {
				if (argv[i]->type == ESEXP_RES_STRING
				    && (urib = camel_lite_url_new(argv[i]->value.string, NULL))) {
					truth = provider->url_equal(uria, urib);
					camel_lite_url_free(urib);
				}
			}
			camel_lite_url_free(uria);
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

/* remember, the size comparisons are done at Kbytes */
static ESExpResult *
get_size (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;

	r = e_sexp_result_new(f, ESEXP_RES_INT);
	r->value.number = camel_lite_message_info_size(fms->info) / 1024;

	return r;
}

#ifndef G_OS_WIN32
static void
child_setup_func (gpointer user_data)
{
	setsid ();
}
#else
#define child_setup_func NULL
#endif

typedef struct {
	gint child_status;
	GMainLoop *loop;
} child_watch_data_t;

static void
child_watch (GPid     pid,
	     gint     status,
	     gpointer data)
{
	child_watch_data_t *child_watch_data = data;

	g_spawn_close_pid (pid);

	child_watch_data->child_status = status;
	g_main_loop_quit (child_watch_data->loop);
}

static int
run_command (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	CamelMimeMessage *message;
	CamelStream *stream;
	int i;
	int pipe_to_child;
	GPid child_pid;
	GError *error = NULL;
	GPtrArray *args;
	child_watch_data_t child_watch_data;
	GSource *source;
	GMainContext *context;

	if (argc < 1 || argv[0]->value.string[0] == '\0')
		return 0;

	args = g_ptr_array_new ();
	for (i = 0; i < argc; i++)
		g_ptr_array_add (args, argv[i]->value.string);
	g_ptr_array_add (args, NULL);

	if (!g_spawn_async_with_pipes (NULL,
				       (gchar **) args->pdata,
				       NULL,
				       G_SPAWN_DO_NOT_REAP_CHILD |
				       G_SPAWN_SEARCH_PATH |
				       G_SPAWN_STDOUT_TO_DEV_NULL |
				       G_SPAWN_STDERR_TO_DEV_NULL,
				       child_setup_func,
				       NULL,
				       &child_pid,
				       &pipe_to_child,
				       NULL,
				       NULL,
				       &error)) {
		g_ptr_array_free (args, TRUE);

		camel_lite_exception_setv (fms->ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to create create child process '%s': %s"),
				      argv[0]->value.string, error->message);
		g_error_free (error);
		return -1;
	}

	g_ptr_array_free (args, TRUE);

	message = camel_lite_filter_search_get_message (fms, f);

	stream = camel_lite_stream_fs_new_with_fd (pipe_to_child);
	camel_lite_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), stream);
	camel_lite_stream_flush (stream);
	camel_lite_object_unref (stream);

	context = g_main_context_new ();
	child_watch_data.loop = g_main_loop_new (context, FALSE);
	g_main_context_unref (context);

	source = g_child_watch_source_new (child_pid);
	g_source_set_callback (source, (GSourceFunc) child_watch, &child_watch_data, NULL);
	g_source_attach (source, g_main_loop_get_context (child_watch_data.loop));
	g_source_unref (source);

	g_main_loop_run (child_watch_data.loop);
	g_main_loop_unref (child_watch_data.loop);

#ifndef G_OS_WIN32
	if (WIFEXITED (child_watch_data.child_status))
		return WEXITSTATUS (child_watch_data.child_status);
	else
		return -1;
#else
	return child_watch_data.child_status;
#endif
}

static ESExpResult *
pipe_message (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	int retval, i;

	/* make sure all args are strings */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type != ESEXP_RES_STRING) {
			retval = -1;
			goto done;
		}
	}

	retval = run_command (f, argc, argv, fms);

 done:
	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = retval;

	return r;
}

#ifdef NON_TINYMAIL_FEATURES
static ESExpResult *
junk_test (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	gboolean retval = FALSE;
	
	d(printf("doing junk test for message from '%s'\n", camel_lite_message_info_from (fms->info)));
	if (fms->session->junk_plugin != NULL) {
		CamelMessageInfo *info = fms->info;
		const GHashTable *ht = camel_lite_session_get_junk_headers (fms->session);
		struct _camel_lite_header_param *node = ((CamelMessageInfoBase *)info)->headers;

		while (node && !retval) {
			if (node->name) {
				char *value = (char *) g_hash_table_lookup ((GHashTable *) ht, node->name);
				d(printf("JunkCheckMatch: %s %s %s\n", node->name, node->value, value));
				if (value)
					retval = camel_lite_strstrcase(node->value, value) != NULL;
		
			}
			node = node->next;
		}
		if (camel_lite_debug ("junk"))
			printf("filtered based on junk header ? %d\n", retval);
		if (!retval) {
			retval = camel_lite_session_lookup_addressbook (fms->session, camel_lite_message_info_from (info)) != TRUE;
			if (camel_lite_debug ("junk"))
				printf("Sender '%s' in book? %d\n", camel_lite_message_info_from (info), !retval);
			
			if (retval) /* Not in book. Could be spam. So check for it*/ {
				d(printf("filtering message\n"));
				retval = camel_lite_junk_plugin_check_junk (fms->session->junk_plugin, camel_lite_filter_search_get_message (fms, f));
			}
		}

		if (camel_lite_debug ("junk"))
			printf("junk filter => %s\n", retval ? "*JUNK*" : "clean");
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.number = retval;

	return r;
}
#endif

/**
 * camel_lite_filter_search_match:
 * @session:
 * @get_message: function to retrieve the message if necessary
 * @data: data for above
 * @info:
 * @source:
 * @expression:
 * @ex:
 *
 * Returns one of CAMEL_SEARCH_MATCHED, CAMEL_SEARCH_NOMATCH, or CAMEL_SEARCH_ERROR.
 **/
int
camel_lite_filter_search_match (CamelSession *session,
			   CamelFilterSearchGetMessageFunc get_message, void *data,
			   CamelMessageInfo *info, const char *source,
			   const char *expression, CamelException *ex)
{
	FilterMessageSearch fms;
	ESExp *sexp;
	ESExpResult *result;
	gboolean retval;
	int i;

	fms.session = session;
	fms.get_message = get_message;
	fms.get_message_data = data;
	fms.message = NULL;
	fms.info = info;
	fms.source = source;
	fms.ex = ex;

	sexp = e_sexp_new ();

	for (i = 0; i < sizeof (symbols) / sizeof (symbols[0]); i++) {
		if (symbols[i].type == 1)
			e_sexp_add_ifunction (sexp, 0, symbols[i].name, (ESExpIFunc *)symbols[i].func, &fms);
		else
			e_sexp_add_function (sexp, 0, symbols[i].name, symbols[i].func, &fms);
	}

	e_sexp_input_text (sexp, expression, strlen (expression));
	if (e_sexp_parse (sexp) == -1) {
		if (!camel_lite_exception_is_set (ex))
			/* A filter search is a search through your filters, ie. your filters is the corpus being searched thru. */
			camel_lite_exception_setv (ex, 1, _("Error executing filter search: %s: %s"),
					      e_sexp_error (sexp), expression);
		goto error;
	}

	result = e_sexp_eval (sexp);
	if (result == NULL) {
		if (!camel_lite_exception_is_set (ex))
			camel_lite_exception_setv (ex, 1, _("Error executing filter search: %s: %s"),
					      e_sexp_error (sexp), expression);
		goto error;
	}

	if (result->type == ESEXP_RES_BOOL)
		retval = result->value.bool ? CAMEL_SEARCH_MATCHED : CAMEL_SEARCH_NOMATCH;
	else
		retval = CAMEL_SEARCH_NOMATCH;

	e_sexp_result_free (sexp, result);
	e_sexp_unref (sexp);

	if (fms.message)
		camel_lite_object_unref (fms.message);

	return retval;

 error:
	if (fms.message)
		camel_lite_object_unref (fms.message);

	e_sexp_unref (sexp);

	return CAMEL_SEARCH_ERROR;
}
