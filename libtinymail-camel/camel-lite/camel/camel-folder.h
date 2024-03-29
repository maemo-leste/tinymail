/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * camel-folder.h: Abstract class for an email folder
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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

#ifndef CAMEL_FOLDER_H
#define CAMEL_FOLDER_H 1

#include <glib.h>
#include <camel/camel-object.h>
#include <camel/camel-folder-summary.h>

#define CAMEL_FOLDER_TYPE     (camel_lite_folder_get_type ())
#define CAMEL_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_FOLDER_TYPE, CamelFolder))
#define CAMEL_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_FOLDER_TYPE, CamelFolderClass))
#define CAMEL_IS_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_FOLDER_TYPE))


enum _CamelFolderReceiveType {
	CAMEL_FOLDER_RECEIVE_FULL = 1<<0,
	CAMEL_FOLDER_RECEIVE_PARTIAL = 1<<1,
	CAMEL_FOLDER_RECEIVE_ANY_OR_FULL = 1<<2,
	CAMEL_FOLDER_RECEIVE_ANY_OR_PARTIAL = 1<<3,
	CAMEL_FOLDER_RECEIVE_PART_SPEC = 1<<4
};

enum _CamelFolderPartState {
	CAMEL_FOLDER_PART_STATE_CACHED_BINARY,
	CAMEL_FOLDER_PART_STATE_CACHED_ENCODED,
	CAMEL_FOLDER_PART_STATE_CACHED_CONVERTED,
	CAMEL_FOLDER_PART_STATE_NOT_CACHED
};

typedef enum _CamelFolderReceiveType CamelFolderReceiveType;
typedef enum _CamelFolderPartState CamelFolderPartState;

G_BEGIN_DECLS

typedef struct _CamelFolderChangeInfo CamelFolderChangeInfo;

enum {
	CAMEL_FOLDER_ARG_FIRST = CAMEL_ARG_FIRST + 0x1000,
	CAMEL_FOLDER_ARG_NAME = CAMEL_FOLDER_ARG_FIRST,
	CAMEL_FOLDER_ARG_FULL_NAME,
	CAMEL_FOLDER_ARG_STORE,
	CAMEL_FOLDER_ARG_PERMANENTFLAGS,
	CAMEL_FOLDER_ARG_TOTAL,
	CAMEL_FOLDER_ARG_UNREAD, /* unread messages */
	CAMEL_FOLDER_ARG_DELETED, /* deleted messages */
	CAMEL_FOLDER_ARG_UID_ARRAY,
	CAMEL_FOLDER_ARG_INFO_ARRAY,
	CAMEL_FOLDER_ARG_PROPERTIES,
	CAMEL_FOLDER_ARG_LAST = CAMEL_ARG_FIRST + 0x2000
};

enum {
	CAMEL_FOLDER_NAME = CAMEL_FOLDER_ARG_NAME | CAMEL_ARG_STR,
	CAMEL_FOLDER_FULL_NAME = CAMEL_FOLDER_ARG_FULL_NAME | CAMEL_ARG_STR,
	CAMEL_FOLDER_STORE = CAMEL_FOLDER_ARG_STORE | CAMEL_ARG_OBJ,
	CAMEL_FOLDER_PERMANENTFLAGS = CAMEL_FOLDER_ARG_PERMANENTFLAGS | CAMEL_ARG_INT,
	CAMEL_FOLDER_TOTAL = CAMEL_FOLDER_ARG_TOTAL | CAMEL_ARG_INT,
	CAMEL_FOLDER_UNREAD = CAMEL_FOLDER_ARG_UNREAD | CAMEL_ARG_INT,
	CAMEL_FOLDER_DELETED = CAMEL_FOLDER_ARG_DELETED | CAMEL_ARG_INT,

	CAMEL_FOLDER_UID_ARRAY = CAMEL_FOLDER_ARG_UID_ARRAY | CAMEL_ARG_PTR,
	CAMEL_FOLDER_INFO_ARRAY = CAMEL_FOLDER_ARG_INFO_ARRAY | CAMEL_ARG_PTR,

	/* GSList of settable folder properties */
	CAMEL_FOLDER_PROPERTIES = CAMEL_FOLDER_ARG_PROPERTIES | CAMEL_ARG_PTR
};

