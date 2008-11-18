#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#define SYSLOG_NAMES
#include <syslog.h>
#include <liblogthread.h>

#include "ccs.h"

/**
 * ccs_lookup_nodename
 * @cd: ccs descriptor
 * @nodename: node name string
 * @retval: pointer to location to assign the result, if found
 *
 * This function takes any valid representation (FQDN, non-qualified
 * hostname, IP address, IPv6 address) of a node's name and finds its
 * canonical name (per cluster.conf). This function will find the primary
 * node name if passed a node's "altname" or any valid representation
 * of it.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_lookup_nodename(int cd, const char *nodename, char **retval)
{
	char path[256];
	char host_only[128];
	char *str;
	char *p;
	int error;
	int ret;
	unsigned int i;
	size_t nodename_len;
	struct addrinfo hints;

	if (nodename == NULL)
		return (-1);

	nodename_len = strlen(nodename);
	ret = snprintf(path, sizeof(path),
		       "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name",
		       nodename);
	if (ret < 0 || (size_t) ret >= sizeof(path)) {
		errno = E2BIG;
		return (-E2BIG);
	}

	str = NULL;
	error = ccs_get(cd, path, &str);
	if (!error) {
		*retval = str;
		return (0);
	}

	if (nodename_len >= sizeof(host_only)) {
		errno = E2BIG;
		return (-E2BIG);
	}

	/* Try just the hostname */
	strcpy(host_only, nodename);
	p = strchr(host_only, '.');
	if (p != NULL) {
		*p = '\0';

		ret = snprintf(path, sizeof(path),
			       "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name",
			       host_only);
		if (ret < 0 || (size_t) ret >= sizeof(path))
			return (-E2BIG);

		str = NULL;
		error = ccs_get(cd, path, &str);
		if (!error) {
			*retval = str;
			return (0);
		}
	}

	memset(&hints, 0, sizeof(hints));
	if (strchr(nodename, ':') != NULL)
		hints.ai_family = AF_INET6;
	else if (isdigit(nodename[nodename_len - 1]))
		hints.ai_family = AF_INET;
	else
		hints.ai_family = AF_UNSPEC;

	/*
	 ** Try to match against each clusternode in cluster.conf.
	 */
	for (i = 1;; i++) {
		char canonical_name[128];
		unsigned int altcnt;

		ret = snprintf(path, sizeof(path),
			       "/cluster/clusternodes/clusternode[%u]/@name",
			       i);
		if (ret < 0 || (size_t) ret >= sizeof(path))
			continue;

		for (altcnt = 0;; altcnt++) {
			size_t len;
			struct addrinfo *ai = NULL;
			char cur_node[128];

			if (altcnt != 0) {
				ret = snprintf(path, sizeof(path),
					       "/cluster/clusternodes/clusternode[%u]/altname[%u]/@name",
					       i, altcnt);
				if (ret < 0 || (size_t) ret >= sizeof(path))
					continue;
			}

			str = NULL;
			error = ccs_get(cd, path, &str);
			if (error || !str) {
				if (altcnt == 0)
					goto out_fail;
				break;
			}

			if (altcnt == 0) {
				if (strlen(str) >= sizeof(canonical_name)) {
					free(str);
					errno = E2BIG;
					return (-E2BIG);
				}
				strcpy(canonical_name, str);
			}

			if (strlen(str) >= sizeof(cur_node)) {
				free(str);
				errno = E2BIG;
				return (-E2BIG);
			}

			strcpy(cur_node, str);

			p = strchr(cur_node, '.');
			if (p != NULL)
				len = p - cur_node;
			else
				len = strlen(cur_node);

			if (strlen(host_only) == len &&
			    !strncasecmp(host_only, cur_node, len)) {
				free(str);
				*retval = strdup(canonical_name);
				if (*retval == NULL) {
					errno = ENOMEM;
					return (-ENOMEM);
				}
				return (0);
			}

			if (getaddrinfo(str, NULL, &hints, &ai) == 0) {
				struct addrinfo *cur;

				for (cur = ai; cur != NULL; cur = cur->ai_next) {
					char hostbuf[512];
					if (getnameinfo
					    (cur->ai_addr, cur->ai_addrlen,
					     hostbuf, sizeof(hostbuf), NULL, 0,
					     hints.ai_family !=
					     AF_UNSPEC ? NI_NUMERICHOST : 0)) {
						continue;
					}

					if (!strcasecmp(hostbuf, nodename)) {
						freeaddrinfo(ai);
						free(str);
						*retval =
						    strdup(canonical_name);
						if (*retval == NULL) {
							errno = ENOMEM;
							return (-ENOMEM);
						}
						return (0);
					}
				}
				freeaddrinfo(ai);
			}

			free(str);

			/* Now try any altnames */
		}
	}

out_fail:
	errno = EINVAL;
	*retval = NULL;
	return (-1);
}

static int facility_id_get(char *name)
{
	unsigned int i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, facilitynames[i].c_name) == 0) {
			return (facilitynames[i].c_val);
		}
	}
	return (-1);
}

static int priority_id_get(char *name)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, prioritynames[i].c_name) == 0) {
			return (prioritynames[i].c_val);
		}
	}
	return (-1);
}

