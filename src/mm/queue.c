#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"

#define QUEUE_ALIGN_SIZE(s) (((s) + 0xf) & ~(0xf))

static void
free_queue_items(queue_obj_t *queue_obj)
{
	assert(queue_obj);

	queue_item_t *item;
	queue_item_t *prev;

	if (!queue_obj->nr_items)
		return;

	prev = item = queue_obj->back;

	while (item)
	{
		item = item->next;
		free(prev);
		prev = item;
	}

	return;
}

int
QUEUE_enqueue(queue_obj_t *queue_obj, void *data, size_t data_len)
{
	assert(queue_obj);
	assert(data);

	queue_item_t *item = malloc(sizeof(queue_item_t));
	if (!item)
		return -1;

	item->data = calloc(QUEUE_ALIGN_SIZE(data_len), 1);
	if (!item->data)
		goto fail;

	memcpy(item->data, data, data_len);
	item->data_len = data_len;

	if (!queue_obj->front)
	{
		queue_obj->back = queue_obj->front = item;
		item->next = item->prev = NULL;
	}
	else
	{
		item->next = queue_obj->back;
		item->next->prev = item;
		item->prev = NULL;
		queue_obj->back = item;
	}

	++queue_obj->nr_items;

	return 0;

fail:
	free(item);

	return -1;
}

queue_item_t *
QUEUE_dequeue(queue_obj_t *queue_obj)
{
	assert(queue_obj);

#ifdef DEBUG
	fprintf(stderr,
		"%d items in queue\n"
		"front @ %p, back @ %p\n",
		queue_obj->nr_items,
		queue_obj->front, queue_obj->back);
#endif

	if (!queue_obj->nr_items)
		return NULL;

	queue_item_t *item = queue_obj->front;
	assert(item);
	queue_obj->front = item->prev;
	if (queue_obj->front)
		queue_obj->front->next = NULL;
	else
		queue_obj->back = NULL;

	item->prev = NULL;
	--queue_obj->nr_items;

	return item;
}

queue_obj_t *
QUEUE_object_new(void)
{
	queue_obj_t *queue_obj = malloc(sizeof(queue_obj_t));

	if (!queue_obj)
		return NULL;

	memset(queue_obj, 0, sizeof(*queue_obj));

	return queue_obj;
}

void
QUEUE_object_destroy(queue_obj_t *queue_obj)
{
	assert(queue_obj);

	free_queue_items(queue_obj);
}
