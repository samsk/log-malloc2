#ifndef LOG_MALLOC2_INTERNAL_H
#define LOG_MALLOC2_INTERNAL_H
/*
 * log-malloc2 API
 *	Malloc logging library with backtrace and byte-exact memory tracking.
 * 
 * Author: Samuel Behan <_samuel_._behan_(at)_dob_._sk> (C) 2013-2015
 *
 * License: GNU LGPLv3 (http://www.gnu.org/licenses/lgpl.html)
 *
 * Web:
 *	http://devel.dob.sk/log-malloc2
 *	http://blog.dob.sk/category/devel/log-malloc2 (howto, tutorials)
 *	https://github.com/samsk/log-malloc2 (git repo)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#else
#define bool uint8_t
#define false 0
#define true 1
#endif

/* pthreads support */
#ifdef HAVE_LIBPTHREAD
#include <pthread.h>
#endif 

/* pthread shortcuts */
#ifdef HAVE_LIBPTHREAD
# define LOCK_INIT()	1
# define LOCK(lock)	(pthread_mutex_trylock(&(lock)) == 0)
# define UNLOCK(lock)	(pthread_mutex_unlock(&(lock)))
#else
# define LOCK_INIT()	1
# define LOCK(lock)	1
# define UNLOCK(lock)	0
#endif


/* init constants */
#define LOG_MALLOC_INIT_NULL		0xFAB321
#define LOG_MALLOC_INIT_DONE		0x123FAB
#define LOG_MALLOC_FINI_DONE		0xFAFBFC

/* global context */
typedef struct log_malloc_ctx_s {
	sig_atomic_t init_done;
        sig_atomic_t mem_used;
	sig_atomic_t mem_rused;
	struct {
		sig_atomic_t malloc;
		sig_atomic_t calloc;
		sig_atomic_t realloc;
		sig_atomic_t memalign;
		sig_atomic_t posix_memalign;
		sig_atomic_t valloc;
		sig_atomic_t free;
		sig_atomic_t unrel_sum; /* unrealiable call count sum */
	} stat;
	int memlog_fd;
	int statm_fd;
	bool memlog_disabled;
	clock_t clock_start;
#ifdef HAVE_LIBPTHREAD
	pthread_mutex_t loglock;
#endif
} log_malloc_ctx_t;

#define LOG_MALLOC_CTX_INIT_BASE		\
		LOG_MALLOC_INIT_NULL,		\
		0,				\
		0,				\
		{0, 0, 0, 0, 0, 0, 0, 0},	\
		LOG_MALLOC_TRACE_FD,		\
		-1,				\
		false,				\
		0

#ifdef HAVE_LIBPTHREAD
#define LOG_MALLOC_CTX_INIT			\
	{					\
		LOG_MALLOC_CTX_INIT_BASE,	\
		PTHREAD_MUTEX_INITIALIZER,	\
	}
#else
#define LOG_MALLOC_CTX_INIT			\
	{					\
		LOG_MALLOC_CTX_INIT_BASE,	\
	}
#endif

/* API function */
log_malloc_ctx_t *log_malloc_ctx_get(void);

#endif
