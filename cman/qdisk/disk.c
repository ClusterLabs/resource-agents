/*
  Copyright Red Hat, Inc. 2002-2003, 2006
  Copyright Mission Critical Linux, 2000

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR lgPURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/** @file
 * Single-block Raw/Direct I/O Functions
 */
/*
 *  author: Tim Burke <tburke at redhat.com>
 *  description: Raw IO Interfaces.
 *
 * The RAW IO code we are using from 2.2.13 requires user buffers and
 * disk offsets to be 512 byte aligned.  So this code consists of a 
 * read and write routine which check to see if the user buffer is 
 * aligned.  If it isn't a temporary aligned buffer is allocated, a data
 * copy is performed along with the IO operation itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <disk.h>
#include <platform.h>
#include <unistd.h>
#include <time.h>

static int diskRawRead(int fd, char *buf, int len);
uint32_t clu_crc32(const char *data, size_t count);


/**
 * Swap the bytes of a shared header so that it's always in big-endian form
 * when stored on disk.
 *
 * @param hdr		Header to encode.
 */
static void
header_encode(shared_header_t *hdr)
{
	/* sanity check - LE machine -> already encoded. */
	if (hdr->h_magic == be_swap32(SHARED_HEADER_MAGIC))
		return;

	swab32(hdr->h_magic);
	swab32(hdr->h_hcrc);
	swab32(hdr->h_dcrc);
	swab32(hdr->h_length);
	swab64(hdr->h_view);
	swab64(hdr->h_timestamp);
}


/**
 * Swap the bytes of a shared header so that it's always in host-byte order
 * after we read it.  This should be a macro calling header_encode.
 *
 * @param hdr		Header to decode.
 */
static void
header_decode(shared_header_t *hdr)
{
	/* sanity check - LE machine -> already decoded. */
	if (hdr->h_magic == SHARED_HEADER_MAGIC)
		return;

	swab32(hdr->h_magic);
	swab32(hdr->h_hcrc);
	swab32(hdr->h_dcrc);
	swab32(hdr->h_length);
	swab64(hdr->h_view);
	swab64(hdr->h_timestamp);
}


/**
 * Generate a shared header suitable for storing data.  This includes:
 * header magic, header crc, data crc, header length, timestamp.
 * The header CRC is generated *after* the data CRC; so the header,
 * in effect, ensures that the data CRC is valid before we even look
 * at the data.  Thus, if the header CRC decodes properly, then we
 * assume that there's a very very high chance that the data CRC is valid.
 * If the data CRC doesn't match the data, it's indicative of a problem.
 *
 * @param hdr		Preallocated pointer to shared_header_t structure.
 * @param data		Data to be stored with hdr.
 * @param count		Size of data.
 * @return		-1 if CRC32 generation fails, or 0 on success.
 */
static int
header_generate(shared_header_t *hdr, const char *data, size_t count)
{
	memset(hdr,0,sizeof(*hdr));

	hdr->h_magic = SHARED_HEADER_MAGIC;

	if (data && count) {
		hdr->h_dcrc = clu_crc32(data, count);
		hdr->h_length = (uint32_t)count;

		if (hdr->h_dcrc == 0) {
			fprintf(stderr, "Invalid CRC32 generated on data!\n");
			return -1;
		}
	}

	hdr->h_timestamp = (uint64_t)time(NULL);

	hdr->h_hcrc = clu_crc32((char *)hdr, sizeof(*hdr));
	if (hdr->h_hcrc == 0) {
		fprintf(stderr, "Invalid CRC32 generated on header!\n");
		return -1;
	}

	header_encode(hdr);

	return 0;
}


/**
 * Verify the integrity of a shared header.  Basically, check the CRC32
 * information against the data and header.  A better name for this would
 * be "shared_block_verify".
 *
 * @param hdr		Preallocated pointer to shared_header_t structure.
 * @param data		Data to be stored with hdr.
 * @param count		Size of data.
 * @return		-1 if CRC32 generation fails, or 0 on success.
 */