struct _CamelFolderChangeInfo {
	GPtrArray *uid_added;
	GPtrArray *uid_removed;
	GPtrArray *uid_changed;
	GPtrArray *uid_recent;

	gboolean push_email_event;
	struct _CamelFolderChangeInfoPrivate *priv;
};

struct _CamelFolder {
	CamelObject parent_object;

	struct _CamelFolderPrivate *priv;

	/* get these via the :get() method, they might not be set otherwise */
	char *name;
	char *full_name;
	char *description;

	CamelStore *parent_store;
	CamelFolderSummary *summary;

	guint32 folder_flags;
	guint32 permanent_flags;
};

#define CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY (1<<0)
#define CAMEL_FOLDER_HAS_SEARCH_CAPABILITY  (1<<1)
#define CAMEL_FOLDER_FILTER_RECENT          (1<<2)
#define CAMEL_FOLDER_HAS_BEEN_DELETED       (1<<3)
#define CAMEL_FOLDER_IS_TRASH               (1<<4)
#define CAMEL_FOLDER_IS_JUNK                (1<<5)
#define CAMEL_FOLDER_FILTER_JUNK	    (1<<6)
#define CAMEL_FOLDER_IS_READONLY            (1<<7)
#define CAMEL_FOLDER_HAS_PUSHEMAIL_CAPABILITY (1<<8)

typedef struct {
	CamelObjectClass parent_class;

	/* Virtual methods */
	void   (*refresh_info) (CamelFolder *folder, CamelException *ex);

	void   (*sync) (CamelFolder *folder, gboolean expunge,
			CamelException *ex);

	const char *  (*get_name)  (CamelFolder *folder);
	const char *  (*get_full_name)   (CamelFolder *folder);

	CamelStore *  (*get_parent_store) (CamelFolder *folder);

	void (*expunge)  (CamelFolder *folder,
			  CamelException *ex);

	int   (*get_message_count)   (CamelFolder *folder);
	int   (*get_local_size)   (CamelFolder *folder);

	void (*append_message)  (CamelFolder *folder,
				 CamelMimeMessage *message,
				 const CamelMessageInfo *info,
				 char **appended_uid,
				 CamelException *ex);

	guint32 (*get_permanent_flags) (CamelFolder *folder);
	guint32 (*get_message_flags)   (CamelFolder *folder,
					const char *uid);
	gboolean (*set_message_flags)   (CamelFolder *folder,
					 const char *uid,
					 guint32 flags, guint32 set);

	gboolean (*get_message_user_flag) (CamelFolder *folder,
					   const char *uid,
					   const char *name);
	void     (*set_message_user_flag) (CamelFolder *folder,
					   const char *uid,
					   const char *name,
					   gboolean value);

	const char * (*get_message_user_tag) (CamelFolder *folder,
					      const char *uid,
					      const char *name);
	void     (*set_message_user_tag) (CamelFolder *folder,
					  const char *uid,
					  const char *name,
					  const char *value);

	CamelMimeMessage * (*get_message)  (CamelFolder *folder,
					    const char *uid, CamelFolderReceiveType type,
					    gint param,
					    CamelException *ex);

	GPtrArray * (*get_uids)       (CamelFolder *folder);
	void (*free_uids)             (CamelFolder *folder,
				       GPtrArray *array);

	GPtrArray * (*get_summary)    (CamelFolder *folder);
	void (*free_summary)          (CamelFolder *folder,
				       GPtrArray *summary);

	gboolean (*has_search_capability) (CamelFolder *folder);

	GPtrArray * (*search_by_expression) (CamelFolder *, const char *, CamelException *);
	GPtrArray * (*search_by_uids) (CamelFolder *, const char *, GPtrArray *uids, CamelException *);

	void (*search_free) (CamelFolder *folder, GPtrArray *result);

	CamelMessageInfo * (*get_message_info) (CamelFolder *, const char *uid);
	void (*ref_message_info) (CamelFolder *, CamelMessageInfo *);
	void (*free_message_info) (CamelFolder *, CamelMessageInfo *);

	void (*transfer_messages_to) (CamelFolder *source,
				      GPtrArray *uids,
				      CamelFolder *destination,
				      GPtrArray **transferred_uids,
				      gboolean delete_originals,
				      CamelException *ex);

	void (*delete)           (CamelFolder *folder);
	void (*rename)           (CamelFolder *folder, const char *newname);

	void     (*freeze)    (CamelFolder *folder);
	void     (*thaw)      (CamelFolder *folder);
	gboolean (*is_frozen) (CamelFolder *folder);
	void (*set_push_email) (CamelFolder *folder, gboolean setting);

	void (*delete_attachments) (CamelFolder *folder, const char *uid);
	void (*rewrite_cache) (CamelFolder *folder, const char *uid, CamelMimeMessage *msg);

	gboolean (*get_allow_external_images) (CamelFolder *folder, const char *uid);
	void (*set_allow_external_images) (CamelFolder *folder, const char *uid, gboolean allow);

	char* (*get_cache_filename) (CamelFolder *folder, const char *uid, const char *spec, CamelFolderPartState *state);
	char* (*fetch) (CamelFolder *folder, const char *uid, const char *spec, gboolean *binary, CamelException *ex);
	char* (*fetch_structure) (CamelFolder *folder, const char *uid, CamelException *ex);
	char* (*convert) (CamelFolder *folder, const char *uid, const char *spec, const char *convert_to, CamelException *ex);

} CamelFolderClass;

