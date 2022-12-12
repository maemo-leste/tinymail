INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	$(DB_CFLAGS)							\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"				\
	-DLOCALEDIR=\""$(localedir)"\"					\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"		\
	-DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\"			\
	-DE_DATA_SERVER_UI_GLADEDIR=\""$(gladedir)"\"			\
	$(E_DATA_SERVER_CFLAGS)

noinst_LTLIBRARIES = libedataserver-1.2.la

if USE_E_ARRAY
E_ARARAY_SOURCES = \
	e-array.c			\
	e-array.h
endif

libedataserver_1_2_la_SOURCES =		\
	e-trie.c			\
	e-iconv.c			\
	e-memory.c			\
	e-msgport.c			\
	e-sexp.c			\
	e-time-utils.c			\
	e-data-server-util.c		\
	$(E_ARARAY_SOURCES)		\
	md5-utils.c

libedataserverincludedir = $(includedir)/libedataserver-lite/libedataserver

libedataserverinclude_HEADERS =		\
	e-lite-trie.h			\
	e-lite-iconv.h			\
	e-lite-memory.h			\
	e-lite-msgport.h		\
	e-lite-sexp.h			\
	e-lite-time-utils.h		\
	e-lite-data-server-util.h	\
	md5-utils.h

