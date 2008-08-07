#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <libxml/parser.h>
#include <corosync/engine/logsys.h>

#include "comm_headers.h"
#include "debug.h"
#include "misc.h"
#include "globals.h"
#include "libcman.h"

typedef struct member_list {
  int count;
  int pad;
  cman_node_t *nodes;
} member_list_t;

static member_list_t *members = NULL;

static member_list_t *get_member_list(cman_handle_t handle);
static void free_member_list(member_list_t *list);
static char *member_id_to_name(member_list_t *list, int node);
static int member_addr_to_id(member_list_t *list, struct sockaddr *addr);

static int select_retry(int max_fd, fd_set *rfds, fd_set *wfds, fd_set *xfds,
			struct timeval *timeout);

static ssize_t read_retry(int fd, void *buf, int count, struct timeval *timeout);

static int check_update_doc(xmlDocPtr tmp_doc)
{
  int error = 0;

  char *str1 = NULL;
  char *str2 = NULL;

  CCSENTER("check_update_doc");

  if (!(str1 = get_cluster_name(tmp_doc))) {
    log_printf(LOG_ERR, "Unable to get cluster name from new config file.\n");
    error = -EINVAL;
    goto fail;
  }

  if (master_doc && master_doc->od_doc &&
      !(str2 = get_cluster_name(master_doc->od_doc))) {
    log_printf(LOG_DEBUG, "Unable to get cluster name from current master doc.\n");
  }

  if (str2 && strcmp(str1, str2)) {
    log_printf(LOG_ERR, "Cluster names for current and update configs do not match.\n");
    log_printf(LOG_ERR, "  Current cluster name:: <%s>\n", str2);
    log_printf(LOG_ERR, "  Proposed update name:: <%s>\n", str1);
    error = -EINVAL;
    goto fail;
  }

  if (master_doc && master_doc->od_doc &&
      (get_doc_version(tmp_doc) <= get_doc_version(master_doc->od_doc))) {
    log_printf(LOG_ERR, "Proposed updated config file does not have greater version number.\n");
    log_printf(LOG_ERR, "  Current config_version :: %d\n", get_doc_version(master_doc->od_doc));
    log_printf(LOG_ERR, "  Proposed config_version:: %d\n", get_doc_version(tmp_doc));
    error = -EINVAL;
  }

fail:

  if (str1) {
    free(str1);
  }

  if (str2) {
    free(str2);
  }

  CCSEXIT("check_update_doc");
  return error;
}

