/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "lock_dlm.h"
#include "ccs.h"

#define OPTION_STRING			"DPhVwpl:o:t:c:a:"
#define LOCKFILE_NAME			"/var/run/gfs_controld.pid"

#define DEFAULT_NO_WITHDRAW 0 /* enable withdraw by default */
#define DEFAULT_NO_PLOCK 0 /* enable plocks by default */

/* max number of plock ops we will cpg-multicast per second */
#define DEFAULT_PLOCK_RATE_LIMIT 100

/* disable ownership by default because it's a different protocol */
#define DEFAULT_PLOCK_OWNERSHIP 0

/* max frequency of drop attempts in ms */
#define DEFAULT_DROP_RESOURCES_TIME 10000 /* 10 sec */

/* max number of resources to drop per time period */
#define DEFAULT_DROP_RESOURCES_COUNT 10

/* resource not accessed for this many ms before subject to dropping */
#define DEFAULT_DROP_RESOURCES_AGE 10000 /* 10 sec */

struct client {
	int fd;
	char type[32];
	struct mountgroup *mg;
	int another_mount;
};

extern struct list_head mounts;
extern struct list_head withdrawn_mounts;
extern group_handle_t gh;

int dmsetup_wait;

/* cpg message protocol
   1.0.0 is initial version
   2.0.0 is incompatible with 1.0.0 and allows plock ownership */
unsigned int protocol_v100[3] = {1, 0, 0};
unsigned int protocol_v200[3] = {2, 0, 0};
unsigned int protocol_active[3];

/* user configurable */
int config_no_withdraw;
int config_no_plock;
uint32_t config_plock_rate_limit;
uint32_t config_plock_ownership;
uint32_t config_drop_resources_time;
uint32_t config_drop_resources_count;
uint32_t config_drop_resources_age;

/* command line settings override corresponding cluster.conf settings */
static int opt_no_withdraw;
static int opt_no_plock;
static int opt_plock_rate_limit;
static int opt_plock_ownership;
static int opt_drop_resources_time;
static int opt_drop_resources_count;
static int opt_drop_resources_age;

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static int cman_fd;
static int cpg_fd;
static int listen_fd;
static int groupd_fd;
static int uevent_fd;
static int plocks_fd;
static int plocks_ci;


int do_read(int fd, void *buf, size_t count)
{
	int rv, off = 0;

	while (off < count) {
		rv = read(fd, buf + off, count - off);
		if (rv == 0)
			return -1;
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
			return -1;
		off += rv;
	}
	return 0;
}

int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		log_error("write errno %d", errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

#if 0
static void make_args(char *buf, int *argc, char **argv, char sep)
{
	char *p = buf;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';
		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;
}
#endif

static char *get_args(char *buf, int *argc, char **argv, char sep, int want)
{
	char *p = buf, *rp = NULL;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';

		if (want == i) { 
			rp = p + 1;
			break;
		}

		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;

	/* we ended by hitting \0, return the point following that */
	if (!rp)
		rp = strchr(buf, '\0') + 1;

	return rp;
}

static int client_add(int fd)
{
	int i;

	while (1) {
		/* This fails the first time with client_size of zero */
		for (i = 0; i < client_size; i++) {
			if (client[i].fd == -1) {
				client[i].fd = fd;
				pollfd[i].fd = fd;
				pollfd[i].events = POLLIN;
				if (i > client_maxi)
					client_maxi = i;
				return i;
			}
		}

		/* We didn't find an empty slot, so allocate more. */
		client_size += MAX_CLIENTS;

		if (!client) {
			client = malloc(client_size * sizeof(struct client));
			pollfd = malloc(client_size * sizeof(struct pollfd));
		} else {
			client = realloc(client, client_size *
						 sizeof(struct client));
			pollfd = realloc(pollfd, client_size *
						 sizeof(struct pollfd));
		}
		if (!client || !pollfd)
			log_error("Can't allocate client memory.");

		for (i = client_size - MAX_CLIENTS; i < client_size; i++) {
			client[i].fd = -1;
			pollfd[i].fd = -1;
		}
	}
}

/* I don't think we really want to try to do anything if mount.gfs is killed,
   because I suspect there are various corner cases where we might not do the
   right thing.  Even without the corner cases things still don't work out
   too nicely.  Best to just tell people not to kill a mount or unmount
   because doing so can leave things (kernel, group, mtab) in inconsistent
   states that can't be straightened out properly without a reboot. */

static void mount_client_dead(struct mountgroup *mg, int ci)
{
	char buf[MAXLINE];
	int rv;

	if (ci != mg->mount_client) {
		log_error("mount client mismatch %d %d", ci, mg->mount_client);
		return;
	}

	/* is checking sysfs really a reliable way of telling whether the
	   kernel has been mounted or not?  might the kernel mount just not
	   have reached the sysfs registration yet? */

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "id", buf, sizeof(buf));
	if (!rv) {
		log_error("mount_client_dead ci %d sysfs id %s", ci, buf);
#if 0
		/* finish the mount, although there will be no mtab entry
		   which will confuse umount causing it to do the kernel
		   umount but not call umount.gfs */
		got_mount_result(mg, 0, ci, client[ci].another_mount);
#endif
		return;
	}

	log_error("mount_client_dead ci %d no sysfs entry for fs", ci);

#if 0
	mp = find_mountpoint_client(mg, ci);
	if (mp) {
		list_del(&mp->list);
		free(mp);
	}
	group_leave(gh, mg->name);
#endif
}

