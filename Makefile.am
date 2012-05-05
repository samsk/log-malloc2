## log-malloc2:

## Place generated object files (.o) into the same directory as their source
## files, in order to avoid collisions when non-recursive make is used.
AUTOMAKE_OPTIONS = subdir-objects

## Additional flags to pass to aclocal when it is invoked automatically at
## make time. The ${ACLOCAL_FLAGS} variable is picked up from the environment
## to provide a way for the user to supply additional arguments.
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

## Define a libtool archive target "liblog-malloc2.la".
## The libtool archive file (.la) will be installed into the directory named
## by the predefined variable $(bindir), along with the actual shared library
## file (.so).
lib_LTLIBRARIES = liblog-malloc2.la

## Instruct libtool to include ABI version information in the generated shared
## library file (.so).
liblog_malloc2_la_LDFLAGS = -version-info $(LOG_MALLOC2_SO_VERSION)


## Define the source file list for the "liblog-malloc2.la" target.
liblog_malloc2_la_SOURCES = src/log-malloc2.c

## Define an independent executable script for inclusion in the distribution
## archive.  However, it will not be installed on an end user's system due to
## the noinst_ prefix.
dist_noinst_SCRIPTS = autogen.sh
