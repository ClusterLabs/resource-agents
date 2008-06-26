#ifndef _XVM_H
#define _XVM_H

#include <stdint.h>
#include <sechash.h>
#include <netinet/in.h>

#define XVM_VERSION "0.9.3"

#define MAX_DOMAINNAME_LENGTH 64 /* XXX MAXHOSTNAMELEN */
#define MAX_ADDR_LEN		sizeof(struct sockaddr_in6)
#define DOMAIN0NAME "Domain-0"
#define DOMAIN0UUID "00000000-0000-0000-0000-000000000000"

typedef enum {
	HASH_NONE = 0x0,	/* No packet signing */
	HASH_SHA1 = 0x1,	/* SHA1 signing */
     	HASH_SHA256 = 0x2,      /* SHA256 signing */
     	HASH_SHA512 = 0x3       /* SHA512 signing */
} fence_hash_t;

#define DEFAULT_HASH HASH_SHA256

typedef enum {
	AUTH_NONE = 0x0,	/* Plain TCP */
	AUTH_SHA1 = 0x1,	/* Challenge-response (SHA1) */
  	AUTH_SHA256 = 0x2,      /* Challenge-response (SHA256) */
	AUTH_SHA512 = 0x3,      /* Challenge-response (SHA512) */
     /* AUTH_SSL_X509 = 0x10        SSL X509 certificates */
} fence_auth_type_t;

#define DEFAULT_AUTH AUTH_SHA256

typedef enum {
	FENCE_NULL   = 0x0,	
	FENCE_OFF    = 0x1,	/* Turn the VM off */
	FENCE_REBOOT = 0x2	/* Hit the reset button */
     /* FENCE_ON = 0x3            Turn the VM on */
} fence_cmd_t;

#define DEFAULT_TTL 4

#ifndef DEFAULT_HYPERVISOR_URI
#define DEFAULT_HYPERVISOR_URI "qemu:///system"
#endif

#define MAX_HASH_LENGTH SHA512_LENGTH
#define MAX_KEY_LEN 4096

typedef struct __attribute__ ((packed)) _fence_req {
	uint8_t  request;		/* Fence request */
	uint8_t  hashtype;		/* Hash type used */
	uint8_t  addrlen;		/* Length of address */
	uint8_t  flags;			/* Special flags */
#define RF_UUID 0x1			   /* Flag specifying UUID */
	uint8_t  domain[MAX_DOMAINNAME_LENGTH]; /* Domain to fence*/
	uint8_t  address[MAX_ADDR_LEN]; /* We're this IP */
	uint16_t port;			/* Port we bound to */
	uint8_t  random[10];		/* Random Data */
	uint32_t family;		/* Address family */
	uint8_t  hash[MAX_HASH_LENGTH];	/* Binary hash */
} fence_req_t;


#endif