static int handle_cluster_message(int fd)
{
  int error = 0;
  int unlock = 0;
  int socket = -1;

  FILE *fp = NULL;
  char *buffer = NULL;
  xmlDocPtr tmp_doc = NULL;
  comm_header_t ch;
  uint64_t nodeid;
  mode_t old_mode;
  socklen_t client_len;

  struct timeval tv;
  struct sockaddr client_addr;
  static uint64_t master_node = 0;

  CCSENTER("handle_cluster_message");

  log_printf(LOG_DEBUG, "Cluster message on socket: %d\n", fd);

  client_len = sizeof(client_addr);

  if ((socket = accept(fd, &client_addr, &client_len)) < 0) {
    log_printf(LOG_ERR, "Failed to accept connection.\n");
    goto fail;
  }

  if ((nodeid = member_addr_to_id(members, &client_addr)) < 0) {
    log_printf(LOG_ERR, "Unable to determine node ID.\n");
    goto fail;
  }

  log_printf(LOG_DEBUG, "Accept socket: %d\n", socket);

  error = recv(socket, &ch, sizeof(comm_header_t), MSG_PEEK);

  if (error < 0) {
    log_printf(LOG_ERR, "Failed to receive message from %s\n",
		member_id_to_name(members, nodeid));
    goto fail;
  }

  log_printf(LOG_DEBUG, "Message (%d bytes) received from %s\n",
	  error, member_id_to_name(members, nodeid));

  swab_header(&ch);

  if (ch.comm_type != COMM_UPDATE) {
    log_printf(LOG_ERR, "Unexpected communication type (%d)... ignoring.\n",
	    ch.comm_type);
    error = -EINVAL;
    goto fail;
  }

  if (ch.comm_flags == COMM_UPDATE_NOTICE) {
    buffer = malloc(ch.comm_payload_size + sizeof(comm_header_t));
    if (!buffer) {
      log_printf(LOG_ERR, "Unable to allocate space to perform update.\n");
      error = -ENOMEM;
      goto fail;
    }

    log_printf(LOG_DEBUG, "Updated config size:: %d\n", ch.comm_payload_size);

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    error = read_retry(socket, buffer, ch.comm_payload_size + sizeof(comm_header_t), &tv);

    if (error < 0) {
      log_printf(LOG_ERR, "Unable to retrieve updated config");
      goto fail;
    }

    pthread_mutex_lock(&update_lock);
    unlock = 1;

    log_printf(LOG_DEBUG, "Got lock 0\n");
    
    tmp_doc = xmlParseMemory(buffer+sizeof(comm_header_t), ch.comm_payload_size);

    if (!tmp_doc) {
      log_printf(LOG_ERR, "Unable to parse updated config file.\n");
      /* ATTENTION -- need better error code */
      error = -EIO;
      goto fail;
    }

    if ((error = check_update_doc(tmp_doc)) < 0) {
      goto fail;
    }

    old_mode = umask(026);

    fp = fopen(DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE "-update", "w");

    umask(old_mode);

    if (!fp) {
      log_printf(LOG_ERR, "Unable to open " DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE "-update");
      error = -errno;
      goto fail;
    }

    if (xmlDocDump(fp, tmp_doc) < 0) {
      log_printf(LOG_ERR, "Unable to write " DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE "-update");
      goto fail;
    }

    log_printf(LOG_DEBUG, "Upload of new config file from %s complete.\n",
	    member_id_to_name(members, nodeid));

    ch.comm_payload_size = 0;
    ch.comm_flags = COMM_UPDATE_NOTICE_ACK;

    log_printf(LOG_DEBUG, "Sending COMM_UPDATE_NOTICE_ACK.\n");

    swab_header(&ch);

    if ((error = write(socket, &ch, sizeof(comm_header_t))) < 0) {
      log_printf(LOG_ERR, "Unable to send COMM_UPDATE_NOTICE_ACK.\n");
      goto fail;
    }

    master_node = nodeid;
    error = 0;
  }

  else if(ch.comm_flags == COMM_UPDATE_COMMIT) {

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    error = read_retry(socket, &ch, sizeof(comm_header_t), &tv);

    if (master_node != nodeid) {
      log_printf(LOG_ERR, "COMM_UPDATE_COMMIT received from node other than initiator.\n");
      log_printf(LOG_ERR, "Hint: There may be multiple updates happening at once.\n");
      error = -EPERM;
      goto fail;
    }

    pthread_mutex_lock(&update_lock);

    unlock = 1;

    log_printf(LOG_DEBUG, "Got lock 1\n");

    tmp_doc = xmlParseFile(DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE "-update");

    if (!tmp_doc) {
      log_printf(LOG_ERR, "Unable to parse updated config file.\n");
      /* ATTENTION -- need better error code */
      error = -EIO;
      goto fail;
    }

    if ((error = check_update_doc(tmp_doc)) < 0) {
      goto fail;
    }

    old_mode = umask(026);

    fp = fopen(DEFAULT_CONFIG_DIR "/." DEFAULT_CONFIG_FILE, "w");

    umask(old_mode);

    if (!fp) {
      log_printf(LOG_ERR, "Unable to open " DEFAULT_CONFIG_DIR "/." DEFAULT_CONFIG_FILE);
      error = -errno;
      goto fail;
    }

    if (xmlDocDump(fp, tmp_doc) < 0) {
      log_printf(LOG_ERR, "Unable to write " DEFAULT_CONFIG_DIR "/." DEFAULT_CONFIG_FILE);
      goto fail;
    }

    rename(DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE "-update", DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE);

    update_required = 1;
    ch.comm_flags = COMM_UPDATE_COMMIT_ACK;

    log_printf(LOG_DEBUG, "Sending COMM_UPDATE_COMMIT_ACK.\n");

    swab_header(&ch);

    if ((error = write(socket, &ch, sizeof(comm_header_t))) < 0) {
      log_printf(LOG_ERR, "Unable to send COMM_UPDATE_NOTICE_ACK.\n");
      goto fail;
    }

    error = 0;
  }

fail:

  if (fp) {
    fclose(fp);
  }

  if (socket >= 0) {
    close(socket);
  }

  if (buffer) {
    free(buffer);
  }

  if (tmp_doc) {
    xmlFreeDoc(tmp_doc);
  }

  if (unlock) {
    pthread_mutex_unlock(&update_lock);
  }

  CCSEXIT("handle_cluster_message");
  return error;
}


static void cman_callback(cman_handle_t handle, void *private, int reason, int arg)
{
  switch (reason) {
    case CMAN_REASON_TRY_SHUTDOWN:
      cman_replyto_shutdown(handle, 1);
      break;

    case CMAN_REASON_STATECHANGE:
      quorate = cman_is_quorate(handle);
      free_member_list(members);
      members = get_member_list(handle);
      break;

    default:
      break;
  }
}


static int handle_cluster_event(cman_handle_t handle)
{
  CCSENTER("handle_cluster_event");

  int rv = 1;
  while (rv > 0) {
    rv = cman_dispatch(handle, CMAN_DISPATCH_ALL);
  }
  if (rv < 0) {
    return -1;
  }

  CCSEXIT("handle_cluster_event");
  return 0;
}


