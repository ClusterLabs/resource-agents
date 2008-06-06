#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <inttypes.h>
#include "gfs2_quota.h"

uint32_t
name_to_id(int user, char *name, int numbers)
{
	struct passwd *u;
	struct group *g;
	uint32_t id;
	int ok = FALSE;

	if (numbers) {
	} else if (user) {
		u = getpwnam(name);
		if (u) {
			id = u->pw_uid;
			ok = TRUE;
		}
	} else {
		g = getgrnam(name);
		if (g) {
			id = g->gr_gid;
			ok = TRUE;
		}
	}

	if (!ok) {
		if (!isdigit(name[0]))
			die("can't find %s %s\n",
			    (user) ? "user" : "group",
			    name);
		sscanf(name, "%u", &id);
	}

	return id;
}

char *
id_to_name(int user, uint32_t id, int numbers)
{
	struct passwd *u;
	struct group *g;
	static char name[256];
	int ok = FALSE;

	if (numbers) {
	} else if (user) {
		u = getpwuid(id);
		if (u) {
			strcpy(name, u->pw_name);
			ok = TRUE;
		}
	} else {
		g = getgrgid(id);
		if (g) {
			strcpy(name, g->gr_name);
			ok = TRUE;
		}
	}

	if (!ok)
		sprintf(name, "%u", id);

	return name;
}
