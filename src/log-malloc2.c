/*
 * log-malloc2 
 *	Malloc logging library with backtrace and byte-exact memory tracking.
 * 
 * Author: Samuel Behan <_samuel_._behan_(at)_dob_._sk> (C) 2011-2015
 *	   partialy based on log-malloc from Ivan Tikhonov
 *
 * License: GNU LGPLv3 (http://www.gnu.org/licenses/lgpl.html)
 *
 * Web:
 *	http://devel.dob.sk/log-malloc2
 *	http://blog.dob.sk/category/devel/log-malloc2 (howto, tutorials)
 *	https://github.com/samsk/log-malloc2 (git repo)
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

/* needed for dlfcn.h */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE	1
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <execinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>

#ifdef HAVE_UNWIND
/* speedup unwinding */
#define UNW_LOCAL_ONLY 1

#include <libunwind.h>
#endif

#include <dlfcn.h>

#include "log-malloc2.h"
#include "log-malloc2_internal.h"

/* config */
#ifdef HAVE_UNWIND
#ifdef HAVE_UNWIND_DETAIL
#define LOG_BUFSIZE	(128 + (256 * LOG_MALLOC_BACKTRACE_COUNT))
#else
#define LOG_BUFSIZE	(128 + (32 * LOG_MALLOC_BACKTRACE_COUNT))
#endif
#else
#define LOG_BUFSIZE	128
#endif

/**
  size       total program size (same as VmSize in /proc/[pid]/status)
  resident   resident set size (same as VmRSS in /proc/[pid]/status)
  share      shared pages (from shared mappings)
  text       text (code)
  lib        library (unused in Linux 2.6)
  data       data + stack
  dt         dirty pages (unused in Linux 2.6)
*/
static const char *g_statm_path = LOG_MALLOC_STATM_PATH;
static const char *g_maps_path	= LOG_MALLOC_MAPS_PATH;

/* handler declarations */
static void *(*real_malloc)(size_t size)	= NULL;
static void  (*real_free)(void *ptr)		= NULL;
static void *(*real_realloc)(void *ptr, size_t size)	= NULL;
static void *(*real_calloc)(size_t nmemb, size_t size)	= NULL;
static void *(*real_memalign)(size_t boundary, size_t size)	= NULL;
static int   (*real_posix_memalign)(void **memptr, size_t alignment, size_t size)	= NULL;
static void *(*real_valloc)(size_t size)	= NULL;

/* memtracking struct */
struct log_malloc_s {
	size_t size;		/* allocation size */
	size_t cb;		/* size check bits */
#ifdef HAVE_MALLOC_USABLE_SIZE
	size_t rsize;		/* really allocated size */
#endif
	char   ptr[0] __attribute__((__aligned__));	/* user memory begin */
};
#define MEM_OFF       (sizeof(struct log_malloc_s))

#define MEM_PTR(mem)  (mem != NULL ? ((void *)(((void *)(mem)) + MEM_OFF)) : NULL)
#define MEM_HEAD(ptr) ((struct log_malloc_s *)(((void *)(ptr)) - MEM_OFF))

