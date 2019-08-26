#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "malloc.h"

void *
wr_malloc(size_t size)
{
	if (size == 0)
		size = 1;

	void *mem = malloc(size);

	//wr_assert(mem != NULL);

	return mem;
}

void *
wr_zmalloc(size_t size)
{
	if (size == 0)
		size = 1;

	void *mem = malloc(size);

	//wr_assert(mem != NULL);

	memset(mem, 0, size);

	return mem;
}

void *
wr_calloc(int nr, size_t size)
{
	void *mem = calloc(nr, size);

	memset(mem, 0, nr);
	//wr_assert(mem != NULL);

	return mem;
}

void *
wr_realloc(void *old_ptr, size_t size)
{
	old_ptr = realloc(old_ptr, size);

	assert(old_ptr);

	return old_ptr;
}

char *
wr_strdup(char *str)
{
	char *dup_str = strdup(str);

	//wr_assert(dup_str != NULL);

	return dup_str;
}