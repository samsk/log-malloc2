#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

void __attribute__ ((noinline)) own(char **ptr)
{
	*ptr = malloc(100);
}

int main()
{
	char *x = malloc(2000);

	own(&x);

	x = malloc(1000);

	free(x);
	return 0;
}
