/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2001 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

/* lock helper process */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define SETEUID_SAVES (1)

/* we try and include as little as possible */

#include "camel-lock-helper.h"
#include "camel-lock.h"

#define d(x)

/* keeps track of open locks */
struct _lock_info {
	struct _lock_info *next;
	uid_t uid;
	int id;
	int depth;
	time_t stamp;		/* when last updated */
	char path[1];
};

static int lock_id = 0;
static struct _lock_info *lock_info_list;
static uid_t lock_root_uid = -1;
static uid_t lock_real_uid = -1;

/* utility functions */

static int read_n(int fd, void *buffer, int inlen)
{
	char *p = buffer;
	int len, left = inlen;

	do {
		len = read(fd, p, left);
		if (len == -1) {
			if (errno != EINTR)
				return -1;
		} else {
			left -= len;
			p += len;
		}
	} while (left > 0 && len != 0);

	return inlen - left;
}

static int write_n(int fd, void *buffer, int inlen)
{
	char *p = buffer;
	int len, left = inlen;

	do {
		len = write(fd, p, left);
		if (len == -1) {
			if (errno != EINTR)
				return -1;
		} else {
			left -= len;
			p += len;
		}
	} while (left > 0);

	return inlen;
}

void
camel_lite_exception_setv (CamelException *ex, ExceptionId id, const char *format, ...)
{
	;
}

void
camel_lite_exception_clear (CamelException *exception)
{
	;
}

char *gettext (const char *msgid);

char *
gettext (const char *msgid)
{
	return NULL;
}

static int lock_path(const char *path, guint32 *lockid)
{
	struct _lock_info *info = NULL;
	int res = CAMEL_LOCK_HELPER_STATUS_OK;
	struct stat st;

	d(fprintf(stderr, "locking path '%s' id = %d\n", path, lock_id));

	/* check to see if we have it locked already, make the lock 'recursive' */
	/* we could also error i suppose, but why bother */
	info = lock_info_list;
	while (info) {
		if (!strcmp(info->path, path)) {
			info->depth++;
			return CAMEL_LOCK_HELPER_STATUS_OK;
		}
		info = info->next;
	}

	/* check we are allowed to lock it, we must own it, be able to write to it, and it has to exist */
	if (stat(path, &st) == -1
	    || st.st_uid != getuid()
	    || !S_ISREG(st.st_mode)
	    || (st.st_mode & 0400) == 0) {
		return CAMEL_LOCK_HELPER_STATUS_INVALID;
	}

	info = malloc(sizeof(*info) + strlen(path));
	if (info == NULL) {
		res = CAMEL_LOCK_HELPER_STATUS_NOMEM;
		goto fail;
	}

	/* we try the real uid first, and if that fails, try the 'root id' */
	if (camel_lite_lock_dot(path, NULL) == -1) {
#ifdef SETEUID_SAVES
		if (lock_real_uid != lock_root_uid) {
			if (seteuid(lock_root_uid) != -1) {
				if (camel_lite_lock_dot(path, NULL) == -1) {
					seteuid(lock_real_uid);
					res = CAMEL_LOCK_HELPER_STATUS_SYSTEM;
					goto fail;
				}
				seteuid(lock_real_uid);
			} else {
				res = CAMEL_LOCK_HELPER_STATUS_SYSTEM;
				goto fail;
			}
		} else {
			res = CAMEL_LOCK_HELPER_STATUS_SYSTEM;
			goto fail;
		}
#else
		res = CAMEL_LOCK_HELPER_STATUS_SYSTEM;
		goto fail;
#endif
	} else {
		info->uid = lock_real_uid;
	}

	strcpy(info->path, path);
	info->id = lock_id;
	info->depth = 1;
	info->next = lock_info_list;
	info->stamp = time (NULL);
	lock_info_list = info;

	if (lockid)
		*lockid = lock_id;

	lock_id++;

	d(fprintf(stderr, "lock ok\n"));

	return res;
fail:
	d(fprintf(stderr, "lock failed\n"));

	if (info)
		free(info);

	return res;
}

static int unlock_id(guint32 lockid)
{
	struct _lock_info *info, *p;

	d(fprintf(stderr, "unlocking id '%d'\n", lockid));

	p = (struct _lock_info *)&lock_info_list;
	info = p->next;
	while (info) {
		if (info->id == lockid) {
			d(fprintf(stderr, "found id %d path '%s'\n", lockid, info->path));
			info->depth--;
			if (info->depth <= 0) {
#ifdef SETEUID_SAVES
				if (info->uid != lock_real_uid) {
					seteuid(lock_root_uid);
					camel_lite_unlock_dot(info->path);
					seteuid(lock_real_uid);
				} else
#endif
					camel_lite_unlock_dot(info->path);

				p->next = info->next;
				free(info);
			}

			return CAMEL_LOCK_HELPER_STATUS_OK;
		}
		p = info;
		info = info->next;
	}

	d(fprintf(stderr, "unknown id asked to be unlocked %d\n", lockid));
	return CAMEL_LOCK_HELPER_STATUS_PROTOCOL;
}

