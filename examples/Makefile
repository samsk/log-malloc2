#
# Makefile 4 examples

CFLAGS2 = -Wall -rdynamic -fPIC -g -I../include

ifeq ($(USE_PKGCONFIG),1)
CFLAGS_API = $(shell pkg-config --cflags log-malloc2)
LDLIBS_API = $(shell pkg-config --libs log-malloc2)
else
CWD := $(shell pwd)
CFLAGS_API = -I../include
LDLIBS_API = -L../.libs -llog-malloc2 -Wl,-rpath,$(realpath $(CWD)/../.libs)
endif


PROGRAMS = leak-01 leak-02 leak-03 api-01 segv-01

# make tar
.PHONY: all
all: $(PROGRAMS)

api-%: api-%.c
	$(CC) $(CFLAGS) $(CFLAGS2) $(CFLAGS_API) $(LDLIBS_API) $^ -o $@

leak-%: leak-%.c
	$(CC) $(CFLAGS) $(CFLAGS2) $^ -o $@

segv-%: segv-%.c
	$(CC) $(CFLAGS) $(CFLAGS2) $^ -o $@

clean:
	rm -f $(PROGRAMS)

#EOF
