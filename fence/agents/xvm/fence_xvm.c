/*
 * @file fence_xvmd.c: Implementation of server daemon for Xen virtual
 * machine fencing.  This uses SA AIS CKPT b.1.0 checkpointing API to 
 * store virtual machine states.
 *
 * Author: Lon Hohberger <lhh at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <libgen.h>
#include <nss.h>

/* Local includes */
#include "xvm.h"
#include "ip_lookup.h"
#include "simple_auth.h"
#include "options.h"
#include "tcp.h"
#include "mcast.h"
#include "debug.h"

LOGSYS_DECLARE_SYSTEM (NULL,
        LOG_MODE_OUTPUT_STDERR | LOG_MODE_OUTPUT_SYSLOG_THREADED |
	LOG_MODE_OUTPUT_FILE,
        LOGDIR "/fence_xvm.log",
        SYSLOGFACILITY);

LOGSYS_DECLARE_SUBSYS ("XVM", SYSLOGLEVEL);

int
tcp_wait_connect(int lfd, int retry_tenths)
{
	int fd;
	fd_set rfds;
	int n;
	struct timeval tv;

	dbg_printf(3, "Waiting for connection from XVM host daemon.\n");
	FD_ZERO(&rfds);
	FD_SET(lfd, &rfds);
	tv.tv_sec = retry_tenths / 10;
	tv.tv_usec = (retry_tenths % 10) * 100000;

	n = select(lfd + 1, &rfds, NULL, NULL, &tv);
	if (n == 0) {
		errno = ETIMEDOUT;
		return -1;
	} else if (n < 0) {
		return -1;
	}

	fd = accept(lfd, NULL, 0);
	if (fd < 0)
		return -1;

	return fd;
}


int
tcp_exchange(int fd, fence_auth_type_t auth, void *key,
	      size_t key_len, int timeout)
{
	char ret;
	fd_set rfds;
	struct timeval tv;

	/* Ok, we're connected */
	dbg_printf(3, "Issuing TCP challenge\n");
	if (tcp_challenge(fd, auth, key, key_len, timeout) <= 0) {
		/* Challenge failed */
		log_printf(LOG_ERR, "Invalid response to challenge\n");
		return 0;
	}

	/* Now they'll send us one, so we need to respond here */
	dbg_printf(3, "Responding to TCP challenge\n");
	if (tcp_response(fd, auth, key, key_len, timeout) <= 0) {
		log_printf(LOG_ERR, "Invalid response to challenge\n");
		return 0;
	}

	dbg_printf(2, "TCP Exchange + Authentication done... \n");

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	ret = 1;
	dbg_printf(3, "Waiting for return value from XVM host\n");
	if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
		return -1;

	/* Read return code */
	if (read(fd, &ret, 1) < 0)
		return -1;

	close(fd);
	if (ret == 0)
		log_printf(LOG_INFO, "Remote: Operation was successful\n");
	else
		log_printf(LOG_INFO, "Remote: Operation failed\n");
	return ret;
}


int
send_multicast_packets(ip_list_t *ipl, fence_xvm_args_t *args, void *key,
		       size_t key_len)
{
	fence_req_t freq;
	int mc_sock;
	ip_addr_t *ipa;
	struct sockaddr_in tgt4;
	struct sockaddr_in6 tgt6;
	struct sockaddr *tgt;
	socklen_t tgt_len;

	for (ipa = ipl->tqh_first; ipa; ipa = ipa->ipa_entries.tqe_next) {

		if (ipa->ipa_family != args->family) {
			dbg_printf(2, "Ignoring %s: wrong family\n", ipa->ipa_address);
			continue;
		}

		if (args->family == PF_INET) {
			mc_sock = ipv4_send_sk(ipa->ipa_address, args->addr,
					       args->port,
					       (struct sockaddr *)&tgt4,
					       sizeof(struct sockaddr_in),
					       args->ttl);
			tgt = (struct sockaddr *)&tgt4;
			tgt_len = sizeof(tgt4);
			
		} else if (args->family == PF_INET6) {
			mc_sock = ipv6_send_sk(ipa->ipa_address, args->addr,
					       args->port,
					       (struct sockaddr *)&tgt6,
					       sizeof(struct sockaddr_in6),
					       args->ttl);
			tgt = (struct sockaddr *)&tgt6;
			tgt_len = sizeof(tgt6);
		} else {
			dbg_printf(2, "Unsupported family %d\n", args->family);
			return -1;
		}

		if (mc_sock < 0)
			continue;

		/* Build our packet */
		memset(&freq, 0, sizeof(freq));
		strncpy((char *)freq.domain, args->domain,
			sizeof(freq.domain));
		freq.request = args->op;
		freq.hashtype = args->hash;

		/* Store source address */
		if (ipa->ipa_family == PF_INET) {
			freq.addrlen = sizeof(struct in_addr);
			/* XXX Swap order for in_addr ? XXX */
			inet_pton(PF_INET, ipa->ipa_address, freq.address);
		} else if (ipa->ipa_family == PF_INET6) {
			freq.addrlen = sizeof(struct in6_addr);
			inet_pton(PF_INET6, ipa->ipa_address, freq.address);
		}

		freq.flags = 0;
		if (args->flags & F_USE_UUID)
			freq.flags |= RF_UUID;
		freq.family = ipa->ipa_family;
		freq.port = args->port;

		sign_request(&freq, key, key_len);

		dbg_printf(3, "Sending to %s via %s\n", args->addr,
		        ipa->ipa_address);

		sendto(mc_sock, &freq, sizeof(freq), 0,
		       (struct sockaddr *)tgt, tgt_len);

		close(mc_sock);
	}

	return 0;
}


