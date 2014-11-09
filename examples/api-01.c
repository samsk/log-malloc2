#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "log-malloc2.h"

int main()
{
	char *x = malloc(2000);

	LOG_MALLOC_SAVE(savepoint1, 0);

	x = malloc(100);
	x[0] = '\0';

LOG_MALLOC_COMPARE(savepoint1, 0);
	ssize_t t = LOG_MALLOC_COMPARE(savepoint1, 0);
	printf("TEST = %ld\n", t);
	LOG_MALLOC_ASSERT(savepoint1, 0);

	//sleep(500);

	return 0;
}
