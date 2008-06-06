//#define DEBUG
/** @file
 * Distributed states using saCkpt interface
 */
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <saAis.h>
#include <saCkpt.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ds.h>

typedef struct _key_node {
	struct _key_node *kn_next;
	char *kn_keyid;
	SaTimeT kn_timeout;
	uint16_t kn_ready; 
	SaNameT kn_cpname;
	SaCkptCheckpointHandleT kn_cph;
} key_node_t;


static key_node_t *key_list = NULL;
static SaCkptHandleT ds_ckpt;
static int ds_ready = 0;
static pthread_mutex_t ds_mutex = PTHREAD_MUTEX_INITIALIZER;


int ais_to_posix(SaAisErrorT err);


static key_node_t *
kn_find_key(char *keyid)
{
	key_node_t *cur;

	for (cur = key_list; cur; cur = cur->kn_next)
		if (!strcmp(cur->kn_keyid,keyid))
			return cur;

	return NULL;
}


/**
 * Adds a key to key node list and sets up callback functions.
 */
static SaAisErrorT
ds_key_init_nt(char *keyid, int maxsize, int timeout)
{
	SaCkptCheckpointCreationAttributesT attrs;
	SaCkptCheckpointOpenFlagsT flags;
#if 0
	SaCkptCheckpointDescriptorT status;
#endif
	SaAisErrorT err = SA_AIS_OK;
	key_node_t *newnode = NULL;
	
	newnode = kn_find_key(keyid);
	if (newnode) {
		printf("Key %s already initialized\n", keyid);
		return SA_AIS_OK;
	}

	newnode = malloc(sizeof(*newnode));
	memset(newnode,0,sizeof(*newnode));
	snprintf((char *)newnode->kn_cpname.value, SA_MAX_NAME_LENGTH-1,
		 "%s", keyid);
	newnode->kn_cpname.length = strlen(keyid);
	newnode->kn_keyid = (char *)newnode->kn_cpname.value;
	newnode->kn_ready = 0;

	if (timeout < 5) {
		/* Join View message timeout must exceed the
		   coordinator timeout */
		timeout = 5;
	}
	newnode->kn_timeout = timeout * SA_TIME_ONE_SECOND;

	flags = SA_CKPT_CHECKPOINT_READ |
		SA_CKPT_CHECKPOINT_WRITE;

	err = saCkptCheckpointOpen(ds_ckpt,
				   &newnode->kn_cpname,
				   NULL,	
				   flags,
				   newnode->kn_timeout,
				   &newnode->kn_cph);

	if (err == SA_AIS_OK) {
#if 0
		saCkptCheckpointStatusGet(newnode->kn_cph,
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
	attrs.maxSections = 1;
	attrs.maxSectionSize = (SaSizeT)maxsize;
	attrs.maxSectionIdSize = (SaSizeT)32;

	flags = SA_CKPT_CHECKPOINT_READ |
		SA_CKPT_CHECKPOINT_WRITE |
		SA_CKPT_CHECKPOINT_CREATE;

	err = saCkptCheckpointOpen(ds_ckpt,
				   &newnode->kn_cpname,
				   &attrs,
				   flags,
				   newnode->kn_timeout,
				   &newnode->kn_cph);
	if (err == SA_AIS_OK)
		goto good;

	/* No checkpoint */
	free(newnode);
	return err;
good:

	newnode->kn_ready = 1;
	newnode->kn_next = key_list;
	key_list = newnode;
#if 0
	printf("Opened ckpt %s\n", keyid);
#endif

	return err;
}


int
ds_key_init(char *keyid, int maxsize, int timeout)
{
	SaAisErrorT err;

	pthread_mutex_lock(&ds_mutex);
	err = ds_key_init_nt(keyid, maxsize, timeout);
	pthread_mutex_unlock(&ds_mutex);

	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return 0;
}


static SaAisErrorT
ds_key_cleanup(key_node_t *node)
{
	if (!node || !node->kn_ready) {
		printf("Key %s already freed\n", node->kn_keyid);
		return SA_AIS_OK;
	}

	return saCkptCheckpointClose(node->kn_cph);
}



static SaAisErrorT
ds_key_finish_nt(char *keyid)
{
	key_node_t *node;

	node = kn_find_key(keyid);
	/* TODO: Free list entry */

	return ds_key_cleanup(node);
}


int
ds_key_finish(char *keyid)
{
	SaAisErrorT err;

	pthread_mutex_lock(&ds_mutex);
	err = ds_key_finish_nt(keyid);
	pthread_mutex_unlock(&ds_mutex);

	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return 0;
}



static void
open_callback(SaInvocationT invocation,
	      SaCkptCheckpointHandleT handle,
	      SaAisErrorT error)
{
	/* Do Open callback here.  Since we use sync calls instead
	   of async calls, this is never used. */
}


static void
sync_callback(SaInvocationT invocation,
	      SaAisErrorT error)
{
	/* Do Sync callback here.  Since we use sync calls instead
	   of async calls, this is never used. */
}


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


int
ds_init(void)
{
	int ret = 0;
	SaAisErrorT err;
	SaVersionT ver;
	SaCkptCallbacksT callbacks;

	pthread_mutex_lock(&ds_mutex);
	if (ds_ready) {
		pthread_mutex_unlock(&ds_mutex);
		return 0;
	}

	ver.releaseCode = 'B';
	ver.majorVersion = 1;
	ver.minorVersion = 1;

	callbacks.saCkptCheckpointOpenCallback = open_callback;
	callbacks.saCkptCheckpointSynchronizeCallback = sync_callback;

	err = saCkptInitialize(&ds_ckpt, &callbacks, &ver);

	if (err != SA_AIS_OK)
		ret = -1;
	else
		ds_ready= 1;

	pthread_mutex_unlock(&ds_mutex);

	if (ret != 0)
		errno = ais_to_posix(err);
	return ret;
}


int
ds_write(char *keyid, void *buf, size_t maxlen)
{
	key_node_t *node;
	SaCkptIOVectorElementT iov = {SA_CKPT_DEFAULT_SECTION_ID,
				      NULL, 0, 0, 0};
	SaAisErrorT err;

	//printf("writing to ckpt %s\n", keyid);

	pthread_mutex_lock(&ds_mutex);

	while ((node = kn_find_key(keyid)) == NULL) {

		err = ds_key_init_nt(keyid,
				(maxlen>DS_MIN_SIZE?maxlen:DS_MIN_SIZE), 5);
		if (err != SA_AIS_OK)
			goto out;
	}

	iov.dataBuffer = buf;
	iov.dataSize = (SaSizeT)maxlen;
	iov.dataOffset = 0;
	iov.readSize = 0;

	err = saCkptCheckpointWrite(node->kn_cph, &iov, 1, NULL);

	if (err == SA_AIS_OK)
		saCkptCheckpointSynchronize(node->kn_cph, node->kn_timeout);

out:
	pthread_mutex_unlock(&ds_mutex);
	
	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return maxlen; /* XXX */
}


int
ds_read(char *keyid, void *buf, size_t maxlen)
{
	key_node_t *node;
	SaCkptIOVectorElementT iov = {SA_CKPT_DEFAULT_SECTION_ID,
				      NULL, 0, 0, 0};
	SaAisErrorT err;

	//printf("reading ckpt %s\n", keyid);

	pthread_mutex_lock(&ds_mutex);

	node = kn_find_key(keyid);
	if (!node) {
		pthread_mutex_unlock(&ds_mutex);
		errno = ENOENT;
		return -1;
	}

	iov.dataBuffer = buf;
	iov.dataSize = (SaSizeT)maxlen;
	iov.dataOffset = 0;
	iov.readSize = 0;

	err = saCkptCheckpointRead(node->kn_cph, &iov, 1, NULL);

	pthread_mutex_unlock(&ds_mutex);
	
	errno = ais_to_posix(err);
	if (errno)
		return -1;
	return iov.readSize; /* XXX */
}


int
ds_finish(void)
{
	int ret = 0;
	SaAisErrorT err;
	key_node_t *node;

	pthread_mutex_lock(&ds_mutex);
	if (!ds_ready) {
		pthread_mutex_unlock(&ds_mutex);
		return 0;
	}

	/* Zap all the checkpoints */
	for (node = key_list; node; node = node->kn_next) {
		ds_key_cleanup(node);
	}

	err = saCkptFinalize(ds_ckpt);

	if (err != SA_AIS_OK)
		ret = -1;
	else
		ds_ready = 0;

	pthread_mutex_unlock(&ds_mutex);

	if (ret != 0)
		errno = ais_to_posix(err);
	return ret;
}


#ifdef STANDALONE
void
usage(int ret)
{
	printf("usage: ckpt <-r key|-w key -d data>\n");
	exit(ret);
}

int
main(int argc, char **argv)
{
	char *keyid = "testing";
	char *val;
	char buf[DS_MIN_SIZE];
	int ret;
	int op = 0;

	while((ret = getopt(argc, argv, "w:r:d:j?")) != EOF) {
		switch(ret) {
		case 'w': 
			op = 'w';
			keyid = optarg;
			break;
		case 'r':
			op = 'r';
			keyid = optarg;
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

	if (!keyid) {
		usage(1);
	}

	if (ds_init() < 0) {
		perror("ds_init");
		return 1;
	}

	if (ds_key_init(keyid, DS_MIN_SIZE, 5) < 0) {
		perror("ds_key_init");
		return 1;
	}

	if (op == 'w') {
		if (ds_write(keyid, val, strlen(val)+1) < 0) {
			perror("ds_write");
			return 1;
		}
	} else if (op == 'r') {
		ret = ds_read(keyid, buf, sizeof(buf));
		if (ret < 0) {
			perror("ds_write");
			return 1;
		}

		printf("%d bytes\nDATA for '%s':\n%s\n", ret, keyid,
		       buf);
	}

	ds_key_finish(keyid);

	if (ds_finish() < 0) {
		perror("ds_finish");
		return 0;
	}

	return 0;
}
#endif
