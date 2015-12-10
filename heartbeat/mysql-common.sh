#!/bin/sh

#######################################################################

# Attempt to detect a default binary
OCF_RESKEY_binary_default=$(which mysqld_safe 2> /dev/null)
if [ "$OCF_RESKEY_binary_default" = "" ]; then
	OCF_RESKEY_binary_default=$(which safe_mysqld 2> /dev/null)
fi

# Fill in some defaults if no values are specified
HOSTOS=`uname`
if [ "X${HOSTOS}" = "XOpenBSD" ];then
	if [ "$OCF_RESKEY_binary_default" = "" ]; then
		OCF_RESKEY_binary_default="/usr/local/bin/mysqld_safe"
	fi
	OCF_RESKEY_config_default="/etc/my.cnf"
	OCF_RESKEY_datadir_default="/var/mysql"
	OCF_RESKEY_user_default="_mysql"
	OCF_RESKEY_group_default="_mysql"
	OCF_RESKEY_log_default="/var/log/mysqld.log"
	OCF_RESKEY_pid_default="/var/mysql/mysqld.pid"
	OCF_RESKEY_socket_default="/var/run/mysql/mysql.sock"
else
	if [ "$OCF_RESKEY_binary_default" = "" ]; then
		OCF_RESKEY_binary_default="/usr/bin/safe_mysqld"
	fi
	OCF_RESKEY_config_default="/etc/my.cnf"
	OCF_RESKEY_datadir_default="/var/lib/mysql"
	OCF_RESKEY_user_default="mysql"
	OCF_RESKEY_group_default="mysql"
	OCF_RESKEY_log_default="/var/log/mysqld.log"
	OCF_RESKEY_pid_default="/var/run/mysql/mysqld.pid"
	OCF_RESKEY_socket_default="/var/lib/mysql/mysql.sock"
fi
OCF_RESKEY_client_binary_default="mysql"
OCF_RESKEY_test_user_default="root"
OCF_RESKEY_test_table_default="mysql.user"
OCF_RESKEY_test_passwd_default=""
OCF_RESKEY_enable_creation_default=0
OCF_RESKEY_additional_parameters_default=""
OCF_RESKEY_replication_user_default="root"
OCF_RESKEY_replication_passwd_default=""
OCF_RESKEY_replication_port_default="3306"
OCF_RESKEY_max_slave_lag_default="3600"
OCF_RESKEY_evict_outdated_slaves_default="false"
OCF_RESKEY_reader_attribute_default="readable"

: ${OCF_RESKEY_binary=${OCF_RESKEY_binary_default}}
MYSQL_BINDIR=`dirname ${OCF_RESKEY_binary}`

: ${OCF_RESKEY_client_binary=${OCF_RESKEY_client_binary_default}}

: ${OCF_RESKEY_config=${OCF_RESKEY_config_default}}
: ${OCF_RESKEY_datadir=${OCF_RESKEY_datadir_default}}

: ${OCF_RESKEY_user=${OCF_RESKEY_user_default}}
: ${OCF_RESKEY_group=${OCF_RESKEY_group_default}}

: ${OCF_RESKEY_log=${OCF_RESKEY_log_default}}
: ${OCF_RESKEY_pid=${OCF_RESKEY_pid_default}}
: ${OCF_RESKEY_socket=${OCF_RESKEY_socket_default}}

: ${OCF_RESKEY_test_user=${OCF_RESKEY_test_user_default}}
: ${OCF_RESKEY_test_table=${OCF_RESKEY_test_table_default}}
: ${OCF_RESKEY_test_passwd=${OCF_RESKEY_test_passwd_default}}

: ${OCF_RESKEY_enable_creation=${OCF_RESKEY_enable_creation_default}}
: ${OCF_RESKEY_additional_parameters=${OCF_RESKEY_additional_parameters_default}}

: ${OCF_RESKEY_replication_user=${OCF_RESKEY_replication_user_default}}
: ${OCF_RESKEY_replication_passwd=${OCF_RESKEY_replication_passwd_default}}
: ${OCF_RESKEY_replication_port=${OCF_RESKEY_replication_port_default}}

: ${OCF_RESKEY_max_slave_lag=${OCF_RESKEY_max_slave_lag_default}}
: ${OCF_RESKEY_evict_outdated_slaves=${OCF_RESKEY_evict_outdated_slaves_default}}