/* Standard Camel function */
CamelType camel_lite_folder_get_type (void);


/* public methods */
void               camel_lite_folder_construct              (CamelFolder *folder,
							CamelStore *parent_store,
							const char *full_name,
							const char *name);

void               camel_lite_folder_refresh_info           (CamelFolder *folder,
							CamelException *ex);
void               camel_lite_folder_sync                   (CamelFolder *folder,
							gboolean expunge,
							CamelException *ex);

CamelStore *       camel_lite_folder_get_parent_store       (CamelFolder *folder);


/* delete operations */
void		   camel_lite_folder_expunge                (CamelFolder *folder,
							CamelException *ex);


/* folder name operations */
const char *      camel_lite_folder_get_name                (CamelFolder *folder);
const char *      camel_lite_folder_get_full_name           (CamelFolder *folder);


/* various properties accessors */
guint32		   camel_lite_folder_get_permanent_flags    (CamelFolder *folder);

#ifndef CAMEL_DISABLE_DEPRECATED
guint32		   camel_lite_folder_get_message_flags      (CamelFolder *folder,
							const char *uid);

gboolean	   camel_lite_folder_set_message_flags      (CamelFolder *folder,
							const char *uid,
							guint32 flags,
							guint32 set);

gboolean	   camel_lite_folder_get_message_user_flag  (CamelFolder *folder,
							const char *uid,
							const char *name);

void		   camel_lite_folder_set_message_user_flag  (CamelFolder *folder,
							const char *uid,
							const char *name,
							gboolean value);

const char *	   camel_lite_folder_get_message_user_tag  (CamelFolder *folder,
						       const char *uid,
						       const char *name);

void		   camel_lite_folder_set_message_user_tag  (CamelFolder *folder,
						       const char *uid,
						       const char *name,
						       const char *value);
#endif



/* message manipulation */
void               camel_lite_folder_append_message         (CamelFolder *folder,
							CamelMimeMessage *message,
							const CamelMessageInfo *info,
							char **appended_uid,
							CamelException *ex);


/* summary related operations */
gboolean           camel_lite_folder_has_summary_capability (CamelFolder *folder);


int                camel_lite_folder_get_message_count     (CamelFolder *folder);

#ifndef CAMEL_DISABLE_DEPRECATED
int                camel_lite_folder_get_unread_message_count (CamelFolder *folder);
#endif

int                camel_lite_folder_get_deleted_message_count (CamelFolder *folder);

GPtrArray *        camel_lite_folder_get_summary           (CamelFolder *folder);
void               camel_lite_folder_free_summary          (CamelFolder *folder,
						       GPtrArray *array);

/* uid based access operations */
CamelMimeMessage * camel_lite_folder_get_message           (CamelFolder *folder,
						       const char *uid, CamelFolderReceiveType type,
						       gint param, CamelException *ex);
#define camel_lite_folder_delete_message(folder, uid) \
	camel_lite_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN)

GPtrArray *        camel_lite_folder_get_uids              (CamelFolder *folder);
void               camel_lite_folder_free_uids             (CamelFolder *folder,
						       GPtrArray *array);

