
#ifndef _CAMEL_IMAPP_ENGINE_H
#define _CAMEL_IMAPP_ENGINE_H

#include <camel/camel-object.h>

#include "camel-imapp-stream.h"
#include <libedataserver/e-lite-msgport.h>
#include "camel-imapp-folder.h"

#define CAMEL_IMAPP_ENGINE_TYPE     (camel_lite_imapp_engine_get_type ())
#define CAMEL_IMAPP_ENGINE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAPP_ENGINE_TYPE, CamelIMAPPEngine))
#define CAMEL_IMAPP_ENGINE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAPP_ENGINE_TYPE, CamelIMAPPEngineClass))
#define CAMEL_IS_IMAP_ENGINE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAPP_ENGINE_TYPE))

G_BEGIN_DECLS

typedef struct _CamelIMAPPEngine CamelIMAPPEngine;
typedef struct _CamelIMAPPEngineClass CamelIMAPPEngineClass;

typedef struct _CamelIMAPPCommandPart CamelIMAPPCommandPart;
typedef struct _CamelIMAPPCommand CamelIMAPPCommand;

typedef enum {
	CAMEL_IMAPP_COMMAND_SIMPLE = 0,
	CAMEL_IMAPP_COMMAND_DATAWRAPPER,
	CAMEL_IMAPP_COMMAND_STREAM,
	CAMEL_IMAPP_COMMAND_AUTH,
	CAMEL_IMAPP_COMMAND_MASK = 0xff,
	CAMEL_IMAPP_COMMAND_CONTINUATION = 0x8000 /* does this command expect continuation? */
} camel_lite_imapp_command_part_t;

struct _CamelIMAPPCommandPart {
	struct _CamelIMAPPCommandPart *next;
	struct _CamelIMAPPCommandPart *prev;

	struct _CamelIMAPPCommand *parent;

	int data_size;
	char *data;

	camel_lite_imapp_command_part_t type;

	int ob_size;
	CamelObject *ob;
};

typedef int (*CamelIMAPPEngineFunc)(struct _CamelIMAPPEngine *engine, guint32 id, void *data);
typedef void (*CamelIMAPPCommandFunc)(struct _CamelIMAPPEngine *engine, struct _CamelIMAPPCommand *, void *data);

struct _CamelIMAPPCommand {
	EMsg msg;

	const char *name;	/* command name/type (e.g. FETCH) */

	/* FIXME: remove this select stuff */
	char *select;		/* if we need to run against a specific folder */
	struct _status_info *status; /* status for command, indicates it is complete if != NULL */

	unsigned int tag;

	struct _CamelStreamMem *mem;	/* for building the part */
	EDList parts;
	CamelIMAPPCommandPart *current;

	CamelIMAPPCommandFunc complete;
	void *complete_data;
};

typedef struct _CamelIMAPPSelectResponse CamelIMAPPSelectResponse;

struct _CamelIMAPPSelectResponse {
	struct _status_info *status;
	guint32 exists;
	guint32 recent;
	guint32 uidvalidity;
	guint32 unseen;
	guint32 permanentflags;
};

enum {
	IMAP_CAPABILITY_IMAP4			= (1 << 0),
	IMAP_CAPABILITY_IMAP4REV1		= (1 << 1),
	IMAP_CAPABILITY_STATUS			= (1 << 2),
	IMAP_CAPABILITY_NAMESPACE		= (1 << 3),
	IMAP_CAPABILITY_UIDPLUS			= (1 << 4),
	IMAP_CAPABILITY_LITERALPLUS		= (1 << 5),
	IMAP_CAPABILITY_STARTTLS                = (1 << 6),
};

/* currently selected states */
typedef enum _camel_lite_imapp_engine_state_t {
	IMAP_ENGINE_DISCONNECT,	/* only happens during shutdown */
	IMAP_ENGINE_CONNECT,	/* connected, not authenticated */
	IMAP_ENGINE_AUTH,	/* connected, and authenticated */
	IMAP_ENGINE_SELECT,	/* and selected, select holds selected folder */
} camel_lite_imapp_engine_state_t;

struct _CamelIMAPPEngine {
	CamelObject parent_object;

	/* incoming requests */
	EMsgPort *port;

	CamelIMAPPStream *stream;

	camel_lite_imapp_engine_state_t state;

	guint32 capa;		/* capabilities for this server, refresh with :capabilities() */

	GHashTable *handlers;

	unsigned char tagprefix; /* out tag prefix 'A' 'B' ... 'Z' */
	unsigned int tag;	/* next command tag */

	char *select;		/* *currently* selected folder */
	char *last_select;	/* last selected or to-be selected folder (e.g. outstanding queued select) */
	CamelIMAPPCommand *literal;/* current literal op */
	EDList active;		/* active queue */
	EDList queue;		/* outstanding queue */
	EDList done;		/* done queue, awaiting reclamation */

	/* keep track of running a select */
	struct _CamelIMAPPSelectResponse *select_response;
};

struct _CamelIMAPPEngineClass {
	CamelObjectClass parent_class;

	unsigned char tagprefix;

	/* Events:
	   status(struct _status_info *);
	*/
};

CamelType       camel_lite_imapp_engine_get_type (void);

CamelIMAPPEngine  *camel_lite_imapp_engine_new(CamelIMAPPStream *stream);

void		camel_lite_imapp_engine_add_handler(CamelIMAPPEngine *imap, const char *response, CamelIMAPPEngineFunc func, void *data);
int 		camel_lite_imapp_engine_iterate(CamelIMAPPEngine *imap, CamelIMAPPCommand *wait); /* throws PARSE,IO exception */
int		camel_lite_imapp_engine_skip(CamelIMAPPEngine *imap);
int		camel_lite_imapp_engine_capabilities(CamelIMAPPEngine *imap);

CamelIMAPPCommand *camel_lite_imapp_engine_command_new  (CamelIMAPPEngine *imap, const char *name, const char *select, const char *fmt, ...);
void		camel_lite_imapp_engine_command_complete(CamelIMAPPEngine *imap, struct _CamelIMAPPCommand *, CamelIMAPPCommandFunc func, void *data);
void            camel_lite_imapp_engine_command_add  (CamelIMAPPEngine *imap, CamelIMAPPCommand *ic, const char *fmt, ...);
void            camel_lite_imapp_engine_command_free (CamelIMAPPEngine *imap, CamelIMAPPCommand *ic);
void 		camel_lite_imapp_engine_command_queue(CamelIMAPPEngine *imap, CamelIMAPPCommand *ic); /* throws IO exception */
CamelIMAPPCommand *camel_lite_imapp_engine_command_find (CamelIMAPPEngine *imap, const char *name);
CamelIMAPPCommand *camel_lite_imapp_engine_command_find_tag(CamelIMAPPEngine *imap, unsigned int tag);

/* util functions */
CamelIMAPPSelectResponse *camel_lite_imapp_engine_select(CamelIMAPPEngine *imap, const char *name);
void camel_lite_imapp_engine_select_free(CamelIMAPPEngine *imap, CamelIMAPPSelectResponse *select);

G_END_DECLS

#endif
