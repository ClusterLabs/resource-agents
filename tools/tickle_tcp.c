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
#include "tickle_tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/tcp.h>

const char *executable_name = "tickle_tcp";

// Invokes the arguments parser, then the application's main I/O loop
int main(int argc, char *argv[])
{
    int rc = ERR_GENERIC;
    if (argc >= 1)
    {
        executable_name = argv[0];
    }

    uint16_t packet_count = DEFAULT_PACKET_COUNT;
    if (parse_arguments(argc, argv, &packet_count))
    {
        rc = run(packet_count);
    }
    return rc;
}

// Reads request line from stdin and hands them over to request line processing
int run(const uint16_t packet_count)
{
    char *const line_buffer = malloc(BUFFER_SIZE);

    struct address_text_buffers buffers;
    buffers.src_ip              = malloc(BUFFER_SIZE);
    buffers.src_port            = malloc(BUFFER_SIZE);
    buffers.src_netif           = malloc(BUFFER_SIZE);
    buffers.dst_ip              = malloc(BUFFER_SIZE);
    buffers.dst_port            = malloc(BUFFER_SIZE);

    struct endpoints endpoint_data;
    endpoint_data.ipv4_src_address = malloc(sizeof (*(endpoint_data.ipv4_src_address)));
    endpoint_data.ipv4_dst_address = malloc(sizeof (*(endpoint_data.ipv4_dst_address)));
    endpoint_data.ipv6_src_address = malloc(sizeof (*(endpoint_data.ipv6_src_address)));
    endpoint_data.ipv6_dst_address = malloc(sizeof (*(endpoint_data.ipv6_dst_address)));

    ipv4_packet *const ipv4_packet_data = malloc(sizeof (*ipv4_packet_data));
    ipv6_packet *const ipv6_packet_data = malloc(sizeof (*ipv6_packet_data));

    bool have_allocations = line_buffer != NULL;
    have_allocations &= buffers.src_ip != NULL && buffers.src_port != NULL;
    have_allocations &= buffers.dst_ip != NULL && buffers.dst_port != NULL;
    have_allocations &= buffers.src_netif != NULL;
    have_allocations &= endpoint_data.ipv4_src_address != NULL && endpoint_data.ipv4_dst_address != NULL;
    have_allocations &= endpoint_data.ipv6_src_address != NULL && endpoint_data.ipv6_dst_address != NULL;
    have_allocations &= ipv4_packet_data != NULL && ipv6_packet_data != NULL;

    error_info error = {0, NULL};

    int rc = EXIT_SUCCESS;
    if (have_allocations)
    {
        char *fgets_rc = NULL;
        do
        {
            fgets_rc = fgets(line_buffer, BUFFER_SIZE, stdin);
            if (fgets_rc != NULL)
            {
                if (strlen(line_buffer) + 1 >= BUFFER_SIZE)
                {
                    error.error_msg = "Error: I/O error while reading input from stdin, cannot continue";
                    error.error_nr = EIO;
                    rc = ERR_IO;
                    break;
                }

                process_line(line_buffer, &buffers, &endpoint_data, packet_count, &error);
            }
            else
            if (feof(stdin) == 0)
            {
                error.error_msg = "Error: I/O error while reading input from stdin, cannot continue";
                error.error_nr = EIO;
                break;
            }
        }
        while (fgets_rc != NULL);
    }
    else
    {
        error.error_msg = ERRMSG_OUT_OF_MEMORY;
        error.error_nr = ENOMEM;
    }

    if (error.error_msg != NULL)
    {
        display_error(&error);
        switch (error.error_nr)
        {
            case ENOMEM:
            {
                rc = ERR_OUT_OF_MEMORY;
                break;
            }
            case EIO:
            {
                rc = ERR_IO;
                break;
            }
            default:
            {
                rc = ERR_GENERIC;
                break;
            }
        }
    }

    free(buffers.src_ip);
    free(buffers.src_port);
    free(buffers.src_netif);
    free(buffers.dst_ip);
    free(buffers.dst_port);

    free(endpoint_data.ipv4_src_address);
    free(endpoint_data.ipv4_dst_address);
    free(endpoint_data.ipv6_src_address);
    free(endpoint_data.ipv6_dst_address);

    free(ipv4_packet_data);
    free(ipv6_packet_data);

    free(line_buffer);

    return rc;
}