static void cluster_communicator(void)
{
  int ccsd_fd = -1;
  int cman_fd = -1;
  int warn_user = 0;
  int opt = 1;
  int max_fd;
  int n;
  int flags;

  fd_set rset;
  cman_handle_t handle = NULL;

  struct sockaddr_storage addr;
  struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
  struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
  int addr_size=0;

  CCSENTER("cluster_communicator");

  memset(&addr, 0, sizeof(struct sockaddr_storage));

  if (IPv6) {
    if ((ccsd_fd = socket(PF_INET6, SOCK_STREAM, 0)) < 0) {
      if(IPv6 == -1) {
	log_printf(LOG_DEBUG, "Unable to create IPv6 socket:: %s\n", strerror(errno));
	IPv6=0;
     }
    }
  }

  if (!IPv6) {
    if ((ccsd_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
      log_printf(LOG_ERR, "Unable to create IPv4 socket.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (setsockopt(ccsd_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt)) < 0) {
    log_printf(LOG_ERR, "Unable to set socket option");
    exit(EXIT_FAILURE);
  }

  if(IPv6){
    addr_size = sizeof(struct sockaddr_in6);
    addr6->sin6_family = AF_INET6;
    addr6->sin6_addr = in6addr_any;
    addr6->sin6_port = htons(cluster_base_port);
  } else {
    addr_size = sizeof(struct sockaddr_in);
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = INADDR_ANY;
    addr4->sin_port = htons(cluster_base_port);
  }

  flags = fcntl(ccsd_fd, F_GETFD, 0);
  flags |= FD_CLOEXEC;
  fcntl(ccsd_fd, F_SETFD, flags);

  if (bind(ccsd_fd, (struct sockaddr *)&addr, addr_size) < 0) {
    log_printf(LOG_ERR, "Unable to bind to socket.\n");
    close(ccsd_fd);
    exit(EXIT_FAILURE);
  }

  if (listen(ccsd_fd, 15) < 0) {
    log_printf(LOG_ERR, "Unable to listen to socket.\n");
    close(ccsd_fd);
    exit(EXIT_FAILURE);
  }

restart:

  while (handle == NULL)
  {
    handle = cman_init(NULL);

    if (handle == NULL) {

      warn_user++;

      if (!(warn_user % 30))
      {
	log_printf(LOG_ERR, "Unable to connect to cluster infrastructure after %d seconds.\n",
		warn_user);
      }

      sleep(1);
    }
  }

  if (ppid) {
    kill(ppid, SIGTERM);	
    ppid = 0;
  }

  cman_start_notification(handle, cman_callback);

  quorate = cman_is_quorate(handle);

  log_printf(LOG_INFO, "Initial status:: %s\n", (quorate)? "Quorate" : "Inquorate");

  members = get_member_list(handle);

  while (1)
  {
    FD_ZERO(&rset);
    cman_fd = cman_get_fd(handle);

    FD_SET(ccsd_fd, &rset);
    FD_SET(cman_fd, &rset);

    max_fd = (ccsd_fd > cman_fd) ? ccsd_fd : cman_fd;

    log_printf(LOG_DEBUG, "Waiting for cluster event.\n");
    
    if ((n = select((max_fd + 1), &rset, NULL, NULL, NULL)) < 0) {
      log_printf(LOG_ERR, "Select failed");
      continue;
    }

    log_printf(LOG_DEBUG, "There are %d cluster messages waiting.\n", n);

    while (n)
    {
      log_printf(LOG_DEBUG, "There are %d messages remaining.\n", n);

      n--;

      if (FD_ISSET(ccsd_fd, &rset)) {
	handle_cluster_message(ccsd_fd);
      }

      if (FD_ISSET(cman_fd, &rset)) {
        if (handle_cluster_event(handle)) {
	  cman_finish(handle);
	  handle = NULL;
	  goto restart;
	}
      }
    }
  }

  CCSEXIT("cluster_communicator");
}


int start_cluster_monitor_thread(void) {
  int error = 0;
  pthread_t thread;

  CCSENTER("start_cluster_monitor_thread");

  pthread_mutex_init(&update_lock, NULL);

  error = pthread_create(&thread, NULL, (void *)cluster_communicator, NULL);

  if (error) {
    log_printf(LOG_ERR, "Failed to create thread: %s\n", strerror(-error));
    goto fail;
  }

  pthread_detach(thread);

fail:

  CCSEXIT("start_cluster_monitor_thread");
  return error;
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


static char *member_id_to_name(member_list_t *list, int node)
{
  int i;

  for (i = 0; i < list->count; i++) {
    if (list->nodes[i].cn_nodeid == node) {
      return list->nodes[i].cn_name;
    }
  }

  return NULL;
}


static int member_addr_to_id(member_list_t *list, struct sockaddr *addr)
{
  int i;

  for (i = 0; i < list->count; i++) {
	  if (memcmp(&list->nodes[i].cn_address.cna_address, addr,
		sizeof(struct sockaddr))) {

		return list->nodes[i].cn_nodeid;
	  }
  }

  return -1;
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
