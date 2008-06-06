#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>

#include "gfs_daemon.h"
#include "config.h"
#include "ccs.h"

/* was a config value set on command line?, 0 or 1.
   optk is a kernel option, optd is a daemon option */

int optd_groupd_compat;
int optd_enable_withdraw;
int optd_enable_plock;
int optd_plock_debug;
int optd_plock_rate_limit;
int optd_plock_ownership;
int optd_drop_resources_time;
int optd_drop_resources_count;
int optd_drop_resources_age;

/* actual config value from command line, cluster.conf, or default.
   cfgk is a kernel config value, cfgd is a daemon config value */

int cfgd_groupd_compat		= DEFAULT_GROUPD_COMPAT;
int cfgd_enable_withdraw	= DEFAULT_ENABLE_WITHDRAW;
int cfgd_enable_plock		= DEFAULT_ENABLE_PLOCK;
int cfgd_plock_debug		= DEFAULT_PLOCK_DEBUG;
int cfgd_plock_rate_limit	= DEFAULT_PLOCK_RATE_LIMIT;
int cfgd_plock_ownership	= DEFAULT_PLOCK_OWNERSHIP;
int cfgd_drop_resources_time	= DEFAULT_DROP_RESOURCES_TIME;
int cfgd_drop_resources_count	= DEFAULT_DROP_RESOURCES_COUNT;
int cfgd_drop_resources_age	= DEFAULT_DROP_RESOURCES_AGE;


static int open_ccs(void)
{
	int i = 0, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}
	return cd;
}

static void read_ccs_int(int cd, char *path, int *config_val)
{
	char *str;
	int val;
	int error;

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return;

	val = atoi(str);

	if (val < 0) {
		log_error("ignore invalid value %d for %s", val, path);
		return;
	}

	*config_val = val;
	log_debug("%s is %u", path, val);
	free(str);
}

#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define ENABLE_WITHDRAW_PATH "/cluster/gfs_controld/@enable_withdraw"
#define ENABLE_PLOCK_PATH "/cluster/gfs_controld/@enable_plock"
#define PLOCK_DEBUG_PATH "/cluster/gfs_controld/@plock_debug"
#define PLOCK_RATE_LIMIT_PATH "/cluster/gfs_controld/@plock_rate_limit"
#define PLOCK_OWNERSHIP_PATH "/cluster/gfs_controld/@plock_ownership"
#define DROP_RESOURCES_TIME_PATH "/cluster/gfs_controld/@drop_resources_time"
#define DROP_RESOURCES_COUNT_PATH "/cluster/gfs_controld/@drop_resources_count"
#define DROP_RESOURCES_AGE_PATH "/cluster/gfs_controld/@drop_resources_age"

/* These config values are set from cluster.conf only if they haven't already
   been set on the command line. */

void read_ccs(void)
{
	int cd;

	cd = open_ccs();
	if (cd < 0)
		return;

	if (!optd_groupd_compat)
		read_ccs_int(cd, GROUPD_COMPAT_PATH, &cfgd_groupd_compat);
	if (!optd_enable_withdraw)
		read_ccs_int(cd, ENABLE_WITHDRAW_PATH, &cfgd_enable_withdraw);
	if (!optd_enable_plock)
		read_ccs_int(cd, ENABLE_PLOCK_PATH, &cfgd_enable_plock);
	if (!optd_plock_debug)
		read_ccs_int(cd, PLOCK_DEBUG_PATH, &cfgd_plock_debug);
	if (!optd_plock_rate_limit)
		read_ccs_int(cd, PLOCK_RATE_LIMIT_PATH, &cfgd_plock_rate_limit);
	if (!optd_plock_ownership)
		read_ccs_int(cd, PLOCK_OWNERSHIP_PATH, &cfgd_plock_ownership);
	if (!optd_drop_resources_time)
		read_ccs_int(cd, DROP_RESOURCES_TIME_PATH, &cfgd_drop_resources_time);
	if (!optd_drop_resources_count)
		read_ccs_int(cd, DROP_RESOURCES_COUNT_PATH, &cfgd_drop_resources_count);
	if (!optd_drop_resources_age)
		read_ccs_int(cd, DROP_RESOURCES_AGE_PATH, &cfgd_drop_resources_age);

	ccs_disconnect(cd);
}

#define LOCKSPACE_NODIR "/cluster/dlm/lockspace[@name=\"%s\"]/@nodir"

void read_ccs_nodir(struct mountgroup *mg, char *buf)
{
	char path[PATH_MAX];
	char *str;
	int val;
	int error;
	int cd;

	cd = open_ccs();
	if (cd < 0)
		return;

	memset(path, 0, PATH_MAX);
	sprintf(path, LOCKSPACE_NODIR, mg->name);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return;

	val = atoi(str);

	if (val < 0) {
		log_error("ignore invalid value %d for %s", val, path);
		return;
	}

	snprintf(buf, 32, ":nodir=%d", val);

	log_debug("%s is %u", path, val);
	free(str);

	ccs_disconnect(cd);
}

