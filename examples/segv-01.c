#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <execinfo.h>

#define BACKTRACE_SIZE 10

static void copyfile(const char *path, int outfd)
{
	int fd = -1;
	char buf[BUFSIZ];
	ssize_t len = 0;

	// no warning here, it will be simply missing
	if((fd = open(path, 0)) == -1)
		return;

	// ignoring EINTR here, use SA_RESTART to fix if problem
	while((len = read(fd, buf, sizeof(buf))) > 0)
		write(outfd, buf, len);

	close(fd);
	return;
}

static void print_backtrace(int fd, int signo, void * const *bt_buffer, int bt_size)
{
	ssize_t len = 0;
	char buff[1024];
	const char *signame = NULL;

	// this is not safe to use in MT apps
	signame = strsignal(signo);

	// header
	len = snprintf(buff, sizeof(buff), "\n--- SIGNAL %d (%s) RECEIVED ---\n",
		signo, signame);
	write(fd, buff, len);

	// backtrace + maps (because of ASLR)
	if(bt_size)
	{
		backtrace_symbols_fd(bt_buffer, bt_size, fd);

		write(fd, ".\n", 2);

		copyfile("/proc/self/maps", fd);
	}

	// footer
	strncpy(buff, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++",
		sizeof(buff));
	buff[len] = '\n';
	write(fd, buff, len + 1);

	_exit(EXIT_FAILURE);
	return;
}

static void sighandler(int signo)
{
	ssize_t bt_size = 0;
	void *bt_buffer[BACKTRACE_SIZE];

	bt_size = backtrace(bt_buffer, BACKTRACE_SIZE);
	print_backtrace(STDERR_FILENO, signo, bt_buffer, bt_size);
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
