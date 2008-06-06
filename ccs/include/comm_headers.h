#ifndef __COMM_HEADERS_DOT_H__
#define __COMM_HEADERS_DOT_H__

#include <byteswap.h>
#include <endian.h>

/* Types of requests */
#define COMM_CONNECT    1
#define COMM_DISCONNECT 2
#define COMM_GET        3
#define COMM_GET_LIST   4
#define COMM_SET        5
#define COMM_GET_STATE  6
#define COMM_SET_STATE  7
#define COMM_BROADCAST  8
#define COMM_UPDATE     9

/* Request flags */
#define COMM_CONNECT_FORCE          1
#define COMM_CONNECT_BLOCKING       2
#define COMM_SET_STATE_RESET_QUERY  4
#define COMM_BROADCAST_FROM_QUORATE 8
#define COMM_UPDATE_NOTICE	    16
#define COMM_UPDATE_NOTICE_ACK	    32
#define COMM_UPDATE_COMMIT	    64
#define COMM_UPDATE_COMMIT_ACK	    128

typedef struct comm_header_s {
  int comm_type;
  int comm_flags;  /* flags that tune a particular type of operation */
  int comm_desc;
  int comm_error;
  int comm_payload_size;
} comm_header_t;

#define COMM_LOCAL_SOCKET "/var/run/cluster/ccsd.sock"

static inline void swab_header(comm_header_t *head) {
#if __BYTE_ORDER == __BIG_ENDIAN
  head->comm_type = bswap_32(head->comm_type);
  head->comm_flags = bswap_32(head->comm_flags);
  head->comm_desc = bswap_32(head->comm_desc);
  head->comm_error = bswap_32(head->comm_error);
  head->comm_payload_size = bswap_32(head->comm_payload_size);
#endif
}

#endif /* __COMM_HEADERS_DOT_H__ */
