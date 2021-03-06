#ifndef __BUFFER_H__
#define __BUFFER_H__ 1

#include <openssl/ssl.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_BUFSIZE 16384
#define BUFFER_MAGIC 0x12344321

typedef struct buf_t
{
	char	*data;
	char	*buf_end; /* End of the allocated buffer memory */
	char	*buf_head; /* Start of our data */
	char	*buf_tail; /* End of our data */
	size_t	data_len; /* length of used data */
	size_t	buf_size; /* total size of buffer */
	unsigned magic;
} buf_t;

#define buf_used(b) ((b)->buf_tail - (b)->buf_head)
#define buf_slack(b) ((b)->buf_size - buf_used(b))

#define BUF_NULL_TERMINATE(b) (*((b)->buf_tail) = 0)

int buf_init(buf_t *, size_t) __nonnull((1));
void buf_destroy(buf_t *) __nonnull((1));
void buf_collapse(buf_t *, off_t, size_t) __nonnull((1));
void buf_shift(buf_t *, off_t, size_t) __nonnull((1));
int buf_extend(buf_t *, size_t) __nonnull((1)) __wur;
int buf_append(buf_t *, char *) __nonnull((1,2)) __wur;
int buf_append_ex(buf_t *, char *, size_t) __nonnull((1,2)) __wur;
void buf_append_fmt(buf_t *, char *, ...) __nonnull((1,2));
void buf_snip(buf_t *, size_t) __nonnull((1));
void buf_clear(buf_t *) __nonnull((1));
void buf_replace(buf_t *, char *, char *) __nonnull((1,2,3));
void buf_push_head(buf_t *, size_t) __nonnull((1));
void buf_pull_head(buf_t *, size_t) __nonnull((1));
void buf_push_tail(buf_t *, size_t) __nonnull((1));
void buf_pull_tail(buf_t *, size_t) __nonnull((1));

char *buf_find(buf_t *, char *);

/* return a buf_t object from the heap duplicate of argument */
buf_t *buf_dup(buf_t *) __nonnull((1)) __wur;
void buf_copy(buf_t *, buf_t *) __nonnull((1,2)) __wur;
int buf_integrity(buf_t *) __nonnull((1)) __wur;
ssize_t buf_read_fd(int, buf_t *, size_t) __nonnull((2)) __wur;
ssize_t buf_read_socket(int, buf_t *, size_t) __nonnull((2)) __wur;
ssize_t buf_read_tls(SSL *, buf_t *, size_t) __nonnull((1,2)) __wur;
ssize_t buf_write_fd(int, buf_t *) __nonnull((2)) __wur;
ssize_t buf_write_socket(int, buf_t *) __nonnull((2)) __wur;
ssize_t buf_write_tls(SSL *, buf_t *) __nonnull((1,2)) __wur;

#ifdef __cplusplus
}
#endif

#endif /* !defined __BUFFER_H__ */
