#ifndef LOG_MALLOC2_H
#define LOG_MALLOC2_H
/*
 * log-malloc2
 *	Malloc logging library with backtrace and byte-exact memory tracking.
 * 
 * Author: Samuel Behan <_samuel_._behan_(at)_dob_._sk> (C) 2011-2015
 *
 * License: GNU LGPLv3 (http://www.gnu.org/licenses/lgpl.html)
 *
 * Web:
 *	http://devel.dob.sk/log-malloc2
 *	http://blog.dob.sk/category/devel/log-malloc2 (howto, tutorials)
 *	https://github.com/samsk/log-malloc2 (git repo)
 *
 */

#include <assert.h>

/* config (LINUX specific) */
#ifndef LOG_MALLOC_TRACE_FD
#define LOG_MALLOC_TRACE_FD		1022
#endif

#ifndef LOG_MALLOC_BACKTRACE_COUNT
#define LOG_MALLOC_BACKTRACE_COUNT	7
#endif

#ifndef LOG_MALLOC_STATM_PATH
#define LOG_MALLOC_STATM_PATH		"/proc/self/statm"
#endif

#ifndef LOG_MALLOC_MAPS_PATH
#define LOG_MALLOC_MAPS_PATH		"/proc/self/maps"
#endif

/* API macros */

/* disable macros */
#ifndef LOG_MALLOC_NDEBUG

/** crate savepoint storing actual memory usage */
#define LOG_MALLOC_SAVE(name, trace)		\
		size_t _log_malloc_sp_##name = log_malloc_get_usage();			\
		ssize_t _log_malloc_sp_diff_##name = 0;					\
		static ssize_t _log_malloc_sp_iter_##name = -1;				\
		_log_malloc_sp_iter_##name++;						\
		if((trace))								\
			log_malloc_trace_printf("# SP %s(%s:%u)/%s: saved=%lu\n",	\
				__FUNCTION__, __FILE__, __LINE__, #name,		\
				_log_malloc_sp_##name);

/** update given savepoint with actual memory usage */
#define LOG_MALLOC_UPDATE(name, trace)		\
		_log_malloc_sp_##name = log_malloc_get_usage();				\
		if((trace))								\
			log_malloc_trace_printf("# SP %s(%s:%u)/%s: updated=%lu\n",	\
				__FUNCTION__, __FILE__, __LINE__, #name,		\
				_log_malloc_sp_##name);


/** test memory level for change */
#define LOG_MALLOC_COMPARE(name, trace)		\
		(									\
		 _log_malloc_sp_diff_##name =						\
		 	(log_malloc_get_usage() - _log_malloc_sp_##name),		\
		(((_log_malloc_sp_diff_##name != 0) || (trace)) ?			\
			log_malloc_trace_printf("# SP-COMPARE %s(%s:%u)/%s: expected=%lu, diff=%+ld\n",\
				__FUNCTION__, __FILE__, __LINE__, #name,		\
				_log_malloc_sp_##name, _log_malloc_sp_diff_##name) : 0)	\
			, _log_malloc_sp_diff_##name )

#ifndef NDEBUG
/** assert if memory usage differs
 * @note	Using internal __asert_fail function !
 */
#define LOG_MALLOC_ASSERT(name, iter)	\
		if(((iter) == 0) || ((iter)) == _log_malloc_sp_iter_##name)		\
		{									\
		 size_t _log_malloc_sp_now_##name = log_malloc_get_usage();		\
		 if(!(_log_malloc_sp_##name == _log_malloc_sp_now_##name))		\
		 	__assert_fail(#name "-mem-usage-before != " #name "-mem-usage-after",		\
		 		__FILE__, __LINE__, __FUNCTION__);			\
		}
#else
#define LOG_MALLOC_ASSERT(name, iter)
#endif

#else

/* noops (NOTE: use of NULL here is experimental :) */
#define LOG_MALLOC_SAVE(name, trace)
#define LOG_MALLOC_UPDATE(name, trace)
#define LOG_MALLOC_COMPARE(name, trace) NULL
#define LOG_MALLOC_ASSERT(name, trace)

#endif

/* API functions */

#ifdef  __cplusplus
extern "C" {
#endif

/** get current memory usage */
size_t log_malloc_get_usage(void);

/** enable trace to LOG_MALLOC_TRACE_FD */
void log_malloc_trace_enable(void);

/** disable trace */
void log_malloc_trace_disable(void);

/** trace smth. to LOG_MALLOC_TRACE_FD */
int log_malloc_trace_printf(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

#ifdef  __cplusplus
}
#endif

#endif
