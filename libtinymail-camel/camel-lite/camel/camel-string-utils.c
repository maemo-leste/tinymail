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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <pthread.h>

#include "camel-string-utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>


static int
isdir (char *name)
{
	struct stat st;
	if (stat (name, &st))
		return 0;
	return S_ISDIR (st.st_mode);
}

static char *ignored_names[] = { ".", "..", NULL };
static int ignorent (char *name)
{
	char **p;
	for (p = ignored_names; *p; p++)
		if (strcmp (name, *p) == 0)
			return 1;
	return 0;
}

void camel_lite_du (char *name, int *my_size)
{
	DIR *dir;
	struct dirent *ent;

	if (!name)
		return;

	dir = opendir (name);

	if (!dir)
		return;

	while ((ent = readdir (dir)))
	{
		if (/*ent->d_name && */ !ignorent (ent->d_name)) {
			char *p = g_strdup_printf ("%s/%s", name, ent->d_name);
			if (isdir (p))
				camel_lite_du (p, my_size);
			else
			{
				struct stat st;
				if (stat (p, &st) == 0)
					*my_size += st.st_size;
			}
			g_free (p);
		}
	}

	closedir (dir);

}

static char *ignored_dnames[] = { ".", "..", NULL };
static int ignorentd (char *name)
{
	char **p;
	for (p = ignored_dnames; *p; p++)
		if (strcmp (name, *p) == 0)
			return 1;
	return 0;
}

void
camel_lite_rm (char *name)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir (name);

	if (!dir)
		return;

	while ((ent = readdir (dir)))
	{
		if (!ignorentd (ent->d_name))
		{
			char *p = g_strdup_printf ("%s/%s", name, ent->d_name);
			if (isdir (p))
				camel_lite_rm (p);
			else
				remove (p);
			g_free (p);
		}
	}

	closedir (dir);
	remove (name);
}

int
camel_lite_strcase_equal (gconstpointer a, gconstpointer b)
{
	return (g_ascii_strcasecmp ((const char *) a, (const char *) b) == 0);
}

guint
camel_lite_strcase_hash (gconstpointer v)
{
	const char *p = (char *) v;
	guint h = 0, g;

	for ( ; *p != '\0'; p++) {
		h = (h << 4) + g_ascii_toupper (*p);
		if ((g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h;
}


static void
free_string (gpointer string, gpointer user_data)
{
	g_free (string);
}

void
camel_lite_string_list_free (GList *string_list)
{
	if (string_list == NULL)
		return;

	g_list_foreach (string_list, free_string, NULL);
	g_list_free (string_list);
}

char *
camel_lite_strstrcase (const char *haystack, const char *needle)
{
	/* find the needle in the haystack neglecting case */
	const char *ptr;
	guint len;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	len = strlen (needle);
	if (len > strlen (haystack))
		return NULL;

	if (len == 0)
		return (char *) haystack;

	for (ptr = haystack; *(ptr + len - 1) != '\0'; ptr++)
		if (!g_ascii_strncasecmp (ptr, needle, len))
			return (char *) ptr;

	return NULL;
}


const char *
camel_lite_strdown (char *str)
{
	register char *s = str;

	while (*s) {
		if (*s >= 'A' && *s <= 'Z')
			*s += 0x20;
		s++;
	}

	return str;
}

/**
 * camel_lite_tolower:
 * @c:
 *
 * ASCII to-lower function.
 *
 * Return value:
 **/
char camel_lite_tolower(char c)
{
	if (c >= 'A' && c <= 'Z')
		c |= 0x20;

	return c;
}

/**
 * camel_lite_toupper:
 * @c:
 *
 * ASCII to-upper function.
 *
 * Return value:
 **/
char camel_lite_toupper(char c)
{
	if (c >= 'a' && c <= 'z')
		c &= ~0x20;

	return c;
}

#ifdef MEMDEBUG
static int cnt=0;
#endif

/* working stuff for pstrings */
static pthread_mutex_t pstring_lock = PTHREAD_MUTEX_INITIALIZER;
static GHashTable *pstring_table = NULL;

/**
 * camel_lite_pstring_add:
 * @str: string to add to the string pool
 * @own: whether the string pool will own the memory pointed to by @str, if @str is not yet in the pool
 *
 * Add the string to the pool.
 *
 * The NULL and empty strings are special cased to constant values.
 *
 * Return value: A pointer to an equivalent string of @s.  Use
 * camel_lite_pstring_free() when it is no longer needed.
 **/
const char *
camel_lite_pstring_add (char *str, gboolean own)
{
	void *pcount;
	char *pstr;
	int count;

	if (str == NULL)
		return NULL;

	if (str[0] == '\0') {
		if (own)
			g_free (str);
		return "";
	}

	pthread_mutex_lock (&pstring_lock);
	if (pstring_table == NULL)
		pstring_table = g_hash_table_new (g_str_hash, g_str_equal);

	if (g_hash_table_lookup_extended (pstring_table, str, (void **) &pstr, &pcount)) {
		count = GPOINTER_TO_INT (pcount) + 1;
		g_hash_table_insert (pstring_table, pstr, GINT_TO_POINTER (count));
		if (own)
			g_free (str);
	} else {
		pstr = own ? str : g_strdup (str);

#ifdef MEMDEBUG
		cnt++;
		printf ("%d\n", cnt);
#endif
		g_hash_table_insert (pstring_table, pstr, GINT_TO_POINTER (1));
	}

	pthread_mutex_unlock (&pstring_lock);

	return pstr;
}


/**
 * camel_lite_pstring_strdup:
 * @s: String to copy.
 *
 * Create a new pooled string entry for the string @s.  A pooled
 * string is a table where common strings are uniquified to the same
 * pointer value.  They are also refcounted, so freed when no longer
 * in use.  In a thread-safe manner.
 *
 * The NULL and empty strings are special cased to constant values.
 *
 * Return value: A pointer to an equivalent string of @s.  Use
 * camel_lite_pstring_free() when it is no longer needed.
 **/
const char *
camel_lite_pstring_strdup (const char *s)
{
	return camel_lite_pstring_add ((char *) s, FALSE);
}


/**
 * camel_lite_pstring_free:
 * @s: String to free.
 *
 * De-ref a pooled string. If no more refs exist to this string, it will be deallocated.
 *
 * NULL and the empty string are special cased.
 **/
void
camel_lite_pstring_free(const char *s)
{
	char *p;
	void *pcount;
	int count;

	if (pstring_table == NULL)
		return;
	if (s == NULL || s[0] == 0)
		return;

	pthread_mutex_lock(&pstring_lock);
	if (g_hash_table_lookup_extended(pstring_table, s, (void **)&p, &pcount)) {
		count = GPOINTER_TO_INT(pcount)-1;
		if (count == 0) {
			g_hash_table_remove(pstring_table, p);
			g_free(p);
#ifdef MEMDEBUG
			cnt--;
			printf ("%d\n", cnt);
#endif
		} else {
			g_hash_table_insert(pstring_table, p, GINT_TO_POINTER(count));
		}
	} else {
		g_warning("Trying to free string not allocated from the pool '%s'", s);
	}
	pthread_mutex_unlock(&pstring_lock);
}
