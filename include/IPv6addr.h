/*
 * This program manages IPv6 address with OCF Resource Agent standard.
 *
 * Author: Huang Zhen <zhenh@cn.ibm.com>
 * Copyright (c) 2004 International Business Machines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef OCF_IPV6_HELPER_H
#define OCF_IPV6_HELPER_H
#include <netinet/icmp6.h>
#include <config.h>
/*
0	No error, action succeeded completely
1 	generic or unspecified error (current practice)
	The "monitor" operation shall return this for a crashed, hung or
	otherwise non-functional resource.
2 	invalid or excess argument(s)
	Likely error code for validate-all, if the instance parameters
	do not validate. Any other action is free to also return this
	exit status code for this case.
3 	unimplemented feature (for example, "reload")
4 	user had insufficient privilege
5 	program is not installed
6 	program is not configured
7 	program is not running
8	resource is running in "master" mode and fully operational
9	resource is in "master" mode but in a failed state
*/
#define	OCF_SUCCESS		0
#define	OCF_ERR_GENERIC		1
#define	OCF_ERR_ARGS		2
#define	OCF_ERR_UNIMPLEMENTED	3
#define	OCF_ERR_PERM		4
#define	OCF_ERR_INSTALLED	5
#define	OCF_ERR_CONFIGURED	6
#define	OCF_NOT_RUNNING		7

#define	HWADDR_LEN 6 /* mac address length */
#define UA_REPEAT_COUNT	5
#define  BCAST_ADDR "ff02::1"
#define IF_INET6 "/proc/net/if_inet6"

int send_ua(struct in6_addr* src_ip, char* if_name);
#endif