static int
header_verify(shared_header_t *hdr, const char *data, size_t count)
{
	uint32_t crc;
	uint32_t bkupcrc;

	header_decode(hdr);
	/*
	 * verify the header's CRC32.  Ok, we know it's overkill taking
	 * the CRC32 of a friggin' 16-byte (12 bytes, really) structure,
	 * but why not?
	 */
	bkupcrc = hdr->h_hcrc;
	hdr->h_hcrc = 0;
	crc = clu_crc32((char *)hdr, sizeof(*hdr));
	hdr->h_hcrc = bkupcrc;
	if (bkupcrc != crc) {
#if 0
		fprintf(stderr, "Header CRC32 mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", bkupcrc, crc);
#endif
		return -1;
	}

	/*
	 * Verify the magic number.
	 */
	if (hdr->h_magic != SHARED_HEADER_MAGIC) {
#if 0
		fprintf(stderr, "Magic mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", SHARED_HEADER_MAGIC, hdr->h_magic);
#endif
		return -1;
	}

	/* 
	 * If there's no data or no count, or perhaps the length fed in is less
	 * then the expected length, bail.
	 */
	if (!data || !count || (count < hdr->h_length))
		return 0;

	crc = clu_crc32(data, (count > hdr->h_length) ?
			hdr->h_length : count);

	if (hdr->h_dcrc != crc) {
#if 0
		fprintf(stderr, "Data CRC32 mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", hdr->h_dcrc, crc);
#endif
		return -1;
	}

	return 0;
}



/*
 * qdisk_open
 * Called to open the shared state partition with appropriate mode.
 * Returns - (the file descriptor), a value >= 0 on success.
 */
int
qdisk_open(char *name)
{
	int fd;
	int retval;

	/*
	 * Open for synchronous writes to insure all writes go directly
	 * to disk.
	 */
	fd = open(name, O_RDWR | O_SYNC | O_DIRECT);
	if (fd < 0) {
		return fd;
	}

	/* Check to verify that the partition is large enough.*/
	retval = lseek(fd, END_OF_DISK, SEEK_SET);

	if (retval < 0) {
		perror("open_partition: seek");
		return -1;
	}

	if (retval < END_OF_DISK) {
		fprintf(stderr, "Partition %s too small\n", name);
		errno = EINVAL;
		return -1;
	}

	/* Set close-on-exec bit */
        retval = fcntl(fd, F_GETFD, 0);
        if (retval < 0) {
                close(fd);
                return -1;
        }

        retval |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, retval) < 0) {
		perror("open_partition: fcntl");
                close(fd);
                return -1;
        }

	return fd;
}


/*
 * qdisk_close
 * Closes the shared state disk partition.
 * Returns - value from close syscall.
 */
int
qdisk_close(int *fd)
{
	int retval;

	if (!fd || *fd < 0) {
		errno = EINVAL;
		return -1;
	}

	retval = close(*fd);
	*fd = -1;

	return retval;
}

/*
 * qdisk_validate
 * Called to verify that the specified device special file representing
 * the partition appears to be a valid device.
 * Returns: 0 - success, 1 - failure
 */
int
qdisk_validate(char *name)
{
	struct stat stat_st, *stat_ptr;
	int fd;
	stat_ptr = &stat_st;

	if (stat(name, stat_ptr) < 0) {
		perror("stat");
		return -1;
	}
	/*
	 * Verify that its a block or character special file.
	 */
	if (S_ISCHR(stat_st.st_mode) == 0 && S_ISBLK(stat_st.st_mode) == 0) {
/*
		errno = EINVAL;
		return -1;
*/
		fprintf(stderr, "Warning: %s is not a block device\n",
		        name);
	}

	/*
	 * Verify read/write permission.
	 */
	fd = qdisk_open(name);
	if (fd < 0) {
		fprintf(stderr, "%s: open of %s for RDWR failed: %s\n",
			__FUNCTION__, name, strerror(errno));
		return -1;
	}
	qdisk_close(&fd);
	return 0;
}