static void client_dead(int ci)
{
	struct mountgroup *mg;

	log_debug("client %d fd %d dead", ci, client[ci].fd);

	/* if the dead mount client is mount.gfs and we've not received
	   a mount result, then try to put things into a clean state */
	   
	mg = client[ci].mg;
	if (mg && mg->mount_client && mg->mount_client_fd)
		mount_client_dead(mg, ci);

	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
	client[ci].mg = NULL;
}

static void client_ignore(int ci, int fd)
{
	pollfd[ci].fd = -1;
	pollfd[ci].events = 0;
}

static void client_back(int ci, int fd)
{
	pollfd[ci].fd = fd;
	pollfd[ci].events = POLLIN;
}

int client_send(int ci, char *buf, int len)
{
	return do_write(client[ci].fd, buf, len);
}

static int do_dump(int fd)
{
	int len;

	if (dump_wrap) {
		len = DUMP_SIZE - dump_point;
		do_write(fd, dump_buf + dump_point, len);
		len = dump_point;
	} else
		len = dump_point;

	/* NUL terminate the debug string */
	dump_buf[dump_point] = '\0';

	do_write(fd, dump_buf, len);

	return 0;
}

#if 0
/* mount.gfs sends us a special fd that it will write an error message to
   if mount(2) fails.  We can monitor this fd for an error message while
   waiting for the kernel mount outside our main poll loop */

void setup_mount_error_fd(struct mountgroup *mg)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec vec;
	char tmp[CMSG_SPACE(sizeof(int))];
	int fd, socket = client[mg->mount_client].fd;
	char ch;
	ssize_t n;

	memset(&msg, 0, sizeof(msg));

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = tmp;
	msg.msg_controllen = sizeof(tmp);

	n = recvmsg(socket, &msg, 0);
	if (n < 0) {
		log_group(mg, "setup_mount_error_fd recvmsg err %d errno %d",
			  n, errno);
		return;
	}
	if (n != 1) {
		log_group(mg, "setup_mount_error_fd recvmsg got %ld", (long)n);
		return;
	}

	cmsg = CMSG_FIRSTHDR(&msg);

	if (cmsg->cmsg_type != SCM_RIGHTS) {
		log_group(mg, "setup_mount_error_fd expected type %d got %d",
			  SCM_RIGHTS, cmsg->cmsg_type);
		return;
	}

	fd = (*(int *)CMSG_DATA(cmsg));
	mg->mount_error_fd = fd;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	log_group(mg, "setup_mount_error_fd got fd %d", fd);
}
#endif

