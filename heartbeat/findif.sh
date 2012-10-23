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
ifcheck_ipv4() {
  local ifcheck=$1
  local procfile="/proc/net/dev"
  local ifstr rest

  while read ifstr rest ; do
    # Interface name may be concatenated to Receive field with ':'
    case "$ifstr" in
      "$ifcheck:"*) return 0;;
    esac
  done < $procfile
  return 1
}
ifcheck_ipv6() {
  local ifcheck="$1"
  local procfile="/proc/net/if_inet6"
  local tmp ifstr

  while read tmp tmp tmp tmp tmp ifstr ; do
    [ "$ifstr" = "$ifcheck" ] && return 0
  done < $procfile
  return 1
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

findif_check_params()
{
  local family="$1"
  local match="$OCF_RESKEY_ip"
  local nic="$OCF_RESKEY_nic"
  local netmask="$OCF_RESKEY_cidr_netmask"
  local brdcast="$OCF_RESKEY_broadcast"

  if [ "$family" = "inet6" ] ; then
    ipcheck_ipv6 $match
    if [ $? = 1 ] ; then
      ocf_log err "IP address [$match] not valid."
      return $OCF_ERR_CONFIGURED
    fi
    if [ -n "$nic" ] ; then
      ifcheck_ipv6 $nic
      if [ $? = 1 ] ; then
        ocf_log err "Unknown interface [$nic] No such device."
        return $OCF_ERR_CONFIGURED
      fi
    else
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
    if [ -n "$nic" ] ; then
      ifcheck_ipv4 $nic
      if [ $? = 1 ] ; then
        ocf_log err "Unknown interface [$nic] No such device."
        return $OCF_ERR_CONFIGURED
      fi
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
        ocf_log err "Invalid broadcast address [$brdcast]."
        return $OCF_ERR_CONFIGURED
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
    set -- `ip -o -f $family route list match $match $scope | grep "dev $nic"`
  else
    set -- `ip -o -f $family route list match $match $scope`
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
