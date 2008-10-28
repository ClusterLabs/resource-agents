#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/param.h>

#include "liblogthread.h"

#define DEFAULT_ENTRIES 4096
#define ENTRY_STR_LEN 128

struct entry {
	int level;
	char str[ENTRY_STR_LEN];
};

static struct entry *ents;
static unsigned int num_ents = DEFAULT_ENTRIES;
static unsigned int head_ent, tail_ent; /* add at head, remove from tail */
static unsigned int dropped;
static unsigned int pending_ents;
static pthread_t thread_handle;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static int logt_mode;
static int logt_facility;
static int logt_priority;
static char logt_name[PATH_MAX];
static char logt_file[PATH_MAX];
static FILE *logt_file_fp;


static void write_entry(int level, char *str)
{
	if (logt_mode & LOG_MODE_OUTPUT_FILE && logt_file_fp) {
		fprintf(logt_file_fp, "%s %s", logt_name, str);
		fflush(logt_file_fp);
	}
	if (logt_mode & LOG_MODE_OUTPUT_STDERR) {
		fprintf(stderr, "%s", str);
		fflush(stderr);
	}
	if (logt_mode & LOG_MODE_OUTPUT_SYSLOG_THREADED) {
		if ((logt_mode & LOG_MODE_FILTER_DEBUG_FROM_SYSLOG) &&
		    (level == LOG_DEBUG))
			return;
		syslog(level, str);
	}
}

static void write_dropped(int level, int num)
{
	char str[ENTRY_STR_LEN];
	sprintf(str, "dropped %d entries", num);
	write_entry(level, str);
}

static void *thread_fn(void *arg)
{
	char str[ENTRY_STR_LEN];
	struct entry *e;
	int level, prev_dropped = 0;

	while (1) {
		pthread_mutex_lock(&mutex);
		while (head_ent == tail_ent) {
			pthread_cond_wait(&cond, &mutex);
		}

		e = &ents[tail_ent++];
		tail_ent = tail_ent % num_ents;
		pending_ents--;

		memcpy(str, e->str, ENTRY_STR_LEN);
		level = e->level;

		prev_dropped = dropped;
		dropped = 0;
		pthread_mutex_unlock(&mutex);

		if (prev_dropped) {
			write_dropped(level, prev_dropped);
			prev_dropped = 0;
		}

		write_entry(level, str);
	}
}

static void _logt_print(int level, char *fmt, va_list ap)
{
	struct entry *e;

	pthread_mutex_lock(&mutex);

	if (pending_ents == num_ents) {
		dropped++;
		goto out;
	}

	e = &ents[head_ent++];
	head_ent = head_ent % num_ents;
	pending_ents++;

	memset(e->str, 0, ENTRY_STR_LEN);
	vsnprintf(e->str, ENTRY_STR_LEN - 1, fmt, ap);
	e->level = level;
 out:
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&cond);
}

void logt_print(int level, char *fmt, ...)
{
	va_list ap;

	if (level > logt_priority)
		return;

	va_start(ap, fmt);
	_logt_print(level, fmt, ap);
	va_end(ap);
}

static void _conf(char *name, int mode, int facility, int priority, char *file)
{
	int fd;

	pthread_mutex_lock(&mutex);
	logt_mode = mode;
	logt_facility = facility;
	logt_priority = priority;
	if (name)
		strncpy(logt_name, name, PATH_MAX);
	if (file)
		strncpy(logt_file, file, PATH_MAX);

	if (logt_mode & LOG_MODE_OUTPUT_FILE && logt_file[0]) {
		if (logt_file_fp)
			fclose(logt_file_fp);
		logt_file_fp = fopen(logt_file, "a+");
		fd = fileno(logt_file_fp);
		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	}

	if (logt_mode & LOG_MODE_OUTPUT_SYSLOG_THREADED) {
		closelog();
		openlog(logt_name, LOG_CONS | LOG_PID, logt_facility);
	}
	pthread_mutex_unlock(&mutex);
}

void logt_conf(char *name, int mode, int facility, int priority, char *file)
{
	_conf(name, mode, facility, priority, file);
}

int logt_init(char *name, int mode, int facility, int priority, char *file)
{
	pthread_attr_t attr;
	int rv;

	_conf(name, mode, facility, priority, file);

	ents = malloc(num_ents * sizeof(struct entry));
	if (!ents)
		return -1;
	memset(ents, 0, num_ents * sizeof(struct entry));

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	rv = pthread_create(&thread_handle, &attr, thread_fn, NULL);

	pthread_attr_destroy(&attr);
	return rv;
}

void logt_exit(void)
{
	void *status;
	int i = 0;

	/* there must be a better way of ensuring the thread
	   finishes its work before terminating from cancel */
	while (i++ < 100) {
		pthread_mutex_lock(&mutex);
		if (!pending_ents) {
			pthread_mutex_unlock(&mutex);
			break;
		}
		pthread_mutex_unlock(&mutex);
		usleep(5000);
	}
	pthread_cancel(thread_handle);
	pthread_join(thread_handle, &status);
	free(ents);
}

#ifdef TEST
int main(int argc, char **argv)
{
	logt_init("test", 0xF, LOG_DAEMON, LOG_DEBUG, "/tmp/logthread");

	logt_print(1, "first message %d\n", argc);
	logt_print(2, "%ld second %d %s\n", time(NULL), 2, "hi");
	sleep(1);
	logt_print(3, "third message\n");

	logt_exit();

	fflush(stdout);
	return 0;
}
#endif