static int
diskRawReadShadow(int fd, off_t readOffset, char *buf, int len)
{
	int ret;
	shared_header_t *hdrp;
	char *data;
	int datalen;

	ret = lseek(fd, readOffset, SEEK_SET);
	if (ret != readOffset) {
#if 0
		fprintf(stderr,
		       "diskRawReadShadow: can't seek to offset %d.\n",
		       (int) readOffset);
#endif
		errno = ENODATA;
		return -1;
	}

	ret = diskRawRead(fd, buf, len);
	if (ret != len) {
#if 0
		fprintf(stderr, "diskRawReadShadow: aligned read "
		       "returned %d, not %d.\n", ret, len);
#endif
		errno = ENODATA;
		return -1;
	}

	/* Decode the header portion so we can run a checksum on it. */
	hdrp = (shared_header_t *)buf;
	data = (char *)buf + sizeof(*hdrp);
	swab_shared_header_t(hdrp);
	datalen = hdrp->h_length;

	if (header_verify(hdrp, data, len)) {
#if 0
		fprintf(stderr, "diskRawReadShadow: bad CRC32, "
		       "fd = %d offset = %d len = %d\n", fd,
		       (int) readOffset, len);
#endif
		errno = EPROTO;
		return -1;
	}

	return 0;
}


/*
 * The RAW IO implementation requires buffers to be 512 byte aligned.
 * Here we check for alignment and do a bounceio if necessary.
 */
static int
diskRawRead(int fd, char *buf, int len)
{
	char *alignedBuf;
	int readret;
	int extraLength;
	int readlen;
	int bounceNeeded = 1;

	if ((((unsigned long) buf & (unsigned long) 0x3ff) == 0) &&
	    ((len % 512) == 0)) {
		bounceNeeded = 0;
	}

	if (bounceNeeded == 0) {
		/* Already aligned and even multiple of 512, no bounceio
		 * required. */
		return (read(fd, buf, len));
	}

	if (len > 512) {
		fprintf(stderr,
			"diskRawRead: not setup for reads larger than %d.\n",
		       512);
		return (-1);
	}
	/*
	 * All IOs must be of size which is a multiple of 512.  Here we
	 * just add in enough extra to accommodate.
	 * XXX - if the on-disk offsets don't provide enough room we're cooked!
	 */
	extraLength = 0;
	if (len % 512) {
		extraLength = 512 - (len % 512);
	}

	readlen = len;
	if (extraLength) {
		readlen += extraLength;
	}

	readret = posix_memalign((void **)&alignedBuf, 512, 512);
	if (readret < 0) {
		return -1;
	}

	readret = read(fd, alignedBuf, readlen);
	if (readret > 0) {
		if (readret > len) {
			bcopy(alignedBuf, buf, len);
			readret = len;
		} else {
			bcopy(alignedBuf, buf, readret);
		}
	}

	free(alignedBuf);
	if (readret != len) {
		fprintf(stderr, "diskRawRead: read err, len=%d, readret=%d\n",
			len, readret);
	}

	return (readret);
}


/*
 * The RAW IO implementation requires buffers to be 512 byte aligned.
 * Here we check for alignment and do a bounceio if necessary.
 */
