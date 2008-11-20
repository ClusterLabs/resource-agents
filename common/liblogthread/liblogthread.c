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
	time_t time;
};

static struct entry *ents;
static unsigned int num_ents = DEFAULT_ENTRIES;
static unsigned int head_ent, tail_ent; /* add at head, remove from tail */
static unsigned int dropped;
static unsigned int pending_ents;
static unsigned int done;
static pthread_t thread_handle;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static int logt_mode; /* LOG_MODE_ */
static int logt_syslog_facility;
static int logt_syslog_priority;
static int logt_logfile_priority;
static char logt_name[PATH_MAX];
static char logt_logfile[PATH_MAX];
static FILE *logt_logfile_fp;

static char *_time(time_t *t)
{
	static char buf[64];

	strftime(buf, sizeof(buf), "%b %d %T", localtime(t));
	return buf;
}

static void write_entry(int level, time_t *t, char *str)
{
	if ((logt_mode & LOG_MODE_OUTPUT_FILE) &&
	    (level <= logt_logfile_priority) && logt_logfile_fp) {
		fprintf(logt_logfile_fp, "%s %s %s", _time(t), logt_name, str);
		fflush(logt_logfile_fp);
	}
	if ((logt_mode & LOG_MODE_OUTPUT_SYSLOG) &&
	    (level <= logt_syslog_priority))
		syslog(level, "%s", str);
}

static void write_dropped(int level, time_t *t, int num)
{
	char str[ENTRY_STR_LEN];
	sprintf(str, "dropped %d entries", num);
	write_entry(level, t, str);
}

static void *thread_fn(void *arg)
{
	char str[ENTRY_STR_LEN];
	struct entry *e;
	time_t time;
	int level, prev_dropped = 0;

	while (1) {
		pthread_mutex_lock(&mutex);
		while (head_ent == tail_ent) {
			if (done) {
				pthread_mutex_unlock(&mutex);
				goto out;
			}
			pthread_cond_wait(&cond, &mutex);
		}

		e = &ents[tail_ent++];
		tail_ent = tail_ent % num_ents;
		pending_ents--;

		memcpy(str, e->str, ENTRY_STR_LEN);
		level = e->level;
		time = e->time;

		prev_dropped = dropped;
		dropped = 0;
		pthread_mutex_unlock(&mutex);

		if (prev_dropped) {
			write_dropped(level, &time, prev_dropped);
			prev_dropped = 0;
		}

		write_entry(level, &time, str);
	}
 out:
	pthread_exit(NULL);
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
	e->time = time(NULL);
 out:
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

void logt_print(int level, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	/* this stderr crap really doesn't belong in this lib, please
	   feel free to not use it */
	if (logt_mode & LOG_MODE_OUTPUT_STDERR)
		vfprintf(stderr, fmt, ap);

	if (level > logt_syslog_priority && level > logt_logfile_priority)
		return;

	_logt_print(level, fmt, ap);
	va_end(ap);
}

static void _conf(char *name, int mode, int syslog_facility,
		  int syslog_priority, int logfile_priority, char *logfile)
{
	int fd;

	pthread_mutex_lock(&mutex);
	logt_mode = mode;
	logt_syslog_facility = syslog_facility;
	logt_syslog_priority = syslog_priority;
	logt_logfile_priority = logfile_priority;
	if (name)
		strncpy(logt_name, name, PATH_MAX);
	if (logfile)
		strncpy(logt_logfile, logfile, PATH_MAX);

	if (logt_mode & LOG_MODE_OUTPUT_FILE && logt_logfile[0]) {
		if (logt_logfile_fp)
			fclose(logt_logfile_fp);
		logt_logfile_fp = fopen(logt_logfile, "a+");
		if (logt_logfile_fp != NULL) {
			fd = fileno(logt_logfile_fp);
			fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
		}
	}

	if (logt_mode & LOG_MODE_OUTPUT_SYSLOG) {
		closelog();
		openlog(logt_name, LOG_CONS | LOG_PID, logt_syslog_facility);
	}
	pthread_mutex_unlock(&mutex);
}

void logt_conf(char *name, int mode, int syslog_facility, int syslog_priority,
	       int logfile_priority, char *logfile)
{
	_conf(name, mode, syslog_facility, syslog_priority, logfile_priority,
	      logfile);
}

int logt_init(char *name, int mode, int syslog_facility, int syslog_priority,
	      int logfile_priority, char *logfile)
{
	_conf(name, mode, syslog_facility, syslog_priority, logfile_priority,
	      logfile);

	ents = malloc(num_ents * sizeof(struct entry));
	if (!ents)
		return -1;
	memset(ents, 0, num_ents * sizeof(struct entry));

	return pthread_create(&thread_handle, NULL, thread_fn, NULL);
}

void logt_exit(void)
{
	pthread_mutex_lock(&mutex);
	done = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
	pthread_join(thread_handle, NULL);
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
	free(ents);
}

#ifdef TEST
int main(int argc, char **argv)
{
	logt_init("test", LOG_MODE_OUTPUT_FILE|LOG_MODE_OUTPUT_SYSLOG,
		  LOG_DAEMON, LOG_DEBUG, LOG_DEBUG, "/tmp/logthread");
	logt_print(LOG_DEBUG, "debugging message %d\n", argc);
	logt_print(LOG_ERR, "error message %d\n", argc);
	sleep(1);
	logt_print(LOG_DEBUG, "second debug message\n");
	logt_exit();
	return 0;
}
#endif

