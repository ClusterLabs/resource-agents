#!/bin/bash

#
#  Copyright Red Hat, Inc. 2004
#  Copyright Mission Critical Linux, Inc. 2000
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.
#

#
# IPv4/IPv6 address management using new /sbin/ifcfg instead of 
# ifconfig utility.
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH


meta_data()
{
    cat <<EOT
<?xml version="1.0" ?>
<resource-agent version="rgmanager 2.0" name="ip">
    <version>1.0</version>

    <longdesc lang="en">
        This is an IP address.  Both IPv4 and IPv6 addresses are supported,
        as well as NIC link monitoring for each IP address.
    </longdesc>
    <shortdesc lang="en">
        This is an IP address.
    </shortdesc>

    <parameters>
        <parameter name="address" unique="1" primary="1">
            <longdesc lang="en">
                IPv4 or IPv6 address to use as a virtual IP
                resource.
            </longdesc>

            <shortdesc lang="en">
                IP Address
            </shortdesc>

	    <content type="string"/>
        </parameter>

        <parameter name="family">
            <longdesc lang="en">
                IPv4 or IPv6 address protocol family.
            </longdesc>

            <shortdesc lang="en">
                Family
            </shortdesc>

            <!--
            <val>auto</val>
            <val>inet</val>
            <val>inet6</val>
            -->
            <content type="string"/>
        </parameter>

        <parameter name="monitor_link">
            <longdesc lang="en">
                Enabling this causes the status check to fail if
                the link on the NIC to which this IP address is
                bound is not present.
            </longdesc>
            <shortdesc lang="en">
                Monitor NIC Link
            </shortdesc>
            <content type="boolean"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="20"/>
        <action name="stop" timeout="20"/>
        <action name="recover" timeout="20"/>
        <action name="status" timeout="20"/>
        <action name="meta-data" timeout="20"/>
        <action name="verify-all" timeout="20"/>
    </actions>

    <special tag="rgmanager"/>
</resource-agent>
EOT
}


verify_address()
{
	# XXX TBD
	return 0
}


verify_all()
{
	# XXX TBD
	return 0
}