/* search api */
gboolean           camel_lite_folder_has_search_capability (CamelFolder *folder);
GPtrArray *	   camel_lite_folder_search_by_expression  (CamelFolder *folder, const char *expr, CamelException *ex);
GPtrArray *	   camel_lite_folder_search_by_uids	      (CamelFolder *folder, const char *expr, GPtrArray *uids, CamelException *ex);
void		   camel_lite_folder_search_free	      (CamelFolder *folder, GPtrArray *result);

/* summary info */
CamelMessageInfo *camel_lite_folder_get_message_info		(CamelFolder *folder, const char *uid);
void		  camel_lite_folder_free_message_info	(CamelFolder *folder, CamelMessageInfo *info);
#ifndef CAMEL_DISABLE_DEPRECATED
void		  camel_lite_folder_ref_message_info		(CamelFolder *folder, CamelMessageInfo *info);
#endif

void               camel_lite_folder_transfer_messages_to   (CamelFolder *source,
							GPtrArray *uids,
							CamelFolder *dest,
							GPtrArray **transferred_uids,
							gboolean delete_originals,
							CamelException *ex);

void               camel_lite_folder_delete                 (CamelFolder *folder);
void               camel_lite_folder_rename                 (CamelFolder *folder, const char *new);

/* stop/restart getting events */
void               camel_lite_folder_freeze                (CamelFolder *folder);
void               camel_lite_folder_thaw                  (CamelFolder *folder);
gboolean           camel_lite_folder_is_frozen             (CamelFolder *folder);

/* For use by subclasses (for free_{uids,summary,subfolder_names}) */
void camel_lite_folder_free_nop     (CamelFolder *folder, GPtrArray *array);
void camel_lite_folder_free_shallow (CamelFolder *folder, GPtrArray *array);
void camel_lite_folder_free_deep    (CamelFolder *folder, GPtrArray *array);

/* update functions for change info */
CamelFolderChangeInfo *	camel_lite_folder_change_info_new		(void);
void			camel_lite_folder_change_info_clear		(CamelFolderChangeInfo *info);
void			camel_lite_folder_change_info_free		(CamelFolderChangeInfo *info);
gboolean		camel_lite_folder_change_info_changed	(CamelFolderChangeInfo *info);

/* for building diff's automatically */
void			camel_lite_folder_change_info_add_source	(CamelFolderChangeInfo *info, const char *uid);
void			camel_lite_folder_change_info_add_source_list(CamelFolderChangeInfo *info, const GPtrArray *list);
void			camel_lite_folder_change_info_add_update	(CamelFolderChangeInfo *info, const char *uid);
void			camel_lite_folder_change_info_add_update_list(CamelFolderChangeInfo *info, const GPtrArray *list);
void			camel_lite_folder_change_info_build_diff	(CamelFolderChangeInfo *info);

/* for manipulating diff's directly */
void			camel_lite_folder_change_info_cat		(CamelFolderChangeInfo *info, CamelFolderChangeInfo *src);
void			camel_lite_folder_change_info_add_uid	(CamelFolderChangeInfo *info, const char *uid);
void			camel_lite_folder_change_info_remove_uid	(CamelFolderChangeInfo *info, const char *uid);
void			camel_lite_folder_change_info_change_uid	(CamelFolderChangeInfo *info, const char *uid);
void			camel_lite_folder_change_info_recent_uid	(CamelFolderChangeInfo *info, const char *uid);

void camel_lite_folder_set_push_email (CamelFolder *folder, gboolean setting);

int camel_lite_folder_get_local_size   (CamelFolder *folder);

void camel_lite_folder_delete_attachments (CamelFolder *folder, const char *uid);
void camel_lite_folder_rewrite_cache (CamelFolder *folder, const char *uid, CamelMimeMessage *msg);

gboolean camel_lite_folder_get_allow_external_images (CamelFolder *folder, const char *uid);
void camel_lite_folder_set_allow_external_images (CamelFolder *folder, const char *uid, gboolean allow);

char* camel_lite_folder_fetch (CamelFolder *folder, const char *uid, const char *spec, gboolean *binary, CamelException *ex);
char* camel_lite_folder_fetch_structure (CamelFolder *folder, const char *uid, CamelException *ex);
char* camel_lite_folder_get_cache_filename (CamelFolder *folder, const char *uid, const char *spec, CamelFolderPartState *state);
char* camel_lite_folder_convert (CamelFolder *folder, const char *uid, const char *spec, const char *convert_to, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_FOLDER_H */