/* TODO: Clean this up!!! */
int
fence_xen_domain(fence_xvm_args_t *args)
{
	ip_list_t ipl;
	char key[MAX_KEY_LEN];
	int lfd, key_len = 0, fd;
	int attempts = 0;
	
	if (args->auth != AUTH_NONE || args->hash != HASH_NONE) {
		key_len = read_key_file(args->key_file, key, sizeof(key));
		if (key_len < 0) {
			log_printf(LOG_INFO,
				   "Could not read %s; trying without "
			           "authentication\n", args->key_file);
			args->auth = AUTH_NONE;
			args->hash = HASH_NONE;
		}
	}

	/* Do the real work */
	if (ip_build_list(&ipl) < 0) {
		log_printf(LOG_ERR, "Error building IP address list\n");
		return 1;
	}

	switch (args->auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			if (args->family == PF_INET) {
				lfd = ipv4_listen(args->port, 10);
			} else {
				lfd = ipv6_listen(args->port, 10);
			}
			break;
		/*case AUTH_X509:*/
			/* XXX Setup SSL listener socket here */
		default:
			return 1;
	}

	if (lfd < 0) {
		log_printf(LOG_ERR, "Failed to listen: %s\n", strerror(errno));
		return 1;
	}

	attempts = args->timeout * 10 / args->retr_time;

	log_printf(LOG_INFO, "Sending fence request for %s\n", 
		   args->domain);

	do {
		if (send_multicast_packets(&ipl, args, key, key_len)) {
			return -1;
		}

		switch (args->auth) {
			case AUTH_NONE:
			case AUTH_SHA1:
			case AUTH_SHA256:
			case AUTH_SHA512:
				fd = tcp_wait_connect(lfd, args->retr_time);
				if (fd < 0 && (errno == ETIMEDOUT ||
					       errno == EINTR))
					continue;
				break;
			/* case AUTH_X509:
				... = ssl_wait_connect... */
			break;
		default:
			return 1;
		}

		break;
	} while (--attempts);

	if (fd < 0) {
		if (attempts <= 0) {
			log_printf(LOG_ERR,
				   "Timed out waiting for response\n");
			return 1;
		}
		log_printf(LOG_ERR, "Fencing failed: %s\n", strerror(errno));
		return -1;
	}

	switch (args->auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			return tcp_exchange(fd, args->auth, key, key_len,
					    args->timeout);
			break;
		/* case AUTH_X509: 
			return ssl_exchange(...); */
		default:
			return 1;
	}

	return 1;
}


int
main(int argc, char **argv)
{
	fence_xvm_args_t args;
	char *my_options = "di:a:p:T:r:C:c:k:H:uo:t:?hV";

	args_init(&args);

	if (argc == 1) {
		args_get_stdin(my_options, &args);
	} else {
		args_get_getopt(argc, argv, my_options, &args);
	}

	if (args.flags & F_HELP) {
		args_usage(argv[0], my_options, 0);

                printf("With no command line argument, arguments are "
                       "read from standard input.\n");
                printf("Arguments read from standard input take "
                       "the form of:\n\n");
                printf("    arg1=value1\n");
                printf("    arg2=value2\n\n");

		args_usage(argv[0], my_options, 1);
		exit(0);
	}

	if (args.flags & F_VERSION) {
		printf("%s %s\n", basename(argv[0]), XVM_VERSION);
#ifdef RELEASE_VERSION
		printf("fence release %s\n", RELEASE_VERSION);
#endif
		exit(0);
	}

	args_finalize(&args);
	dset(args.debug);
	
	if (args.debug > 0) {
                logsys_config_priority_set (LOG_LEVEL_DEBUG);
		args_print(&args);
	}

	/* Additional validation here */
	if (!args.domain) {
		log_printf(LOG_ERR, "No domain specified!\n");
		args.flags |= F_ERR;
	}

	if (args.flags & F_ERR) {
		args_usage(argv[0], my_options, (argc == 1));
		exit(1);
	}

	/* Initialize NSS; required to do hashing, as silly as that
	   sounds... */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		log_printf(LOG_ERR, "Could not initialize NSS\n");
		return 1;
	}

	return fence_xen_domain(&args);
}
