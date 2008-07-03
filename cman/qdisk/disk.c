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
#include <linux/fs.h>
#include <openais/service/logsys.h>

static int diskRawRead(target_info_t *disk, char *buf, int len);
uint32_t clu_crc32(const char *data, size_t count);

LOGSYS_DECLARE_SUBSYS ("QDISK", SYSLOGLEVEL);

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
			log_printf(LOG_ERR, "Invalid CRC32 generated on data!\n");
			return -1;
		}
	}

	hdr->h_timestamp = (uint64_t)time(NULL);

	hdr->h_hcrc = clu_crc32((char *)hdr, sizeof(*hdr));
	if (hdr->h_hcrc == 0) {
		log_printf(LOG_ERR, "Invalid CRC32 generated on header!\n");
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
		log_printf(LOG_DEBUG, "Header CRC32 mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", bkupcrc, crc);
		return -1;
	}

	/*
	 * Verify the magic number.
	 */
	if (hdr->h_magic != SHARED_HEADER_MAGIC) {
		log_printf(LOG_DEBUG, "Magic mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", SHARED_HEADER_MAGIC, hdr->h_magic);
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
		log_printf(LOG_DEBUG, "Data CRC32 mismatch; Exp: 0x%08x "
			"Got: 0x%08x\n", hdr->h_dcrc, crc);
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
qdisk_open(char *name, target_info_t *disk)
{
	int ret;
	int ssz;

	/*
	 * Open for synchronous writes to insure all writes go directly
	 * to disk.
	 */
	disk->d_fd = open(name, O_RDWR | O_SYNC | O_DIRECT);
	if (disk->d_fd < 0)
		return disk->d_fd;

	ret = ioctl(disk->d_fd, BLKSSZGET, &ssz);
	if (ret < 0) {
		log_printf(LOG_ERR, "qdisk_open: ioctl(BLKSSZGET)");
		return -1;
	}

	disk->d_blksz = ssz;
	disk->d_pagesz = sysconf(_SC_PAGESIZE);

	/* Check to verify that the partition is large enough.*/
	ret = lseek(disk->d_fd, END_OF_DISK(disk->d_blksz), SEEK_SET);
	if (ret < 0) {
		log_printf(LOG_DEBUG, "open_partition: seek");
		return -1;
	}

	if (ret < END_OF_DISK(disk->d_blksz)) {
		log_printf(LOG_ERR, "Partition %s too small\n", name);
		errno = EINVAL;
		return -1;
	}

	/* Set close-on-exec bit */
        ret = fcntl(disk->d_fd, F_GETFD, 0);
        if (ret < 0) {
		log_printf(LOG_ERR, "open_partition: fcntl(F_GETFD)");
                close(disk->d_fd);
                return -1;
        }

        ret |= FD_CLOEXEC;
        if (fcntl(disk->d_fd, F_SETFD, ret) < 0) {
		log_printf(LOG_ERR, "open_partition: fcntl(F_SETFD)");
                close(disk->d_fd);
                return -1;
        }

	return 0;
}


/*
 * qdisk_close
 * Closes the shared state disk partition.
 * Returns - value from close syscall.
 */
int
qdisk_close(target_info_t *disk)
{
	int retval;

	if (!disk || disk->d_fd < 0) {
		errno = EINVAL;
		return -1;
	}

	retval = close(disk->d_fd);
	disk->d_fd = -1;

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
	target_info_t disk;
	stat_ptr = &stat_st;

	if (stat(name, stat_ptr) < 0) {
		log_printf(LOG_ERR, "stat");
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
		log_printf(LOG_WARNING, "Warning: %s is not a block device\n",
		        name);
	}

	/*
	 * Verify read/write permission.
	 */
	if (qdisk_open(name, &disk) < 0) {
		log_printf(LOG_DEBUG, "%s: open of %s for RDWR failed: %s\n",
			__FUNCTION__, name, strerror(errno));
		return -1;
	}
	qdisk_close(&disk);
	return 0;
}


static int
diskRawReadShadow(target_info_t *disk, off_t readOffset, char *buf, int len)
{
	int ret;
	shared_header_t *hdrp;
	char *data;
	int datalen;

	ret = lseek(disk->d_fd, readOffset, SEEK_SET);
	if (ret != readOffset) {
		log_printf(LOG_DEBUG,
		       "diskRawReadShadow: can't seek to offset %d.\n",
		       (int) readOffset);
		errno = ENODATA;
		return -1;
	}

	ret = diskRawRead(disk, buf, len);
	if (ret != len) {
		log_printf(LOG_DEBUG, "diskRawReadShadow: aligned read "
		       "returned %d, not %d.\n", ret, len);
		errno = ENODATA;
		return -1;
	}

	/* Decode the header portion so we can run a checksum on it. */
	hdrp = (shared_header_t *)buf;
	data = (char *)buf + sizeof(*hdrp);
	swab_shared_header_t(hdrp);
	datalen = hdrp->h_length;

	if (header_verify(hdrp, data, len)) {
		log_printf(LOG_DEBUG, "diskRawReadShadow: bad CRC32, "
		       "offset = %d len = %d\n",
		       (int) readOffset, len);
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
diskRawRead(target_info_t *disk, char *buf, int len)
{
	void *alignedBuf;
	int readret;
	int extraLength;
	int readlen;
	int bounceNeeded = 1;

	
	/* was 3ff, which is (512<<1-1) */
	if ((((unsigned long) buf &
	      (unsigned long) ((disk->d_blksz << 1) -1)) == 0) &&
	    ((len % (disk->d_blksz)) == 0)) {
		bounceNeeded = 0;
	}

	if (bounceNeeded == 0) {
		/* Already aligned and even multiple of 512, no bounceio
		 * required. */
		return (read(disk->d_fd, buf, len));
	}

	if (len > disk->d_blksz) {
		log_printf(LOG_ERR,
			"diskRawRead: not setup for reads larger than %d.\n",
		       (int)disk->d_blksz);
		return (-1);
	}
	/*
	 * All IOs must be of size which is a multiple of 512.  Here we
	 * just add in enough extra to accommodate.
	 * XXX - if the on-disk offsets don't provide enough room we're cooked!
	 */
	extraLength = 0;
	if (len % disk->d_blksz) {
		extraLength = disk->d_blksz - (len % disk->d_blksz);
	}

	readlen = len;
	if (extraLength) {
		readlen += extraLength;
	}

	readret = posix_memalign((void **)&alignedBuf, disk->d_pagesz, disk->d_blksz);
	if (readret < 0) {
		return -1;
	}

	readret = read(disk->d_fd, alignedBuf, readlen);
	if (readret > 0) {
		if (readret > len) {
			memcpy(alignedBuf, buf, len);
			readret = len;
		} else {
			memcpy(alignedBuf, buf, readret);
		}
	}

	free(alignedBuf);
	if (readret != len) {
		log_printf(LOG_ERR, "diskRawRead: read err, len=%d, readret=%d\n",
			len, readret);
	}

	return (readret);
}


/*
 * The RAW IO implementation requires buffers to be 512 byte aligned.
 * Here we check for alignment and do a bounceio if necessary.
 */
static int
diskRawWrite(target_info_t *disk, char *buf, int len)
{
	void *alignedBuf;
	int ret;
	int extraLength;
	int writelen;
	int bounceNeeded = 1;

	/* was 3ff, which is (512<<1-1) */
	if ((((unsigned long) buf &
	      (unsigned long) ((disk->d_blksz << 1) -1)) == 0) &&
	    ((len % (disk->d_blksz)) == 0)) {
		bounceNeeded = 0;
	}

	if (bounceNeeded == 0) {
		/* Already aligned and even multiple of 512, no bounceio
		 * required. */
		return (write(disk->d_fd, buf, len));
	}

	if (len > disk->d_blksz) {
		log_printf(LOG_ERR,
			"diskRawRead: not setup for reads larger than %d.\n",
		       (int)disk->d_blksz);
		return (-1);
	}
	/*
	 * All IOs must be of size which is a multiple of 512.  Here we
	 * just add in enough extra to accommodate.
	 * XXX - if the on-disk offsets don't provide enough room we're cooked!
	 */
	extraLength = 0;
	if (len % disk->d_blksz) {
		extraLength = disk->d_blksz - (len % disk->d_blksz);
	}

	writelen = len;
	if (extraLength) {
		writelen += extraLength;
	}

	ret = posix_memalign((void **)&alignedBuf, disk->d_pagesz, disk->d_blksz);
	if (ret < 0) {
		return -1;
	}

	if (len > disk->d_blksz) {
		log_printf(LOG_ERR,
		       "diskRawWrite: not setup for larger than %d.\n",
		       (int)disk->d_blksz);
		return (-1);
	}

	memcpy(buf, alignedBuf, len);
	ret = write(disk->d_fd, alignedBuf, writelen);
	if (ret > len) {
		ret = len;
	}

	free(alignedBuf);
	if (ret != len) {
		log_printf(LOG_ERR, "diskRawWrite: write err, len=%d, ret=%dn",
		       len, ret);
	}

	return (ret);
}


static int
diskRawWriteShadow(target_info_t *disk, __off64_t writeOffset, char *buf, int len)
{
	off_t retval_seek;
	ssize_t retval_write;

	if ((writeOffset < 0) || (len < 0)) {
		log_printf(LOG_ERR,
		       "diskRawWriteShadow: writeOffset=%08x, "
		       "len=%08x.\n", (int)writeOffset, len);
		return (-1);
	}

	retval_seek = lseek(disk->d_fd, writeOffset, SEEK_SET);
	if (retval_seek != writeOffset) {
		log_printf(LOG_ERR,
		       "diskRawWriteShadow: can't seek to offset %d\n",
		       (int) writeOffset);
		return (-1);
	}

	retval_write = diskRawWrite(disk, buf, len);
	if (retval_write != len) {
		if (retval_write == -1) {
			log_printf(LOG_ERR, "%s: %s\n", __FUNCTION__,
			       strerror(errno));
		}
		log_printf(LOG_ERR,
		       "diskRawWriteShadow: aligned write returned %d"
		       ", not %d\n", (int)retval_write, (int)len);
		return (-1);
	}

	return 0;
}


int
qdisk_read(target_info_t *disk, __off64_t offset, void *buf, int count)
{
	shared_header_t *hdrp;
	void *ptr;
	char *data;
	size_t total;
	int rv;

	/*
	 * Calculate the total length of the buffer, including the header.
	 * Raw blocks are 512 byte aligned.
	 */
	total = count + sizeof(shared_header_t);
	if (total < disk->d_blksz)
		total = disk->d_blksz;

	/* Round it up */
	if (total % disk->d_blksz) 
		total = total + (disk->d_blksz * !!(total % disk->d_blksz)) - (total % disk->d_blksz);

	ptr = NULL;
	rv = posix_memalign((void **)&ptr, disk->d_pagesz, disk->d_blksz);
	if (rv < 0)
		return -1;

	if (ptr == NULL) 
		return -1;

	hdrp = (shared_header_t *)ptr;
	data = (char *)hdrp + sizeof(shared_header_t);

	rv = diskRawReadShadow(disk, offset, (char *)hdrp, disk->d_blksz);
	
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
qdisk_write(target_info_t *disk, __off64_t offset, const void *buf, int count)
{
	size_t maxsize;
	shared_header_t *hdrp;
	void *ptr;
	char *data;
	size_t total = 0, rv = -1, psz = disk->d_blksz; //sysconf(_SC_PAGESIZE);

	maxsize = psz - (sizeof(shared_header_t));
	if (count >= (maxsize + sizeof(shared_header_t))) {
		log_printf(LOG_ERR, "error: count %d >= (%d + %d)\n", (int)count,
		       (int)maxsize, (int)sizeof(shared_header_t));
		errno = ENOSPC;
		return -1;
	}

	/*
	 * Calculate the total length of the buffer, including the header.
	 */
	total = count + sizeof(shared_header_t);
	if (total < psz)
		total = psz;

	/* Round it up */
	if (total % psz) 
		total = total + (psz * !!(total % psz)) - (total % psz);

	ptr = NULL;
	rv = posix_memalign((void **)&ptr, disk->d_pagesz, total);
	if (rv < 0) {
		log_printf(LOG_ERR, "posix_memalign");
		return -1;
	}

	/* 
	 * Copy the data into our new buffer
	 */
	hdrp = (shared_header_t *)ptr;
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
		rv = diskRawWriteShadow(disk, offset, (char *)hdrp, psz);

	if (rv == -1)
		log_printf(LOG_ERR, "diskRawWriteShadow");
	
	free((char *)hdrp);
	if (rv == -1)
		return -1;
	return count;
}


static int
header_init(target_info_t *disk, char *label)
{
	quorum_header_t qh;

	if (qdisk_read(disk, OFFSET_HEADER, &qh, sizeof(qh)) == sizeof(qh)) {
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
		log_printf(LOG_ERR, "gethostname");
		return -1;
	}

	/* Copy in the cluster/label name */
	snprintf(qh.qh_cluster, sizeof(qh.qh_cluster)-1, "%s", label);

	qh.qh_version = VERSION_MAGIC_V2;
	if ((qh.qh_timestamp = (uint64_t)time(NULL)) <= 0) {
		log_printf(LOG_ERR, "time");
		return -1;
	}

	qh.qh_magic = HEADER_MAGIC_NUMBER;
	qh.qh_blksz = disk->d_blksz;
	qh.qh_kernsz = 0;

	swab_quorum_header_t(&qh);
	if (qdisk_write(disk, OFFSET_HEADER, &qh, sizeof(qh)) != sizeof(qh)) {
		return -1;
	}

	return 0;
}


int
qdisk_init(char *partname, char *label)
{
	target_info_t disk;
	status_block_t ps, wps;
	int nid, ret;
	time_t t;

	ret = qdisk_validate(partname);
	if (ret < 0) {
		log_printf(LOG_DEBUG, "qdisk_verify");
		return -1;
	}

	ret = qdisk_open(partname, &disk);
	if (ret < 0) {
		log_printf(LOG_ERR, "qdisk_open");
		return -1;
	}

	if (header_init(&disk, label) < 0) {
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

		if (qdisk_write(&disk, qdisk_nodeid_offset(nid, disk.d_blksz), &wps, sizeof(wps)) < 0) {
			printf("Error writing node ID block %d\n", nid);
			qdisk_close(&disk);
			return -1;
		}
	}

	qdisk_close(&disk);

	return 0;
}

