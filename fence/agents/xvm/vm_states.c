/** @file
 * Distributed VM states using saCkpt interface
 */
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <openais/saAis.h>
#include <openais/saCkpt.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

typedef struct {
	uint32_t ck_ready;
	int ck_timeout;
	SaCkptCheckpointHandleT ck_checkpoint;
	SaCkptHandleT ck_handle;
	char *ck_name;
} ckpt_handle;


#define READY_MAGIC 0x13fd237c
#define VALIDATE(h) \
do { \
	if (!h || h->ck_ready != READY_MAGIC) { \
		errno = EINVAL; \
		return -1; \
	} \
} while(0)

int ais_to_posix(SaAisErrorT err);

int
ais_to_posix(SaAisErrorT err)
{
	switch (err) {
	case SA_AIS_OK:
		return 0;
	case SA_AIS_ERR_LIBRARY:
		return ELIBBAD;
	case SA_AIS_ERR_VERSION:
		return EPROTONOSUPPORT; //XXX
	case SA_AIS_ERR_INIT:
		return EFAULT; //XXX
	case SA_AIS_ERR_TIMEOUT:
		return ETIMEDOUT;
	case SA_AIS_ERR_TRY_AGAIN:
		return EAGAIN;
	case SA_AIS_ERR_INVALID_PARAM:
		return EINVAL;
	case SA_AIS_ERR_NO_MEMORY:
		return ENOMEM;
	case SA_AIS_ERR_BAD_HANDLE:
		return EBADF;
	case SA_AIS_ERR_BUSY:
		return EBUSY;
	case SA_AIS_ERR_ACCESS:
		return EACCES;
	case SA_AIS_ERR_NOT_EXIST:
		return ENOENT;
	case SA_AIS_ERR_NAME_TOO_LONG:
		return ENAMETOOLONG;
	case SA_AIS_ERR_EXIST:
		return EEXIST;
	case SA_AIS_ERR_NO_SPACE:
		return ENOSPC;
	case SA_AIS_ERR_INTERRUPT:
		return EINTR;
	case SA_AIS_ERR_NAME_NOT_FOUND:
		return ENOENT;
	case SA_AIS_ERR_NO_RESOURCES:
		return ENOMEM; //XXX
	case SA_AIS_ERR_NOT_SUPPORTED:
		return ENOSYS;
	case SA_AIS_ERR_BAD_OPERATION:
		return EINVAL; //XXX
	case SA_AIS_ERR_FAILED_OPERATION:
		return EIO; //XXX
	case SA_AIS_ERR_MESSAGE_ERROR:
		return EIO; // XXX
	case SA_AIS_ERR_QUEUE_FULL:
		return ENOBUFS;
	case SA_AIS_ERR_QUEUE_NOT_AVAILABLE:
		return ENOENT;
	case SA_AIS_ERR_BAD_FLAGS:
		return EINVAL;
	case SA_AIS_ERR_TOO_BIG:
		return E2BIG;
	case SA_AIS_ERR_NO_SECTIONS:
		return ENOENT; // XXX
	}

	return -1;
}