static int process_client(int ci)
{
	struct mountgroup *mg;
	char buf[MAXLINE], *argv[MAXARGS], out[MAXLINE];
	char *cmd = NULL;
	int argc = 0, rv, fd;

	memset(buf, 0, MAXLINE);
	memset(out, 0, MAXLINE);
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = read(client[ci].fd, buf, MAXLINE);
	if (!rv) {
		client_dead(ci);
		return 0;
	}
	if (rv < 0) {
		log_debug("client %d fd %d read error %d %d", ci,
			   client[ci].fd, rv, errno);
		return rv;
	}

	log_debug("client %d: %s", ci, buf);

	get_args(buf, &argc, argv, ' ', 7);
	cmd = argv[0];
	rv = 0;

	if (!strcmp(cmd, "join")) {
		/* ci, dir (mountpoint), type (gfs/gfs2), proto (lock_dlm),
		   table (fsname:clustername), extra (rw), dev (/dev/sda1) */

		rv = do_mount(ci, argv[1], argv[2], argv[3], argv[4], argv[5],
			      argv[6], &mg);
		fd = client[ci].fd;
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		if (!rv || rv == -EALREADY) {
			client[ci].another_mount = rv;
			client[ci].mg = mg;
			mg->mount_client_fd = fd;
		}
		goto reply;
	} else if (!strcmp(cmd, "mount_result")) {
		got_mount_result(client[ci].mg, atoi(argv[3]), ci,
				 client[ci].another_mount);
	} else if (!strcmp(cmd, "leave")) {
		rv = do_unmount(ci, argv[1], atoi(argv[3]));
		goto reply;

	} else if (!strcmp(cmd, "remount")) {
		rv = do_remount(ci, argv[1], argv[3]);
		goto reply;

	} else if (!strcmp(cmd, "dump")) {
		do_dump(client[ci].fd);
		close(client[ci].fd);

	} else if (!strcmp(cmd, "plocks")) {
		dump_plocks(argv[1], client[ci].fd);
		client_dead(ci);

	} else {
		rv = -EINVAL;
		goto reply;
	}

	return rv;

 reply:
	sprintf(out, "%d", rv);
	rv = client_send(ci, out, MAXLINE);
	return rv;
}

static int setup_listen(void)
{
	struct sockaddr_un addr;
	socklen_t addrlen;
	int rv, s;

	/* we listen for new client connections on socket s */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_error("socket error %d %d", s, errno);
		return s;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], LOCK_DLM_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	rv = bind(s, (struct sockaddr *) &addr, addrlen);
	if (rv < 0) {
		log_error("bind error %d %d", rv, errno);
		close(s);
		return rv;
	}

	rv = listen(s, 5);
	if (rv < 0) {
		log_error("listen error %d %d", rv, errno);
		close(s);
		return rv;
	}

	log_debug("listen %d", s);

	return s;
}

int process_uevent(void)
{
	char buf[MAXLINE];
	char *argv[MAXARGS], *act;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("uevent recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "gfs") || !strstr(buf, "lock_module"))
		return 0;

	get_args(buf, &argc, argv, '/', 4);
	if (argc != 4)
		log_error("uevent message has %d args", argc);
	act = argv[0];

	log_debug("kernel: %s %s", act, argv[3]);

	if (!strcmp(act, "change@"))
		kernel_recovery_done(argv[3]);
	else if (!strcmp(act, "offline@"))
		do_withdraw(argv[3]);
	else
		ping_kernel_mount(argv[3]);

	return 0;
}

int setup_uevent(void)
{
	struct sockaddr_nl snl;
	int s, rv;

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("netlink socket error %d errno %d", s, errno);
		return s;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("uevent bind error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	log_debug("uevent %d", s);

	return s;
}

int loop(void)
{
	int rv, i, f, error, poll_timeout = -1, ignore_plocks_fd = 0;

	rv = listen_fd = setup_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd);

	rv = cman_fd = setup_cman();
	if (rv < 0)
		goto out;
	client_add(cman_fd);

	rv = cpg_fd = setup_cpg();
	if (rv < 0)
		goto out;
	client_add(cpg_fd);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd);

	rv = uevent_fd = setup_uevent();
	if (rv < 0)
		goto out;
	client_add(uevent_fd);

	rv = plocks_fd = setup_plocks();
	if (rv < 0)
		goto out;
	plocks_ci = client_add(plocks_fd);

	log_debug("setup done");

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, poll_timeout);
		if (rv < 0)
			log_error("poll error %d errno %d", rv, errno);

		/* client[0] is listening for new connections */

		if (pollfd[0].revents & POLLIN) {
			f = accept(client[0].fd, NULL, NULL);
			if (f < 0)
				log_debug("accept error %d %d", f, errno);
			else
				client_add(f);
		}

		for (i = 1; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;

			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == cman_fd)
					process_cman();
				else if (pollfd[i].fd == cpg_fd)
					process_cpg();
				else if (pollfd[i].fd == uevent_fd)
					process_uevent();
				else if (pollfd[i].fd == plocks_fd) {
					error = process_plocks();
					if (error == -EBUSY) {
						client_ignore(plocks_ci,
							      plocks_fd);
						ignore_plocks_fd = 1;
						poll_timeout = 100;
					}
				} else
					process_client(i);
			}

			if (pollfd[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
				if (pollfd[i].fd == cman_fd) {
					log_error("cman connection died");
					exit_cman();
				} else if (pollfd[i].fd == groupd_fd) {
					log_error("groupd connection died");
					exit_cman();
				} else if (pollfd[i].fd == cpg_fd) {
					log_error("cpg connection died");
					exit_cman();
				}
				client_dead(i);
			}

			/* check if our plock rate limit has expired so we
			   can start taking more local plock requests again */

			if (ignore_plocks_fd) {
				error = process_plocks();
				if (error != -EBUSY) {
					client_back(plocks_ci, plocks_fd);
					ignore_plocks_fd = 0;
					poll_timeout = -1;
				}
			}

			if (dmsetup_wait) {
				update_dmsetup_wait();
				if (dmsetup_wait) {
					if (poll_timeout == -1)
						poll_timeout = 1000;
				} else {
					if (poll_timeout == 1000)
						poll_timeout = -1;
				}
			}
		}
	}
	rv = 0;
 out:
	return rv;
}

