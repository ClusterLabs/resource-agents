#ifndef LOGTHREAD_DOT_H
#define LOGTHREAD_DOT_H

#include <syslog.h>

#define LOG_MODE_OUTPUT_FILE			1
#define LOG_MODE_OUTPUT_STDERR			2
#define LOG_MODE_OUTPUT_SYSLOG_THREADED		4
#define LOG_MODE_FILTER_DEBUG_FROM_SYSLOG	8

int logt_init(char *name, int mode, int facility, int priority, char *file);
void logt_conf(char *name, int mode, int facility, int priority, char *file);
void logt_exit(void);
void logt_print(int level, char *fmt, ...)
	__attribute__((format(printf, 2, 3)));;

#endif
