/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "fd.h"
#include "ccs.h"

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

#define OUR_NAME_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define CLEAN_START_PATH "/cluster/fence_daemon/@clean_start"
#define POST_JOIN_DELAY_PATH "/cluster/fence_daemon/@post_join_delay"
#define POST_FAIL_DELAY_PATH "/cluster/fence_daemon/@post_fail_delay"
#define OVERRIDE_PATH_PATH "/cluster/fence_daemon/@override_path"
#define OVERRIDE_TIME_PATH "/cluster/fence_daemon/@override_time"

int read_ccs(struct fd *fd)
{
	char path[256];
	char *str;
	int error, cd, i = 0, count = 0;

	cd = open_ccs();
	if (cd < 0)
		return cd;

	/* Our own nodename must be in cluster.conf before we're allowed to
	   join the fence domain and then mount gfs; other nodes need this to
	   fence us. */

	str = NULL;
	memset(path, 0, 256);
	snprintf(path, 256, OUR_NAME_PATH, our_name);

	error = ccs_get(cd, path, &str);
	if (error || !str) {
		log_error("local cman node name \"%s\" not found in the "
			  "configuration", our_name);
		return error;
	}
	if (str)
		free(str);

	/* The comline config options are initially set to the defaults,
	   then options are read from the command line to override the
	   defaults, for options not set on command line, we look for
	   values set in cluster.conf. */

	if (!comline.groupd_compat_opt)
		read_ccs_int(cd, GROUPD_COMPAT_PATH, &comline.groupd_compat);
	if (!comline.clean_start_opt)
		read_ccs_int(cd, CLEAN_START_PATH, &comline.clean_start);
	if (!comline.post_join_delay_opt)
		read_ccs_int(cd, POST_JOIN_DELAY_PATH, &comline.post_join_delay);
	if (!comline.post_fail_delay_opt)
		read_ccs_int(cd, POST_FAIL_DELAY_PATH, &comline.post_fail_delay);
	if (!comline.override_time_opt)
		read_ccs_int(cd, OVERRIDE_TIME_PATH, &comline.override_time);

	if (!comline.override_path_opt) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, OVERRIDE_PATH_PATH);

		error = ccs_get(cd, path, &str);
		if (!error && str) {
			free(comline.override_path);
			comline.override_path = strdup(str);
		}
		if (str)
			free(str);
	}

	if (comline.clean_start) {
		log_debug("clean start, skipping initial nodes");
		goto out;
	}

	for (i = 1; ; i++) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		add_complete_node(fd, atoi(str));
		free(str);
		count++;
	}

	log_debug("added %d nodes from ccs", count);
 out:
	ccs_disconnect(cd);
	return 0;
}