/* DL resolving */
#define DL_RESOLVE(fn)	\
	((!real_ ## fn) ? (real_ ## fn = dlsym(RTLD_NEXT, # fn)) : (real_ ## fn = ((void *)0x1)))
#define DL_RESOLVE_CHECK(fn)	\
	((!real_ ## fn) ? __init_lib() : ((void *)0x1))

/* data context */
static log_malloc_ctx_t g_ctx = LOG_MALLOC_CTX_INIT;

/*
 *  INTERNAL API FUNCTIONS
 */
log_malloc_ctx_t *log_malloc_ctx_get(void)
{
	return &g_ctx;
}

/*
 *  LIBRARY INIT/FINI FUNCTIONS
 */
static inline void copyfile(const char *head, size_t head_len,
	const char *path, int outfd)
{
	int w;
	int fd = -1;
	char buf[BUFSIZ];
	ssize_t len = 0;

	// no warning here, it will be simply missing in log
	if((fd = open(path, 0)) == -1)
		return;

	w = write(outfd, head, head_len);
	// ignoring EINTR here, use SA_RESTART to fix if problem
	while((len = read(fd, buf, sizeof(buf))) > 0)
		w = write(outfd, buf, len);

	close(fd);
	return;
}
 
static void *__init_lib(void)
{
	/* check already initialized */
	if(!__sync_bool_compare_and_swap(&g_ctx.init_done,
		LOG_MALLOC_INIT_NULL, LOG_MALLOC_INIT_DONE))
		return NULL;

	LOCK_INIT();

	/* open statm */
	if(g_statm_path[0] != '\0' && (g_ctx.statm_fd = open(g_statm_path, 0)) == -1)
		fprintf(stderr, "\n*** log-malloc: could not open %s\n\n", g_statm_path);

	/* get real functions pointers */
	DL_RESOLVE(malloc);
	DL_RESOLVE(calloc);
	DL_RESOLVE(free);
	DL_RESOLVE(realloc);
	DL_RESOLVE(memalign);
	DL_RESOLVE(posix_memalign);
	DL_RESOLVE(valloc);

	/* clock */
	g_ctx.clock_start = clock();

	/* post-init status */
	if(!g_ctx.memlog_disabled)
	{
		int s, w;
		char path[256];
		char buf[LOG_BUFSIZE + sizeof(path)];

		s = snprintf(buf, sizeof(buf), "# CLOCK-START %lu\n", g_ctx.clock_start);
		w = write(g_ctx.memlog_fd, buf, s);

		s = snprintf(buf, sizeof(buf), "# PID %u\n", getpid());
		w = write(g_ctx.memlog_fd, buf, s);

		s = readlink("/proc/self/exe", path, sizeof(path));
		if(s > 1)
		{
			path[s] = '\0';
			s = snprintf(buf, sizeof(buf), "# EXE %s\n", path);
			w = write(g_ctx.memlog_fd, buf, s);
		}

		s = readlink("/proc/self/cwd", path, sizeof(path));
		if(s > 1)
		{
			path[s] = '\0';
			s = snprintf(buf, sizeof(buf), "# CWD %s\n", path);
			w = write(g_ctx.memlog_fd, buf, s);
		}

		s = snprintf(buf, sizeof(buf), "+ INIT [%u:%u] malloc=%u calloc=%u realloc=%u memalign=%u/%u valloc=%u free=%u\n",
				g_ctx.mem_used, g_ctx.mem_rused,
				g_ctx.stat.malloc, g_ctx.stat.calloc, g_ctx.stat.realloc,
				g_ctx.stat.memalign, g_ctx.stat.posix_memalign,
				g_ctx.stat.valloc,
				g_ctx.stat.free);
		w = write(g_ctx.memlog_fd, buf, s);


		/* auto-disable trace if file is not open  */
		if(w == -1 && errno == EBADF)
			g_ctx.memlog_disabled = true;

		fprintf(stderr, "\n *** log-malloc trace-fd = %d *** \n\n",
			g_ctx.memlog_fd);
	}

	return (void *)0x01;
}

static void __attribute__ ((constructor))log_malloc2_init(void)
{
	__init_lib();
  	return;
}

static void __fini_lib(void)
{
	clock_t clck = clock();

	/* check already finalized */
	if(!__sync_bool_compare_and_swap(&g_ctx.init_done,
		LOG_MALLOC_INIT_DONE, LOG_MALLOC_FINI_DONE))
		return;

	if(!g_ctx.memlog_disabled)
	{
		int s, w;
		char buf[LOG_BUFSIZE];
		const char maps_head[] = "# FILE /proc/self/maps\n";

		s = snprintf(buf, sizeof(buf), "+ FINI [%u:%u] malloc=%u calloc=%u realloc=%u memalign=%u/%u valloc=%u free=%u\n",
				g_ctx.mem_used, g_ctx.mem_rused,
				g_ctx.stat.malloc, g_ctx.stat.calloc, g_ctx.stat.realloc,
				g_ctx.stat.memalign, g_ctx.stat.posix_memalign,
				g_ctx.stat.valloc,
				g_ctx.stat.free);
		w = write(g_ctx.memlog_fd, buf, s);

		/* maps out here, because dynamic libs could by mapped during run */
		copyfile(maps_head, sizeof(maps_head) - 1, g_maps_path, g_ctx.memlog_fd);

		s = snprintf(buf, sizeof(buf), "# CLOCK-END %lu\n", clck);
		w = write(g_ctx.memlog_fd, buf, s);

		s = snprintf(buf, sizeof(buf), "# CLOCK-DIFF %lu\n", clck - g_ctx.clock_start);
		w = write(g_ctx.memlog_fd, buf, s);
	}

	if(g_ctx.statm_fd != -1)
		close(g_ctx.statm_fd);
	g_ctx.statm_fd = -1;

	return;
}

static void __attribute__ ((destructor))log_malloc2_fini(void)
{
	__fini_lib();
  	return;
}


/*
 *  INTERNAL FUNCTIONS
 */
static inline size_t int2hex(unsigned long int num, char *str, size_t max_size)
{
	size_t len = 0;
	const static char hex[] = { '0', '1', '2', '3', '4', '5', '6', '7',
	                        '8', '9' ,'a', 'b', 'c', 'd', 'e', 'f' };

	do
	{
		str[len++] = hex[ num & 0xf ];
		num >>= 4;
 	} while(num!=0);
 
 	unsigned int ii = 0;
	for(ii = 0; ii < (len / 2); ii++)
	{
		const unsigned int pos = len - ii - 1;
		const char w = str[ii];

		str[ii] = str[pos];
		str[pos] = w;
	}
	str[len] = '\0';

	return len;
}


static inline void log_trace(char *str, size_t len, size_t max_size, int print_stack)
{
	int w;
	static __thread int in_trace = 0;

	/* prevent deadlock, because inital backtrace call might involve some allocs */
	if(!in_trace)
	{
#ifdef HAVE_UNWIND
		int unwind = 0;
		unw_context_t uc;
		unw_cursor_t cursor; 
		int unwind_count = 0;

		if(print_stack)
		{
			unwind = (unw_getcontext(&uc) == 0);
			if(unwind)
				unwind = (unw_init_local(&cursor, &uc) == 0);
		}
#else
#ifdef HAVE_BACKTRACE
		int nptrs = 0;
		void *buffer[LOG_MALLOC_BACKTRACE_COUNT + 1];

		in_trace = 1;	/* backtrace may allocate memory !*/

		if(print_stack)
			nptrs = backtrace(buffer, LOG_MALLOC_BACKTRACE_COUNT);
#endif
#endif

		if(g_ctx.statm_fd != -1 && (max_size - len) > 2)
		{
			str[len - 1] = ' '; /* remove NL char */
			str[len]     = '#';
			len += pread(g_ctx.statm_fd, str + len + 1, max_size - len - 1, 0);
			str[len++] = '\n';   /* add NL back */
		}

#ifdef HAVE_UNWIND
		while(print_stack && unwind && unwind_count < LOG_MALLOC_BACKTRACE_COUNT
			&& unw_step(&cursor) > 0
			&& max_size - len > (16 + 5))
		{
			unw_word_t ip = 0;
			unw_word_t offp = 0;
			size_t len_start = len;

			unw_get_reg(&cursor, UNW_REG_IP, &ip);

#ifdef HAVE_UNWIND_DETAIL
			/* this harms performance */
			str[len++] = '*';
			str[len++] = '(';
			if(unw_get_proc_name(&cursor, &str[len], max_size - len - 1, &offp) == 0)
			{
				len += strnlen(&str[len], max_size - len - 1);
				len += snprintf(&str[len], max_size - len - 1, "+0x%lx", offp);
				str[len++] = ')';
			}
			else
				len += -2;
#endif

			str[len++] = '[';
			str[len++] = '0';
			str[len++] = 'x';
			len += int2hex(ip, &str[len], max_size - len - 1); // max 16 chars
			str[len++] = ']';
			str[len++] = '\n';
			
			unwind_count++;
		}
#else
#if defined(HAVE_BACKTRACE) && defined(HAVE_BACKTRACE_SYMBOLS_FD)
		/* try synced write */
		if(nptrs && print_stack && LOCK(g_ctx.loglock))
		{
			w = write(g_ctx.memlog_fd, str, len);
			backtrace_symbols_fd(&buffer[1], nptrs, g_ctx.memlog_fd);
			in_trace = UNLOCK(g_ctx.loglock); /* failed unlock will not re-enable synced tracing */
		}
		else
#endif
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


/*
 *  LIBRARY FUNCTIONS
 */
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
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.malloc, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ malloc %zu %p [%u:%u]\n",
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
	size_t calloc_size = 0;

	if(!DL_RESOLVE_CHECK(calloc))
		return NULL;

	calloc_size = (nmemb * size);	//FIXME: what about check for overflow here ?
	if((mem = real_calloc(1, calloc_size + MEM_OFF)) != NULL)
	{
		mem->size = calloc_size;
		mem->cb = ~mem->size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, mem->size);

#ifdef HAVE_MALLOC_USABLE_SIZE
		mem->rsize = malloc_usable_size(mem);
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, mem->rsize);
#endif
	}
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.calloc, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		//getrusage(RUSAGE_SELF, &ruse);
		s = snprintf(buf, sizeof(buf), "+ calloc %zu %p [%u:%u] (%zu %zu)\n",
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

	//FIXME: not handling foreign memory here (seems not needed)
	if(mem && (mem->size != ~mem->cb))
	{
		assert(mem->size != ~mem->cb);
		return NULL;
	}

	if((mem = real_realloc(mem, size + MEM_OFF)) != NULL)
	{
		memchange = (ptr) ? size - mem->size : size;
		memuse = __sync_add_and_fetch(&g_ctx.mem_used, memchange);

#ifdef HAVE_MALLOC_USABLE_SIZE
		rsize = malloc_usable_size(mem);

		memrchange = (ptr) ? rsize - mem->rsize : rsize;
		memruse = __sync_add_and_fetch(&g_ctx.mem_rused, memrchange);
#endif
	}
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.realloc, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ realloc %d %p %p (%zu %zu) [%u:%u]\n",
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

	if(boundary > MEM_OFF)
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
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.memalign, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ memalign %zu %p (%zu) [%u:%u]\n",
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

	if(alignment > MEM_OFF)
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
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.posix_memalign, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ posix_memalign %zu %p (%zu %zu : %d) [%u:%u]\n",
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
#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.valloc, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		s = snprintf(buf, sizeof(buf), "+ valloc %zu %p [%u:%u]\n",
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
#ifdef HAVE_MALLOC_USABLE_SIZE
	memruse = __sync_sub_and_fetch(&g_ctx.mem_rused, (foreign) ? 0 : mem->rsize);
	if(foreign)
		rsize = malloc_usable_size(ptr);
#endif

#ifndef DISABLE_CALL_COUNTS
	(void)__sync_fetch_and_add(&g_ctx.stat.free, 1);
	g_ctx.stat.unrel_sum++;
#endif

	if(!g_ctx.memlog_disabled)
	{
		int s;
		char buf[LOG_BUFSIZE];

		//getrusage(RUSAGE_SELF, &ruse);
		if(!foreign)
		{
			s = snprintf(buf, sizeof(buf), "+ free -%zu %p [%u:%u]\n",
				mem->size, MEM_PTR(mem),
				memuse, memruse);
		}
		else
		{
			s = snprintf(buf, sizeof(buf), "+ free -%zu %p [%u:%u] !f\n",
				rsize, ptr,
				memuse, memruse);
		}

		log_trace(buf, s, sizeof(buf), foreign);
	}

	real_free((foreign) ? ptr : mem);
	return;
}

/* EOF */
