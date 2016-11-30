# ora-common.sh
#
# Description: Common code for oracle and oralsnr resource agents
#
#
# Author:      Dejan Muhamedagic
# Support:     users@clusterlabs.org
# License:     GNU General Public License (GPL)
# Copyright:   (C) 2012 Dejan Muhamedagic, SUSE/Attachmate
#

#      Gather up information about our oracle instance

rmtmpfiles() {
	rm -f $TMPFILES
}

ora_common_getconfig() {
	ORACLE_SID=$1
	# optional, defaults to whatever is in oratab
	ORACLE_HOME=$2
	# optional, defaults to the owner of ORACLE_HOME
	ORACLE_OWNER=$3
	# optional, defaults to $ORACLE_HOME/network/admin
	# (only the oralsnr may provide and use this one)
	TNS_ADMIN=$4

	# get ORACLE_HOME from /etc/oratab if not set
	[ x = "x$ORACLE_HOME" ] &&
		ORACLE_HOME=`awk -F: "/^$ORACLE_SID:/"'{print $2}' /etc/oratab`

	# there a better way to find out ORACLE_OWNER?
	[ x = "x$ORACLE_OWNER" ] &&
		ORACLE_OWNER=`ls -ld $ORACLE_HOME/. 2>/dev/null | awk 'NR==1{print $3}'`

	# There are use-cases were users want to be able to set a custom TMS_ADMIN path.
	# When TNS_ADMIN is not provided, use the default path.
	[ x = "x$TNS_ADMIN" ] &&
		TNS_ADMIN=$ORACLE_HOME/network/admin

	LD_LIBRARY_PATH=$ORACLE_HOME/lib
	LIBPATH=$ORACLE_HOME/lib
	PATH=$ORACLE_HOME/bin:$ORACLE_HOME/dbs:$PATH
	export ORACLE_SID ORACLE_HOME ORACLE_OWNER TNS_ADMIN
	export LD_LIBRARY_PATH LIBPATH

	ORA_ENVF=`mktemp`
	dumporaenv > $ORA_ENVF
	chmod 644 $ORA_ENVF
	TMPFILES="$ORA_ENVF"
	trap "rmtmpfiles" EXIT
}

ora_common_validate_all() {
	#	Let's make sure a few important things are set...
	if [ x = "x$ORACLE_HOME" ]; then
		ocf_log info "ORACLE_HOME not set"
		return $OCF_ERR_INSTALLED
	fi
	if [ x = "x$ORACLE_OWNER" ]; then
		ocf_log info "ORACLE_OWNER not set"
		return $OCF_ERR_INSTALLED
	fi

	US=`id -u -n`
	if [ $US != root -a $US != $ORACLE_OWNER ]
	then
	  ocf_exit_reason "$0 must be run as root or $ORACLE_OWNER"
	  return $OCF_ERR_PERM
	fi
	return 0
}

dumporaenv() {
cat<<EOF
PATH=$ORACLE_HOME/bin:$ORACLE_HOME/dbs:$PATH
ORACLE_SID=$ORACLE_SID
ORACLE_HOME=$ORACLE_HOME
ORACLE_OWNER=$ORACLE_OWNER
LD_LIBRARY_PATH=$ORACLE_HOME/lib
LIBPATH=$ORACLE_HOME/lib
TNS_ADMIN=$TNS_ADMIN
export ORACLE_SID ORACLE_HOME ORACLE_OWNER TNS_ADMIN
export LD_LIBRARY_PATH LIBPATH
EOF
}

# vim:tabstop=4:shiftwidth=4:textwidth=0:wrapmargin=0