static int 
ckpt_open(ckpt_handle *h, const char *ckpt_name, int maxsize,
	  int maxsec, int maxsecsize, int timeout)
{
	SaCkptCheckpointCreationAttributesT attrs;
	SaCkptCheckpointOpenFlagsT flags;
	SaNameT cpname;
#if 0
	SaCkptCheckpointDescriptorT status;
#endif
	SaAisErrorT err = SA_AIS_OK;

	VALIDATE(h);

	flags = SA_CKPT_CHECKPOINT_READ |
		SA_CKPT_CHECKPOINT_WRITE;

	snprintf((char *)cpname.value, SA_MAX_NAME_LENGTH-1,
		 "%s", ckpt_name);
	cpname.length = strlen(ckpt_name);

	h->ck_timeout = timeout;

	err = saCkptCheckpointOpen(h->ck_handle,
				   &cpname,
				   NULL,	
				   flags,
				   timeout,
				   &h->ck_checkpoint);

	if (err == SA_AIS_OK) {
#if 0
		saCkptCheckpointStatusGet(h->ck_handle,
					  &status);

		printf("Checkpoint Size = %d bytes\n", (int)
			status.checkpointCreationAttributes.checkpointSize);
		printf("Flags = ");
		if (status.checkpointCreationAttributes.creationFlags &
			SA_CKPT_WR_ALL_REPLICAS) {
			printf("%s ", "SA_CKPT_WR_ALL_REPLICAS");
		}
		if (status.checkpointCreationAttributes.creationFlags &
			SA_CKPT_WR_ACTIVE_REPLICA) {
			printf("%s ", "SA_CKPT_WR_ACTIVE_REPLICA");
		}
		if (status.checkpointCreationAttributes.creationFlags &
			SA_CKPT_WR_ACTIVE_REPLICA_WEAK) {
			printf("%s ", "SA_CKPT_WR_ACTIVE_REPLICA_WEAK");
		}
		if (status.checkpointCreationAttributes.creationFlags &
			SA_CKPT_CHECKPOINT_COLLOCATED) {
			printf("%s ", "SA_CKPT_CHECKPOINT_COLLOCATED");
		}
		printf("\nMax sections = %d\n",
			(int)status.checkpointCreationAttributes.maxSections);
		printf("Max section size = %d\n",
			(int)status.checkpointCreationAttributes.maxSectionSize);
		printf("Max section ID size = %d\n",
			(int)status.checkpointCreationAttributes.maxSectionIdSize);
		printf("Section count = %d\n", status.numberOfSections);
		printf("\n");
#endif
		goto good;
	}

	attrs.creationFlags = SA_CKPT_WR_ALL_REPLICAS;
	attrs.checkpointSize = (SaSizeT)maxsize;
	attrs.retentionDuration = SA_TIME_ONE_HOUR;
	attrs.maxSections = maxsec;
	attrs.maxSectionSize = (SaSizeT)maxsecsize;
	attrs.maxSectionIdSize = (SaSizeT)32;

	flags = SA_CKPT_CHECKPOINT_READ |
		SA_CKPT_CHECKPOINT_WRITE |
		SA_CKPT_CHECKPOINT_CREATE;

	err = saCkptCheckpointOpen(h->ck_handle,
				   &cpname,
				   &attrs,
				   flags,
				   timeout,
				   &h->ck_checkpoint);
	if (err == SA_AIS_OK)
		goto good;

	/* No checkpoint */
	errno = ais_to_posix(err);
	return (errno == 0 ? 0 : -1);
good:
	printf("Opened ckpt %s\n", ckpt_name);
	h->ck_name = strdup(ckpt_name);

	errno = ais_to_posix(err);
	return (errno == 0 ? 0 : -1);
}


void *
ckpt_init(char *ckpt_name, int maxlen, int maxsec,
	  int maxseclen, int timeout)
{
	ckpt_handle *h;
	SaAisErrorT err;
	SaVersionT ver;

	if (!ckpt_name || !strlen(ckpt_name)) {
		errno = EINVAL;
		return NULL;
	}
	h = malloc(sizeof(*h));
	if (!h)
		return NULL;
	memset(h, 0, sizeof(*h));

	ver.releaseCode = 'B';
	ver.majorVersion = 1;
	ver.minorVersion = 1;

	err = saCkptInitialize(&h->ck_handle, NULL, &ver);

	if (err != SA_AIS_OK) {
		free(h);
		return NULL;
	} else
		h->ck_ready = READY_MAGIC;

	if (ckpt_open(h, ckpt_name, maxlen, maxsec, maxseclen,
		      timeout) < 0) {
		saCkptCheckpointClose(h->ck_checkpoint);
		if (h->ck_name) 
			free(h->ck_name);
		free(h);
		return NULL;
	}

	return (void *)h;
}