static int
diskRawWrite(int fd, char *buf, int len)
{
	char *alignedBuf;
	int ret;
	int extraLength;
	int writelen;
	int bounceNeeded = 1;

	if ((((unsigned long) buf & (unsigned long) 0x3ff) == 0) &&
	    ((len % 512) == 0)) {
		bounceNeeded = 0;
	}
	if (bounceNeeded == 0) {
		/* Already aligned and even multiple of 512, no bounceio
		 * required. */
		return (write(fd, buf, len));
	}

	if (len > 512) {
		fprintf(stderr,
		       "diskRawWrite: not setup for larger than %d.\n",
		       512);
		return (-1);
	}

	/*
	 * All IOs must be of size which is a multiple of 512.  Here we
	 * just add in enough extra to accommodate.
	 * XXX - if the on-disk offsets don't provide enough room we're cooked!
	 */
	extraLength = 0;
	if (len % 512) {
		extraLength = 512 - (len % 512);
	}

	writelen = len;
	if (extraLength) {
		writelen += extraLength;
	}

	ret = posix_memalign((void **)&alignedBuf, 512,512);
	if (ret < 0) {
		return (-1);
	}

	bcopy(buf, alignedBuf, len);
	ret = write(fd, alignedBuf, writelen);
	if (ret > len) {
		ret = len;
	}

	free(alignedBuf);
	if (ret != len) {
		fprintf(stderr, "diskRawWrite: write err, len=%d, ret=%dn",
		       len, ret);
	}

	return (ret);
}


static int
diskRawWriteShadow(int fd, __off64_t writeOffset, char *buf, int len)
{
	off_t retval_seek;
	ssize_t retval_write;

	if ((writeOffset < 0) || (len < 0)) {
		fprintf(stderr,
		       "diskRawWriteShadow: writeOffset=%08x, "
		       "len=%08x.\n", (int)writeOffset, len);
		return (-1);
	}

	retval_seek = lseek(fd, writeOffset, SEEK_SET);
	if (retval_seek != writeOffset) {
		fprintf(stderr,
		       "diskRawWriteShadow: can't seek to offset %d\n",
		       (int) writeOffset);
		return (-1);
	}

	retval_write = diskRawWrite(fd, buf, len);
	if (retval_write != len) {
		if (retval_write == -1) {
			fprintf(stderr, "%s: %s\n", __FUNCTION__,
			       strerror(errno));
		}
		fprintf(stderr,
		       "diskRawWriteShadow: aligned write returned %d"
		       ", not %d\n", (int)retval_write, (int)len);
		return (-1);
	}

	return 0;
}


int
qdisk_read(int fd, __off64_t offset, void *buf, int count)
{
	shared_header_t *hdrp;
	char *data;
	size_t total;
	int rv;

	/*
	 * Calculate the total length of the buffer, including the header.
	 * Raw blocks are 512 byte aligned.
	 */
	total = count + sizeof(shared_header_t);
	if (total < 512)
		total = 512;

	/* Round it up */
	if (total % 512) 
		total = total + (512 * !!(total % 512)) - (total % 512);

	hdrp = NULL;
	rv = posix_memalign((void **)&hdrp, sysconf(_SC_PAGESIZE), total);
	if (rv < 0)
		return -1;

	if (hdrp == NULL) 
		return -1;

	data = (char *)hdrp + sizeof(shared_header_t);

	rv = diskRawReadShadow(fd, offset, (char *)hdrp, total);
	
	if (rv == -1) {
		return -1;
	}
	
	/* Copy out the data */
	memcpy(buf, data, hdrp->h_length);

	/* Zero out the remainder. */
	if (hdrp->h_length < count) {
		memset(buf + hdrp->h_length, 0,
		       count - hdrp->h_length);
	}

	free(hdrp);
	return count;
}


