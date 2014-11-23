## log-malloc2:

# general
AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

## libtool archive target "liblog-malloc2.la".
lib_LTLIBRARIES = liblog-malloc2.la

## libtool to include ABI version information in the generated shared
liblog_malloc2_la_LDFLAGS = -version-info $(LOG_MALLOC2_SO_VERSION)

## source file list for the "liblog-malloc2.la" target.
liblog_malloc2_la_SOURCES = src/log-malloc2.c src/log-malloc2_api.c src/log-malloc2_internal.h

## includes
AM_CPPFLAGS = -I$(top_srcdir)/include
pkginclude_HEADERS = include/log-malloc2.h include/log-malloc2_util.h

## scripts
dist_libexec_SCRIPTS = scripts/backtrace2line.pl scripts/log-malloc.pl \
                scripts/log-malloc-findleak.pl scripts/log-malloc-trackusage.pl
libexec_SCRIPTS = scripts/log-malloc.pm

install-exec-hook:
	for i in $(dist_libexec_SCRIPTS); do bn=`basename $${i}`;\
		$(LN_S) -f "${libexecdir}/$${bn}" "$(bindir)/$${bn%.pl}"; done;

## pkconfig
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = log-malloc2.pc

## scripts dist
dist_noinst_SCRIPTS = autogen.sh

## examples
dist_noinst_DATA = examples/Makefile examples/*.c

# manpages
scripts_man_MANS = $(dist_libexec_SCRIPTS:.pl=.1)
man_MANS = $(scripts_man_MANS)
%.1: %.pl
	pod2man --release='$(PACKAGE_VERSION)' --center='$(PACKAGE_NAME)' \
		--section=1 $< > $@
CLEANFILES = $(man_MANS)

## test
.PHONY: test
