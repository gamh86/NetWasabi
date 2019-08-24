#ifndef WR_CACHE_H
#define WR_CACHE_H 1

#include "webreaper.h"

#define WR_CACHE_SIZE	4096

typedef int (*wr_cache_ctor_t)(void *);
typedef void (*wr_cache_dtor_t)(void *);

typedef struct wr_cache_t
{
	void *cache;
	int next_free; /* index of next free object */
	int nr_free;
	int capacity;
	size_t objsize;
	char *name;
	wr_cache_ctor_t ctor;
	wr_cache_dtor_t dtor;
} wr_cache_t;


void *wr_cache_create(char *, size_t, int, wr_cache_ctor_t, wr_cache_dtor_t);
void wr_cache_destroy(wr_cache_t *) __nonnull((1));
void *wr_cache_alloc(wr_cache_t *) __nonnull((1)) __wur;
void wr_cache_dealloc(wr_cache_t *, void *) __nonnull((1,2));

#define wr_cache_mark_used(c, s) \
	off_t off = ((char *)(s) - (char *)(c)->cache) / (c)->objsize;\
	(c)->cache_slots[off] = 1;

#define wr_cache_mark_unused(c, s) \
	off_t off = ((char *)(s) - (char *)(c)->cache) (c)->objsize;\
	(c)->cache_slots[off] = 0;

#endif /* WR_CACHE_H */
