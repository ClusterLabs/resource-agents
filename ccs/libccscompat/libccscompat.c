/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>

#include "debug.h"
#include "comm_headers.h"
#include "libccscompat.h"

#include <stdio.h>

static int fe_port = 50006;
static int comm_proto=-1;

static int setup_interface_ipv6(int *sp, int port){
  int sock = -1;
  int error = 0;
  struct sockaddr_in6 addr;
  int trueint = 1;

  memset(&addr, 0, sizeof(struct sockaddr_in6));

  sock = socket(PF_INET6, SOCK_STREAM, 0);
  if(sock < 0){
    error = -errno;
    goto fail;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int));

  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  addr.sin6_addr = in6addr_loopback;

  if(bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in6))){
    error = -errno;
    goto fail;
  }

  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons(fe_port);
  error = connect(sock, (struct sockaddr *)&addr,
		  sizeof(struct sockaddr_in6));
  if(error < 0){
    error = -errno;
    goto fail;
  }

  *sp = sock;
  return 0;

 fail:
  if(sock >= 0){
    close(sock);
  }
  return error;
}

static int setup_interface_ipv4(int *sp, int port){
  int sock = -1;
  int error = 0;
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(struct sockaddr_in));
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if(sock < 0){
    error = -errno;
    goto fail;
  }

  addr.sin_family = AF_INET;
  inet_aton("127.0.0.1", (struct in_addr *)&addr.sin_addr.s_addr);
  addr.sin_port = htons(port);

  if(bindresvport(sock, &addr)){
    error = -errno;
    goto fail;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(fe_port);
  error = connect(sock, (struct sockaddr *)&addr,
		  sizeof(struct sockaddr_in));
  if(error < 0){
    error = -errno;
    goto fail;
  }

  *sp = sock;
  return 0;

 fail:
  if(sock >= 0){
    close(sock);
  }
  return error;
}


static int
setup_interface_local(int *sp)
{
  struct sockaddr_un sun;
  int sock = -1, error = 0;

  sun.sun_family = PF_LOCAL;
  snprintf(sun.sun_path, sizeof(sun.sun_path), COMM_LOCAL_SOCKET);

  sock = socket(PF_LOCAL, SOCK_STREAM, 0);
  if (sock < 0) {
    error = errno;
    goto fail;
  }

  error = connect(sock, (struct sockaddr *)&sun, sizeof(sun));
  if (error < 0) {
    error = errno;
    goto fail;
  }

  *sp = sock;
  return PF_LOCAL;

fail:
  if (sock >= 0){
    close(sock);
  }
  return -error;
}


/**
 * setup_interface
 * @sp: pointer gets filled in with open socket
 *
 * This function (through helper functions) handles the details
 * of creating and binding a socket followed be the connect.
 *
 * Returns: AF_INET | AF_INET6 on success, or -errno on error
 */
static int setup_interface(int *sp){
  int error=-1;
  int res_port = IPPORT_RESERVED-1;
  int timo=1;
  int ipv4 = (comm_proto < 0) ? 1 : (comm_proto == PF_INET);
  int ipv6 = (comm_proto < 0) ? 1 : (comm_proto == PF_INET6);
  int local = (comm_proto < 0) ? 1 : (comm_proto == PF_LOCAL);

  srandom(getpid());

  /* Try to do a local connect first */
  if (local && !(error = setup_interface_local(sp)))
    error = PF_LOCAL;

  if (error < 0) {
    for(; res_port >= 512; res_port--){
      if (ipv6 && !(error = setup_interface_ipv6(sp, res_port))){
        error = PF_INET6;
        break;
      } else if (ipv4 && !(error = setup_interface_ipv4(sp, res_port))){
        error = PF_INET;
        break;
      }
      if(error == -ECONNREFUSED){
        break;
      }

      /* Connections could have colided, giving ECONNREFUSED, or **
      ** the port we are trying to bind to may already be in use **
      ** and since we don't want to collide again, wait a random **
      ** amount of time......................................... */
      timo = random();
      timo /= (RAND_MAX/4);
      sleep(timo);
    }
  }
  return error;
}


/**
 * do_request: send request an receive results
 * @buffer: package to send
 *
 * This function does not interpret the contents of the package, except
 * to check the 'comm_err' field.
 *
 * Returns: 0 on success, < 0 on error
 */