/* requires string buffer to be PATH_MAX */
static void read_string(int fd, char *path, char *string)
{
	char *str;
	int error;

	error = ccs_get(fd, path, &str);
	if (error || !str)
		return;

	memset(string, 0, PATH_MAX);
	strcpy(string, str);

	free(str);
}

static void read_yesno(int fd, char *path, int *yes, int *no)
{
	char *str;
	int error;

	*yes = 0;
	*no = 0;

	error = ccs_get(fd, path, &str);
	if (error || !str)
		return;

	if (!strcmp(str, "yes"))
		*yes = 1;
	else if (!strcmp(str, "no"))
		*no = 1;

	free(str);
}

static void read_onoff(int fd, char *path, int *on, int *off)
{
	char *str;
	int error;

	*on = 0;
	*off = 0;

	error = ccs_get(fd, path, &str);
	if (error || !str)
		return;

	if (!strcmp(str, "on"))
		*on = 1;
	else if (!strcmp(str, "off"))
		*off = 1;

	free(str);
}

/* requires path buffer to be PATH_MAX */
static void create_subsys_path(char *name, char *field, char *path)
{
	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX,
		 "/cluster/logging/logging_subsys[@subsys=\"%s\"]/%s",
		 name, field);
}

/* Values should be initialized to default values before calling
   this function; they are not changed if cluster.conf has nothing
   to say about them.  If *debug is already set , then *logfile_priority
   is set to LOG_DEBUG; all debug and logfile_priority values from
   cluster.conf are ignored. */

void ccs_read_logging(int fd, char *name, int *debug, int *mode,
		      int *syslog_facility, int *syslog_priority,
		      int *logfile_priority, char *logfile)
{
	char string[PATH_MAX];
	char path[PATH_MAX];
	int val, y, n, on, off;

	/*
	 * to_syslog 
	 */
	create_subsys_path(name, "to_syslog", path);

	read_yesno(fd, "/cluster/logging/@to_syslog", &y, &n);
	if (y)
		*mode |= LOG_MODE_OUTPUT_SYSLOG;
	if (n)
		*mode &= ~LOG_MODE_OUTPUT_SYSLOG;

	read_yesno(fd, path, &y, &n);
	if (y)
		*mode |= LOG_MODE_OUTPUT_SYSLOG;
	if (n)
		*mode &= ~LOG_MODE_OUTPUT_SYSLOG;

	/*
	 * to_logfile
	 */
	create_subsys_path(name, "to_logfile", path);

	read_yesno(fd, "/cluster/logging/@to_logfile", &y, &n);
	if (y)
		*mode |= LOG_MODE_OUTPUT_FILE;
	if (n)
		*mode &= ~LOG_MODE_OUTPUT_FILE;

	read_yesno(fd, path, &y, &n);
	if (y)
		*mode |= LOG_MODE_OUTPUT_FILE;
	if (n)
		*mode &= ~LOG_MODE_OUTPUT_FILE;

	/*
	 * syslog_facility
	 */
	create_subsys_path(name, "syslog_facility", path);

	read_string(fd, "/cluster/logging/@syslog_facility", string);

	if (string[0]) {
		val = facility_id_get(string);
		if (val >= 0)
			*syslog_facility = val;
	}

	read_string(fd, path, string);

	if (string[0]) {
		val = facility_id_get(string);
		if (val >= 0)
			*syslog_facility = val;
	}

	/*
	 * syslog_priority
	 */
	create_subsys_path(name, "syslog_priority", path);

	read_string(fd, "/cluster/logging/@syslog_priority", string);

	if (string[0]) {
		val = priority_id_get(string);
		if (val >= 0)
			*syslog_priority = val;
	}

	read_string(fd, path, string);

	if (string[0]) {
		val = priority_id_get(string);
		if (val >= 0)
			*syslog_priority = val;
	}

	/*
	 * logfile
	 */
	create_subsys_path(name, "logfile", path);

	read_string(fd, "/cluster/logging/@logfile", string);

	if (string[0])
		strcpy(logfile, string);

	read_string(fd, path, string);

	if (string[0])
		strcpy(logfile, string);

	/*
	 * debug is only ever turned on, not off, so if it's already on
	 * (from the daemon), then just skip the debug lookups.
	 */
	if (*debug) {
		*logfile_priority = LOG_DEBUG;
		return;
	}

	/*
	 * debug
	 * debug=on is a shortcut for logfile_priority=LOG_DEBUG
	 */
	create_subsys_path(name, "debug", path);

	read_onoff(fd, "/cluster/logging/@debug", &on, &off);
	if (on)
		*debug = 1;

	read_onoff(fd, path, &on, &off);
	if (on)
		*debug = 1;
	else if (off)
		*debug = 0;

	if (*debug)
		*logfile_priority = LOG_DEBUG;

	/* should we return here if debug has been turned on?, or should
	   we allow an explicit logfile_priority setting to override the
	   implicit setting from debug=on? */

	/*
	 * logfile_priority
	 */
	create_subsys_path(name, "logfile_priority", path);

	read_string(fd, "/cluster/logging/@logfile_priority", string);

	if (string[0]) {
		val = priority_id_get(string);
		if (val >= 0)
			*logfile_priority = val;
	}

	read_string(fd, path, string);

	if (string[0]) {
		val = priority_id_get(string);
		if (val >= 0)
			*logfile_priority = val;
	}
}

