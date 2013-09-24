/*
 * log-malloc2 
 *	Malloc logging library with backtrace and byte-exact memory tracking.
 * 
 * Author: Samuel Behan <_sam_(at)_dob_._sk> (C) 2011-2012
 *	   partialy based on log-malloc from Ivan Tikhonov
 *
 * License: GNU LGPLv3 (http://www.gnu.org/licenses/lgpl.html)
 *
 * Web:
 *	http://devel.dob.sk/log-malloc2
 *	http://sam.blog.dob.sk/category/devel/log-malloc2 (blog)
 *
 */

/* Copyright (C) 2007 Ivan Tikhonov
   Ivan Tikhonov, http://www.brokestream.com, kefeer@netangels.ru

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Ivan Tikhonov, kefeer@brokestream.com

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <execinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <dlfcn.h>

/* handler declarations */
static void *(*real_malloc)(size_t size)	= NULL;
static void  (*real_free)(void *ptr)		= NULL;
static void *(*real_realloc)(void *ptr, size_t size)	= NULL;
static void *(*real_calloc)(size_t nmemb, size_t size)	= NULL;
static void *(*real_memalign)(size_t boundary, size_t size)	= NULL;
static int   (*real_posix_memalign)(void **memptr, size_t alignment, size_t size)	= NULL;
static void *(*real_valloc)(size_t size)	= NULL;

/**
  size       total program size (same as VmSize in /proc/[pid]/status)
  resident   resident set size (same as VmRSS in /proc/[pid]/status)
  share      shared pages (from shared mappings)
  text       text (code)
  lib        library (unused in Linux 2.6)
  data       data + stack
  dt         dirty pages (unused in Linux 2.6)
*/
const char* statm_path = "/proc/self/statm";

/* memtracking */
struct log_malloc_s {
	size_t size;		/* allocation size */
	size_t cb;		/* size check bits */
#ifdef HAVE_MALLOC_USABLE_SIZE
	size_t rsize;		/* really allocated size */
#endif
	char   ptr[0] __attribute__ ((aligned (__BIGGEST_ALIGNMENT__)));	/* user memory begin */
};
#define MEM_OFF       (sizeof(struct log_malloc_s))

#define MEM_PTR(mem)  (mem != NULL ? ((void *)(((void *)(mem)) + MEM_OFF)) : NULL)
#define MEM_HEAD(ptr) ((struct log_malloc_s *)(((void *)(ptr)) - MEM_OFF))

/* pthreads support */
#ifdef HAVE_LIBPTHREAD
#include <pthread.h>
#endif 

/* global context */
static struct {
	sig_atomic_t init_done;
	int memlog_fd;
	int statm_fd;
        sig_atomic_t mem_used;
#ifdef HAVE_MALLOC_USABLE_SIZE
	sig_atomic_t mem_rused;
#endif
	struct {
		sig_atomic_t malloc;
		sig_atomic_t calloc;
		sig_atomic_t realloc;
		sig_atomic_t memalign;
		sig_atomic_t posix_memalign;
		sig_atomic_t valloc;
		sig_atomic_t free;
	} stat;
#ifdef HAVE_LIBPTHREAD
	pthread_mutex_t loglock;
#endif
} g_ctx = {
	0,
	1022,
	-1,
	0,
#ifdef HAVE_MALLOC_USABLE_SIZE
	0,
#endif
	{0, 0, 0, 0, 0},
#ifdef HAVE_LIBPTHREAD
	PTHREAD_MUTEX_INITIALIZER,
#endif
};

/* shortcuts */
#ifdef HAVE_LIBPTHREAD
//# define LOCK_INIT	(pthread_mutex_init(&g_ctx.loglock, 0))
# define LOCK_INIT	1
# define LOCK		(pthread_mutex_trylock(&g_ctx.loglock) == 0)
# define UNLOCK		(pthread_mutex_unlock(&g_ctx.loglock))
#else
# define LOCK_INIT	1
# define LOCK		1
# define UNLOCK		0
#endif

/* config */
#define LOG_BUFSIZE	128
#define BACKTRACE_COUNT 7

