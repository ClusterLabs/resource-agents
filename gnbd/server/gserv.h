#ifndef __gserv_h__
#define __gserv_h__

#include "device.h"

#define PROTOCOL_VERSION 2

struct login_req_s {
  uint64_t timestamp;
  uint16_t version;
  uint8_t pad[6];
  char devname[32];
};
typedef struct login_req_s login_req_t;

#define BE_LOGIN_REQ_TO_CPU(x)\
(x)->timestamp = be64_to_cpu((x)->timestamp);\
(x)->version = be16_to_cpu((x)->version);

#define CPU_TO_BE_LOGIN_REQ(x)\
(x)->timestamp = cpu_to_be64((x)->timestamp);\
(x)->version = cpu_to_be16((x)->version);

struct login_reply_s {
  uint64_t sectors;
  uint16_t version;
  uint8_t err;
  uint8_t pad[5];
};
typedef struct login_reply_s login_reply_t;

#define BE_LOGIN_REPLY_TO_CPU(x)\
(x)->sectors = be64_to_cpu((x)->sectors);\
(x)->version = be16_to_cpu((x)->version);

#define CPU_TO_BE_LOGIN_REPLY(x)\
(x)->sectors = cpu_to_be64((x)->sectors);\
(x)->version = cpu_to_be16((x)->version);

extern int is_gserv;

void gserv(int sock, char *node, uint64_t sectors, unsigned int flags,
           char *name, int devfd);
int add_gserv_info(int sock, char *node, dev_info_t *dev, pid_t pid);
void fork_gserv(int sock, char *node, dev_info_t *dev, int devfd);
int gserv_login(int sock, char *node, login_req_t *login_req, dev_info_t **dev,
                int *devfd);
int kill_gserv(char *node, dev_info_t * dev, int sock);
int find_gserv_info(char *node, dev_info_t * dev);
void sig_chld(int sig);
void kill_all_gserv(void);
int get_gserv_info(char **buffer, uint32_t *size_size);
void validate_gservs(void);

#endif /* __gserv_h__ */
