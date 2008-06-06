#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "comm_headers.h"
#include "libccscompat.h"
#include "libcman.h"

typedef struct member_list {
  int count;
  int pad;
  cman_node_t *nodes;
} member_list_t;

static member_list_t *get_member_list(cman_handle_t handle);
static void free_member_list(member_list_t *list);

static int select_retry(int max_fd, fd_set *rfds, fd_set *wfds, fd_set *xfds,
			struct timeval *timeout);

static ssize_t read_retry(int fd, void *buf, int count, struct timeval *timeout);

static int ccs_open(cman_node_t node, uint16_t baseport, int timeout);
static int ipv4_connect(struct in_addr *addr, uint16_t port, int timeout);
static int ipv6_connect(struct in6_addr *addr, uint16_t port, int timeout);
static int connect_nb(int fd, struct sockaddr *addr, socklen_t len, int timeout);

extern int globalverbose;

int cluster_base_port = 50008;

static int get_doc_version(xmlDocPtr ldoc)
{
  int i;
  int error = 0;

  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  ctx = xmlXPathNewContext(ldoc);

  if (!ctx) {
    fprintf(stderr, "Unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression((xmlChar *)"/cluster/@config_version", ctx);

  if (!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)) {
    fprintf(stderr, "Error while retrieving config_version.\n");
    error = -ENODATA;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];

  if (node->type != XML_ATTRIBUTE_NODE) {
    fprintf(stderr, "Object returned is not of attribute type.\n");
    error = -ENODATA;
    goto fail;
  }

  if (!node->children->content || !strlen((char *)node->children->content)) {
    error = -ENODATA;
    goto fail;
  }

  for (i = 0; i < strlen((char *)node->children->content); i++) {
    if (!isdigit(node->children->content[i])) {
      fprintf(stderr, "config_version is not a valid integer.\n");
      error = -EINVAL;
      goto fail;
    }
  }

  error = atoi((char *)node->children->content);

fail:

  if (ctx) {
    xmlXPathFreeContext(ctx);
  }

  if (obj) {
    xmlXPathFreeObject(obj);
  }

  return error;
}


int update(char *location)
{
  int error = 0;
  int i, fd;
  int cluster_fd = -1;
  char true_location[256];
  xmlDocPtr doc = NULL;
  xmlChar *mem_doc;
  int doc_size = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL, rch;
  member_list_t *members = NULL;
  cman_handle_t handle = NULL;
  int desc;
  char *v1_str,*v3_str;
  int v1, v2, v3;

  struct timeval tv;

  if (location[0] != '/') {
    memset(true_location, 0, 256);
    if (!getcwd(true_location, 256)) {
      fprintf(stderr, "Unable to get the current working directory.\n");
      return -errno;
    }
    true_location[strlen(true_location)] = '/';
    strncpy(true_location+strlen(true_location), location, 256-strlen(true_location));
  } else {
    strncpy(true_location, location, 256);
  }

  desc = ccs_connect();

  if (desc < 0) {
    fprintf(stderr, "Unable to connect to the CCS daemon: %s\n", strerror(-desc));
    return desc;
  }

  if ((error = ccs_get(desc, "/cluster/@config_version", &v1_str))) {
    fprintf(stderr, "Unable to get current config_version: %s\n", strerror(-error));
    ccs_disconnect(desc);
    return error;
  }

  ccs_disconnect(desc);

  for (i = 0; i < strlen(v1_str); i++) {
    if (!isdigit(v1_str[i])) {
      fprintf(stderr, "config_version is not a valid integer.\n");
      free(v1_str);
      return -EINVAL;
    }
  }

  v1 = atoi(v1_str);
  free(v1_str);

  doc = xmlParseFile(true_location);

  if (!doc) {
    fprintf(stderr, "Unable to parse %s\n", true_location);
    return -EINVAL;
  }

  v2 = get_doc_version(doc);

  if (v2 < 0) {
    fprintf(stderr, "Unable to get the config_version from %s\n", location);
    xmlFreeDoc(doc);
    return -EINVAL;
  }

  if (v2 <= v1)  {
    fprintf(stderr,
	    "Proposed updated config file does not have greater version number.\n"
	    "  Current config_version :: %d\n"
	    "  Proposed config_version:: %d\n", v1, v2);
    xmlFreeDoc(doc);
    return -EINVAL;
  }    

  xmlDocDumpFormatMemory(doc, &mem_doc, &doc_size, 0);

  if (!mem_doc) {
    fprintf(stderr, "Unable to allocate memory for update document.\n");
    xmlFreeDoc(doc);
    return -ENOMEM;
  }

  xmlFreeDoc(doc);

  buffer = malloc(doc_size + sizeof(comm_header_t));

  if (!buffer) {
    fprintf(stderr, "Unable to allocate memory for transfer buffer.\n");
    free(mem_doc);
    return -ENOMEM;
  }

  memset(buffer, 0, (doc_size + sizeof(comm_header_t)));
  ch = (comm_header_t *)buffer;

  memcpy(buffer+sizeof(comm_header_t), mem_doc, doc_size);
  free(mem_doc);

  ch->comm_type = COMM_UPDATE;
  ch->comm_flags= COMM_UPDATE_NOTICE;
  ch->comm_payload_size = doc_size;

  handle = cman_admin_init(NULL);

  cluster_fd = cman_get_fd(handle);

  /* Should we test the cman handle of file descriptor to determine connectivity? */

  if (cluster_fd < 0) {
    fprintf(stderr, "Unable to connect to cluster infrastructure.\n");
    return cluster_fd;
  }

  if (!cman_is_quorate(handle)) {
    fprintf(stderr, "Unable to honor update request. Cluster is not quorate.\n");
    return -EPERM;
  }

  members = get_member_list(handle);

  swab_header(ch);
  
  for (i = 0; i < members->count; i++) {
    if (members->nodes[i].cn_nodeid == 0)
      continue;
    if (members->nodes[i].cn_member == 0)
      continue;

    fd = ccs_open(members->nodes[i], cluster_base_port, 5);

    if (fd < 0) {
      fprintf(stderr, "Unable to open connection to %s: %s\n",
	      members->nodes[i].cn_name, strerror(errno));
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    error = write(fd, buffer, sizeof(comm_header_t) + doc_size);

    if (error < 0) {
      fprintf(stderr, "Unable to send msg to %s: %s\n",
	      members->nodes[i].cn_name, strerror(errno));
      close(fd);
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    error = read_retry(fd, &rch, sizeof(comm_header_t), &tv);

    swab_header(&rch);

    if (error < 0) {
      fprintf(stderr, "Failed to receive COMM_UPDATE_NOTICE_ACK from %s.\n",
	      members->nodes[i].cn_name);
      fprintf(stderr, "Hint: Check the log on %s for reason.\n",
	      members->nodes[i].cn_name);
      close(fd);
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    close(fd);
  }

  swab_header(ch);
  
  ch->comm_flags = COMM_UPDATE_COMMIT;

  swab_header(ch);

  for (i=0; i < members->count; i++) {
    if (members->nodes[i].cn_nodeid == 0)
      continue;
    if (members->nodes[i].cn_member == 0)
      continue;

    fd = ccs_open(members->nodes[i], cluster_base_port, 5);
    if(fd < 0){
      fprintf(stderr, "Unable to open connection to %s: %s\n",
	      members->nodes[i].cn_name, strerror(errno));
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    error = write(fd, buffer, sizeof(comm_header_t));

    if (error < 0) {
      fprintf(stderr, "Unable to send msg to %s: %s\n",
	      members->nodes[i].cn_name, strerror(errno));
      close(fd);
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    error = read_retry(fd, &rch, sizeof(comm_header_t), &tv);

    swab_header(&rch);

    if (error < 0) {
      fprintf(stderr, "Failed to receive COMM_UPDATE_COMMIT_ACK from %s.\n",
	      members->nodes[i].cn_name);
      fprintf(stderr, "Hint: Check the log on %s for reason.\n",
	      members->nodes[i].cn_name);
      close(fd);
      free(buffer);
      free_member_list(members);
      return -errno;
    }

    close(fd);
    error = 0;
  }

  free(buffer);
  free_member_list(members);

  /* If we can't connect here, it doesn't mean the update failed **
  ** It means that we simply can't report the change in version  */
  desc = ccs_connect();

  if (desc < 0) {
    fprintf(stderr, "Unable to connect to the CCS daemon: %s\n", strerror(-desc));
    return 0;
  }

  if ((error = ccs_get(desc, "/cluster/@config_version", &v3_str))) {
    ccs_disconnect(desc);
    return 0;
  }

  v3 = atoi(v3_str);
  free(v3_str);

  ccs_disconnect(desc);

  if (v2 == v3) {
    cman_version_t cman_ver;
    printf("Config file updated from version %d to %d\n", v1, v2);
    cman_get_version(handle, &cman_ver);
    cman_ver.cv_config = v2;
    if (cman_set_version(handle, &cman_ver)) {
	    perror("Failed to tell cman of new version number");
    }
  } else {
    fprintf(stderr, "Warning:: Simultaneous update requests detected.\n"
	    "  You have lost the race.\n"
	    "  Old config version :: %d\n"
	    "  Proposed config version :: %d\n"
	    "  Winning config version  :: %d\n\n"
	    "Check " DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE " to ensure it contains the desired contents.\n", v1, v2, v3);
    return -EAGAIN;
  }

  return 0;
}


static member_list_t *get_member_list(cman_handle_t handle)
{
  int count = 0;

  member_list_t *list = NULL;
  cman_node_t *nodes = NULL;

  do
  {

    if (nodes != NULL) {
      free(nodes);
    }

    count = cman_get_node_count(handle);

    if (count <= 0) {
      return NULL;
    }

    if (list == NULL) {
      list = malloc(sizeof(*list));
    }

    if (list == NULL) {
      return NULL;
    }

    nodes = malloc(sizeof(*nodes) * count);

    if (nodes == NULL) {
      free(list);
      return NULL;
    }

    memset(list, 0, sizeof(*list));
    memset(nodes, 0, sizeof(*nodes) * count);

    cman_get_nodes(handle, count, &list->count, nodes);

  } while (list->count != count);

  list->count = count;
  list->nodes = nodes;

  return list;
}


static void free_member_list(member_list_t *list)
{
  if (list != NULL) {
    if (list->nodes != NULL) {
      free(list->nodes);
    }
    free(list);
  }
}


static int select_retry(int max_fd, fd_set *rfds, fd_set *wfds, fd_set *xfds,
			struct timeval *timeout)
{
  int rv;

  while (1) {
    rv = select(max_fd, rfds, wfds, xfds, timeout);
    if ((rv == -1) && (errno == EINTR)) {
      /* return on EBADF/EINVAL/ENOMEM; continue on EINTR */
      continue;
    }
    return rv;
  }
}


static ssize_t read_retry(int fd, void *buf, int count, struct timeval *timeout)
{
  int n, total = 0, remain = count, rv = 0;
  fd_set rfds, xfds;

  while (total < count) 
  {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    FD_ZERO(&xfds);
    FD_SET(fd, &xfds);

    /*
     * Select on the socket, in case it closes while we're not
     * looking...
     */
    rv = select_retry(fd + 1, &rfds, NULL, &xfds, timeout);
    if (rv == -1) {
      return -1;
    }
    else if (rv == 0) {
      errno = ETIMEDOUT;
      return -1;
    }

    if (FD_ISSET(fd, &xfds)) {
      errno = EPIPE;
      return -1;
    }

    /* 
     * Attempt to read off the socket 
     */
    n = read(fd, buf + (off_t) total, remain);

    /*
     * When we know our socket was select()ed and we receive 0 bytes
     * when we read, the socket was closed.
     */
    if ((n == 0) && (rv == 1)) {
      errno = EPIPE;
      return -1;
    }

    if (n == -1) {
      if ((errno == EAGAIN) || (errno == EINTR)) {
	/* 
	 * Not ready? Wait for data to become available
	 */
	continue;
      }

      /* Other errors: EPIPE, EINVAL, etc */
      return -1;
    }

    total += n;
    remain -= n;
  }

  return total;
}


static int ccs_open(cman_node_t node, uint16_t port, int timeout)
{
  struct in_addr *addr;
  struct in6_addr *addr6;
  int fd, family;
  char buf[INET6_ADDRSTRLEN];

  if (globalverbose) {
    memset(buf, 0, sizeof(buf));
    printf("Processing node: %s\n", node.cn_name);
  }

  family = ((struct sockaddr *)&(node.cn_address.cna_address))->sa_family;

  if (family == AF_INET6) {

    addr6 = &(((struct sockaddr_in6 *)&(node.cn_address.cna_address))->sin6_addr);

    if (globalverbose) {
      inet_ntop(family, addr6, buf, sizeof(buf));
      printf(" family: ipv6\n address: %s\n", buf);
    }

    if ((fd = ipv6_connect(addr6, port, timeout)) < 0) {
      return -1;
    }

  } else {

    addr = &(((struct sockaddr_in *)&(node.cn_address.cna_address))->sin_addr);

    if (globalverbose) {
      inet_ntop(family, addr, buf, sizeof(buf));
      printf(" family: ipv4\n address: %s\n", buf);
    }

    if ((fd = ipv4_connect(addr, port, timeout)) < 0) {
      return -1;
    }

  }

  return fd;
}


static int ipv4_connect(struct in_addr *addr, uint16_t port, int timeout)
{
  struct sockaddr_in sin;

  int fd;

  if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);

  memcpy(&sin.sin_addr, addr, sizeof(sin.sin_addr));

  if (connect_nb(fd, (struct sockaddr *)&sin, sizeof(sin), timeout) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}


static int ipv6_connect(struct in6_addr *addr, uint16_t port, int timeout)
{
  struct sockaddr_in6 sin6;

  int fd;

  if ((fd = socket(PF_INET6, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  memset(&sin6, 0, sizeof(sin6));

  sin6.sin6_family = AF_INET6;
  sin6.sin6_port = htons(port);
  sin6.sin6_flowinfo = 0;

  memcpy(&sin6.sin6_addr, addr, sizeof(sin6.sin6_addr));

  if (connect_nb(fd, (struct sockaddr *)&sin6, sizeof(sin6), timeout)) {
    close(fd);
    return -1;
  }

  return fd;
}


static int connect_nb(int fd, struct sockaddr *addr, socklen_t len, int timeout)
{
  int err;
  int ret;
  int flags = 1;
  unsigned l;
  fd_set rfds, wfds;

  struct timeval tv;

  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags)) < 0) {
    return -1;
  }

  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  ret = connect(fd, addr, len);

  if ((ret < 0) && (errno != EINPROGRESS)) {
    return -1;
  }

  if (ret != 0) {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    if (select_retry((fd + 1), &rfds, &wfds, NULL, &tv) == 0) {
      errno = ETIMEDOUT;
      return -1;
    }

    if (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &wfds)) {
      l = sizeof(err);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &l) < 0) {
	close(fd);
	return -1;
      }

      if (err != 0) {
	close(fd);
	errno = err;
	return -1;
      }

      fcntl(fd, F_SETFL, flags);
      return 0;
    }
  }

  errno = EIO;
  return -1;
}