// Processes an input line
void process_line(
    char *const                         line_buffer,
    struct address_text_buffers *const  buffers,
    struct endpoints *const             endpoint_data,
    const uint16_t                      packet_count,
    error_info *const                   error
)
{
    // Trim trailing newline character
    trim_newline(line_buffer, strlen(line_buffer));

    // Trim leading and trailing space or tab characters
    trim_lead(line_buffer, strlen(line_buffer));
    trim_trail(line_buffer, strlen(line_buffer));

    // WARNING: line_buffer_length may no longer be valid after the parse_endpoints function returns
    const size_t line_buffer_length = strlen(line_buffer);
    if (line_buffer_length >= 1)
    {
        bool is_ipv6 = false;
        // The parse_endpoints function modifies the line_buffer
        const bool have_endpoints = parse_endpoints(
            line_buffer, line_buffer_length, buffers, endpoint_data, &is_ipv6, error
        );
        if (have_endpoints)
        {
            bool send_rc = false;
            if (is_ipv6)
            {
                send_rc = send_ipv6_packet(
                    endpoint_data->ipv6_src_address,
                    endpoint_data->ipv6_dst_address,
                    packet_count,
                    error
                );
            }
            else
            {
                send_rc = send_ipv4_packet(
                    endpoint_data->ipv4_src_address,
                    endpoint_data->ipv4_dst_address,
                    packet_count,
                    error
                );
            }

            if (!send_rc)
            {
                display_error(error);
                clear_error(error);
            }
        }
        else
        {
            display_error(error);
            clear_error(error);
        }
    }
}

// Displays information about the application's command line arguments
void display_help(void)
{
    fputs("Syntax: tickle_tcp [ -n <packet_count ]\n", stdout);
}


// Displays an error message and/or the system's description of a system error code
void display_error(error_info *const error)
{
    if (error->error_msg != NULL)
    {
        fprintf(stderr, "%s: Error: %s\n", executable_name, error->error_msg);
    }
    display_error_nr_msg(error->error_nr);
}

// Displays the system's description for an error code received from system functions
void display_error_nr_msg(const int error_nr)
{
    if (error_nr != 0)
    {
        const char* const errno_msg = strerror(error_nr);
        if (errno_msg != NULL)
        {
            fprintf(stderr, "    Error description: %s\n", errno_msg);
            fprintf(stderr, "    Error code: %d\n", error_nr);
        }
    }
}

// Clears error information in the error_info data structure
void clear_error(error_info *const error)
{
    error->error_nr = 0;
    error->error_msg = NULL;
}

// Parses the command line arguments for this application
//
// @returns true if successful, otherwise false
bool parse_arguments(const int argc, char *argv[], uint16_t *const packet_count)
{
    bool rc = true;
    if (argv != NULL)
    {
        const char *crt_key = NULL;

        for (int idx = 1; idx < argc; ++idx)
        {
            if (crt_key == NULL)
            {
                if (strcmp(OPT_PACKET_COUNT, argv[idx]) == 0)
                {
                    crt_key = OPT_PACKET_COUNT;
                }
                else
                if (strcmp(OPT_HELP, argv[idx]) == 0 || strcmp(LONG_OPT_HELP, argv[idx]) == 0)
                {
                    display_help();
                    rc = false;
                    break;
                }
                else
                {
                    fprintf(stderr, "Invalid command line argument '%s'\n", argv[idx]);
                    display_help();
                    rc = false;
                    break;
                }
            }
            else
            {
                if (crt_key == OPT_PACKET_COUNT)
                {
                    error_info error;
                    // bool parse_uint16(const char *input, size_t input_length, uint16_t *value, error_info *error);
                    if (!parse_uint16(argv[idx], strlen(argv[idx]), packet_count, &error))
                    {
                        rc = false;
                        display_error(&error);
                        break;
                    }
                }
                crt_key = NULL;
            }
        }
    }
    return rc;
}

