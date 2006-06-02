#ifndef _FDOPS_H
#define _FDOPS_H
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

int _select_retry(int fdmax, fd_set * rfds, fd_set * wfds, fd_set * xfds,
   		  struct timeval *timeout);
ssize_t _write_retry(int fd, void *buf, int count, struct timeval * timeout);
ssize_t _read_retry(int sockfd, void *buf, int count,
		    struct timeval * timeout);

#endif
