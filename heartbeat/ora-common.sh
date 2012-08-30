# ora-common.sh
#
# Description: Common code for oracle and oralsnr resource agents
#
#
# Author:      Dejan Muhamedagic
# Support:     linux-ha@lists.linux-ha.org
# License:     GNU General Public License (GPL)
# Copyright:   (C) 2012 Dejan Muhamedagic, SUSE/Attachmate
#

#      Gather up information about our oracle instance

ora_info() {
	ORACLE_SID=$1
	ORACLE_HOME=$2
	ORACLE_OWNER=$3

	# get ORACLE_HOME from /etc/oratab if not set
	[ x = "x$ORACLE_HOME" ] &&
		ORACLE_HOME=`awk -F: "/^$ORACLE_SID:/"'{print $2}' /etc/oratab`

	# there a better way to find out ORACLE_OWNER?
	[ x = "x$ORACLE_OWNER" ] &&
		ORACLE_OWNER=`ls -ld $ORACLE_HOME/. 2>/dev/null | awk 'NR==1{print $3}'`

	sqlplus=$ORACLE_HOME/bin/sqlplus
	lsnrctl=$ORACLE_HOME/bin/lsnrctl
	tnsping=$ORACLE_HOME/bin/tnsping
}

testoraenv() {
	#	Let's make sure a few important things are set...
	if [ x = "x$ORACLE_HOME" ]; then
		ocf_log info "ORACLE_HOME not set"
		return $OCF_ERR_INSTALLED
	fi
	if [ x = "x$ORACLE_OWNER" ]; then
		ocf_log info "ORACLE_OWNER not set"
		return $OCF_ERR_INSTALLED
	fi
	#	and some important things are there
	if [ ! -x "$sqlplus" ]; then
		ocf_log info "$sqlplus does not exist"
		return $OCF_ERR_INSTALLED
	fi
	if [ ! -x "$lsnrctl" ]; then
		ocf_log err "$lsnrctl does not exist"
		return $OCF_ERR_INSTALLED
	fi
	if [ ! -x "$tnsping" ]; then
		ocf_log err "$tnsping does not exist"
		return $OCF_ERR_INSTALLED
	fi
	return 0
}

setoraenv() {
	LD_LIBRARY_PATH=$ORACLE_HOME/lib
	LIBPATH=$ORACLE_HOME/lib
	TNS_ADMIN=$ORACLE_HOME/network/admin
	PATH=$ORACLE_HOME/bin:$ORACLE_HOME/dbs:$PATH
	export ORACLE_SID ORACLE_HOME ORACLE_OWNER TNS_ADMIN
	export LD_LIBRARY_PATH LIBPATH
}
dumporaenv() {
cat<<EOF
PATH=$ORACLE_HOME/bin:$ORACLE_HOME/dbs:$PATH
ORACLE_SID=$ORACLE_SID
ORACLE_HOME=$ORACLE_HOME
ORACLE_OWNER=$ORACLE_OWNER
LD_LIBRARY_PATH=$ORACLE_HOME/lib
LIBPATH=$ORACLE_HOME/lib
TNS_ADMIN=$ORACLE_HOME/network/admin
export ORACLE_SID ORACLE_HOME ORACLE_OWNER TNS_ADMIN
export LD_LIBRARY_PATH LIBPATH
EOF
}

check_user() {
	US=`id -u -n`
	if [ $US != root -a $US != $ORACLE_OWNER ]
	then
	  ocf_log err "$0 must be run as root or $ORACLE_OWNER"
	  exit $OCF_ERR_PERM
	fi
}
