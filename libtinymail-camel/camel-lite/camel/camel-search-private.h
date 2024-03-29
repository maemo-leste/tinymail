/*
 *  Copyright (C) 2001 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_SEARCH_PRIVATE_H
#define _CAMEL_SEARCH_PRIVATE_H

/* POSIX requires <sys/types.h> be included before <regex.h> */
#include <sys/types.h>

#include <regex.h>

#include "camel-exception.h"
#include "libedataserver/e-lite-sexp.h"

G_BEGIN_DECLS

typedef enum {
	CAMEL_SEARCH_MATCH_START = 1<<0,
	CAMEL_SEARCH_MATCH_END = 1<<1,
	CAMEL_SEARCH_MATCH_REGEX = 1<<2, /* disables the first 2 */
	CAMEL_SEARCH_MATCH_ICASE = 1<<3,
	CAMEL_SEARCH_MATCH_NEWLINE = 1<<4
} camel_lite_search_flags_t;

typedef enum {
	CAMEL_SEARCH_MATCH_EXACT,
	CAMEL_SEARCH_MATCH_CONTAINS,
	CAMEL_SEARCH_MATCH_STARTS,
	CAMEL_SEARCH_MATCH_ENDS,
	CAMEL_SEARCH_MATCH_SOUNDEX
} camel_lite_search_match_t;

typedef enum {
	CAMEL_SEARCH_TYPE_ASIS,
	CAMEL_SEARCH_TYPE_ENCODED,
	CAMEL_SEARCH_TYPE_ADDRESS,
	CAMEL_SEARCH_TYPE_ADDRESS_ENCODED,
	CAMEL_SEARCH_TYPE_MLIST /* its a mailing list pseudo-header */
} camel_lite_search_t;

/* builds a regex that represents a string search */
int camel_lite_search_build_match_regex(regex_t *pattern, camel_lite_search_flags_t type, int argc, void *argv, CamelException *ex);
gboolean camel_lite_search_message_body_contains(CamelDataWrapper *object, regex_t *pattern);

gboolean camel_lite_search_header_match(const char *value, const char *match, camel_lite_search_match_t how, camel_lite_search_t type, const char *default_charset);
gboolean camel_lite_search_camel_lite_header_soundex(const char *header, const char *match);

/* TODO: replace with a real search function */
const char *camel_lite_ustrstrcase(const char *haystack, const char *needle);

/* Some crappy utility functions for handling multiple search words */
typedef enum _camel_lite_search_word_t {
	CAMEL_SEARCH_WORD_SIMPLE = 1,
	CAMEL_SEARCH_WORD_COMPLEX = 2,
	CAMEL_SEARCH_WORD_8BIT = 4
} camel_lite_search_word_t;

struct _camel_lite_search_word {
	camel_lite_search_word_t type;
	char *word;
};

struct _camel_lite_search_words {
	int len;
	camel_lite_search_word_t type;	/* OR of all word types in list */
	struct _camel_lite_search_word **words;
};

struct _camel_lite_search_words *camel_lite_search_words_split(const unsigned char *in);
struct _camel_lite_search_words *camel_lite_search_words_simple(struct _camel_lite_search_words *wordin);
void camel_lite_search_words_free(struct _camel_lite_search_words *);

G_END_DECLS

#endif /* ! _CAMEL_SEARCH_PRIVATE_H */
