#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sechash.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

/* Local includes */
#include "xvm.h"
#include "simple_auth.h"
#include "debug.h"


void
print_hash(unsigned char *hash, size_t hashlen)
{
	int x; 

	for (x = 0; x < hashlen; x++)
		printf("%02x", (hash[x]&0xff));
}


static void
sha_sign(fence_req_t *req, void *key, size_t key_len)
{
	unsigned char hash[SHA512_LENGTH];
	HASHContext *h;
	HASH_HashType ht;
	unsigned int rlen;
	int devrand;

	switch(req->hashtype) {
		case HASH_SHA1:
			ht = HASH_AlgSHA1;
			break;
		case HASH_SHA256:
			ht = HASH_AlgSHA256;
			break;
		case HASH_SHA512:
			ht = HASH_AlgSHA512;
			break;
		default:
			return;
	}

	dbg_printf(4, "Opening /dev/urandom\n");
	devrand = open("/dev/urandom", O_RDONLY);
	if (devrand >= 0) {
		if (read(devrand, req->random, sizeof(req->random)) < 0) {
			perror("read /dev/urandom");
		}
		close(devrand);
	}

	memset(hash, 0, sizeof(hash));
	h = HASH_Create(ht);
	if (!h)
		return;

	HASH_Begin(h);
	HASH_Update(h, key, key_len);
	HASH_Update(h, (void *)req, sizeof(*req));
	HASH_End(h, hash, &rlen, sizeof(hash));
	HASH_Destroy(h);

	memcpy(req->hash, hash, sizeof(req->hash));
}


static int
sha_verify(fence_req_t *req, void *key, size_t key_len)
{
	unsigned char hash[SHA512_LENGTH];
	unsigned char pkt_hash[SHA512_LENGTH];
	HASHContext *h = NULL;
	HASH_HashType ht;
	unsigned int rlen;
	int ret;

	switch(req->hashtype) {
		case HASH_SHA1:
			ht = HASH_AlgSHA1;
			break;
		case HASH_SHA256:
			ht = HASH_AlgSHA256;
			break;
		case HASH_SHA512:
			ht = HASH_AlgSHA512;
			break;
		default:
			dbg_printf(3, "%s: no-op (HASH_NONE)\n", __FUNCTION__);
			return 0;
	}

	memset(hash, 0, sizeof(hash));
	h = HASH_Create(ht);
	if (!h)
		return 0;

	memcpy(pkt_hash, req->hash, sizeof(pkt_hash));
	memset(req->hash, 0, sizeof(req->hash));

	HASH_Begin(h);
	HASH_Update(h, key, key_len);
	HASH_Update(h, (void *)req, sizeof(*req));
	HASH_End(h, hash, &rlen, sizeof(hash));
	HASH_Destroy(h);

	memcpy(req->hash, pkt_hash, sizeof(req->hash));

	ret = !memcmp(hash, pkt_hash, sizeof(hash));
	if (!ret) {
		printf("Hash mismatch:\nPKT = ");
		print_hash(pkt_hash, sizeof(pkt_hash));
		printf("\nEXP = ");
		print_hash(hash, sizeof(hash));
		printf("\n");
	}

	return ret;
}


int
sign_request(fence_req_t *req, void *key, size_t key_len)
{
	memset(req->hash, 0, sizeof(req->hash));
	switch(req->hashtype) {
	case HASH_NONE:
		dbg_printf(3, "%s: no-op (HASH_NONE)\n", __FUNCTION__);
		return 0;
	case HASH_SHA1:
	case HASH_SHA256:
	case HASH_SHA512:
		sha_sign(req, key, key_len);
		return 0;
	default:
		break;
	}
	return -1;
}


int
verify_request(fence_req_t *req, fence_hash_t min,
	       void *key, size_t key_len)
{
	if (req->hashtype < min) {
		printf("Hash type not strong enough (%d < %d)\n",
		       req->hashtype, min);
		return 0;
	}
	switch(req->hashtype) {
	case HASH_NONE:
		return 1;
	case HASH_SHA1:
	case HASH_SHA256:
	case HASH_SHA512:
		return sha_verify(req, key, key_len);
	default:
		break;
	}
	return 0;
}


