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

static int logt_mode; /* LOG_MODE_ */
static int logt_syslog_facility;
static int logt_syslog_priority;
static int logt_logfile_priority;
static char logt_name[PATH_MAX];
static char logt_logfile[PATH_MAX];
static FILE *logt_logfile_fp;

static char *_time(void)
{
	static char buf[64];
	time_t t = time(NULL);

	strftime(buf, sizeof(buf), "%b %d %T", localtime(&t));
	return buf;
}

static void write_entry(int level, char *str)
{
	if ((logt_mode & LOG_MODE_OUTPUT_FILE) &&
	    (level <= logt_logfile_priority) && logt_logfile_fp) {
		fprintf(logt_logfile_fp, "%s %s %s", _time(), logt_name, str);
		fflush(logt_logfile_fp);
	}
	if ((logt_mode & LOG_MODE_OUTPUT_SYSLOG) &&
	    (level <= logt_syslog_priority))
		syslog(level, "%s", str);
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

	if (level > logt_syslog_priority && level > logt_logfile_priority)
		return;

	va_start(ap, fmt);
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
	pthread_attr_t attr;
	int rv;

	_conf(name, mode, syslog_facility, syslog_priority, logfile_priority,
	      logfile);

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
	logt_init("test", LOG_MODE_OUTPUT_FILE|LOG_MODE_OUTPUT_SYSLOG,
		  LOG_DAEMON, LOG_DEBUG, LOG_DEBUG, "/tmp/logthread");
	logt_print(LOG_DEBUG, "test debugging message %d\n", argc);
	logt_print(LOG_ERR, "test error message %d\n", argc);
	logt_exit();
	return 0;
}
#endif