int
ckpt_write(void *hp, char *secid, void *buf, size_t maxlen)
{
	ckpt_handle *h = (ckpt_handle *)hp;
	SaCkptIOVectorElementT iov = {SA_CKPT_DEFAULT_SECTION_ID,
				      NULL, 0, 0, 0};
	SaAisErrorT err;
	SaCkptSectionCreationAttributesT attrs;

	VALIDATE(h);

	/* Set section ID here */
	iov.sectionId.id = (uint8_t *)secid;
	iov.sectionId.idLen = strlen(secid);
	iov.dataBuffer = buf;
	iov.dataSize = (SaSizeT)maxlen;
	iov.dataOffset = 0;
	iov.readSize = 0;

	err = saCkptCheckpointWrite(h->ck_checkpoint, &iov, 1, NULL);

	if (err == SA_AIS_ERR_NOT_EXIST) {
		attrs.sectionId = &iov.sectionId;
		attrs.expirationTime = SA_TIME_END;

		err = saCkptSectionCreate(h->ck_checkpoint, &attrs, 
					  buf, maxlen);
	}

	if (err == SA_AIS_OK)
		saCkptCheckpointSynchronize(h->ck_checkpoint,
					    h->ck_timeout);

	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return maxlen; /* XXX */
}


int
ckpt_read(void *hp, char *secid, void *buf, size_t maxlen)
{
	ckpt_handle *h = (ckpt_handle *)hp;
	SaCkptIOVectorElementT iov = {SA_CKPT_DEFAULT_SECTION_ID,
				      NULL, 0, 0, 0};
	SaAisErrorT err;

	VALIDATE(h);
	//printf("reading ckpt %s\n", keyid);

	iov.sectionId.id = (uint8_t *)secid;
	iov.sectionId.idLen = strlen(secid);
	iov.dataBuffer = buf;
	iov.dataSize = (SaSizeT)maxlen;
	iov.dataOffset = 0;
	iov.readSize = 0;

	err = saCkptCheckpointRead(h->ck_checkpoint, &iov, 1, NULL);

	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return iov.readSize; /* XXX */
}


int
ckpt_finish(void *hp)
{
	ckpt_handle *h = (ckpt_handle *)hp;
	int ret = 0;
	SaAisErrorT err;

	saCkptCheckpointClose(h->ck_checkpoint);
	err = saCkptFinalize(h->ck_handle);

	if (err != SA_AIS_OK)
		ret = -1;
	else
		h->ck_ready = 0;

	if (h->ck_name)
		free(h->ck_name);

	if (ret != 0)
		errno = ais_to_posix(err);
	return ret;
}


#ifdef STANDALONE
void
usage(int ret)
{
	printf("usage: ckpt [-c ckpt_name] <-r key|-w key -d data>\n");
	exit(ret);
}

int
main(int argc, char **argv)
{
	char *ckptname = "ckpt_test";
	char *sec = "default";
	char *val;
	void *h;
	char buf[64];
	int ret;
	int op = 0;

	while((ret = getopt(argc, argv, "c:w:r:d:j?")) != EOF) {
		switch(ret) {
		case 'c': 
			ckptname = optarg;
			break;
		case 'w': 
			op = 'w';
			sec = optarg;
			break;
		case 'r':
			op = 'r';
			sec = optarg;
			break;
		case 'd':
			val = optarg;
			break;
		case '?':
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	if (!op) {
		usage(1);
	}

	if (!sec) {
		usage(1);
	}

	h = ckpt_init(ckptname, 262144, 4096, 64, 10);
	if (!h) {
		perror("ckpt_init");
		return -1;
	}

	if (op == 'w') {
		if (ckpt_write(h, sec, val, strlen(val)+1) < 0) {
			perror("ckpt_write");
			return 1;
		}
	} else if (op == 'r') {
		ret = ckpt_read(h, sec, buf, sizeof(buf));
		if (ret < 0) {
			perror("ckpt_read");
			return 1;
		}

		printf("%d bytes\nDATA for '%s':\n%s\n", ret, sec,
		       buf);
	}

	ckpt_finish(h);

	return 0;
}
#endif
