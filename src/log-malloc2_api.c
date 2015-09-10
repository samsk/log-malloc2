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
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>

#include "log-malloc2.h"
#include "log-malloc2_internal.h"

/*
 *  API FUNCTIONS
 */
size_t log_malloc_get_usage(void)
{
	const log_malloc_ctx_t *ctx = log_malloc_ctx_get();

	return ctx->mem_used;
}

/* enable trace to LOG_MALLOC_TRACE_FD */
void log_malloc_trace_enable(void)
{
	log_malloc_ctx_t *ctx = log_malloc_ctx_get();

	ctx->memlog_disabled = false;
	return;
}

/* disable trace */
void log_malloc_trace_disable(void)
{
	log_malloc_ctx_t *ctx = log_malloc_ctx_get();

	ctx->memlog_disabled = true;
	return;
}

/* sprintf trace */
int log_malloc_trace_printf(const char *fmt, ...)
{
	int s;
	int w = 0;
	char buf[1024];
	va_list args;
	log_malloc_ctx_t *ctx = log_malloc_ctx_get();

	va_start(args, fmt);
	s = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	{
		int ww = 0;
		const int max = ((s > (sizeof(buf) - 1)) ? (sizeof(buf) - 1) : s);

		buf[max - 1] = '\n';
		ww = write(ctx->memlog_fd, buf, max);
	}
	return w;
}

/* EOF */
