#ifdef MDEBUG
#include <malloc.h>
#include <stdio.h>
#include <sys/queue.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <list.h>

struct __mallocnode {
	list_head();
	void *p;
	size_t length;
	char *file;
	int line;
#ifdef FDEBUG
	int frees; /* Number of times freed; for freelist */
#endif
};

static struct __mallocnode *mallochead = NULL;
#ifdef FDEBUG
static struct __mallocnode *freehead = NULL;
#endif
static pthread_mutex_t mallocm = PTHREAD_MUTEX_INITIALIZER;


void dump_mem_table(void)
{
	struct __mallocnode *mn;

	printf("+++ BEGIN mem table dump\n");
	pthread_mutex_lock(&mallocm);
	list_do(&mallochead, mn) {
		printf("%p (%d bytes) from %s:%d\n",
		       mn->p, mn->length, mn->file, mn->line);
	} while (!list_done(&mallochead, mn));
	printf("--- END mem table dump\n");
#ifdef FDEBUG
	printf("+++ BEGIN free table dump\n");
	list_do(&freehead, mn) {
		if (mn->frees <= 1)
			continue;
		printf("%p (%d bytes) freed %d times (first @ %s:%d)\n",
		       mn->p, mn->length, mn->frees, mn->file, mn->line);
	} while (!list_done(&freehead, mn));
	printf("--- END free table dump\n");
#endif
	pthread_mutex_unlock(&mallocm);
}


void qfree(void *p)
{
	free(p);
}


void _dfree(void *p, char *file, int line)
{
	struct __mallocnode *mn;

	pthread_mutex_lock(&mallocm);
	list_do(&mallochead, mn) {
		if (p == mn->p) {
			list_remove(&mallochead, mn);
#ifdef FDEBUG
			/* Store in freed list */
			mn->file = file;
			mn->line = line;
			mn->frees = 1;
			list_insert(&freehead, mn);
#endif /* FDEBUG */
			pthread_mutex_unlock(&mallocm);
			free(mn->p);
			return;
		}
	} while (!list_done(&mallochead, mn));

#ifdef FDEBUG
	list_do(&freehead, mn) {
		if (p == mn->p) {
			mn->frees++;
			fprintf(stderr, "Double free(%p) @ %s:%d (previously "
				"freed @ %s:%d)\n",p, file, line,
				mn->file, mn->line);
		}
	} while (!list_done(&freehead, mn));
#endif

	pthread_mutex_unlock(&mallocm);

	fprintf(stderr, "!!! free(%p) @ %s:%d - Not allocated\n",p,
		file, line);
	fflush(stderr);
	free(p);
}


void *_dmalloc(size_t count, char *file, int line)
{
	void *p;
	struct __mallocnode *mn;
#ifdef FDEBUG
	struct __mallocnode *fn;
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
	list_do(&freehead, fn) {
		if (p == mn->p) {
			list_remove(&freehead, fn);
			free(fn);
			break;
		}
	} while (!list_done(&freehead, fn));
#endif
	list_insert(&mallochead, mn);
	pthread_mutex_unlock(&mallocm);
	return p;
}


void *
_drealloc(void *p, size_t count, char *file, int line)
{
	struct __mallocnode *mn = NULL;
	int found = 0;
	void *ret;

	pthread_mutex_lock(&mallocm);
	list_do(&mallochead, mn) {
		if (mn->p == p) {
			found = 1;
			break;
		}
	} while (!list_done(&mallochead, mn));

	if (!found) {
		fprintf(stderr, "!!! realloc(%p) @ %s:%d - Not allocated\n",p,
			file, line);
		fflush(stderr);
	}

	mn->p = realloc(p, count);
	mn->length = count;
	ret = mn->p;

	pthread_mutex_unlock(&mallocm);
	return ret;
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