#define PLOCK_RATE_LIMIT_PATH "/cluster/gfs_controld/@plock_rate_limit"
#define PLOCK_OWNERSHIP_PATH "/cluster/gfs_controld/@plock_ownership"
#define DROP_RESOURCES_TIME_PATH "/cluster/gfs_controld/@drop_resources_time"
#define DROP_RESOURCES_COUNT_PATH "/cluster/gfs_controld/@drop_resources_count"
#define DROP_RESOURCES_AGE_PATH "/cluster/gfs_controld/@drop_resources_age"

static void set_ccs_config(void)
{
	char path[PATH_MAX], *str;
	int i = 0, cd, error;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", PLOCK_RATE_LIMIT_PATH);
	str = NULL;

	error = ccs_get(cd, path, &str);
	if (!error) {
		if (!opt_plock_rate_limit)
			config_plock_rate_limit = atoi(str);
	}
	if (str)
		free(str);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", PLOCK_OWNERSHIP_PATH);
	str = NULL;

	error = ccs_get(cd, path, &str);
	if (!error) {
		if (!opt_plock_ownership)
			config_plock_ownership = atoi(str);
	}
	if (str)
		free(str);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", DROP_RESOURCES_TIME_PATH);
	str = NULL;

	error = ccs_get(cd, path, &str);
	if (!error) {
		if (!opt_drop_resources_time)
			config_drop_resources_time = atoi(str);
	}
	if (str)
		free(str);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", DROP_RESOURCES_COUNT_PATH);
	str = NULL;

	error = ccs_get(cd, path, &str);
	if (!error) {
		if (!opt_drop_resources_count)
			config_drop_resources_count = atoi(str);
	}
	if (str)
		free(str);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", DROP_RESOURCES_AGE_PATH);
	str = NULL;

	error = ccs_get(cd, path, &str);
	if (!error) {
		if (!opt_drop_resources_age)
			config_drop_resources_age = atoi(str);
	}
	if (str)
		free(str);
}

