#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

struct _retbuf {
	char *data;
	ssize_t maxsize;
	ssize_t cursize;
	char magic[8];
};

static char _x_buf_magic[]="m461kz31";

void *
buf_init(void *buf, size_t len)
{
	struct _retbuf *b = (struct _retbuf *)buf;

	errno = EINVAL;
	if (!len || !buf)
		return NULL;
	if (len < sizeof(*b) + 16)
		return NULL;

	memset(b, 0, len);
	b->data = buf + sizeof(*b);
	b->maxsize = len - sizeof (*b);
	b->cursize = 0;
	memcpy(b->magic, _x_buf_magic, sizeof(b->magic));

	return buf;
}

ssize_t
buf_append(void *buf, char *info)
{
	struct _retbuf *b = (struct _retbuf *)buf;
	ssize_t len;

	errno = EINVAL;
	if (!buf)
		return -1;
	if (memcmp(b->magic, _x_buf_magic, sizeof(b->magic)))
		return -1;
	if (!info)
		return 0;
       	len = strlen(info);
	if (!len)
		return 0;

	errno = ENOSPC;
	if (b->maxsize - b->cursize < len)
		return -1;

	memcpy(&(b->data[b->cursize]), info, len);
	b->cursize += len;
	return len;
}

char *
buf_data(void *buf)
{
	struct _retbuf *b = (struct _retbuf *)buf;
	errno = EINVAL;
	if (!buf)
		return NULL;
	if (memcmp(b->magic, _x_buf_magic, sizeof(b->magic)))
		return NULL;
	return ((struct _retbuf *)buf)->data;
}


int
buf_finished(void *buf)
{
	struct _retbuf *b = (struct _retbuf *)buf;
	errno = EINVAL;
	if (!buf)
		return -1;
	if (memcmp(b->magic, _x_buf_magic, sizeof(b->magic)))
		return -1;
	memset(b->magic, 0, sizeof(b->magic));
	return 0;
}
