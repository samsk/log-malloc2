#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

int main()
{
	char *x = malloc(2000);

	x = malloc(100);

	sleep(60);
	free(x);
	return 0;
}