int
qdisk_write(int fd, __off64_t offset, const void *buf, int count)
{
	size_t maxsize;
	shared_header_t *hdrp;
	char *data;
	size_t total = 0, rv = -1, psz = 512; //sysconf(_SC_PAGESIZE);

	maxsize = psz - (sizeof(shared_header_t));
	if (count >= (maxsize + sizeof(shared_header_t))) {
		printf("error: count %d >= (%d + %d)\n", (int)count,
		       (int)maxsize, (int)sizeof(shared_header_t));
		errno = ENOSPC;
		return -1;
	}

	/*
	 * Calculate the total length of the buffer, including the header.
	 * Raw blocks are 512 byte aligned.
	 */
	total = count + sizeof(shared_header_t);
	if (total < psz)
		total = psz;

	/* Round it up */
	if (total % psz) 
		total = total + (psz * !!(total % psz)) - (total % psz);

	hdrp = NULL;
	rv = posix_memalign((void **)&hdrp, sysconf(_SC_PAGESIZE), total);
	if (rv < 0) {
		perror("posix_memalign");
		return -1;
	}

	/* 
	 * Copy the data into our new buffer
	 */
	data = (char *)hdrp + sizeof(shared_header_t);
	memcpy(data, buf, count);

	if (header_generate(hdrp, buf, count) == -1) {
		free((char *)hdrp);
		return -1;
	}
	swab_shared_header_t(hdrp);

	/* 
	 * Locking must be performed elsewhere.  We make no assumptions
	 * about locking here.
	 */
	if (total == psz)
		rv = diskRawWriteShadow(fd, offset, (char *)hdrp, psz);

	if (rv == -1)
		perror("diskRawWriteShadow");
	
	free((char *)hdrp);
	if (rv == -1)
		return -1;
	return count;
}


static int
header_init(int fd, char *label)
{
	quorum_header_t qh;

	if (qdisk_read(fd, OFFSET_HEADER, &qh, sizeof(qh)) == sizeof(qh)) {
		swab_quorum_header_t(&qh);
		if (qh.qh_magic == HEADER_MAGIC_OLD) {
			printf("Warning: Red Hat Cluster Manager 1.2.x "
			       "header found\n");
		} else if (qh.qh_magic == HEADER_MAGIC_NUMBER) {
			printf("Warning: Initializing previously "
			       "initialized partition\n");
		}
	}

	if (gethostname(qh.qh_updatehost, sizeof(qh.qh_updatehost)) < 0) {
		perror("gethostname");
		return -1;
	}

	/* Copy in the cluster/label name */
	snprintf(qh.qh_cluster, sizeof(qh.qh_cluster)-1, label);

	if ((qh.qh_timestamp = (uint64_t)time(NULL)) <= 0) {
		perror("time");
		return -1;
	}

	qh.qh_magic = HEADER_MAGIC_NUMBER;
	swab_quorum_header_t(&qh);
	if (qdisk_write(fd, OFFSET_HEADER, &qh, sizeof(qh)) != sizeof(qh)) {
		return -1;
	}

	return 0;
}


int
qdisk_init(char *partname, char *label)
{
	int fd;
	status_block_t ps, wps;
	int nid;
	time_t t;

	fd = qdisk_validate(partname);
	if (fd < 0) {
		perror("qdisk_verify");
		return -1;
	}

	fd = qdisk_open(partname);
	if (fd < 0) {
		perror("qdisk_open");
		return -1;
	}

	if (header_init(fd, label) < 0) {
		return -1;
	}

	time(&t);

	ps.ps_magic = STATE_MAGIC_NUMBER;
	ps.ps_updatenode = 0;
	ps.pad0 = 0;
	ps.ps_timestamp = (uint64_t)t;
	ps.ps_state = (uint8_t)S_NONE;
	ps.pad1[0] = 0;
	ps.ps_flags = 0;
	ps.ps_score = 0;
	ps.ps_scoremax = 0;
	ps.ps_ca_sec = 0;
	ps.ps_ca_usec = 0;
	ps.ps_lc_sec = 0;
	ps.ps_ca_usec = 0;

	/* Node IDs 1..N */
	for (nid = 1; nid <= MAX_NODES_DISK; nid++) {
		ps.ps_nodeid = nid;

		printf("Initializing status block for node %d...\n", nid);
		wps = ps;
		swab_status_block_t(&wps);

		if (qdisk_write(fd, qdisk_nodeid_offset(nid), &wps, sizeof(wps)) < 0) {
			printf("Error writing node ID block %d\n", nid);
			qdisk_close(&fd);
			return -1;
		}
	}

	qdisk_close(&fd);

	return 0;
}

