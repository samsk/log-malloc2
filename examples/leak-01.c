#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main()
{
	char *x = malloc(2000);

	x = malloc(100);

	free(x);
	return 0;
}
