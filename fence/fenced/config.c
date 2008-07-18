#include "fd.h"
#include "config.h"
#include "ccs.h"

static int ccs_handle;

/* was a config value set on command line?, 0 or 1. */

int optd_groupd_compat;
int optd_clean_start;
int optd_post_join_delay;
int optd_post_fail_delay;
int optd_override_time;
int optd_override_path;

/* actual config value from command line, cluster.conf, or default. */

int cfgd_groupd_compat   = DEFAULT_GROUPD_COMPAT;
int cfgd_clean_start     = DEFAULT_CLEAN_START;
int cfgd_post_join_delay = DEFAULT_POST_JOIN_DELAY;
int cfgd_post_fail_delay = DEFAULT_POST_FAIL_DELAY;
int cfgd_override_time   = DEFAULT_OVERRIDE_TIME;
char *cfgd_override_path = DEFAULT_OVERRIDE_PATH;

int setup_ccs(void)
{
	int i = 0, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check cluster status", cd);
	}

	ccs_handle = cd;
	return 0;
}

void close_ccs(void)
{
	ccs_disconnect(ccs_handle);
}

void read_ccs_name(char *path, char *name)
{
	char *str;
	int error;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	strcpy(name, str);

	free(str);
}

void read_ccs_yesno(char *path, int *yes, int *no)
{
	char *str;
	int error;

	*yes = 0;
	*no = 0;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	if (!strcmp(str, "yes"))
		*yes = 1;

	else if (!strcmp(str, "no"))
		*no = 1;

	free(str);
}

void read_ccs_int(char *path, int *config_val)
{
	char *str;
	int val;
	int error;

	error = ccs_get(ccs_handle, path, &str);
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

#define OUR_NAME_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define CLEAN_START_PATH "/cluster/fence_daemon/@clean_start"
#define POST_JOIN_DELAY_PATH "/cluster/fence_daemon/@post_join_delay"
#define POST_FAIL_DELAY_PATH "/cluster/fence_daemon/@post_fail_delay"
#define OVERRIDE_PATH_PATH "/cluster/fence_daemon/@override_path"
#define OVERRIDE_TIME_PATH "/cluster/fence_daemon/@override_time"

int read_ccs(struct fd *fd)
{
	char path[PATH_MAX];
	char *str;
	int error, i = 0, count = 0;

	/* Our own nodename must be in cluster.conf before we're allowed to
	   join the fence domain and then mount gfs; other nodes need this to
	   fence us. */

	str = NULL;
	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path), OUR_NAME_PATH, our_name);

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str) {
		log_error("local cman node name \"%s\" not found in the "
			  "configuration", our_name);
		return error;
	}
	if (str)
		free(str);

	if (!optd_groupd_compat)
		read_ccs_int(GROUPD_COMPAT_PATH, &cfgd_groupd_compat);
	if (!optd_clean_start)
		read_ccs_int(CLEAN_START_PATH, &cfgd_clean_start);
	if (!optd_post_join_delay)
		read_ccs_int(POST_JOIN_DELAY_PATH, &cfgd_post_join_delay);
	if (!optd_post_fail_delay)
		read_ccs_int(POST_FAIL_DELAY_PATH, &cfgd_post_fail_delay);
	if (!optd_override_time)
		read_ccs_int(OVERRIDE_TIME_PATH, &cfgd_override_time);

	if (!optd_override_path) {
		str = NULL;
		memset(path, 0, sizeof(path));
		sprintf(path, OVERRIDE_PATH_PATH);

		error = ccs_get(ccs_handle, path, &str);
		if (!error && str)
			cfgd_override_path = strdup(str);
		if (str)
			free(str);
	}

	if (cfgd_clean_start) {
		log_debug("clean start, skipping initial nodes");
		goto out;
	}

	for (i = 1; ; i++) {
		str = NULL;
		memset(path, 0, sizeof(path));
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(ccs_handle, path, &str);
		if (error || !str)
			break;

		add_complete_node(fd, atoi(str));
		free(str);
		count++;
	}

	log_debug("added %d nodes from ccs", count);
 out:
	return 0;
}