static void lock_touch(const char *path)
{
	char *name;

	/* we could also check that we haven't had our lock stolen from us here */

	name = alloca(strlen(path) + 10);
	sprintf(name, "%s.lock", path);

	d(fprintf(stderr, "Updating lock %s\n", name));
	utime(name, NULL);
}

static void setup_process(void)
{
	struct sigaction sa;
	sigset_t sigset;

	/* ignore sigint/sigio */
	sa.sa_handler = SIG_IGN;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGIO);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	sigaction (SIGIO, &sa, NULL);
	sigaction (SIGINT, &sa, NULL);

	/* FIXME: add more sanity checks/setup here */

#ifdef SETEUID_SAVES
	/* here we change to the real user id, this is probably not particularly
	   portable so may need configure checks */
	lock_real_uid = getuid();
	lock_root_uid = geteuid();
	if (lock_real_uid != lock_root_uid)
		seteuid(lock_real_uid);
#endif
}

int main(int argc, char **argv)
{
	struct _CamelLockHelperMsg msg;
	int len;
	int res;
	char *path;
	fd_set rset;
	struct timeval tv;
	struct _lock_info *info;

	setup_process();

	do {
		/* do a poll/etc, so we can refresh the .locks as required ... */
		FD_ZERO(&rset);
		FD_SET(STDIN_FILENO, &rset);

		/* check the minimum timeout we need to refresh the next oldest lock */
		if (lock_info_list) {
			time_t now = time (NULL);
			time_t left;
			time_t delay = CAMEL_DOT_LOCK_REFRESH;

			info = lock_info_list;
			while (info) {
				left = CAMEL_DOT_LOCK_REFRESH - (now - info->stamp);
				left = MAX(left, 0);
				delay = MIN(left, delay);
				info = info->next;
			}

			tv.tv_sec = delay;
			tv.tv_usec = 0;
		}

		d(fprintf(stderr, "lock helper waiting for input\n"));
		if (select(STDIN_FILENO+1, &rset, NULL, NULL, lock_info_list?&tv:NULL) == -1) {
			if (errno == EINTR)
				break;

			continue;
		}

		/* did we get a timeout?  scan for any locks that need updating */
		if (!FD_ISSET(STDIN_FILENO, &rset)) {
			time_t now = time (NULL);
			time_t left;

			d(fprintf(stderr, "Got a timeout, checking locks\n"));

			info = lock_info_list;
			while (info) {
				left = (now - info->stamp);
				if (left >= CAMEL_DOT_LOCK_REFRESH) {
					lock_touch(info->path);
					info->stamp = now;
				}
				info = info->next;
			}

			continue;
		}


		len = read_n(STDIN_FILENO, &msg, sizeof(msg));
		if (len == 0)
			break;

		res = CAMEL_LOCK_HELPER_STATUS_PROTOCOL;
		if (len == sizeof(msg) && msg.magic == CAMEL_LOCK_HELPER_MAGIC) {
			switch(msg.id) {
			case CAMEL_LOCK_HELPER_LOCK:
				res = CAMEL_LOCK_HELPER_STATUS_NOMEM;
				if (msg.data > 0xffff) {
					res = CAMEL_LOCK_HELPER_STATUS_PROTOCOL;
				} else if ((path = malloc(msg.data+1)) != NULL) {
					res = CAMEL_LOCK_HELPER_STATUS_PROTOCOL;
					len = read_n(STDIN_FILENO, path, msg.data);
					if (len == msg.data) {
						path[len] = 0;
						res = lock_path(path, &msg.data);
					}
					free(path);
				}
				break;
			case CAMEL_LOCK_HELPER_UNLOCK:
				res = unlock_id(msg.data);
				break;
			}
		}
		d(fprintf(stderr, "returning result %d\n", res));
		msg.id = res;
		msg.magic = CAMEL_LOCK_HELPER_RETURN_MAGIC;
		write_n(STDOUT_FILENO, &msg, sizeof(msg));
	} while (1);

	d(fprintf(stderr, "parent exited, clsoing down remaining id's\n"));
	while (lock_info_list)
		unlock_id(lock_info_list->id);

	return 0;
}
