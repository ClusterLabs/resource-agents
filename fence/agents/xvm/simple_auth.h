#ifndef _XVM_SIMPLE_AUTH_H
#define _XVM_SIMPLE_AUTH_H

#include <sys/types.h>

/* 2-way challenge/response simple auth */
#define DEFAULT_KEY_FILE DEFAULT_CONFIG_DIR "/fence_xvm.key"

int read_key_file(char *, char *, size_t);
int tcp_challenge(int, fence_auth_type_t, void *, size_t, int);
int tcp_response(int, fence_auth_type_t, void *, size_t, int);
int sign_request(fence_req_t *, void *, size_t);
int verify_request(fence_req_t *, fence_hash_t, void *, size_t);

/* SSL certificate-based authentication TBD */

#endif
