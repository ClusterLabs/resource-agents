#ifdef MDEBUG
#include <malloc.h>
#include <stdio.h>
#include <sys/queue.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

struct _mallocnode {
	TAILQ_ENTRY(_mallocnode) link;
	void *p;
	size_t length;
	char *file;
	int line;
};

TAILQ_HEAD(_mallochead, _mallocnode);
static struct _mallochead mallochead = { NULL, &mallochead.tqh_first };
#ifdef FDEBUG
static struct _mallochead freehead = { NULL, &freehead.tqh_first };
#endif
static pthread_mutex_t mallocm = PTHREAD_MUTEX_INITIALIZER;


void dump_mem_table(void)
{
	struct _mallocnode *mn;

	printf("+++ BEGIN mem table dump\n");
	pthread_mutex_lock(&mallocm);
	for (mn = mallochead.tqh_first; mn; mn = mn->link.tqe_next) {
		printf("%p (%d bytes) from %s:%d\n",
		       mn->p, mn->length, mn->file, mn->line);
	}
	pthread_mutex_unlock(&mallocm);
	printf("--- END mem table dump\n");
}


void _dfree(void *p, char *file, int line)
{
	struct _mallocnode *mn;

	pthread_mutex_lock(&mallocm);
	for (mn = mallochead.tqh_first; mn; mn = mn->link.tqe_next) {
		if (p == mn->p) {
			TAILQ_REMOVE(&mallochead, mn, link);
#ifdef FDEBUG
			/* Store in freed list */
			mn->file = file;
			mn->line = line;
			TAILQ_INSERT_TAIL(&freehead, mn, link);
#endif /* FDEBUG */
			pthread_mutex_unlock(&mallocm);
			free(mn->p);
			return;
		}
	}

#ifdef FDEBUG
	for (mn = freehead.tqh_first; mn; mn = mn->link.tqe_next) {
		if (p == mn->p) {
			fprintf(stderr, "Double free(%p) @ %s:%d (previously "
				"freed @ %s:%d)\n",p, file, line,
				mn->file, mn->line);
		}
	}
#endif

	pthread_mutex_unlock(&mallocm);

	if (file || line) {
		fprintf(stderr, "!!! free(%p) @ %s:%d - Not allocated\n",p,
			file, line);
		fflush(stderr);
	}
	free(p);
}


void *_dmalloc(size_t count, char *file, int line)
{
	void *p;
	struct _mallocnode *mn;
#ifdef FDEBUG
	struct _mallocnode *fn;
#endif

	p = malloc(count);
	if (!p)
		return NULL;

	mn = malloc(sizeof(*mn));
	if (!mn) {
		free(p);
		return NULL;
	}

	mn->p = p;
	mn->length = count;
	mn->file = file;
	mn->line = line;
	pthread_mutex_lock(&mallocm);
#ifdef FDEBUG
	/* Free the pointer from the old free list if we allocated
	   the same pointer again */
	for (fn = freehead.tqh_first; fn; fn = fn->link.tqe_next) {
		if (p == mn->p) {
			TAILQ_REMOVE(&freehead, fn, link);
			free(fn);
			break;
		}
	}
#endif
	TAILQ_INSERT_HEAD(&mallochead, mn, link);
	pthread_mutex_unlock(&mallocm);
	return p;
}


void *
_drealloc(void *p, size_t count, char *file, int line)
{
	struct _mallocnode *mn;
	int found = 0;

	pthread_mutex_lock(&mallocm);
	for (mn = mallochead.tqh_first; mn; mn = mn->link.tqe_next) {
		if (mn->p == p) {
			found = 1;
			break;
		}
	}

	if (!found) {
		fprintf(stderr, "!!! realloc(%p) @ %s:%d - Not allocated\n",p,
			file, line);
		fflush(stderr);
	}

	mn->p = realloc(p, count);
	mn->length = count;
	p = mn->p;

	pthread_mutex_unlock(&mallocm);
	return p;
}


char *
_strdup(char *src, char *file, int line)
{
	void *p;
	size_t count = strlen(src);
	
	p = _dmalloc(count + 1, file, line);
	if (p) {
		memset(p, 0, count + 1);
		memcpy(p, src, count);
	}

	return p;
}


#ifdef STANDALONE
#include "mallocdbg.h"
int
main(void)
{
	int *n;

	n = malloc(sizeof(int));
	free(n);
	free(n);
	n = NULL;
	free(n);
}
#endif
#endif /* MDEBUG */
