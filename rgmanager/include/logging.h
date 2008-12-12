#ifndef _LOGGING_H
#define _LOGGING_H

/* #include <corosync/engine/logsys.h>*/
#include <liblogthread.h>

void init_logging(char *name, int foreground, int debugging);
void setup_logging(int ccs_handle);
void close_logging(void);

#endif
