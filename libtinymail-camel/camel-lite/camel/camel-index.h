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

#ifndef _CAMEL_INDEX_H
#define _CAMEL_INDEX_H

#include <camel/camel-exception.h>
#include <camel/camel-object.h>

#define CAMEL_INDEX(obj)         CAMEL_CHECK_CAST (obj, camel_lite_index_get_type (), CamelIndex)
#define CAMEL_INDEX_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_index_get_type (), CamelIndexClass)
#define CAMEL_IS_INDEX(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_index_get_type ())

G_BEGIN_DECLS

typedef struct _CamelIndex      CamelIndex;
typedef struct _CamelIndexClass CamelIndexClass;

#define CAMEL_INDEX_NAME(obj)         CAMEL_CHECK_CAST (obj, camel_lite_index_name_get_type (), CamelIndexName)
#define CAMEL_INDEX_NAME_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_index_name_get_type (), CamelIndexNameClass)
#define CAMEL_IS_INDEX_NAME(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_index_name_get_type ())

typedef struct _CamelIndexName      CamelIndexName;
typedef struct _CamelIndexNameClass CamelIndexNameClass;

#define CAMEL_INDEX_CURSOR(obj)         CAMEL_CHECK_CAST (obj, camel_lite_index_cursor_get_type (), CamelIndexCursor)
#define CAMEL_INDEX_CURSOR_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_lite_index_cursor_get_type (), CamelIndexCursorClass)
#define CAMEL_IS_INDEX_CURSOR(obj)      CAMEL_CHECK_TYPE (obj, camel_lite_index_cursor_get_type ())

typedef struct _CamelIndexCursor      CamelIndexCursor;
typedef struct _CamelIndexCursorClass CamelIndexCursorClass;

typedef char * (*CamelIndexNorm)(CamelIndex *idx, const char *word, void *data);

/* ********************************************************************** */

struct _CamelIndexCursor {
	CamelObject parent;

	struct _CamelIndexCursorPrivate *priv;

	CamelIndex *index;
};

struct _CamelIndexCursorClass {
	CamelObjectClass parent;

	const char * (*next) (CamelIndexCursor *idc);
	void         (*reset) (CamelIndexCursor *idc);
};

CamelType	           camel_lite_index_cursor_get_type(void);

CamelIndexCursor  *camel_lite_index_cursor_new(CamelIndex *idx, const char *name);

const char        *camel_lite_index_cursor_next(CamelIndexCursor *idc);
void               camel_lite_index_cursor_reset(CamelIndexCursor *idc);

/* ********************************************************************** */

struct _CamelIndexName {
	CamelObject parent;

	struct _CamelIndexNamePrivate *priv;

	CamelIndex *index;

	char *name;		/* name being indexed */

	GByteArray *buffer;	/* used for normalisation */
	GHashTable *words;	/* unique list of words */
};

struct _CamelIndexNameClass {
	CamelObjectClass parent;

	int (*sync)(CamelIndexName *name);
	void (*add_word)(CamelIndexName *name, const char *word);
	size_t (*add_buffer)(CamelIndexName *name, const char *buffer, size_t len);
};

CamelType	           camel_lite_index_name_get_type	(void);

CamelIndexName    *camel_lite_index_name_new(CamelIndex *idx, const char *name);

void               camel_lite_index_name_add_word(CamelIndexName *name, const char *word);
size_t             camel_lite_index_name_add_buffer(CamelIndexName *name, const char *buffer, size_t len);

/* ********************************************************************** */

struct _CamelIndex {
	CamelObject parent;

	struct _CamelIndexPrivate *priv;

	char *path;
	guint32 version;
	guint32 flags;		/* open flags */
	guint32 state;

	CamelIndexNorm normalise;
	void *normalise_data;
};

struct _CamelIndexClass {
	CamelObjectClass parent_class;

	int			(*sync)(CamelIndex *idx);
	int			(*compress)(CamelIndex *idx);
	int			(*delete)(CamelIndex *idx);

	int			(*rename)(CamelIndex *idx, const char *path);

	int 			(*has_name)(CamelIndex *idx, const char *name);
	CamelIndexName *	(*add_name)(CamelIndex *idx, const char *name);
	int			(*write_name)(CamelIndex *idx, CamelIndexName *idn);
	CamelIndexCursor *	(*find_name)(CamelIndex *idx, const char *name);
	void 			(*delete_name)(CamelIndex *idx, const char *name);
	CamelIndexCursor * 	(*find)(CamelIndex *idx, const char *word);

	CamelIndexCursor *      (*words)(CamelIndex *idx);
	CamelIndexCursor *      (*names)(CamelIndex *idx);
};

/* flags, stored in 'state', set with set_state */
#define CAMEL_INDEX_DELETED (1<<0)

CamelType	           camel_lite_index_get_type	(void);

CamelIndex        *camel_lite_index_new(const char *path, int flags);
void               camel_lite_index_construct(CamelIndex *, const char *path, int flags);
int		   camel_lite_index_rename(CamelIndex *, const char *path);

void               camel_lite_index_set_normalise(CamelIndex *idx, CamelIndexNorm func, void *data);

int                camel_lite_index_sync(CamelIndex *idx);
int                camel_lite_index_compress(CamelIndex *idx);
int		   camel_lite_index_delete(CamelIndex *idx);

int                camel_lite_index_has_name(CamelIndex *idx, const char *name);
CamelIndexName    *camel_lite_index_add_name(CamelIndex *idx, const char *name);
int                camel_lite_index_write_name(CamelIndex *idx, CamelIndexName *idn);
CamelIndexCursor  *camel_lite_index_find_name(CamelIndex *idx, const char *name);
void               camel_lite_index_delete_name(CamelIndex *idx, const char *name);
CamelIndexCursor  *camel_lite_index_find(CamelIndex *idx, const char *word);

CamelIndexCursor  *camel_lite_index_words(CamelIndex *idx);
CamelIndexCursor  *camel_lite_index_names(CamelIndex *idx);

G_END_DECLS

#endif /* ! _CAMEL_INDEX_H */
