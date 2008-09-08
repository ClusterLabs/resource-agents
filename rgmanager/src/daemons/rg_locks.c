#include <pthread.h>
#include <stdio.h>
#ifdef NO_CCS
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <string.h>
char*xpath_get_one(xmlDocPtr, xmlXPathContextPtr, char *);
#else
#include <ccs.h>
#endif

static int __rg_quorate = 0;
static int __rg_lock = 0;
static int __rg_threadcnt = 0;
static int __rg_initialized = 0;

static int _rg_statuscnt = 0;
static int _rg_statusmax = 5; /* XXX */

static pthread_cond_t unlock_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t zero_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t init_cond = PTHREAD_COND_INITIALIZER;

#ifdef WRAP_LOCKS
static pthread_mutex_t locks_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
static pthread_mutex_t _ccs_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _ccs_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef NO_CCS
static xmlDocPtr ccs_doc = NULL;
static char *conffile = DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE;
#endif

int
rg_initialized(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_initialized;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_set_initialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	__rg_initialized = 1;
	pthread_cond_broadcast(&init_cond);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_uninitialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	__rg_initialized = 0;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_wait_initialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	while (!__rg_initialized)
		pthread_cond_wait(&init_cond, &locks_mutex);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


/**
  not sure if ccs is thread safe or not
  */
int
ccs_lock(void)
#ifndef NO_CCS
{
	int ret;
	pthread_mutex_lock(&_ccs_mutex);
       	ret = ccs_connect();
	if (ret < 0) {
		pthread_mutex_unlock(&_ccs_mutex);
		return -1;
	}
	return ret;
}
#else /* No ccs support */
{
	pthread_mutex_lock(&_ccs_mutex);
	xmlInitParser();
       	ccs_doc = xmlParseFile(conffile);
	xmlCleanupParser();
	if (!ccs_doc)
		return -1;
	return 0;
}
#endif


int
#ifndef NO_CCS
ccs_unlock(int fd)
{
	int ret;

       	ret = ccs_disconnect(fd);
	pthread_mutex_unlock(&_ccs_mutex);
	if (ret < 0) {
		return -1;
	}
	return 0;
}
#else
ccs_unlock(int __attribute__((unused)) fd)
{
	xmlFreeDoc(ccs_doc);
	ccs_doc = NULL;
	pthread_mutex_unlock(&_ccs_mutex);
	return 0;
}


void
conf_setconfig(char *path)
{
	pthread_mutex_lock(&_ccs_mutex);
	conffile = path;
	pthread_mutex_unlock(&_ccs_mutex);
}


int
conf_get(char *path, char **value)
{
	char *foo;
	xmlXPathContextPtr ctx;

	ctx = xmlXPathNewContext(ccs_doc);
	foo = xpath_get_one(ccs_doc, ctx, path);
	xmlXPathFreeContext(ctx);

	if (foo) {
		*value = foo;
		return 0;	
	}
	return 1;
}
#endif


int
rg_lockall(int flag)
{
	pthread_mutex_lock(&locks_mutex);
	if (!__rg_lock)
		__rg_lock |= flag;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_locked(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_lock;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_unlockall(int flag)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_lock)
		__rg_lock &= ~flag;
	pthread_cond_broadcast(&unlock_cond);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_quorate(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (!__rg_quorate)
		__rg_quorate = 1;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_inquorate(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_quorate)
		__rg_quorate = 0;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_quorate(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_quorate;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_inc_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	++__rg_threadcnt;
#ifdef DEBUG
	printf("%s: %d threads active\n", __FILE__, __rg_threadcnt);
#endif
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_dec_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	--__rg_threadcnt;
	if (__rg_threadcnt <= 0) {
		__rg_threadcnt = 0;
		pthread_cond_broadcast(&zero_cond);
	}
#ifdef DEBUG
	printf("%s: %d threads active\n", __FILE__, __rg_threadcnt);
#endif
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_statusmax(int max)
{
	int old;
	
	if (max <= 3)
		max = 3;
	
	pthread_mutex_lock(&locks_mutex);
	old = _rg_statusmax;
	_rg_statusmax = max;
	pthread_mutex_unlock(&locks_mutex);
	return old;
}


int
rg_inc_status(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (_rg_statuscnt >= _rg_statusmax) {
		pthread_mutex_unlock(&locks_mutex);
		return -1;
	}
	++_rg_statuscnt;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_dec_status(void)
{
	pthread_mutex_lock(&locks_mutex);
	--_rg_statuscnt;
	if (_rg_statuscnt < 0)
		_rg_statuscnt = 0;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_wait_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_threadcnt)
		pthread_cond_wait(&zero_cond, &locks_mutex);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}
