#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <openais/service/logsys.h>
#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "logging.h"

LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

int subsys_mask = 0;

void set_debuglog(int subsystems)
{
	subsys_mask = subsystems;
}