/* DL resolving */
#define DL_RESOLVE(fn)	\
	((!real_ ## fn) ? (real_ ## fn = dlsym(RTLD_NEXT, # fn)) : ((void *)0x1))
#define DL_RESOLVE_CHECK(fn)	\
	((!real_ ## fn) ? __init_lib() : ((void *)0x1))

static void *__init_lib()
{
	int s, w;
	char buf[LOG_BUFSIZE];

	/* check already initialized */
	if(!__sync_bool_compare_and_swap(&g_ctx.init_done, 0, 1))
		return NULL;

	LOCK_INIT;

	fprintf(stderr, "\n *** log-malloc trace-fd = %d *** \n\n",
		g_ctx.memlog_fd);

	/* open statm */
	if(statm_path[0] != '\0' && (g_ctx.statm_fd = open(statm_path, 0)) == -1)
		fprintf(stderr, "\n*** log-malloc: could not open %s\n\n", statm_path);

	/* get real functions pointers */
	DL_RESOLVE(malloc);
	DL_RESOLVE(calloc);
	DL_RESOLVE(free);
	DL_RESOLVE(realloc);
	DL_RESOLVE(memalign);
	DL_RESOLVE(posix_memalign);
	DL_RESOLVE(valloc);

	/* post-init status */
	s = snprintf(buf, sizeof(buf), "+ INIT [%u:%u] malloc=%u calloc=%u realloc=%u memalign=%u/%u valloc=%u free=%u\n",
			g_ctx.mem_used, g_ctx.mem_rused,
			g_ctx.stat.malloc, g_ctx.stat.calloc, g_ctx.stat.realloc,
			g_ctx.stat.memalign, g_ctx.stat.posix_memalign,
			g_ctx.stat.valloc,
			g_ctx.stat.free);
	w = write(g_ctx.memlog_fd, buf, s);

	return (void *)0x1;
}

void __attribute__ ((constructor)) _init (void)
{
	__init_lib();
  	return;
}

static void __fini_lib()
{
	int s, w;
	char buf[LOG_BUFSIZE];

	s = snprintf(buf, sizeof(buf), "+ FINI [%u:%u] malloc=%u calloc=%u realloc=%u memalign=%u/%u valloc=%u free=%u\n",
			g_ctx.mem_used, g_ctx.mem_rused,
			g_ctx.stat.malloc, g_ctx.stat.calloc, g_ctx.stat.realloc,
			g_ctx.stat.memalign, g_ctx.stat.posix_memalign,
			g_ctx.stat.valloc,
			g_ctx.stat.free);
	w = write(g_ctx.memlog_fd, buf, s);

	if(g_ctx.statm_fd != -1)
		close(g_ctx.statm_fd);

	return;
}

void __attribute__ ((constructor)) _fini (void)
{
	__fini_lib();
  	return;
}

static inline void log_trace(char *str, size_t len, size_t max_size, int print_stack)
{
	int w;
	static __thread int in_trace = 0;

	/* prevent deadlock, because inital backtrace call can involve some allocs */
	if(!in_trace)
	{
#ifdef HAVE_BACKTRACE
		int nptrs;
		void *buffer[BACKTRACE_COUNT + 1];

		in_trace = 1;	/* backtrace may allocate memory !*/

		nptrs = backtrace(buffer, BACKTRACE_COUNT);
#endif

		if(g_ctx.statm_fd != -1 && (max_size - len) > 2)
		{
			str[len - 1] = ' '; /* remove NL char */
			str[len]     = '#';
			len += pread(g_ctx.statm_fd, str + len + 1, max_size - len - 1, 0);
			str[len++] = '\n';   /* add NL back */
		}

		/* try synced write */
#if defined(HAVE_BACKTRACE) && defined(HAVE_BACKTRACE_SYMBOLS_FD)
		if(nptrs && print_stack && LOCK)
		{
			w = write(g_ctx.memlog_fd, str, len);
			backtrace_symbols_fd(&buffer[1], nptrs, g_ctx.memlog_fd);
			in_trace = UNLOCK;	/* failed unlock will not re-enable synced tracing */
		}
		else
#endif
		{
			w = write(g_ctx.memlog_fd, str, len);
			in_trace = 0;
		}
	}
	else
	{
		str[len - 1]	= '!';
		str[len++]	= '\n';	/* there is alway one char left, with '\0' from sprintf */

		w = write(g_ctx.memlog_fd, str, len);
	}
	return;
}

void *malloc(size_t size)
{
	struct log_malloc_s *mem;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;

	if(!DL_RESOLVE_CHECK(malloc))
		return NULL;

	if((mem = real_malloc(size + MEM_OFF)) != NULL)
	{
		mem->size = size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);

#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.malloc, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ malloc %zu 0x%p [%u:%u]\n",
			size, MEM_PTR(mem),
			memuse, memruse);

		log_trace(buf, s, sizeof(buf), 1);
	}
	return MEM_PTR(mem);
}

void *calloc(size_t nmemb, size_t size)
{
	struct log_malloc_s *mem;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;

	if(!DL_RESOLVE_CHECK(calloc))
		return NULL;

	if((mem = real_calloc(1, (nmemb * size) + MEM_OFF)) != NULL)
	{
		mem->size = nmemb * size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);

#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.calloc, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		//getrusage(RUSAGE_SELF, &ruse);
		s = snprintf(buf, sizeof(buf), "+ calloc %zu 0x%p [%u:%u] (%zu %zu)\n",
			nmemb * size, MEM_PTR(mem),
			memuse, memruse,
			nmemb, size);

		log_trace(buf, s, sizeof(buf), 1);
	}
	return MEM_PTR(mem);
}

void *realloc(void *ptr, size_t size)
{
	struct log_malloc_s *mem;
	sig_atomic_t memuse = 0;
	sig_atomic_t memruse = 0;
	sig_atomic_t memchange = 0;
#ifdef HAVE_MALLOC_USABLE_SIZE
	size_t       rsize = 0;
	sig_atomic_t memrchange = 0;
#endif

	if(!DL_RESOLVE_CHECK(realloc))
		return NULL;

	mem = (ptr != NULL) ? MEM_HEAD(ptr) : NULL;
	if((mem = real_realloc(mem, size + MEM_OFF)) != NULL)
	{
		memchange = ptr ? size - mem->size : size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, memchange);

#ifdef HAVE_MALLOC_USABLE_SIZE
		rsize = malloc_usable_size(mem);

		memrchange = ptr ? rsize - mem->rsize : rsize;
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, memrchange);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.realloc, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ realloc %d 0x%p 0x%p (%zu %zu) [%u:%u]\n",
			memchange, ptr,
			MEM_PTR(mem), (mem ? mem->size : 0), size,
			memuse, memruse);

		log_trace(buf, s, sizeof(buf), 1);
	}

	/* now we can update */
	if(mem != NULL)
	{
		mem->size = size;
		mem->cb = ~mem->size;
#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = rsize;
#endif
	}
	return MEM_PTR(mem);
}

