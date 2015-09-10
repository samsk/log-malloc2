#ifndef LOG_MALLOC2_UTIL_H
#define LOG_MALLOC2_UTIL_H
/*
 * log-malloc2_util
 *	Helper function, all inlined, no linking required.
 * 
 * Author: Samuel Behan <_samuel_._behan_(at)_dob_._sk> (C) 2011-2015
 *
 * License: MIT (http://opensource.org/licenses/MIT) (free to use, modify, relicense...)
 *
 * Web:
 *	http://devel.dob.sk/log-malloc2
 *	http://blog.dob.sk/category/devel/log-malloc2 (howto, tutorials)
 *	https://github.com/samsk/log-malloc2 (git repo)
 *
 */

#include <execinfo.h>

#ifndef LOG_MALLOC_BACKTRACE_SIZE
#define LOG_MALLOC_BACKTRACE_SIZE 7
#endif


/* API functions */

#ifdef  __cplusplus
extern "C" {
#endif

#define LOG_MALLOC_WRITE(fd, msg) \
		(void)write((fd), (msg), (sizeof((msg)) / sizeof((msg[0]))) - 1);

/** pre-init backtrace
 * @note: backtrace() function might allocate some memory on first call, what is
 *	a potentially dangerous operation if handling SEGV. Calling this
 *	function in a safe situation (aplication start) will avoid this.
 *
 */
static inline void log_malloc_backtrace_init(void)
{
	void *bt[LOG_MALLOC_BACKTRACE_SIZE];

	backtrace(bt, LOG_MALLOC_BACKTRACE_SIZE);
	return;
}

/** printout complete backtrace to 
  * @param	fd	output file descriptor (usually STDERR_FILENO)
  */
static inline ssize_t log_malloc_backtrace(int fd)
{
	int fdin = -1;
	ssize_t nbt = 0;
	ssize_t len = 0;
	char buffer[BUFSIZ];
	void *bt[LOG_MALLOC_BACKTRACE_SIZE];

	nbt = backtrace(bt, LOG_MALLOC_BACKTRACE_SIZE);
	if(nbt)
	{
		LOG_MALLOC_WRITE(fd, "\n======= Backtrace =========\n");
		backtrace_symbols_fd(bt, nbt, fd);

		fdin = open("/proc/self/maps", 0);
		if(fdin != -1)
		{
			LOG_MALLOC_WRITE(fd, "======= Memory map ========\n");

			// ignoring EINTR here, use SA_RESTART to fix if problem
			while((len = read(fdin, buffer, sizeof(buffer))) > 0)
				write(fd, buffer, len);

			close(fdin);
		}
		LOG_MALLOC_WRITE(fd, "===========================\n");
	}

	return nbt;
}

#undef LOG_MALLOC_WRITE

#ifdef  __cplusplus
}
#endif

#endif