: ${OCF_RESKEY_reader_attribute=${OCF_RESKEY_reader_attribute_default}}

#######################################################################
# Convenience variables

MYSQL=$OCF_RESKEY_client_binary
MYSQL_OPTIONS_LOCAL="-S $OCF_RESKEY_socket"
MYSQL_OPTIONS_REPL="$MYSQL_OPTIONS_LOCAL --user=$OCF_RESKEY_replication_user --password=$OCF_RESKEY_replication_passwd"
MYSQL_OPTIONS_TEST="$MYSQL_OPTIONS_LOCAL --user=$OCF_RESKEY_test_user --password=$OCF_RESKEY_test_passwd"
MYSQL_TOO_MANY_CONN_ERR=1040

CRM_MASTER="${HA_SBIN_DIR}/crm_master -l reboot "
NODENAME=$(ocf_local_nodename)
CRM_ATTR="${HA_SBIN_DIR}/crm_attribute -N $NODENAME "
INSTANCE_ATTR_NAME=`echo ${OCF_RESOURCE_INSTANCE}| awk -F : '{print $1}'`
CRM_ATTR_REPL_INFO="${HA_SBIN_DIR}/crm_attribute --type crm_config --name ${INSTANCE_ATTR_NAME}_REPL_INFO -s mysql_replication"

#######################################################################

mysql_common_validate()
{

    if ! have_binary "$OCF_RESKEY_binary"; then
        ocf_exit_reason "Setup problem: couldn't find command: $OCF_RESKEY_binary"
        return $OCF_ERR_INSTALLED;
    fi

    if ! have_binary "$OCF_RESKEY_client_binary"; then
        ocf_exit_reason "Setup problem: couldn't find command: $OCF_RESKEY_client_binary"
        return $OCF_ERR_INSTALLED;
    fi

    if [ ! -f $OCF_RESKEY_config ]; then
        ocf_exit_reason "Config $OCF_RESKEY_config doesn't exist";
        return $OCF_ERR_INSTALLED;
    fi

    if [ ! -d $OCF_RESKEY_datadir ]; then
        ocf_exit_reason "Datadir $OCF_RESKEY_datadir doesn't exist";
        return $OCF_ERR_INSTALLED;
    fi

    getent passwd $OCF_RESKEY_user >/dev/null 2>&1
    if [ ! $? -eq 0 ]; then
        ocf_exit_reason "User $OCF_RESKEY_user doesn't exit";
        return $OCF_ERR_INSTALLED;
    fi

    getent group $OCF_RESKEY_group >/dev/null 2>&1
    if [ ! $? -eq 0 ]; then
        ocf_exit_reason "Group $OCF_RESKEY_group doesn't exist";
        return $OCF_ERR_INSTALLED;
    fi

    return $OCF_SUCCESS
}

mysql_common_check_pid() {
    local pid=$1

    if [ -d /proc -a -d /proc/1 ]; then
        [ "u$pid" != "u" -a -d /proc/$pid ]
    else
        kill -s 0 $pid >/dev/null 2>&1
    fi
    return $?
}

mysql_common_status() {
    local loglevel=$1
    local pid=$2
    if [ -z "$pid" ]; then
        if [ ! -e $OCF_RESKEY_pid ]; then
            ocf_log $loglevel "MySQL is not running"
            return $OCF_NOT_RUNNING;
        fi

        pid=`cat $OCF_RESKEY_pid`;
    fi

    mysql_common_check_pid $pid


    if [ $? -eq 0 ]; then
        return $OCF_SUCCESS;
    else
        if [ -e $OCF_RESKEY_pid ]; then
            ocf_log $loglevel "MySQL not running: removing old PID file"
            rm -f $OCF_RESKEY_pid
        fi
        return $OCF_NOT_RUNNING;
    fi
}