static int do_request(char *buffer){
  int error=0;
  int sock=-1;
  char *proto;
  comm_header_t *ch = (comm_header_t *)buffer;
 
  if((error = setup_interface(&sock)) < 0){
    goto fail;
  }

  /* In the future, we will only try the protocol that worked first */
  if (comm_proto < 0){
    if (error == AF_INET) {
      proto = "IPv4";
    } else if (error == AF_INET6) {
      proto = "IPv6";
    } else if (error == PF_LOCAL) {
      proto = "Local Domain";
    } else {
      proto = "Unknown";
    }

    comm_proto = error;
  }

  error = write(sock, buffer, sizeof(comm_header_t)+ch->comm_payload_size);
  if(error < 0){
    goto fail;
  } else if(error < (sizeof(comm_header_t)+ch->comm_payload_size)){
    error = -EBADE;
    goto fail;
  }

  /* ok to take in two passes ? */
  error = read(sock, buffer, sizeof(comm_header_t));
  if(error < 0){
    goto fail;
  } else if(error < sizeof(comm_header_t)){
    error = -EBADE;
    goto fail;
  } else if(ch->comm_error){
    error = ch->comm_error;
    goto fail;
  } else {
    error = 0;
  }
  if(ch->comm_payload_size){
    error = read(sock, buffer+sizeof(comm_header_t), ch->comm_payload_size);
    if(error < 0){
      goto fail;
    } else if(error < ch->comm_payload_size){
      error = -EBADE;
      goto fail;
    } else {
      error = 0;
    }
  }
 fail:
  if(sock >= 0) { close(sock); }
  return error;
}


/**
* _ccs_connect
* @cluster_name: name of the cluster (optional)
* @flags: blocking or force
*
* This function will return a descriptor on success that should be
* used with the other functions for proper operation.
*
* Returns: >= 0 on success, < 0 on error
*/
static int _ccs_connect(const char *cluster_name, int flags){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_CONNECT;
  if(flags & COMM_CONNECT_BLOCKING){
    ch->comm_flags |= COMM_CONNECT_BLOCKING;
  }

  if(flags & COMM_CONNECT_FORCE){
    ch->comm_flags |= COMM_CONNECT_FORCE;
  }

  if(cluster_name){
    ch->comm_payload_size = strlen(cluster_name)+1;
    if(ch->comm_payload_size > (512 - sizeof(comm_header_t))){
      error = -ENAMETOOLONG;
      goto fail;
    }

    strcpy(payload, cluster_name); /* already checked if it will fit */
  }

  if(!(error = do_request(buffer))){
    /* Not an error, just reusing the 'error' variable */
    error = ch->comm_desc;
  }

 fail:
  if(buffer) { free(buffer); }
  return error;
}


/**
 * ccs_connect
 *
 * This function will only allow a connection if the node is part of
 * a quorate cluster.
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_connect(void){
  return _ccs_connect(NULL, 0);
}


/**
 * ccs_force_connect
 *
 * This function will only allow a connection even if the node is not
 * part of a quorate cluster.  It will use the configuration file
 * as specified at build time (default: /etc/cluster/cluster.conf).  If that
 * file does not exist, a copy of the file will be broadcasted for.  If
 * blocking is specified, the broadcasts will be retried until a config file
 * is located.  Otherwise, the fuction will return an error if the initial
 * broadcast is not successful.
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_force_connect(const char *cluster_name, int blocking){
  if(blocking){
    return _ccs_connect(cluster_name, COMM_CONNECT_FORCE | COMM_CONNECT_BLOCKING);
  } else {
    return _ccs_connect(cluster_name, COMM_CONNECT_FORCE);
  }
}


/**
 * ccs_disconnect
 * @desc: the descriptor returned by ccs_connect
 *
 * This function frees all associated state kept with an open connection
 *
 * Returns: 0 on success, < 0 on error
 */
int ccs_disconnect(int desc){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  if (desc < 0)
	  return -EINVAL;

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_DISCONNECT;
  ch->comm_desc = desc;

  error = do_request(buffer);

 fail:
  if(buffer) { free(buffer); }

  return error;
}


/**
 * _ccs_get
 * @desc:
 * @query:
 * @rtn: value returned
 * @list: 1 to operate in list fashion
 *
 * This function will allocate space for the value that is the result
 * of the given query.  It is the user's responsibility to ensure that
 * the data returned is freed.
 *
 * Returns: 0 on success, < 0 on failure
 */
static int _ccs_get(int desc, const char *query, char **rtn, int list){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  if (desc < 0)
	  return -EINVAL;

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = (list)?COMM_GET_LIST:COMM_GET;
  ch->comm_desc = desc;

  ch->comm_payload_size = sprintf(payload, "%s", query)+1;

  error = do_request(buffer);

  if(!error){
    if(ch->comm_payload_size){
      *rtn = (char *)strdup(payload);
      if(!*rtn){ error = -ENOMEM; }
    } else {
      *rtn = NULL;
    }
  }

 fail:
  if(buffer) { free(buffer); }

  return error;
}

