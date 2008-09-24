#ifndef _LOGGING_H
#define _LOGGING_H

#include <corosync/engine/logsys.h>

void init_logging(void);
void setup_logging(int ccs_handle);
void close_logging(void);

#endif