mysql_common_prepare_dirs()
{
    local rc

    touch $OCF_RESKEY_log
    chown $OCF_RESKEY_user:$OCF_RESKEY_group $OCF_RESKEY_log
    chmod 0640 $OCF_RESKEY_log
    [ -x /sbin/restorecon ] && /sbin/restorecon $OCF_RESKEY_log

    if ocf_is_true "$OCF_RESKEY_enable_creation" && [ ! -d $OCF_RESKEY_datadir/mysql ] ; then
        ocf_log info "Initializing MySQL database: "
        $MYSQL_BINDIR/mysql_install_db --datadir=$OCF_RESKEY_datadir
        rc=$?
        if [ $rc -ne 0 ] ; then
            ocf_exit_reason "Initialization failed: $rc";
            exit $OCF_ERR_GENERIC
        fi
        chown -R $OCF_RESKEY_user:$OCF_RESKEY_group $OCF_RESKEY_datadir
    fi

    pid_dir=`dirname $OCF_RESKEY_pid`
    if [ ! -d $pid_dir ] ; then
        ocf_log info "Creating PID dir: $pid_dir"
        mkdir -p $pid_dir
        chown $OCF_RESKEY_user:$OCF_RESKEY_group $pid_dir
    fi

    socket_dir=`dirname $OCF_RESKEY_socket`
    if [ ! -d $socket_dir ] ; then
        ocf_log info "Creating socket dir: $socket_dir"
        mkdir -p $socket_dir
        chown $OCF_RESKEY_user:$OCF_RESKEY_group $socket_dir
    fi

    # Regardless of whether we just created the directory or it
    # already existed, check whether it is writable by the configured
    # user
    for dir in $pid_dir $socket_dir; do
        if ! su -s /bin/sh - $OCF_RESKEY_user -c "test -w $dir"; then
            ocf_exit_reason "Directory $dir is not writable by $OCF_RESKEY_user"
            exit $OCF_ERR_PERM;
        fi
    done
}

mysql_common_start()
{
    local mysql_extra_params="$1"
    local pid

    ${OCF_RESKEY_binary} --defaults-file=$OCF_RESKEY_config \
    --pid-file=$OCF_RESKEY_pid \
    --socket=$OCF_RESKEY_socket \
    --datadir=$OCF_RESKEY_datadir \
    --log-error=$OCF_RESKEY_log \
    --user=$OCF_RESKEY_user $OCF_RESKEY_additional_parameters \
    $mysql_extra_params >/dev/null 2>&1 &
    pid=$!

    # Spin waiting for the server to come up.
    # Let the CRM/LRM time us out if required.
    start_wait=1
    while [ $start_wait = 1 ]; do
        if ! ps $pid > /dev/null 2>&1; then
            wait $pid
            ocf_exit_reason "MySQL server failed to start (pid=$pid) (rc=$?), please check your installation"
            return $OCF_ERR_GENERIC
        fi
        mysql_common_status info
        rc=$?
        if [ $rc = $OCF_SUCCESS ]; then
            start_wait=0
        elif [ $rc != $OCF_NOT_RUNNING ]; then
            ocf_log info "MySQL start failed: $rc"
            return $rc
        fi
        sleep 2
    done

    return $OCF_SUCCESS
}

mysql_common_stop()
{
    local pid
    local rc

    if [ ! -f $OCF_RESKEY_pid ]; then
        ocf_log info "MySQL is not running"
        return $OCF_SUCCESS
    fi

    pid=`cat $OCF_RESKEY_pid 2> /dev/null `

    mysql_common_check_pid $pid
    if [ $? -ne 0 ]; then
        rm -f $OCF_RESKEY_pid
        ocf_log info "MySQL is already stopped"
        return $OCF_SUCCESS;
    fi

    /bin/kill $pid > /dev/null
    rc=$?
    if [ $rc != 0 ]; then
        ocf_exit_reason "MySQL couldn't be stopped"
        return $OCF_ERR_GENERIC
    fi
    # stop waiting
    shutdown_timeout=15
    if [ -n "$OCF_RESKEY_CRM_meta_timeout" ]; then
        shutdown_timeout=$((($OCF_RESKEY_CRM_meta_timeout/1000)-5))
    fi
    count=0
    while [ $count -lt $shutdown_timeout ]
    do
        mysql_common_status info $pid
        rc=$?
        if [ $rc = $OCF_NOT_RUNNING ]; then
            break
        fi
        count=`expr $count + 1`
        sleep 1
        ocf_log debug "MySQL still hasn't stopped yet. Waiting..."
    done

    mysql_common_status info $pid
    if [ $? != $OCF_NOT_RUNNING ]; then
        ocf_log info "MySQL failed to stop after ${shutdown_timeout}s using SIGTERM. Trying SIGKILL..."
        /bin/kill -KILL $pid > /dev/null
    fi

    ocf_log info "MySQL stopped";
    rm -f /var/lock/subsys/mysqld
    rm -f $OCF_RESKEY_socket
    return $OCF_SUCCESS

}
