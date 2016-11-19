#!/bin/sh
ipcheck_ipv4() {
  local r1_to_255="([1-9][0-9]?|1[0-9][0-9]|2[0-4][0-9]|25[0-5])"
  local r0_to_255="([0-9][0-9]?|1[0-9][0-9]|2[0-4][0-9]|25[0-5])"
  local r_ipv4="^$r1_to_255\.$r0_to_255\.$r0_to_255\.$r0_to_255$"
  echo "$1" | grep -q -Ee "$r_ipv4"
}
ipcheck_ipv6() {
  ! echo "$1" | grep -qs "[^0-9:a-fA-F]"
}
ifcheck() {
  local ifname="$1"
  $IP2UTIL link show dev $ifname 2>&1
}
prefixcheck() {
  local prefix=$1
  local prefix_length=${#prefix}
  local prefix_check=$2

  if [ $prefix_length -gt 3 -o $prefix_length -eq 0 ] ; then
    return 1
  fi
  echo "$prefix" | grep -qs "[^0-9]"
  if [ $? = 0 ] ; then
    return 1
  fi
  if [ $prefix -lt 1 -o $prefix -gt $prefix_check ] ; then
    return 1
  fi
  return 0
}
getnetworkinfo()
{
  local line netinfo
  ip -o -f inet route list match $OCF_RESKEY_ip table local scope host | (while read line;
  do
    netinfo=`echo $line | awk '{print $2}'`
    case $netinfo in
    */*)
      set -- $line
      break
      ;;
    esac
  done
  echo $line)
}

# previous versions of the IPaddr2 resource agent used to accept a netmask
# in dotted quad notation (and convert to cidr notation implicitly; possibly
# with warnings nobody ever noticed)
# We can do so here as well.
maybe_convert_dotted_quad_to_cidr()
{
	# does this even look like a dotted quad notation?
	case $netmask in
	# invalid if it contains other than digits and dots
	# invalid if it contains adjacent dots,
	#   or starts or ends with a dot
	#   or more than three dots
	#   or more than three digits in a row
	*[!0-9.]* | *..* | *.*.*.*.* | .* | *. | *[0-9][0-9][0-9][0-9]* )
		return ;;

	# do we have three dots?
	# component range check on <= 255 is done below
	*.*.*.*) : ;;

	*)	return ;;
	esac

	local IFS=.
	set -- $netmask
	[ $# = 4 ] || return;

	local b m=0 mask;
	for b ; do
		[ $b -le 255 ] || return;
		m=$(( (m << 8) + b ));
	done;
	case $m in
	# for i in `seq 32 -1 0`; do printf "%10u) netmask=$i ;;\n" $(( ((1 << i)-1) << (32 - i) )); done
        4294967295) mask=32 ;;
        4294967294) mask=31 ;;
        4294967292) mask=30 ;;
        4294967288) mask=29 ;;
        4294967280) mask=28 ;;
        4294967264) mask=27 ;;
        4294967232) mask=26 ;;
        4294967168) mask=25 ;;
        4294967040) mask=24 ;;
        4294966784) mask=23 ;;
        4294966272) mask=22 ;;
        4294965248) mask=21 ;;
        4294963200) mask=20 ;;
        4294959104) mask=19 ;;
        4294950912) mask=18 ;;
        4294934528) mask=17 ;;
        4294901760) mask=16 ;;
        4294836224) mask=15 ;;
        4294705152) mask=14 ;;
        4294443008) mask=13 ;;
        4293918720) mask=12 ;;
        4292870144) mask=11 ;;
        4290772992) mask=10 ;;
        4286578688) mask=9 ;;
        4278190080) mask=8 ;;
        4261412864) mask=7 ;;
        4227858432) mask=6 ;;
        4160749568) mask=5 ;;
        4026531840) mask=4 ;;
        3758096384) mask=3 ;;
        3221225472) mask=2 ;;
        2147483648) mask=1 ;;
                 0) mask=0 ;;
		 *) ocf_log err "Bogus netmask: $netmask" ; return ;;
	esac
	ocf_log warn "Please convert dotted quad netmask $netmask to CIDR notation $mask!"
	netmask=$mask
}

findif_check_params()
{
  local family="$1"
  local match="$OCF_RESKEY_ip"
  local nic="$OCF_RESKEY_nic"
  # netmask NOT local, see maybe_convert_dotted_quad_to_cidr
        netmask="$OCF_RESKEY_cidr_netmask"
  local brdcast="$OCF_RESKEY_broadcast"
  local errmsg

  maybe_convert_dotted_quad_to_cidr

  # Do a sanity check only on start and validate-all
  # to avoid returning OCF_ERR_CONFIGURED from the monitor operation.
  case $__OCF_ACTION in
      start|validate-all)	true;;
      *)			return $OCF_SUCCESS;;
  esac

  if [ -n "$nic" ] ; then
    errmsg=`ifcheck $nic`
    if [ $? -ne 0 ] ; then
      ocf_log err "Invalid interface name [$nic]: $errmsg"
      return $OCF_ERR_CONFIGURED
    fi
  fi

  if [ "$family" = "inet6" ] ; then
    ipcheck_ipv6 $match
    if [ $? = 1 ] ; then
      ocf_log err "IP address [$match] not valid."
      return $OCF_ERR_CONFIGURED
    fi
    if [ -z "$nic" ] ; then
      echo $match | grep -qis '^fe80::'
      if [ $? = 0 ] ; then
        ocf_log err "'nic' parameter is mandatory for a link local address [$match]."
        return $OCF_ERR_CONFIGURED
      fi
    fi
    if [ -n "$netmask" ] ; then
      prefixcheck $netmask 128
      if [ $? = 1 ] ; then
        ocf_log err "Invalid netmask specification [$netmask]."
        return $OCF_ERR_CONFIGURED
      fi
    fi
  else
    # family = inet
    ipcheck_ipv4 $match
    if [ $? = 1 ] ; then
      ocf_log err "IP address [$match] not valid."
      return $OCF_ERR_CONFIGURED
    fi
    if [ -n "$netmask" ] ; then
      prefixcheck $netmask 32
      if [ $? = 1 ] ; then
        ocf_log err "Invalid netmask specification [$netmask]."
        return $OCF_ERR_CONFIGURED
      fi
    fi
    if [ -n "$brdcast" ] ; then
      ipcheck_ipv4 $brdcast
      if [ $? = 1 ] ; then
        if [ "$brdcast" != "+" -a "$brdcast" != "-" ]; then
          ocf_log err "Invalid broadcast address [$brdcast]."
          return $OCF_ERR_CONFIGURED
        fi
      fi
    fi
  fi
  return $OCF_SUCCESS
}

findif()
{
  local match="$OCF_RESKEY_ip"
  local family
  local scope
  local nic="$OCF_RESKEY_nic"
  local netmask="$OCF_RESKEY_cidr_netmask"
  local brdcast="$OCF_RESKEY_broadcast"

  echo $match | grep -qs ":"
  if [ $? = 0 ] ; then
    family="inet6"
  else
    family="inet"
    scope="scope link"
  fi
  findif_check_params $family || return $?

  if [ -n "$netmask" ] ; then
      match=$match/$netmask
  fi
  if [ -n "$nic" ] ; then
    # NIC supports more than two.
    set -- $(ip -o -f $family route list match $match $scope | grep "dev $nic " | awk 'BEGIN{best=0} { mask=$1; sub(".*/", "", mask); if( int(mask)>=best ) { best=int(mask); best_ln=$0; } } END{print best_ln}')
  else
    set -- $(ip -o -f $family route list match $match $scope | awk 'BEGIN{best=0} { mask=$1; sub(".*/", "", mask); if( int(mask)>=best ) { best=int(mask); best_ln=$0; } } END{print best_ln}')
  fi
  if [ $# = 0 ] ; then
    case $OCF_RESKEY_ip in
    127.*)
      set -- `getnetworkinfo`
      shift;;
    esac
  fi
  if [ -z "$nic" -o -z "$netmask" ] ; then
    if [ $# = 0 ] ; then
      ocf_log err "Unable to find nic or netmask."
      return $OCF_ERR_GENERIC
    fi
    case $1 in
    */*) : OK ;;
    *)
      ocf_log err "Unable to find cidr_netmask."
      return $OCF_ERR_GENERIC ;;
    esac
  fi
  [ -z "$nic" ] && nic=$3
  [ -z "$netmask" ] && netmask=${1#*/}
  if [ $family = "inet" ] ; then
    if [ -z "$brdcast" ] ; then
      if [ -n "$7" ] ; then
        set -- `ip -o -f $family addr show | grep $7`
        [ "$5" = brd ] && brdcast=$6
      fi
    fi
  else
    if [ -z "$OCF_RESKEY_nic" -a "$netmask" != "${1#*/}" ] ; then
      ocf_log err "Unable to find nic, or netmask mismatch."
      return $OCF_ERR_GENERIC
    fi
  fi
  echo "$nic netmask $netmask broadcast $brdcast"
  return $OCF_SUCCESS
}
