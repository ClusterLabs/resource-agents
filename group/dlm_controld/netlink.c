#include "dlm_daemon.h"
#include "config.h"
#include <linux/dlm.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/dlm_netlink.h>

#define DEADLOCK_CHECK_SECS		10

/* FIXME: look into using libnl/libnetlink */

#define GENLMSG_DATA(glh)       ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)    (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)	    	((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

/* Maximum size of response requested or message sent */
#define MAX_MSG_SIZE    1024

struct msgtemplate {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[MAX_MSG_SIZE];
};

static int send_genetlink_cmd(int sd, uint16_t nlmsg_type, uint32_t nlmsg_pid,
			      uint8_t genl_cmd, uint16_t nla_type,
			      void *nla_data, int nla_len)
{
	struct nlattr *na;
	struct sockaddr_nl nladdr;
	int r, buflen;
	char *buf;

	struct msgtemplate msg;

	msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type = nlmsg_type;
	msg.n.nlmsg_flags = NLM_F_REQUEST;
	msg.n.nlmsg_seq = 0;
	msg.n.nlmsg_pid = nlmsg_pid;
	msg.g.cmd = genl_cmd;
	msg.g.version = 0x1;
	na = (struct nlattr *) GENLMSG_DATA(&msg);
	na->nla_type = nla_type;
	na->nla_len = nla_len + 1 + NLA_HDRLEN;
	if (nla_data)
		memcpy(NLA_DATA(na), nla_data, nla_len);
	msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	buf = (char *) &msg;
	buflen = msg.n.nlmsg_len ;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	while ((r = sendto(sd, buf, buflen, 0, (struct sockaddr *) &nladdr,
			   sizeof(nladdr))) < buflen) {
		if (r > 0) {
			buf += r;
			buflen -= r;
		} else if (errno != EAGAIN)
			return -1;
	}
	return 0;
}

/*
 * Probe the controller in genetlink to find the family id
 * for the DLM family
 */
static int get_family_id(int sd)
{
	char genl_name[100];
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[256];
	} ans;

	int id = 0, rc;
	struct nlattr *na;
	int rep_len;

	strcpy(genl_name, DLM_GENL_NAME);
	rc = send_genetlink_cmd(sd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
				CTRL_ATTR_FAMILY_NAME, (void *)genl_name,
				strlen(DLM_GENL_NAME)+1);

	rep_len = recv(sd, &ans, sizeof(ans), 0);
	if (ans.n.nlmsg_type == NLMSG_ERROR ||
	    (rep_len < 0) || !NLMSG_OK((&ans.n), rep_len))
		return 0;

	na = (struct nlattr *) GENLMSG_DATA(&ans);
	na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
		id = *(uint16_t *) NLA_DATA(na);
	}
	return id;
}

/* genetlink messages are timewarnings used as part of deadlock detection */

int setup_netlink(void)
{
	struct sockaddr_nl snl;
	int s, rv;
	uint16_t id;

	s = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (s < 0) {
		log_error("generic netlink socket");
		return s;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("gen netlink bind error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	id = get_family_id(s);
	if (!id) {
		log_error("Error getting family id, errno %d", errno);
		close(s);
		return -1;
	}

	rv = send_genetlink_cmd(s, id, getpid(), DLM_CMD_HELLO, 0, NULL, 0);
	if (rv < 0) {
		log_error("error sending hello cmd, errno %d", errno);
		close(s);
		return -1;
	}

	return s;
}

static void process_timewarn(struct dlm_lock_data *data)
{
	struct lockspace *ls;
	struct timeval now;
	unsigned int sec;

	ls = find_ls_id(data->lockspace_id);
	if (!ls)
		return;

	data->resource_name[data->resource_namelen] = '\0';

	log_group(ls, "timewarn: lkid %x pid %d name %s",
		  data->id, data->ownpid, data->resource_name);

	/* Problem: we don't want to get a timewarn, assume it's resolved
	   by the current cycle, but in fact it's from a deadlock that
	   formed after the checkpoints for the current cycle.  Then we'd
	   have to hope for another warning (that may not come) to trigger
	   a new cycle to catch the deadlock.  If our last cycle ckpt
	   was say N (~5?) sec before we receive the timewarn, then we
	   can be confident that the cycle included the lock in question.
	   Otherwise, we're not sure if the warning is for a new deadlock
	   that's formed since our last cycle ckpt (unless it's a long
	   enough time since the last cycle that we're confident it *is*
	   a new deadlock).  When there is a deadlock, I suspect it will
	   be common to receive warnings before, during, and possibly
	   after the cycle that resolves it.  Wonder if we should record
	   timewarns and match them with deadlock cycles so we can tell
	   which timewarns are addressed by a given cycle and which aren't.  */


	gettimeofday(&now, NULL);

	/* don't send a new start until at least SECS after the last
	   we sent, and at least SECS after the last completed cycle */

	sec = now.tv_sec - ls->last_send_cycle_start.tv_sec;

	if (sec < DEADLOCK_CHECK_SECS) {
		log_group(ls, "skip send: recent send cycle %d sec", sec);
		return;
	}

	sec = now.tv_sec - ls->cycle_end_time.tv_sec;

	if (sec < DEADLOCK_CHECK_SECS) {
		log_group(ls, "skip send: recent cycle end %d sec", sec);
		return;
	}

	gettimeofday(&ls->last_send_cycle_start, NULL);

	if (cfgd_enable_deadlk)
		send_cycle_start(ls);
}

void process_netlink(int ci)
{
	struct msgtemplate msg;
	struct nlattr *na;
	int len;
	int fd;

	fd = client_fd(ci);

	len = recv(fd, &msg, sizeof(msg), 0);

	if (len < 0) {
		log_error("nonfatal netlink error: errno %d", errno);
		return;
	}

	if (msg.n.nlmsg_type == NLMSG_ERROR || !NLMSG_OK((&msg.n), len)) {
		struct nlmsgerr *err = NLMSG_DATA(&msg);
		log_error("fatal netlink error: errno %d", err->error);
		return;
	}

	na = (struct nlattr *) GENLMSG_DATA(&msg);

	process_timewarn((struct dlm_lock_data *) NLA_DATA(na));
}

