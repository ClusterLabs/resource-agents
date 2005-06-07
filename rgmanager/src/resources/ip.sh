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
            <content type="boolean" default="1"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="20"/>
        <action name="stop" timeout="20"/>
	<!-- No recover action.  If the IP address is not useable, then
	     resources may or may not depend on it.  If it's been 
	     deconfigured, resources using it are in a bad state. -->

	<!-- Checks to see if the IP is up and (optionally) the link is
	     working -->
        <action name="status" interval="20" timeout="10"/>
        <action name="monitor" interval="20" timeout="10"/>

	<!-- Checks to see if we can ping the IP address locally -->
        <action name="status" depth="10" interval="60" timeout="20"/>
        <action name="monitor" depth="10" interval="60" timeout="20"/>

	<!-- Checks to see if we can ping the router -->
        <action name="status" depth="20" interval="2m" timeout="20"/>
        <action name="monitor" depth="20" interval="2m" timeout="20"/>

        <action name="meta-data" timeout="20"/>
        <action name="verify-all" timeout="20"/>
    </actions>

    <special tag="rgmanager">
	<attributes maxinstances="1"/>
    </special>
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
	
	maskbits=${addr/*\//}
	if [ "$maskbits" = "$addr" ]; then
		maskbits=""
	else
		# chop off mask bits
		addr=${addr/\/*/}
	fi

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
		m=${addrl/*\//}

		[ -n "$m" ] || return 1

	fi

	if [ "${addrr}" != "${addrr/*\//}" ] &&
	   [ "$m" != "${addrr/*\//}" ]; then
		return 1
	fi

	addrl=${addrl/\/*/}
	if [ ${#addrl} -lt 39 ]; then
		addrl=$(ipv6_expand $addrl)
	fi

	addrr=${addrr/\/*/}
	if [ ${#addrr} -lt 39 ]; then
		addrr=$(ipv6_expand $addrr)
	fi

	# Calculate the amount to compare directly
	x=$(($m/4+$m/16-(($m%4)==0)))

	# and the remaining number of bits
	r=$(($m%4))

	if [ $r -ne 0 ]; then
		# If we have any remaining bits, we will need to compare
		# them later.  Get them now.
		llsb=`printf "%d" 0x${addrl:$x:1}`
		rlsb=`printf "%d" 0x${addrr:$x:1}`

		# One less byte to compare directly, please
		((--x))
	fi
	
	# direct (string comparison) to see if they are equal
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
		echo "usage: ipv4_same_subnet current_addr new_addr [maskbits]"
		return 255
	fi


	#
	# Chop the netmask off of the ipaddr:
	# e.g. 1.2.3.4/22 -> 22
	#
	if [ -z "$m" ]; then
		m=${addrl/*\//}
		[ -n "$m" ] || return 1
	fi

	#
	# Check to see if there was a subnet mask provided on the
	# new IP address.  If there was one and it does not match
	# our expected subnet mask, we are done.
	#
	if [ "${addrr}" != "${addrr/\/*/}" ] &&
	   [ "$m" != "${addrr/*\//}" ]; then
		return 1
	fi

	#
	# Chop off subnet bits for good.
	#
	addrl=${addrl/\/*/}
	addrr=${addrr/\/*/}

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
		ifaddr=$(ipv6_expand $ifaddr)

		if [ "$ifaddr" = "$newaddr" ]; then
			#
			# Already running?
			#
			echo $dev ${ifaddr/*\/}
			return 0
		fi

		#
		# Always list the new address second so that _same_subnet
		# matches based on i/f subnet.
		#
		if ipv6_same_subnet $ifaddr $newaddr; then
			echo $dev ${ifaddr/*\//}
			return 0
		fi
	done < <(ip -o -f inet6 addr | awk '{print $1,$2,$4}')

	return 1
}


#
# Find slaves for a bonded interface
#
findSlaves()
{
	declare mastif=$1
	declare line
	declare intf
	declare interfaces

	if [ -z "$mastif" ]; then
		echo "usage: findSlaves <master I/F>"
		return 1
	fi

	line=$(/sbin/ip link list dev $mastif | grep "<.*MASTER.*>")
	if [ $? -ne 0 ]; then
		echo "Error determining status of $mastif"
		return 1
	fi

	if [ -z "`/sbin/ip link list dev $mastif | grep \"<.*MASTER.*>\"`" ]
	then
		echo "$mastif is not a master device"
		return 1
	fi

	while read line; do
		set - $line
		while [ $# -gt 0 ]; do
			case $1 in
			eth*:)
				interfaces="${1/:/} $interfaces"
				continue 2
				;;
			esac
			shift
		done
	done < <( /sbin/ip link list | grep "master $mastif" )

	echo $interfaces
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


#
# Checks the physical link status of an ethernet or bonded interface.
#
network_link_up()
{
	declare slaves
	declare intf_arg=$1
	declare link_up=1		# Assume link down
	declare intf_test

	if [ -z "$intf_arg" ]; then
		echo "usage: network_link_up <intf>"
		return 1
	fi
	
	#
	# XXX assumes bond* interfaces are the bonding driver. (Fair
	# assumption on Linux, I think)
	#
	if [ "${intf_arg/bond/}" != "$intf_arg" ]; then
		
		#
		# Bonded driver.  Check link of all slaves for this interface.
		# If any link is up, the bonding driver is expected to route
		# traffic through that link.  Thus, the entire bonded link
		# is declared up.
		#
		slaves=$(findSlaves $intf_arg)
		if [ $? -ne 0 ]; then
			echo "Error finding slaves of $intf_arg"
			return 1
		fi
		for intf_test in $slaves; do
			ethernet_link_up $intf_test && link_up=0
		done
	else
		ethernet_link_up $intf_arg
		link_up=$?
	fi

	if [ $link_up -eq 0 ]; then
		echo "Link for $intf_arg: Detected"
	else
		echo "Link for $intf_arg: Not detected"
	fi

	return $link_up
}


ipv4_find_interface()
{
	declare idx dev ifaddr
	declare newaddr=$1

	while read idx dev ifaddr; do

		idx=${idx/:/}

		if [ "$ifaddr" = "$newaddr" ]; then
			# for most things, 
			echo $dev ${ifaddr/*\//}
			return 0
		fi

		if ipv4_same_subnet $ifaddr $newaddr; then
			echo $dev ${ifaddr/*\//}
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
	declare addr=$2

	read dev maskbits < <(ipv6_find_interface $addr)

	if [ -z "$dev" ]; then
		return 1
	fi

	if [ "${addr}" = "${addr/*\//}" ]; then
		addr="$addr/$maskbits"
	fi

	if [ "$1" = "add" ]; then
		network_link_up $dev
		if [ $? -ne 0 ]; then
			echo "Cannot add $addr to $dev; no link"
			return 1
		fi
	fi

	echo "Attempting to $1 IPv6 address $addr ($dev)"

	/sbin/ip -f inet6 addr $1 dev $dev $addr
	[ $? -ne 0 ] && return 1

	#
	# NDP should take of figuring out our new address.  Plus,
	# we do not have something (like arping) to do this for ipv6
	# anyway.
	# 
	# RFC 2461, section 7.2.6 states thusly:
	#
   	# Note that because unsolicited Neighbor Advertisements do not
	# reliably update caches in all nodes (the advertisements might
	# not be received by all nodes), they should only be viewed as
	# a performance optimization to quickly update the caches in
	#  most neighbors. 
	#

	# Not sure if this is necessary for ipv6 either.
	file=$(which rdisc 2>/dev/null)
	if [ -f "$file" ]; then
		killall -HUP rdisc || rdisc -fs
	fi

	return 0
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

	#if [ "${addr}" = "${addr/\*\//}" ]; then
		#addr="$addr/$maskbits"
	#fi

	if [ "$1" = "add" ]; then
		network_link_up $dev
		if [ $? -ne 0 ]; then
			echo "Cannot add $addr to $dev; no link"
			return 1
		fi
	fi

	echo "Attempting to $1 IPv4 address $addr ($dev)"

	#/sbin/ip $dev $1 $addr
	/sbin/ip -f inet addr $1 dev $dev $addr
	[ $? -ne 0 ] && return 1

	#
	# The following is needed only with ifconfig; ifcfg does it for us
	#
	if [ "$1" = "add" ]; then
		# do that freak arp thing

		hwaddr=$(ip -o link show $dev)
		hwaddr=${hwaddr/*link\/ether\ /}
		hwaddr=${hwaddr/\ \*/}

		addr=${addr/\/*/}
		echo Sending gratuitous ARP: $addr $hwaddr
 		arping -q -c 2 -U -I $dev $addr
	fi

	file=$(which rdisc 2>/dev/null)
	if [ -f "$file" ]; then
		killall -HUP rdisc || rdisc -fs
	fi

	return 0
}


#
# Usage:
# ping_check <family> <address>
#
ping_check()
{
	declare ops="-c 1 -w 2"
	declare ipv6ops=""

	if [ "$1" = "ipv6" ]; then
		ipv6ops="-6"
	fi

	return $(ping $ipv6ops $ops $2 &> /dev/null)
}


#
# Usage:
# ip_op <family> <operation> <address> [quiet]
#
ip_op()
{
	declare dev
	declare rtr

	if [ "$2" = "status" ]; then

		echo Checking $3, Level $OCF_CHECK_LEVEL
	
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
		if ! network_link_up $dev; then
			[ -n "$4" ] || echo "No Link"
			return 1
		fi
		[ -n "$4" ] || echo "Active"

		[ $OCF_CHECK_LEVEL -lt 10 ] && return 0
		[ -n "$4" ] || echo -n "Pinging $3..."
		if ! ping_check $1 $3; then
			[ -n "$4" ] || echo "Fail"
			return 1
		fi
		echo "OK"

		#
		# XXX may be ipv4 only; disable for now. 
		#
		if [ "$OCF_RESKEY_family" = "inet6" ]; then
			return 0;
		fi
		[ $OCF_CHECK_LEVEL -lt 20 ] && return 0
		rtr=`ip route | grep "default via.*dev $dev" | awk '{print $3}'`
		[ -n "$4" ] || echo -n "Pinging $rtr..."
		if ! ping_check $1 $rtr; then
			[ -n "$4" ] || echo "Fail"
			return 1
		fi
		echo "OK"

		return 0
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
		export OCF_RESKEY_family=inet6
	else
		export OCF_RESKEY_family=inet
	fi
	;;
esac


if [ -z "$OCF_CHECK_LEVEL" ]; then
	OCF_CHECK_LEVEL=0
fi


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
restart)
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