static void lockfile(void)
{
	int fd, error;
	struct flock lock;
	char buf[33];

	memset(buf, 0, 33);

	fd = open(LOCKFILE_NAME, O_CREAT|O_WRONLY,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd < 0) {
		fprintf(stderr, "cannot open/create lock file %s\n",
			LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (error) {
		fprintf(stderr, "gfs_controld is already running\n");
		exit(EXIT_FAILURE);
	}

	error = ftruncate(fd, 0);
	if (error) {
		fprintf(stderr, "cannot clear lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	sprintf(buf, "%d\n", getpid());

	error = write(fd, buf, strlen(buf));
	if (error <= 0) {
		fprintf(stderr, "cannot write lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("gfs_controld [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -P	       Enable plock debugging\n");
	printf("  -w	       Disable withdraw\n");
	printf("  -p	       Disable plocks\n");
	printf("  -l <limit>   Limit the rate of plock operations\n");
	printf("	       Default is %d, set to 0 for no limit\n", DEFAULT_PLOCK_RATE_LIMIT);
	printf("  -o <n>       plock ownership, 1 enable, 0 disable\n");
	printf("               Default is %d\n", DEFAULT_PLOCK_OWNERSHIP);
	printf("  -t <ms>      drop resources time (milliseconds)\n");
	printf("               Default is %u\n", DEFAULT_DROP_RESOURCES_TIME);
	printf("  -c <num>     drop resources count\n");
	printf("               Default is %u\n", DEFAULT_DROP_RESOURCES_COUNT);
	printf("  -a <ms>      drop resources age (milliseconds)\n");
	printf("               Default is %u\n", DEFAULT_DROP_RESOURCES_AGE);
	printf("  -h	       Print this help, then exit\n");
	printf("  -V	       Print program version information, then exit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'P':
			plock_debug_opt = 1;
			break;

		case 'w':
			config_no_withdraw = 1;
			opt_no_withdraw = 1;
			break;

		case 'p':
			config_no_plock = 1;
			opt_no_plock = 1;
			break;

		case 'l':
			config_plock_rate_limit = atoi(optarg);
			opt_plock_rate_limit = 1;
			break;

		case 'o':
			config_plock_ownership = atoi(optarg);
			opt_plock_ownership = 1;
			break;

		case 't':
			config_drop_resources_time = atoi(optarg);
			opt_drop_resources_time = 1;
			break;

		case 'c':
			config_drop_resources_count = atoi(optarg);
			opt_drop_resources_count = 1;
			break;

		case 'a':
			config_drop_resources_age = atoi(optarg);
			opt_drop_resources_age = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("gfs_controld (built %s %s)\n", __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}
}

void set_oom_adj(int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");
	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}

void set_scheduler(void)
{
	struct sched_param sched_param;
	int rv;

	rv = sched_get_priority_max(SCHED_RR);
	if (rv != -1) {
		sched_param.sched_priority = rv;
		rv = sched_setscheduler(0, SCHED_RR, &sched_param);
		if (rv == -1)
			log_error("could not set SCHED_RR priority %d err %d",
				   sched_param.sched_priority, errno);
	} else {
		log_error("could not get maximum scheduler priority err %d",
			  errno);
	}
}

int main(int argc, char **argv)
{
	INIT_LIST_HEAD(&mounts);
	INIT_LIST_HEAD(&withdrawn_mounts);

	config_no_withdraw = DEFAULT_NO_WITHDRAW;
	config_no_plock = DEFAULT_NO_PLOCK;
	config_plock_rate_limit = DEFAULT_PLOCK_RATE_LIMIT;
	config_plock_ownership = DEFAULT_PLOCK_OWNERSHIP;
	config_drop_resources_time = DEFAULT_DROP_RESOURCES_TIME;
	config_drop_resources_count = DEFAULT_DROP_RESOURCES_COUNT;
	config_drop_resources_age = DEFAULT_DROP_RESOURCES_AGE;

	decode_arguments(argc, argv);

	lockfile();

	if (!daemon_debug_opt) {
		if (daemon(0, 0) < 0) {
			perror("daemon error");
			exit(EXIT_FAILURE);
		}
	}
	openlog("gfs_controld", LOG_PID, LOG_DAEMON);

	/* ccs settings override the defaults, but not the command line */
	set_ccs_config();

	if (config_plock_ownership)
		memcpy(protocol_active, protocol_v200, sizeof(protocol_v200));
	else
		memcpy(protocol_active, protocol_v100, sizeof(protocol_v100));

	log_debug("config_no_withdraw %d", config_no_withdraw);
	log_debug("config_no_plock %d", config_no_plock);
	log_debug("config_plock_rate_limit %u", config_plock_rate_limit);
	log_debug("config_plock_ownership %u", config_plock_ownership);
	log_debug("config_drop_resources_time %u", config_drop_resources_time);
	log_debug("config_drop_resources_count %u", config_drop_resources_count);
	log_debug("config_drop_resources_age %u", config_drop_resources_age);
	log_debug("protocol %u.%u.%u", protocol_active[0], protocol_active[1],
		  protocol_active[2]);

	set_scheduler();
	set_oom_adj(-16);

	return loop();
}

void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

int plock_debug_opt;
int daemon_debug_opt;
char daemon_debug_buf[256];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