int
sha_challenge(int fd, fence_auth_type_t auth, void *key,
	      size_t key_len, int timeout)
{
	fd_set rfds;
	struct timeval tv;
	unsigned char hash[MAX_HASH_LENGTH];
	unsigned char challenge[MAX_HASH_LENGTH];
	unsigned char response[MAX_HASH_LENGTH];
	int devrand;
	int ret;
	HASHContext *h;
	HASH_HashType ht;
	unsigned int rlen;

	devrand = open("/dev/urandom", O_RDONLY);
	if (read(devrand, challenge, sizeof(challenge)) < 0) {
		perror("read /dev/urandom");
		return 0;
	}
	close(devrand);

	if (write(fd, challenge, sizeof(challenge)) < 0) {
		perror("write");
		return 0;
	}

	switch(auth) {
		case HASH_SHA1:
			ht = HASH_AlgSHA1;
			break;
		case HASH_SHA256:
			ht = HASH_AlgSHA256;
			break;
		case HASH_SHA512:
			ht = HASH_AlgSHA512;
			break;
		default:
			return 0;
	}

	memset(hash, 0, sizeof(hash));
	h = HASH_Create(ht);
	if (!h)
		return 0;

	HASH_Begin(h);
	HASH_Update(h, key, key_len);
	HASH_Update(h, challenge, sizeof(challenge));
	HASH_End(h, hash, &rlen, sizeof(hash));
	HASH_Destroy(h);

	memset(response, 0, sizeof(response));

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
		perror("select");
		return 0;
	}

	if (read(fd, response, sizeof(response)) < sizeof(response)) {
		perror("read");
		return 0;
	}

	ret = !memcmp(response, hash, sizeof(response));
	if (!ret) {
		printf("Hash mismatch:\nC = ");
		print_hash(challenge, sizeof(challenge));
		printf("\nH = ");
		print_hash(hash, sizeof(hash));
		printf("\nR = ");
		print_hash(response, sizeof(response));
		printf("\n");
	}

	return ret;
}


int
sha_response(int fd, fence_auth_type_t auth, void *key,
	     size_t key_len, int timeout)
{
	fd_set rfds;
	struct timeval tv;
	unsigned char challenge[MAX_HASH_LENGTH];
	unsigned char hash[MAX_HASH_LENGTH];
	HASHContext *h;
	HASH_HashType ht;
	unsigned int rlen;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
		perror("select");
		return 0;
	}

	if (read(fd, challenge, sizeof(challenge)) < 0) {
		perror("read");
		return 0;
	}

	switch(auth) {
		case AUTH_SHA1:
			ht = HASH_AlgSHA1;
			break;
		case AUTH_SHA256:
			ht = HASH_AlgSHA256;
			break;
		case AUTH_SHA512:
			ht = HASH_AlgSHA512;
			break;
		default:
			dbg_printf(3, "%s: no-op (AUTH_NONE)\n", __FUNCTION__);
			return 0;
	}

	memset(hash, 0, sizeof(hash));
	h = HASH_Create(ht); /* */
	if (!h)
		return 0;

	HASH_Begin(h);
	HASH_Update(h, key, key_len);
	HASH_Update(h, challenge, sizeof(challenge));
	HASH_End(h, hash, &rlen, sizeof(hash));
	HASH_Destroy(h);

	if (write(fd, hash, sizeof(hash)) < sizeof(hash)) {
		perror("read");
		return 0;
	}

	return 1;
}


int
tcp_challenge(int fd, fence_auth_type_t auth, void *key, size_t key_len,
	      int timeout)
{
	switch(auth) {
	case AUTH_NONE:
		dbg_printf(3, "%s: no-op (AUTH_NONE)\n", __FUNCTION__);
		return 1;
	case AUTH_SHA1:
	case AUTH_SHA256:
	case AUTH_SHA512:
		return sha_challenge(fd, auth, key, key_len, timeout);
	default:
		break;
	}
	return -1;
}


int
tcp_response(int fd, fence_auth_type_t auth, void *key, size_t key_len,
	     int timeout)
{
	switch(auth) {
	case AUTH_NONE:
		dbg_printf(3, "%s: no-op (AUTH_NONE)\n", __FUNCTION__);
		return 1;
	case AUTH_SHA1:
	case AUTH_SHA256:
	case AUTH_SHA512:
		return sha_response(fd, auth, key, key_len, timeout);
	default:
		break;
	}
	return -1;
}


int
read_key_file(char *file, char *key, size_t max_len)
{
	int fd;
	int nread, remain = max_len;
	char *p;

	dbg_printf(3, "Reading in key file %s into %p (%d max size)\n",
		file, key, (int)max_len);
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		dbg_printf(2, "Error opening key file: %s\n", strerror(errno));
		return -1;
	}

	memset(key, 0, max_len);
	p = key;
	remain = max_len;

	while (remain) {
		nread = read(fd, p, remain);
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			dbg_printf(2, "Error from read: %s\n", strerror(errno));
			close(fd);
			return -1;
		}

		if (nread == 0) {
			dbg_printf(3, "Stopped reading @ %d bytes",
				(int)max_len-remain);
			break;
		}
		
		p += nread;
		remain -= nread;
	}

	close(fd);	
	dbg_printf(3, "Actual key length = %d bytes", (int)max_len-remain);
	
	return (int)(max_len - remain);
}