// Parses source/destination endpoints (IP address & port) information
//
// @returns true if successful, otherwise false
bool parse_endpoints(
    char *const                                 line_buffer,
    const size_t                                line_length,
    const struct address_text_buffers *const    buffers,
    struct endpoints *const                     endpoint_data,
    bool *const                                 is_ipv6,
    error_info *const                           error
)
{
    bool have_endpoints = false;
    const size_t split_idx = find_whitespace(line_buffer, line_length);
    if (split_idx < line_length)
    {
        // Split source address & port
        const bool have_src_str = split_address_and_port(
            line_buffer,
            split_idx,
            buffers->src_ip,
            buffers->src_port,
            error
        );

        if (have_src_str)
        {
            *is_ipv6 = strchr(buffers->src_ip, ':') != NULL;

            // Split destination address & port
            const size_t dst_str_offset = split_idx + 1;
            // Trim leading space & tabs in the destination address
            {
                const size_t dst_str_length = line_length - dst_str_offset;
                trim_lead(&line_buffer[dst_str_offset], dst_str_length);
            }
            const bool have_dst_str = split_address_and_port(
                &line_buffer[dst_str_offset],
                strlen(&line_buffer[dst_str_offset]),
                buffers->dst_ip,
                buffers->dst_port,
                error
            );

            if (have_dst_str)
            {
                if (*is_ipv6)
                {
                    const bool have_netif = split_address_and_netif(
                        buffers->src_ip,
                        strlen(buffers->src_ip),
                        buffers->src_netif,
                        error
                    );
                    if (have_netif)
                    {
                        const bool have_src_address = parse_ipv6(
                            buffers->src_ip,
                            strlen(buffers->src_ip),
                            buffers->src_netif,
                            strlen(buffers->src_netif),
                            buffers->src_port,
                            strlen(buffers->src_port),
                            endpoint_data->ipv6_src_address,
                            error
                        );

                        if (have_src_address)
                        {
                            have_endpoints = parse_ipv6(
                                buffers->dst_ip,
                                strlen(buffers->dst_ip),
                                NULL,
                                0,
                                buffers->dst_port,
                                strlen(buffers->dst_port),
                                endpoint_data->ipv6_dst_address,
                                error
                            );
                        }
                    }
                }
                else
                {
                    const bool have_src_address = parse_ipv4(
                        buffers->src_ip,
                        strlen(buffers->src_ip),
                        buffers->src_port,
                        strlen(buffers->src_port),
                        endpoint_data->ipv4_src_address,
                        error
                    );

                    if (have_src_address)
                    {
                        have_endpoints = parse_ipv4(
                            buffers->dst_ip,
                            strlen(buffers->dst_ip),
                            buffers->dst_port,
                            strlen(buffers->dst_port),
                            endpoint_data->ipv4_dst_address,
                            error
                        );
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Warning: Ignored invalid input line: \"%s\"\n", line_buffer);
    }
    return have_endpoints;
}

// Splits the referenced address_and_port string into a separate address string and port string
//
// The port is expected to be the part of the string following the last occurrence of the separator character ":".
//
// @returns true if successful, otherwise false
bool split_address_and_port(
    const char *const   address_and_port,
    const size_t        address_and_port_length,
    char *const         address,
    char *const         port,
    error_info *const   error
)
{
    bool have_split = false;

    const size_t split_idx = find_last_char(address_and_port, address_and_port_length, ':');
    if (split_idx < address_and_port_length)
    {
        const size_t address_length = split_idx;
        const size_t port_offset = split_idx + 1;
        const size_t port_length = address_and_port_length - port_offset;

        strncpy(address, address_and_port, address_length);
        address[address_length] = '\0';

        strncpy(port, &address_and_port[port_offset], port_length);
        port[port_length] = '\0';

        have_split = true;
    }
    else
    {
        error->error_msg = ERRMSG_UNPARSABLE_ENDPOINT;
        error->error_nr = 0;
    }

    return have_split;
}

// Splits the referenced character array into an address and network interface part
//
// This applies to the string representation of link-local IPv6 addresses.
// The format is: address%interface.
// Input is expected in the address character array. If a network interface suffix is present, the separator character
// "%" is replaced by a null character in the address character array, and the interface name is copied into
// the netif character array.
//
// If a network interface suffix is present and the argument netif is a NULL pointer, the address is treated as
// invalid and the function returns with an error.
//
// @returns true if successful, otherwise false
bool split_address_and_netif(
    char *const         address,
    const size_t        address_length,
    char *const         netif,
    error_info *const   error
)
{
    bool have_split = false;
    size_t split_idx = find_last_char(address, strlen(address), '%');
    if (split_idx < address_length)
    {
        if (netif != NULL)
        {
            const size_t netif_offset = split_idx + 1;
            const size_t netif_length = address_length - netif_offset;

            strncpy(netif, &address[netif_offset], netif_length);
            netif[netif_length] = '\0';
            address[split_idx] = '\0';
            have_split = true;
        }
        else
        {
            // Link-local scope id specified, but not valid for this address
            // (e.g., a destination address)
            error->error_msg = ERRMSG_UNPARSABLE_ENDPOINT;
            error->error_nr = 0;
        }
    }
    else
    {
        if (netif != NULL)
        {
            netif[0] = '\0';
        }
        // Nothing to split, indicate success
        have_split = true;
    }

    return have_split;
}

// Removes leading space and tab characters from the referenced character array
void trim_lead(char *const buffer, const size_t length)
{
    if (length >= 1)
    {
        size_t src_idx = 0;
        while (src_idx < length && (buffer[src_idx] == ' ' || buffer[src_idx] == '\t'))
        {
            ++src_idx;
        }
        if (src_idx > 0)
        {
            size_t dst_idx = 0;
            while (src_idx < length)
            {
                buffer[dst_idx] = buffer[src_idx];
                ++src_idx;
                ++dst_idx;
            }
            buffer[dst_idx] = '\0';
        }
    }
}

// Truncates a trailing newline character in the referenced character array
void trim_newline(char *const buffer, const size_t length)
{
    if (length >= 1)
    {
        const size_t last_idx = length - 1;
        if (buffer[last_idx] == '\n')
        {
            buffer[last_idx] = '\0';
        }
    }
}

// Truncates trailing space and tab characters in the referenced character array
void trim_trail(char *const buffer, const size_t length)
{
    if (length >= 1)
    {
        size_t idx = length;
        while (idx > 0)
        {
            if (buffer[idx - 1] != ' ' && buffer[idx - 1] != '\t')
            {
                break;
            }
            --idx;
        }
        buffer[idx] = '\0';
    }
}

// Finds the index of the first whitespace (space or tab) character in the specified character array
size_t find_whitespace(const char *const buffer, const size_t length)
{
    size_t idx = 0;
    while (idx < length && buffer[idx] != ' ' && buffer[idx] != '\t')
    {
        ++idx;
    }
    return idx < length ? idx : ~((size_t) 0);
}

// Finds the index of the last occurrence of the specified character in the specified character array
size_t find_last_char(const char *const buffer, const size_t length, const char letter)
{
    size_t split_idx = ~((size_t) 0);
    if (length >= 1)
    {
        size_t idx = length;
        while (idx > 0)
        {
            --idx;
            if (buffer[idx] == letter)
            {
                split_idx = idx;
                break;
            }
        }
    }
    return split_idx;
}

// Parses the string representation of a port number
//
// @returns true if successful, otherwise false
bool parse_uint16(const char *const input, const size_t input_length, uint16_t *const value, error_info *const error)
{
    bool have_result = false;

    // Not using strtol/strtoll/etc. due to the various shortcomings of those functions,
    // such as allowing whitespace or allowing to parse negative numbers
    // as unsigned values.
    //
    // Derived from the integerparse module of the
    // C++ DSA library at https://github.com/raltnoeder/cppdsaext
    uint16_t result = 0;
    if (input_length >= 1)
    {
        const uint16_t max_value_base = UINT16_MAX / 10;
        size_t index = 0;
        while (index < input_length)
        {
            if (result > max_value_base)
            {
                break;
            }
            result *= 10;

            const unsigned char digit_char = (const unsigned char) input[index];
            if (!(digit_char >= '0' && digit_char <= '9'))
            {
                break;
            }
            const uint16_t digit_value = digit_char - (const unsigned char) '0';
            if (digit_value > UINT16_MAX - result)
            {
                break;
            }
            result += digit_value;
            ++index;
        }
        have_result = index == input_length;
    }

    if (have_result)
    {
        *value = result;
    }
    else
    {
        error->error_msg = ERRMSG_INVALID_NR;
    }

    return have_result;
}

// Parses the string representation of unsigned decimal integer numbers with a width of at most 16 bits
//
// @returns true if successful, otherwise false
bool parse_port_number(
    const char *const   input,
    const size_t        input_length,
    uint16_t *const     port_number,
    error_info *const   error
)
{
    bool have_port_number = parse_uint16(input, input_length, port_number, error);
    if (!have_port_number)
    {
        error->error_msg = ERRMSG_INVALID_PORT_NR;
    }
    return have_port_number;
}

// Parses the string representatio of an IPv4 address and port combination and applies the result
// to a sockaddr_in data structure
//
// @returns true if successful, otherwise false
bool parse_ipv4(
    const char *const           address_input,
    const size_t                address_input_length,
    const char *const           port_input,
    const size_t                port_input_length,
    struct sockaddr_in *const   address,
    error_info *const           error
)
{
    uint16_t port_number = 0;
    bool have_address = parse_port_number(port_input, port_input_length, &port_number, error);
    if (have_address)
    {
        errno = 0;
        int rc = inet_pton(AF_INET, address_input, &address->sin_addr);
        have_address = check_inet_pton_rc(rc, errno, error);
        if (have_address)
        {
            address->sin_family = AF_INET;
            address->sin_port = htons(port_number);
        }
    }
    return have_address;
}

// Parses the string representation of an IPv6 address and port combination and applies the result
// to a sockaddr_in6 data structure
//
// @returns true if successful, otherwise false
bool parse_ipv6(
    const char *const           address_input,
    const size_t                address_input_length,
    const char *const           netif_input,
    const size_t                netif_input_length,
    const char *const           port_input,
    const size_t                port_input_length,
    struct sockaddr_in6 *const  address,
    error_info *const           error
)
{
    uint16_t port_number = 0;
    bool have_address = false;
    if (parse_port_number(port_input, port_input_length, &port_number, error))
    {
        int rc = inet_pton(AF_INET6, address_input, &address->sin6_addr);
        if (check_inet_pton_rc(rc, errno, error))
        {
            address->sin6_family = AF_INET6;
            address->sin6_port = htons(port_number);
            address->sin6_flowinfo = 0;
            address->sin6_scope_id = 0;

            if (IN6_IS_ADDR_LINKLOCAL(&(address->sin6_addr)) != 0)
            {
                if (netif_input != NULL)
                {
                    if (netif_input_length >= 1)
                    {
                        const unsigned int netif_index = if_nametoindex(netif_input);
                        if (netif_index != 0)
                        {
                            address->sin6_scope_id = (uint32_t) netif_index;
                            have_address = true;
                        }
                        else
                        {
                            error->error_msg = ERRMSG_LINKLOCAL_NO_NETIF;
                            error->error_nr = errno;
                        }
                    }
                    else
                    {
                        error->error_msg = ERRMSG_UNUSABLE_LINKLOCAL;
                        error->error_nr = 0;
                    }
                }
                else
                {
                    // no IPv6 zone ID is required (e.g. it's a destination address)
                    have_address = true;
                }
            }
            else
            {
                have_address = true;
            }
        }
    }
    return have_address;
}

// Creates an IPv4 packet and sends it to the specified destination by calling send_packet
//
// @returns true if successful, otherwise false
bool send_ipv4_packet(
    const struct sockaddr_in *const src_address,
    const struct sockaddr_in *const dst_address,
    const uint16_t packet_count,
    error_info *const error
)
{
    bool rc = false;

    ipv4_packet *const packet = malloc(sizeof (*packet));
    if (packet != NULL)
    {
        memset((void *) packet, 0, sizeof (*packet));

        ipv4_header_no_opt *const ip_header = &packet->ip_header;
        ip_header->vsn_length   = sizeof (ipv4_header_no_opt) / sizeof (uint32_t);
        ip_header->vsn_length   |= 0x40;
        ip_header->length       = htons(sizeof (ipv4_header_no_opt) + sizeof (custom_tcp_header));
        ip_header->ttl          = 255;
        ip_header->protocol     = IPPROTO_TCP;
        memcpy(ip_header->src_address, &src_address->sin_addr, sizeof (ip_header->src_address));
        memcpy(ip_header->dst_address, &dst_address->sin_addr, sizeof (ip_header->dst_address));
        ip_header->chksum       = 0;

        custom_tcp_header *const tcp_header = &packet->tcp_header;
        tcp_header->src_port        = src_address->sin_port;
        tcp_header->dst_port        = dst_address->sin_port;
        tcp_header->tcp_flags       = TCP_ACK;
        tcp_header->cmb_data_off_ns = (sizeof (*tcp_header) / 4) << 4;
        tcp_header->window_size     = htons(1234);
        tcp_header->checksum        = ipv4_tcp_checksum(
            (const unsigned char *) tcp_header,
            sizeof (*tcp_header),
            &packet->ip_header
        );

        rc = send_packet(
            AF_INET,
            (const struct sockaddr *) dst_address,
            sizeof (*dst_address),
            ntohs((uint16_t) dst_address->sin_port),
            (void *) packet,
            sizeof (*packet),
            packet_count,
            error
        );
        free(packet);
    }
    else
    {
        error->error_nr = ENOMEM;
        error->error_msg = ERRMSG_OUT_OF_MEMORY;
    }
    return rc;
}

// Creates an IPv6 packet and sends it to the specified destination by calling send_packet
//
// @returns true if successful, otherwise false
bool send_ipv6_packet(
    const struct sockaddr_in6 *const src_address,
    const struct sockaddr_in6 *const dst_address,
    const uint16_t packet_count,
    error_info *const error
)
{
    bool rc = false;

    ipv6_packet *const packet = malloc(sizeof (*packet));
    if (packet != NULL)
    {
        memset((void *) packet, 0, sizeof (*packet));

        ipv6_header *const ip_header = &packet->ip_header;
        ip_header->vsn_cls_flowlbl  = htons(0x6000);
        ip_header->hop_limit        = 64;
        ip_header->next_header      = IPPROTO_TCP;
        ip_header->length           = htons(20);
        memcpy(ip_header->src_address, &src_address->sin6_addr, sizeof (ip_header->src_address));
        memcpy(ip_header->dst_address, &dst_address->sin6_addr, sizeof (ip_header->dst_address));

        custom_tcp_header *const tcp_header = &packet->tcp_header;
        tcp_header->src_port        = src_address->sin6_port;
        tcp_header->dst_port        = dst_address->sin6_port;
        tcp_header->tcp_flags       = TCP_ACK;
        tcp_header->cmb_data_off_ns = (sizeof (*tcp_header) / 4) << 4;
        tcp_header->window_size     = htons(1234);
        tcp_header->checksum        = ipv6_tcp_checksum(
            (const unsigned char *) tcp_header,
            sizeof (*tcp_header),
            &packet->ip_header
        );

        // Required for sending an IPv6 packet without generating an EINVAL error.
        // This behavior seems to be undocumented.
        struct sockaddr_in6 *const send_dst_address = malloc(sizeof (*send_dst_address));
        if (send_dst_address != NULL)
        {
            *send_dst_address = *dst_address;
            send_dst_address->sin6_port = 0;

            rc = send_packet(
                AF_INET6,
                (const struct sockaddr *) send_dst_address,
                sizeof (*send_dst_address),
                ntohs((uint16_t) dst_address->sin6_port),
                (void *) packet,
                sizeof (*packet),
                packet_count,
                error
            );
            free(send_dst_address);
        }
        else
        {
            error->error_nr = ENOMEM;
            error->error_msg = ERRMSG_OUT_OF_MEMORY;
        }
        free(packet);
    }
    else
    {
        error->error_nr = ENOMEM;
        error->error_msg = ERRMSG_OUT_OF_MEMORY;
    }
    return rc;
}

// Sends a packet to the specified destination address
//
// @returns true if successful, otherwise false
bool send_packet(
    const int                       socket_domain,
    const struct sockaddr* const    dst_address,
    const size_t                    dst_address_size,
    const uint16_t                  dst_port,
    const void* const               packet_data,
    const size_t                    packet_data_size,
    const uint16_t                  packet_count,
    error_info* const               error
)
{
    bool rc = false;

    bool have_connection = false;
    int socket_dsc = -1;
    for (uint16_t counter = 0; counter < packet_count; ++counter)
    {
        if (!have_connection)
        {
            // Create a raw IP socket
            socket_dsc = socket(socket_domain, SOCK_RAW, IPPROTO_RAW);
            if (socket_dsc != -1)
            {
                // Make the socket non-blocking
                if (set_fcntl_flags(socket_dsc, O_NONBLOCK, error))
                {
                    have_connection = socket_domain == AF_INET6;
                    if (!have_connection)
                    {
                        // Change socket options for inclusion of the custom IP header
                        const uint32_t sockopt_value = 1;
                        have_connection = setsockopt(
                            socket_dsc,
                            SOL_IP,
                            IP_HDRINCL,
                            &sockopt_value,
                            sizeof (sockopt_value)
                        ) == 0;
                    }
                }
                if (!have_connection)
                {
                    close_file_dsc(&socket_dsc);

                    error->error_nr = errno;
                    error->error_msg = "I/O error: Adjusting socket options failed";
                }
            }
        }

        if (have_connection)
        {
            // Send the packet. This is supposed to either send the entire packet or send nothing and fail,
            // returning -1. I/O errors generate a warning but are otherwise ignored, so the program can
            // continue sending packets to other destinations.
            const ssize_t write_count = sendto(
                socket_dsc,
                packet_data,
                packet_data_size,
                0,
                dst_address,
                dst_address_size
            );
            if (write_count == -1)
            {
                // Write failed, reset socket
                close_file_dsc(&socket_dsc);
                have_connection = false;

                const int send_error_code = errno;
                char *const dst_address_str = malloc(BUFFER_SIZE);
                if (dst_address_str != NULL)
                {
                    const void *dst_ip = NULL;
                    if (socket_domain == AF_INET6)
                    {
                        const struct sockaddr_in6 *const ipv6_dst_address =
                            (const struct sockaddr_in6 *const) dst_address;
                        dst_ip = (const void *) &ipv6_dst_address->sin6_addr;
                    }
                    else
                    {
                        const struct sockaddr_in *const ipv4_dst_address =
                            (const struct sockaddr_in *const) dst_address;
                        dst_ip = (const void *) &ipv4_dst_address->sin_addr;
                    }

                    const char *const ntop_rc = inet_ntop(
                        socket_domain,
                        dst_ip,
                        dst_address_str,
                        BUFFER_SIZE
                    );
                    if (ntop_rc != NULL)
                    {
                        fprintf(
                            stderr,
                            "Warning: Sending a packet to destination %s:%u failed\n",
                            dst_address_str, dst_port
                        );
                        display_error_nr_msg(send_error_code);
                    }
                    else
                    {
                        const int ntop_error_code = errno;
                        fputs("Warning: Sending a packet failed\n", stderr);
                        display_error_nr_msg(send_error_code);
                        fputs(
                            "Warning: Failed to generate a string representation "
                            "of the destination address\n",
                            stderr
                        );
                        display_error_nr_msg(ntop_error_code);
                    }
                    free(dst_address_str);
                }
                else
                {
                    error->error_msg = ERRMSG_OUT_OF_MEMORY;
                    error->error_nr = ENOMEM;
                    break;
                }
            }
            else
            {
                // Sending at least one packet succeeded
                rc = true;
            }
        }
        else
        {
            error->error_nr = errno;
            error->error_msg = "I/O error: Creation of a raw IP protocol socket failed";
        }
    }
    if (have_connection)
    {
        close_file_dsc(&socket_dsc);
    }

    return rc;
}

// Checks the result of an inet_pton call and sets error_info information describing the problem
// if the inet_pton call was not successful
//
// @returns true if successful, otherwise false
bool check_inet_pton_rc(const int rc, const int error_nr, error_info *const error)
{
    bool success_flag = false;
    switch (rc)
    {
        case 0:
        {
            // Invalid address string
            error->error_nr = error_nr;
            error->error_msg = "Invalid IP address";
            break;
        }
        case 1:
        {
            // Successful sockaddr initialization
            success_flag = true;
            break;
        }
        case -1:
        {
            // Unsupported address family
            error->error_nr = error_nr;
            error->error_msg = "Unsupported address family";
            break;
        }
        default:
        {
            // Undocumented return code
            error->error_nr = error_nr;
            error->error_msg = "Library function inet_pton(...) returned an unexpected return code";
            break;
        }
    }
    return success_flag;
}

// Sets flags on a file descriptor
//
// @returns true if successful, otherwise false
bool set_fcntl_flags(const int file_dsc, const int flags, error_info *const error)
{
    bool rc = false;
    const int current_flags = fcntl(file_dsc, F_GETFL, 0);
    if (current_flags != -1)
    {
        if (fcntl(file_dsc, F_SETFL, current_flags | flags) == 0)
        {
            rc = true;
        }
        else
        {
            error->error_nr = errno;
            error->error_msg = ERRMSG_FCNTL_FAIL;
        }
    }
    else
    {
        error->error_nr = errno;
        error->error_msg = ERRMSG_FCNTL_FAIL;
    }
    return rc;
}

// Closes a file descriptor
void close_file_dsc(int* const file_dsc)
{
    int rc;
    do
    {
        rc = close(*file_dsc);
    }
    while (rc != 0 && errno == EINTR);
    *file_dsc = -1;
}

// Calculates an IPv4 packet's TCP checksum
uint16_t ipv4_tcp_checksum(
    const unsigned char *const      data,
    const size_t                    length,
    const ipv4_header_no_opt *const header
)
{
    uint32_t native_sum = checksum(data, length);
    native_sum += checksum((const unsigned char *) &header->src_address, sizeof (header->src_address));
    native_sum += checksum((const unsigned char *) &header->dst_address, sizeof (header->dst_address));
    native_sum += header->protocol + (uint32_t) length;
    // Rotate / add twice
    native_sum = (native_sum & 0xFFFF) + (native_sum >> 16);
    native_sum = (native_sum & 0xFFFF) + (native_sum >> 16);

    uint16_t network_sum = htons((uint16_t) native_sum);
    // Invert checksum, unless it's 0xFFFF
    network_sum = network_sum != 0xFFFF ? ~network_sum : 0xFFFF;
    return network_sum;
}

// Calculates an IPv6 packet's TCP checksum
uint16_t ipv6_tcp_checksum(
    const unsigned char *const  data,
    const size_t                length,
    const ipv6_header *const    header
)
{
    uint32_t native_sum = 0;
    native_sum += checksum((const unsigned char *) &header->src_address, 16);
    native_sum += checksum((const unsigned char *) &header->dst_address, 16);

    {
        uint32_t proto_header[2];
        proto_header[0] = htonl(length);
        proto_header[1] = htonl(header->next_header);
        native_sum += checksum((const unsigned char *) &proto_header[0], sizeof (proto_header));
    }

    native_sum += checksum(data, length);

    // Rotate / add twice
    native_sum = (native_sum & 0xFFFF) + (native_sum >> 16);
    native_sum = (native_sum & 0xFFFF) + (native_sum >> 16);

    uint16_t network_sum = htons(native_sum);
    // Invert checksum, unless it's 0xFFFF
    network_sum = network_sum != 0xFFFF ? ~network_sum : 0xFFFF;
    return network_sum;
}

uint32_t checksum(const unsigned char *const data, const size_t length)
{
    uint32_t result = 0;
    for (size_t idx = 0; idx < length; ++idx)
    {
        result += (idx & 0x1) == 0 ? ((uint16_t) data[idx]) << 8 : (uint16_t) data[idx];
    }
    return result;
}
