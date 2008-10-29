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

static int facility_id_get (char *name)
{
	unsigned int i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, facilitynames[i].c_name) == 0) {
			return (facilitynames[i].c_val);
		}
	}
	return (-1);
}

static int priority_id_get (char *name)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, prioritynames[i].c_name) == 0) {
			return (prioritynames[i].c_val);
		}
	}
	return (-1);
}

void ccs_read_logging(int ccsfd, char *name, int *debug, int *mode, int *facility, int *priority, char **file)
{
	char *val = NULL;
	char tmppath[PATH_MAX];
	int global_debug = 0;

	if (!*debug) {
		if (ccs_get(ccsfd, "/cluster/logging/@debug", &val) == 0) {
			if(!strcmp(val, "on")) {
				global_debug = 1;
			} else
			if(!strcmp(val, "off")) {
				global_debug = 0;
			}
			free(val);
			val = NULL;
		}

		memset(tmppath, 0, PATH_MAX);
		snprintf(tmppath, PATH_MAX - 1, "/cluster/logging/logger_subsys[@subsys=\"%s\"]/@debug", name);
		if (ccs_get(ccsfd, tmppath, &val) == 0) {
			if(!strcmp(val, "on")) {
				*debug = 1;
			} else
			if(!strcmp(val, "off")) { /* debug from cmdline/envvars override config */
				*debug = 0;
			}
			free(val);
			val = NULL;
		} else {
			*debug = global_debug; /* global debug overrides subsystem only if latter is not specified */
			*priority = LOG_DEBUG;
		}

		memset(tmppath, 0, PATH_MAX);
		snprintf(tmppath, PATH_MAX - 1, "/cluster/logging/logger_subsys[@subsys=\"%s\"]/@syslog_level", name);
		if (ccs_get(ccsfd, tmppath, &val) == 0) {
			*priority = priority_id_get (val);
			if (*priority < 0)
				*priority = SYSLOGLEVEL;

			if (!*debug)
				if (*priority == LOG_DEBUG)
					*debug = 1;

			free(val);
			val = NULL;
		}
	} else
		*priority = LOG_DEBUG;

	if (ccs_get(ccsfd, "/cluster/logging/@to_stderr", &val) == 0) {
		if(!strcmp(val, "yes")) {
			*mode |= LOG_MODE_OUTPUT_STDERR;
		} else
		if(!strcmp(val, "no")) {
			*mode &= ~LOG_MODE_OUTPUT_STDERR;
		}
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@to_syslog", &val) == 0) {
		if(!strcmp(val, "yes")) {
			*mode |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
		} else
		if(!strcmp(val, "no")) {
			*mode &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;
		}
		free(val);
		val = NULL;
	}
	if (ccs_get(ccsfd, "/cluster/logging/@to_file", &val) == 0) {
		if(!strcmp(val, "yes")) {
			*mode |= LOG_MODE_OUTPUT_FILE;
		} else
		if(!strcmp(val, "no")) {
			*mode &= ~LOG_MODE_OUTPUT_FILE;
		}
		free(val);
		val = NULL;
	}
	if (ccs_get(ccsfd, "/cluster/logging/@logfile", &val) == 0) {
		*file = strdup(val);
		free(val);
		val = NULL;
	}
	if (ccs_get(ccsfd, "/cluster/logging/@syslog_facility", &val) == 0) {
		*facility = facility_id_get (val);
		if (*facility < 0)
			*facility = SYSLOGFACILITY;

		free(val);
		val = NULL;
	}

	return;
}
