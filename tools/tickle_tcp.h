/**
 * "Tickle-ACK" TCP connection failover support utility
 *
 * Author: Robert Altnoeder
 * Derived from prior work authored by Jiaju Zhang, Andrew Tridgell, Ronnie Sahlberg
 * and the Samba project.
 *
 * This file is part of tickle_tcp.
 *
 * tickle_tcp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tickle_tcp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tickle_tcp.  If not, see <https://www.gnu.org/licenses/>
 */
#ifndef TICKLE_TCP_H
#define TICKLE_TCP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

typedef struct
{
    int         error_nr;
    const char  *error_msg;
}
error_info;

typedef struct
{
    // Version, Length:
    // Version:     4 bits  (mask 0xF0)
    // Length:      4 bits  (mask 0x0F)
    uint8_t         vsn_length;
    // Type of service: DSCP, ECN
    // DSCP:        6 bits  (mask 0xFC)
    // ECN:         2 bits  (mask 0x03)
    uint8_t         tos;
    uint16_t        length;
    uint16_t        id;
    // Flags, fragment offset:
    // Flags:       3 bits  (mask 0xE000)
    // Frg offset:  13 bits (mask 0x1FFF)
    uint16_t        flags_frg_off;
    uint8_t         ttl;
    uint8_t         protocol;
    uint16_t        chksum;
    uint8_t         src_address[4];
    uint8_t         dst_address[4];
}
ipv4_header_no_opt;

typedef struct
{
    // Version, Traffic class, Flow label:
    // Version:         4 bits  (mask 0xF0000000)
    // Traffic class:   8 bits  (mask 0x0FF00000)
    // Flow label:      20 bits (mask 0x000FFFFF)
    uint32_t    vsn_cls_flowlbl;
    uint16_t    length;
    uint8_t     next_header;
    uint8_t     hop_limit;
    uint8_t     src_address[16];
    uint8_t     dst_address[16];
}
ipv6_header;

typedef struct
{
    uint16_t    src_port;
    uint16_t    dst_port;
    uint32_t    seq_nr;
    uint32_t    ack_nr;
    // Combined (from high order bits to low order bits):
    // data offset (4 bits), reserved (3 bits), ns (1 bit)
    uint8_t     cmb_data_off_ns;
    uint8_t     tcp_flags;
    uint16_t    window_size;
    uint16_t    checksum;
    uint16_t    urgent_ptr;
}
custom_tcp_header;

typedef struct
{
    ipv4_header_no_opt  ip_header;
    custom_tcp_header   tcp_header;
}
ipv4_packet;

typedef struct
{
    ipv6_header         ip_header;
    custom_tcp_header   tcp_header;
}
ipv6_packet;

struct address_text_buffers
{
    char    *src_ip;
    char    *src_netif;
    char    *src_port;
    char    *dst_ip;
    char    *dst_port;
};

struct endpoints
{
    struct sockaddr_in  *ipv4_src_address;
    struct sockaddr_in  *ipv4_dst_address;
    struct sockaddr_in6 *ipv6_src_address;
    struct sockaddr_in6 *ipv6_dst_address;
};

const int ERR_GENERIC       = 1;
const int ERR_OUT_OF_MEMORY = 2;
const int ERR_IO            = 3;

const size_t BUFFER_SIZE = 140;

const uint16_t DEFAULT_PACKET_COUNT = 1;

const uint8_t TCP_FIN = 0x01;
const uint8_t TCP_SYN = 0x02;
const uint8_t TCP_RST = 0x04;
const uint8_t TCP_PSH = 0x08;
const uint8_t TCP_ACK = 0x10;
const uint8_t TCP_URG = 0x20;
const uint8_t TCP_ECE = 0x40;
const uint8_t TCP_CWR = 0x80;

