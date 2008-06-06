#ifndef __extern_req_h__
#define __extern_req_h__

struct device_req_s {
  char name[32];
};
typedef struct device_req_s device_req_t;

struct node_req_s {
  char node_name[65];
};
typedef struct node_req_s node_req_t;

struct import_info_s {
  uint32_t timeout;
  uint16_t flags;
  char name[32];
};
typedef struct import_info_s import_info_t;

#define NODENAME_SIZE 65

#define EXTERN_NAMES_REQ        1
#define EXTERN_FENCE_REQ        2
#define EXTERN_UNFENCE_REQ      3
/* FIXME -- should this be external */
#define EXTERN_LIST_BANNED_REQ  4
/* FIXME -- should this be only external */
#define EXTERN_KILL_GSERV_REQ   5
#define EXTERN_LOGIN_REQ        6
#define EXTERN_NODENAME_REQ     7
#define EXTERN_UID_REQ          8

#define EXTERN_SUCCESS_REPLY    0
/* FIXME -- is this used */
#define REPLY_ERR(x) (-((int)(x)))

extern char nodename[NODENAME_SIZE];
int start_extern_socket(short unsigned int port);
int accept_extern_connection(int listening_sock);
int check_extern_data_len(uint32_t req, int size);
void handle_extern_request(int sock, uint32_t cmd, void *buf);

#endif /* __extern_req_h__ */