void *memalign(size_t boundary, size_t size)
{
	struct log_malloc_s *mem;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;

	if(!DL_RESOLVE_CHECK(memalign))
		return NULL;

	if(boundary>MEM_OFF)
		return NULL;

	if((mem = real_memalign(boundary, size + MEM_OFF)) != NULL)
	{
		mem->size = size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);
#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.memalign, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ memalign %zu 0x%p (%zu) [%u:%u]\n",
			size, MEM_PTR(mem),
			boundary, 
			memuse, memruse);

		log_trace(buf, s, sizeof(buf), 1);
	}
	return MEM_PTR(mem);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	int ret = 0;
	struct log_malloc_s *mem = NULL;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;

	if(!DL_RESOLVE_CHECK(posix_memalign))
		return ENOMEM;

	if(alignment>MEM_OFF)
		return ENOMEM;

	if((ret = real_posix_memalign((void **)&mem, alignment, size + MEM_OFF)) == 0)
	{
		mem->size = size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);
#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.posix_memalign, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ posix_memalign %zu 0x%p (%zu %zu : %d) [%u:%u]\n",
			size, MEM_PTR(mem),
			alignment, size, ret,
			memuse, memruse);

		log_trace(buf, s, sizeof(buf), 1);
	}
	return ret;
}

void *valloc(size_t size)
{
	struct log_malloc_s *mem;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;

	if(!DL_RESOLVE_CHECK(valloc))
		return NULL;

	if((mem = real_valloc(size + MEM_OFF)) != NULL)
	{
		mem->size = size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);
#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
	(void)__sync_fetch_and_add(&g_ctx.stat.valloc, 1);

	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ valloc %zu 0x%p [%u:%u]\n",
			size, MEM_PTR(mem),
			memuse, memruse);

		log_trace(buf, s, sizeof(buf), 1);
	}
	return MEM_PTR(mem);
}

void free(void *ptr)
{
	int foreign;
	sig_atomic_t memuse;
	sig_atomic_t memruse = 0;
	size_t       rsize = 0;
	struct log_malloc_s *mem = MEM_HEAD(ptr);

	if(!DL_RESOLVE_CHECK(free) || ptr == NULL)
		return;

	/* check if we allocated it */
	foreign = (mem->size != ~mem->cb);
	memuse = __sync_sub_and_fetch(&g_ctx.mem_used, (foreign) ? 0: mem->size);
	(void)__sync_fetch_and_add(&g_ctx.stat.free, 1);
#ifdef HAVE_MALLOC_USABLE_SIZE
	memruse = __sync_sub_and_fetch(&g_ctx.mem_rused, (foreign) ? 0 : mem->rsize);
	if(foreign)
		rsize = malloc_usable_size(ptr);
#endif

	{
		int s;
		char buf[LOG_BUFSIZE];

		//getrusage(RUSAGE_SELF, &ruse);
		if(!foreign)
			s = snprintf(buf, sizeof(buf), "+ free -%zu 0x%p [%u:%u]\n",
				mem->size, MEM_PTR(mem),
				memuse, memruse);
		else
		{
			s = snprintf(buf, sizeof(buf), "+ free -%zu 0x%p [%u:%u] !f\n",
				rsize, ptr,
				memuse, memruse);
		}

		log_trace(buf, s, sizeof(buf), foreign);
	}

	real_free((foreign) ? ptr : mem);
	return;
}

/* EOF */
