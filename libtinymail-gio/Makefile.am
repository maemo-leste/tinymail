INCLUDES = $(LIBTINYMAIL_GIO_CFLAGS) \
	-I. -I$(top_srcdir) $(EXTRA_CFLAGS) \
	-I$(top_srcdir)/libtinymail

lib_LTLIBRARIES = libtinymail-gio-1.0.la

libtinymail_gio_1_0_headers =		\
	tny-vfs-stream.h

libtinymail_gio_1_0_la_SOURCES = 		\
	$(libtinymail_gio_1_0_headers)	\
	tny-vfs-stream.c

libtinymail_gio_1_0_la_LIBADD =		\
	$(LIBTINYMAIL_GIO_LIBS)		\
	$(top_builddir)/libtinymail/libtinymail-$(API_VERSION).la

libtinymail_gio_1_0_la_LDFLAGS = -export-dynamic \
	-version-info $(LT_VERSION_INFO) $(LIBTOOL_EXPORT_OPTIONS)

libtinymail_gio_1_0includedir = $(includedir)/libtinymail-gio-$(API_VERSION)
libtinymail_gio_1_0include_HEADERS = $(libtinymail_gio_1_0_headers)

libtinymail-gio-$(API_VERSION).pc: libtinymail-gio.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libtinymail-gio-$(API_VERSION).pc
EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)