#
# Expand an IPv6 address.
#
ipv6_expand()
{
	typeset addr=$1
	typeset maskbits
	typeset -i x
	
	maskbits=${addr/\*\//}

	# chop off mask bits
	addr=${addr/\/\*/}

	# use space as placeholder
	addr=${addr/::/\ }

	# get rid of colons
	addr=${addr//:/}

	# add in zeroes where the doublecolon was
	len=$((${#addr}-1))
	zeroes=
	while [ $len -lt 32 ]; do
		zeroes="0$zeroes"
		((len++))
	done
	addr=${addr/\ /$zeroes}

	# probably a better way to do this
	for (( x=0; x < ${#addr} ; x++)); do
		naddr=$naddr${addr:x:1}

		if (( x < (${#addr} - 1) && x%4 == 3)); then
			naddr=$naddr:
		fi
	done

	if [ -n "$maskbits" ]; then
		echo "$naddr/$maskbits"
		return 0
	fi

	echo "$naddr"
	return 0
}


#
# see if two ipv6 addrs are in the same subnet
#
ipv6_same_subnet()
{
	declare addrl=$1
	declare addrr=$2
	declare m=$3 
	declare r x llsb rlsb

	if [ $# -lt 2 ]; then
		echo "usage: ipv6_same_subnet addr1 addr2 [mask]"
		return 255
	fi
	
	if [ -z "$m" ]; then
		m=${addrl/\*\//}

		[ -n "$m" ] || return 1

		if [ "${addrr}" != "${addrr/\*\//}" ] &&
		   [ "${addrl/\*\//}" != "${addrr/\*\//}" ]; then
			return 1
		fi
	fi

	addrl=${addrl/\/\*/}
	if [ ${#addrl} -lt 39 ]; then
		addrl=$(ipv6_expand $addrl)
	fi

	addrr=${addrr/\/\*/}
	if [ ${#addrr} -lt 39 ]; then
		addrr=$(ipv6_expand $addrr)
	fi

	# Calculate the amount to compare directly
	x=$(($m/4+$m/16-(($m%4)==0)))

	# and the remaining number of bits
	r=$(($m%4))

	if [ $r -ne 0 ]; then
		# If we have any remaining bits, we'll need to compare
		# them later.  Get them now.
		llsb=`printf "%d" 0x${addrl:$x:1}`
		rlsb=`printf "%d" 0x${addrr:$x:1}`

		# One less byte to compare directly, please
		((--x))
	fi
	
	# direct (string comparison) to see if they're equal
	if [ "${addrl:0:$x}" != "${addrr:0:$x}" ]; then
		return 1
	fi

	case $r in
	0)
		return 0
		;;
	1)	
		[ $(($llsb & 8)) -eq $(($rlsb & 8)) ]
		return $?
		;;
	2)
		[ $(($llsb & 12)) -eq $(($rlsb & 12)) ]
		return $?
		;;
	3)
		[ $(($llsb & 14)) -eq $(($rlsb & 14)) ]
		return $?
		;;
	esac

	return 1
}


ipv4_same_subnet()
{
	declare addrl=$1
	declare addrr=$2
	declare m=$3 
	declare r x llsb rlsb

	if [ $# -lt 2 ]; then
		echo "usage: ipv6_same_subnet addr1 addr2 [mask]"
		return 255
	fi

	if [ -z "$m" ]; then
		m=${addrl/\*\//}

		[ -n "$m" ] || return 1

		if [ "${addrr}" != "${addrr/\*\//}" ] &&
		   [ "${addrl/\*\//}" != "${addrr/\*\//}" ]; then
			return 1
		fi

	fi

	addrl=${addrl/\/\*/}
	addrr=${addrr/\/\*/}
	#
	# Remove '.' characters from dotted decimal notation and save
	# in arrays. i.e.
	#
	#	192.168.1.163 -> array[0] = 192
	#	                 array[1] = 168
	#	                 array[2] = 1
	#	                 array[3] = 163
	#
	let x=0
	for quad in ${addrl//./\ }; do
		ip1[((x++))]=$quad
	done

	x=0
	for quad in ${addrr//./\ }; do
		ip2[((x++))]=$quad
	done

	x=0
	while [ $m -ge 8 ]; do
		((m-=8))
		if [ ${ip1[x]} -ne ${ip2[x]} ]; then
			return 1
		fi
		((x++))
	done

	case $m in
	0)
		return 0
		;;
	1)	
		[ $((${ip1[x]} & 128)) -eq $((${ip2[x]} & 128)) ]
		return $?
		;;
	2)
		[ $((${ip1[x]} & 192)) -eq $((${ip2[x]} & 192)) ]
		return $?
		;;
	3)
		[ $((${ip1[x]} & 224)) -eq $((${ip2[x]} & 224)) ]
		return $?
		;;
	4)
		[ $((${ip1[x]} & 240)) -eq $((${ip2[x]} & 240)) ]
		return $?
		;;
	5)
		[ $((${ip1[x]} & 248)) -eq $((${ip2[x]} & 248)) ]
		return $?
		;;
	6)
		[ $((${ip1[x]} & 252)) -eq $((${ip2[x]} & 252)) ]
		return $?
		;;
	7)
		[ $((${ip1[x]} & 254)) -eq $((${ip2[x]} & 254)) ]
		return $?
		;;
	esac

	return 1
}


ipv6_find_interface()
{
	declare idx dev ifaddr
	declare newaddr=$(ipv6_expand $1)

	while read idx dev ifaddr; do

		idx=${idx/:/}

		#
		# expand the addr to the full deal
		#
		ifaddr=$(ipv6_expand $addr)

		if [ "$ifaddr" = "$newaddr" ]; then
			# for most things, 
			echo $dev ${ifaddr/\*\/}
			return 0
		fi

		#
		# Always list the new address second so that _same_subnet
		# matches based on i/f subnet.
		#
		if ipv6_same_subnet $ifaddr $newaddr; then
			echo $dev ${ifaddr/\*\/}
			return 0
		fi
	done < <(ip -o -f inet6 addr | awk '{print $1,$2,$4}')

	return 1
}


ethernet_link_up()
{
	declare linkstate=$(ethtool $1 | grep "Link detected:" |\
			    awk '{print $3}')
	
	[ -n "$linkstate" ] || return 0

	case $linkstate in
	yes)
		return 0
		;;
	*)
		return 1
		;;
	esac
	
	return 1
}


ipv4_find_interface()
{
	declare idx dev ifaddr
	declare newaddr=$1

	while read idx dev ifaddr; do

		idx=${idx/:/}

		if [ "$ifaddr" = "$newaddr" ]; then
			# for most things, 
			echo $dev ${ifaddr/\*\/}
			return 0
		fi

		if ipv4_same_subnet $ifaddr $newaddr; then
			echo $dev ${ifaddr/\*\/}
			return 0
		fi
	done < <(ip -o -f inet addr | awk '{print $1,$2,$4}')

	return 1
}


#
# Add an IP address to our interface.
#
ipv6()
{
	declare dev maskbits

	read dev maskbits < <(ipv6_find_interface $2)

	if [ -z "$dev" ]; then
		return 1
	fi

	if [ "${addr}" = "${addr/\*\//}" ]; then
		addr="$addr/$maskbits"
	fi

	if [ "$1" = "add" ]; then
		ethernet_link_up $dev
		if [ $? -ne 0 ]; then
			echo "Cannot add $addr to $dev; no link"
			return 1
		fi
	fi

	echo "Attempting to $1 IPv4 address $addr ($dev)"

	/sbin/ifcfg $dev $1 $addr
}


#
# Add an IP address to our interface.
#
ipv4()
{
	declare dev maskbits hwaddr
	declare addr=$2
		
	read dev maskbits < <(ipv4_find_interface $addr)

	if [ -z "$dev" ]; then
		return 1
	fi

	if [ "${addr}" = "${addr/\*\//}" ]; then
		addr="$addr/$maskbits"
	fi

	if [ "$1" = "add" ]; then
		ethernet_link_up $dev
		if [ $? -ne 0 ]; then
			echo "Cannot add $addr to $dev; no link"
			return 1
		fi
	fi

	echo "Attempting to $1 IPv4 address $addr ($dev)"

	/sbin/ifcfg $dev $1 $addr

	[ $? -ne 0 ] && return 1

	if [ "$1" = "add" ]; then
		# do that freak arp thing

		hwaddr=$(ip -o link show $dev)
		hwaddr=${hwaddr/*link\/ether\ /}
		hwaddr=${hwaddr/\ \*/}

		addr=${addr/\/\*/}
		echo Sending gratuitous ARP: $addr $hwaddr
		#cluarp $addr $hwaddr $addr ffffffffffff $devS
 		arping -q -c 2 -U -I eth0 $addr
	fi
}


ip_op()
{
	declare dev

	if [ "$2" = "status" ]; then
	
		dev=$(ip -f $1 -o addr | grep $3 | awk '{print $2}')
		if [ -z "$dev" ]; then
			[ -n "$4" ] || echo "$3 is not configured"
			return 1
		fi

		[ -n "$4" ] || echo "$3 present on $dev"
		if [ "${OCF_RESKEY_monitor_link}" != "yes" ]; then
			return 0
		fi

		[ -n "$4" ] || echo -n "Checking link status of $dev..."
		if ethernet_link_up $dev; then
			[ -n "$4" ] || echo "Active"
			return 0
		fi

		[ -n "$4" ] || echo "No Link"
		return 1
	fi

	case $1 in
	inet)
		ipv4 $2 $3
		return $?
		;;
	inet6)
		ipv6 $2 $3
		return $?
		;;
	esac
	return 1
}
	

case ${OCF_RESKEY_family} in
inet)
	;;
inet6)
	;;
*)
	if [ "${OCF_RESKEY_address//:/}" != "${OCF_RESKEY_address}" ]; then
		OCF_RESKEY_family=inet6
	else
		OCF_RESKEY_family=inet
	fi
	;;
esac


case $1 in
start)
	if ip_op ${OCF_RESKEY_family} status ${OCF_RESKEY_address} quiet; then
		echo "${OCF_RESKEY_address} already configured"
		exit 0
	fi
	ip_op ${OCF_RESKEY_family} add ${OCF_RESKEY_address}
	exit $?
	;;
stop)
	unset _monitor_link
	if ip_op ${OCF_RESKEY_family} status ${OCF_RESKEY_address} quiet; then
		ip_op ${OCF_RESKEY_family} del ${OCF_RESKEY_address}

		# Make sure it's down
		if ip_op ${OCF_RESKEY_family} status ${OCF_RESKEY_address} quiet; then
			exit 1
		fi
	else
		echo "${OCF_RESKEY_address} is not configured"
	fi
	exit 0
	;;
status|monitor)
	ip_op ${OCF_RESKEY_family} status ${OCF_RESKEY_address}
	exit $?
	;;
restart|recover)
	$0 stop || exit 1
	$0 start || exit 1
	exit 0
	;;
meta-data)
	meta_data
	exit 0
	;;
verify-all)
	verify_all
	exit $?
	;;
esac


