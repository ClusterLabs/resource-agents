/** @file
 * Wrapper functions around read/write/select to retry in the event
 * of interrupts.
 */
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

/**
 * This is a wrapper around select which will retry in the case we receive
 * EINTR.  This is necessary for _read_retry, since it wouldn't make sense
 * to have _read_retry terminate if and only if two EINTRs were received
 * in a row - one during the read() call, one during the select call...
 *
 * See select(2) for description of parameters.
 */
int
_select_retry(int fdmax, fd_set * rfds, fd_set * wfds, fd_set * xfds,
	       struct timeval *timeout)
{
	int rv;

	while (1) {
		rv = select(fdmax, rfds, wfds, xfds, timeout);
		if ((rv == -1) && (errno == EINTR))
			/* return on EBADF/EINVAL/ENOMEM; continue on EINTR */
			continue;
		return rv;
	}
}

/**
 * Retries a write in the event of a non-blocked interrupt signal.
 *
 * @param fd		File descriptor to which we are writing.
 * @param buf		Data buffer to send.
 * @param count		Number of bytes in buf to send.
 * @param timeout	(struct timeval) telling us how long we should retry.
 * @return		The number of bytes written to the file descriptor,
 * 			or -1 on error (with errno set appropriately).
 */
ssize_t
_write_retry(int fd, void *buf, int count, struct timeval * timeout)
{
	int n, total = 0, remain = count, rv = 0;
	fd_set wfds, xfds;

	while (total < count) {

		/* Create the write FD set of 1... */
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		FD_ZERO(&xfds);
		FD_SET(fd, &xfds);

		/* wait for the fd to be available for writing */
		rv = _select_retry(fd + 1, NULL, &wfds, &xfds, timeout);
		if (rv == -1)
			return -1;
		else if (rv == 0) {
			errno = ETIMEDOUT;
			return -1;
		}

		if (FD_ISSET(fd, &xfds)) {
			errno = EPIPE;
			return -1;
		}

		/* 
		 * Attempt to write to fd
		 */
		n = write(fd, buf + (off_t) total, remain);

		/*
		 * When we know our fd was select()ed and we receive 0 bytes
		 * when we write, the fd was closed.
		 */
		if ((n == 0) && (rv == 1)) {
			errno = EPIPE;
			return -1;
		}

		if (n == -1) {
			if ((errno == EAGAIN) || (errno == EINTR)) {
				/* 
				 * Not ready?
				 */
				continue;
			}

			/* Other errors: EIO, EINVAL, etc */
			return -1;
		}

		total += n;
		remain -= n;
	}

	return total;
}

/**
 * Retry reads until we (a) time out or (b) get our data.  Of course, if
 * timeout is NULL, it'll wait forever.
 *
 * @param sockfd	File descriptor we want to read from.
 * @param buf		Preallocated buffer into which we will read data.
 * @param count		Number of bytes to read.
 * @param timeout	(struct timeval) describing how long we should retry.
 * @return 		The number of bytes read on success, or -1 on failure.
 			Note that we will always return (count) or (-1).
 */
ssize_t
_read_retry(int sockfd, void *buf, int count, struct timeval * timeout)
{
	int n, total = 0, remain = count, rv = 0;
	fd_set rfds, xfds;

	while (total < count) {
		FD_ZERO(&rfds);
		FD_SET(sockfd, &rfds);
		FD_ZERO(&xfds);
		FD_SET(sockfd, &xfds);
		
		/*
		 * Select on the socket, in case it closes while we're not
		 * looking...
		 */
		rv = _select_retry(sockfd + 1, &rfds, NULL, &xfds, timeout);
		if (rv == -1)
			return -1;
		else if (rv == 0) {
			errno = ETIMEDOUT;
			return -1;
		}

		if (FD_ISSET(sockfd, &xfds)) {
			errno = EPIPE;
			return -1;
		}

		/* 
		 * Attempt to read off the socket 
		 */
		n = read(sockfd, buf + (off_t) total, remain);

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
