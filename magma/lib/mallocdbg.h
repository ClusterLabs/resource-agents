#ifndef _MALLOCDBG_H
#define _MALLOCDBG_H
#include <sys/types.h>

void dump_mem_table(void);

void * _dmalloc(size_t, char *, int);
void * _drealloc(void *, size_t, char *, int);
void _dfree(void *, char *, int);
char * _strdup(char *, char *, int);

#define malloc(x) _dmalloc(x, __FILE__, __LINE__)
#define realloc(p, x) _drealloc(p, x, __FILE__, __LINE__)
#define free(x) _dfree(x, __FILE__, __LINE__)
#define strdup(x) _strdup(x, __FILE__, __LINE__)

#endif