const char *const ERRMSG_OUT_OF_MEMORY          = "Out of memory";
const char *const ERRMSG_INVALID_NR             = "Unparsable number";
const char *const ERRMSG_INVALID_PORT_NR        = "Invalid port number";
const char *const ERRMSG_UNPARSABLE_ENDPOINT    = "Unparsable IP address:port string";
const char *const ERRMSG_FCNTL_FAIL             = "I/O error: Changing the mode of a file descriptor failed";
const char *const ERRMSG_UNUSABLE_LINKLOCAL     =
    "Unusable IPv6 link-local address: Missing network interface specifier (%netif suffix)";
const char *const ERRMSG_LINKLOCAL_NO_NETIF     =
    "Nonexistent IPv6 link-local network interface";

const char *const OPT_PACKET_COUNT  = "-n";
const char *const OPT_HELP          = "-h";
const char *const LONG_OPT_HELP     = "--help";

int run(uint16_t packet_count);
void process_line(
    char                        *line_buffer,
    struct address_text_buffers *buffers,
    struct endpoints            *endpoint_data,
    uint16_t                    packet_count,
    error_info                  *error
);
void display_help(void);
void display_error(error_info *error);
void display_error_nr_msg(int error_nr);
void clear_error(error_info *error);
bool parse_arguments(int argc, char *argv[], uint16_t *packet_count);
bool parse_endpoints(
    char                                        *line_buffer,
    size_t                                      line_length,
    const struct address_text_buffers           *buffers,
    struct endpoints                            *endpoint_data,
    bool                                        *is_ipv6,
    error_info                                  *error
);
bool split_address_and_port(
    const char  *address_and_port,
    size_t      address_and_port_length,
    char        *address,
    char        *port,
    error_info  *error
);
bool split_address_and_netif(
    char        *address,
    size_t      address_length,
    char        *netif,
    error_info  *error
);
void trim_newline(char *buffer, size_t length);
void trim_lead(char *buffer, size_t length);
void trim_trail(char *buffer, size_t length);
size_t find_whitespace(const char *buffer, size_t length);
size_t find_last_char(const char *buffer, size_t length, char value);
bool parse_uint16(const char *input, size_t input_length, uint16_t *value, error_info *error);
bool parse_port_number(
    const char *const   input,
    const size_t        input_length,
    uint16_t *const     port_number,
    error_info *const   error
);
bool parse_ipv4(
    const char          *address_input,
    size_t              address_input_length,
    const char          *port_input,
    size_t              port_input_length,
    struct sockaddr_in  *address,
    error_info          *error
);
bool parse_ipv6(
    const char          *address_input,
    size_t              address_input_length,
    const char          *netif_input,
    size_t              netif_input_length,
    const char          *port_input,
    size_t              port_input_length,
    struct sockaddr_in6 *address,
    error_info          *error
);
bool send_ipv4_packet(
    const struct sockaddr_in *src_address,
    const struct sockaddr_in *dst_address,
    uint16_t packet_count,
    error_info *error
);
bool send_ipv6_packet(
    const struct sockaddr_in6 *src_address,
    const struct sockaddr_in6 *dst_address,
    uint16_t packet_count,
    error_info *error
);
bool send_packet(
    int                             socket_domain,
    const struct sockaddr           *dst_address,
    size_t                          dst_address_size,
    uint16_t                        dst_port,
    const void                      *packet_data,
    size_t                          packet_data_size,
    uint16_t                        packet_count,
    error_info                      *error
);
bool check_inet_pton_rc(int rc, int error_nr, error_info *error);
bool set_fcntl_flags(int file_dsc, int flags, error_info *error);
void close_file_dsc(int *file_dsc);
uint16_t ipv4_tcp_checksum(
    const unsigned char         *data,
    size_t                      length,
    const ipv4_header_no_opt    *header
);
uint16_t ipv6_tcp_checksum(
    const unsigned char *data,
    size_t              length,
    const ipv6_header   *header
);
uint32_t checksum(const unsigned char *data, size_t length);

#endif /* TICKLE_TCP_H */