int ccs_get(int desc, const char *query, char **rtn){
  return _ccs_get(desc, query, rtn, 0);
}

int ccs_get_list(int desc, const char *query, char **rtn){
  return _ccs_get(desc, query, rtn, 1);
}


/**
 * ccs_set: set an individual element's value in the config file.
 * @desc:
 * @path:
 * @val:
 *
 * This function is used to update individual elements in a config file.
 * It's effects are cluster wide.  It only succeeds when the node is part
 * of a quorate cluster.
 *
 * Note currently implemented.
 * 
 * Returns: 0 on success, < 0 on failure
 */
int ccs_set(int desc, const char *path, char *val){
  return -ENOSYS;
}


/**
 * ccs_get_state: return the stored state of the connection
 * @desc:
 * @cw_path:
 * @prev_query:
 *
 * This function will return the current working path and the
 * previous query.  It is the user's responsibility to free
 * both returned values.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_get_state(int desc, char **cw_path, char **prev_query){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  if (desc < 0)
	  return -EINVAL;

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  *cw_path = *prev_query = NULL;

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_GET_STATE;
  ch->comm_desc = desc;

  error = do_request(buffer);
  if(!error){
    *cw_path = (char *)strdup(payload);
    if(!*cw_path){
      error = -ENOMEM;
      goto fail;
    }
    *prev_query = (char *)strdup(payload+strlen(payload)+1);
    if(!*prev_query){
      error = -ENOMEM;
      free(*cw_path);
      *cw_path = NULL;
      goto fail;
    }
  }

 fail:
  if(buffer) { free(buffer); }

  return error;
}


/**
 * ccs_set_state
 * @desc:
 * @cw_path:
 * @reset_query:
 *
 * This function allows the user to specify a current working path,
 * from which all later queries will be relative.  It also allows the
 * user to erase memory of the last query - useful if the user wanted
 * to reset the index of a list to 0.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_set_state(int desc, const char *cw_path, int reset_query){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  if (desc < 0)
	  return -EINVAL;

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_SET_STATE;
  ch->comm_desc = desc;

  if(reset_query){
    ch->comm_flags |= COMM_SET_STATE_RESET_QUERY;
  }

  if(strlen(cw_path)+1 > 512-sizeof(comm_header_t)){
    error = -ENAMETOOLONG;
    goto fail;
  }

  /* sprintf does not include trailing \0 */
  ch->comm_payload_size = sprintf(payload, "%s", cw_path) + 1;

  error = do_request(buffer);

 fail:
  if(buffer) { free(buffer); }

  return error;
}

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
int ccs_lookup_nodename(int cd, const char *nodename, char **retval) {
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
			"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name", nodename);
	if (ret < 0 || (size_t) ret >= sizeof(path))
		return (-E2BIG);

	str = NULL;
	error = ccs_get(cd, path, &str);
	if (!error) {
		*retval = str;
		return (0);
	}

	if (nodename_len >= sizeof(host_only))
		return (-E2BIG);

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
	for (i = 1 ; ; i++) {
		char canonical_name[128];
		unsigned int altcnt;

		ret = snprintf(path, sizeof(path),
				"/cluster/clusternodes/clusternode[%u]/@name", i);
		if (ret < 0 || (size_t) ret >= sizeof(path))
			continue;

		for (altcnt = 0 ; ; altcnt++) {
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
					return (-E2BIG);
				}
				strcpy(canonical_name, str);
			}

			if (strlen(str) >= sizeof(cur_node)) {
				free(str);
				return (-E2BIG);
			}

			strcpy(cur_node, str);

			p = strchr(cur_node, '.');
			if (p != NULL)
				len = p - cur_node;
			else
				len = strlen(cur_node);

			if (strlen(host_only) == len &&
				!strncasecmp(host_only, cur_node, len))
			{
				free(str);
				*retval = strdup(canonical_name);
				if (*retval == NULL)
					return (-ENOMEM);
				return (0);
			}

			if (getaddrinfo(str, NULL, &hints, &ai) == 0) {
				struct addrinfo *cur;

				for (cur = ai ; cur != NULL ; cur = cur->ai_next) {
					char hostbuf[512];
					if (getnameinfo(cur->ai_addr, cur->ai_addrlen,
							hostbuf, sizeof(hostbuf),
							NULL, 0,
							hints.ai_family != AF_UNSPEC ? NI_NUMERICHOST : 0))
					{
						continue;
					}

					if (!strcasecmp(hostbuf, nodename)) {
						freeaddrinfo(ai);
						free(str);
						*retval = strdup(canonical_name);
						if (*retval == NULL)
							return (-ENOMEM);
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
	*retval = NULL;
	return (-1);
}
