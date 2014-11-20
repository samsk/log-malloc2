#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <execinfo.h>

#include <log-malloc2_util.h>

static void sighandler(int signo)
{
	char buf[256];
	ssize_t len = 0;
	const char *signame = NULL;

	// this is not safe to use in MT apps
	signame = strsignal(signo);

	len = snprintf(buf, sizeof(buf), "\n--- SIGNAL %d (%s) RECEIVED ---\n",
		signo, signame);
	write(STDERR_FILENO, buf, len);

	// backtrace + maps (because of ASLR)
	log_malloc_backtrace(STDERR_FILENO);

	_exit(EXIT_FAILURE);
	return;
}

int main()
{
	char *x = (char *)0x1;

	// setup signal
	signal(SIGSEGV, &sighandler);

	// raise segfault anyhow
	x[123] = 234;
	raise(SIGSEGV);

	return EXIT_SUCCESS;
}
