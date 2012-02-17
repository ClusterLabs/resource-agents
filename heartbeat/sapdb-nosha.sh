#
# sapdatabase-nosha - for systems not having SAPHostAgent installed
# (sourced by SAPDatabase)
#
# Description:	this code is separated from the SAPDatabase agent to
#               be downward compatible and support systems which do
#               not have SAPHostAgent installed.
#               It will be removed in a later release completely. 
#
# Author:       Alexander Krauth, October 2006
# Support:      linux@sap.com
# License:      GNU General Public License (GPL)
# Copyright:    (c) 2006, 2007 Alexander Krauth
#


trap_handler() {
  rm -f $TEMPFILE
  exit $OCF_ERR_GENERIC
}


#
# listener_start: Start the given listener
#
listener_start() {
  local orasid="ora`echo $SID | tr '[:upper:]' '[:lower:]'`"
  local lrc=$OCF_SUCCESS
  local output
  output=`echo "lsnrctl start $NETSERVICENAME" | su - $orasid 2>&1`
  if [ $? -eq 0 ]
  then
    ocf_log info "Oracle Listener $NETSERVICENAME started: $output"
    lrc=$OCF_SUCCESS
  else
    ocf_log err "Oracle Listener $NETSERVICENAME start failed: $output"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# listener_stop: Stop the given listener
#
listener_stop() {
  local orasid="ora`echo $SID | tr '[:upper:]' '[:lower:]'`"
  local lrc=$OCF_SUCCESS
  if
      listener_status
  then
      : listener is running, trying to stop it later...
  else
      return $OCF_SUCCESS
  fi
  local output
  output=`echo "lsnrctl stop $NETSERVICENAME" | su - $orasid 2>&1`
  if [ $? -eq 0 ]
  then
    ocf_log info "Oracle Listener $NETSERVICENAME stopped: $output"
  else
    ocf_log err "Oracle Listener $NETSERVICENAME stop failed: $output"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# listener_status: is the given listener running?
#
listener_status() {
  local lrc=$OCF_SUCCESS
  local orasid="ora`echo $SID | tr '[:upper:]' '[:lower:]'`"
  # Note: ps cuts off it's output at column $COLUMNS, so "ps -ef" can not be used here
  # as the output might be to long.
  local cnt=`ps efo args --user $orasid | grep $NETSERVICENAME | grep -c tnslsnr`
  if [ $cnt -eq 1 ]
  then
    lrc=$OCF_SUCCESS
  else
    ocf_log info "listener process not running for $NETSERVICENAME for $SID"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# x_server_start: Start the given x_server
#
x_server_start() {
  local rc=$OCF_SUCCESS
  local output
  output=`echo "x_server start" | su - $sidadm 2>&1`
  if [ $? -eq 0 ]
  then
    ocf_log info "MaxDB x_server start: $output"
    lrc=$OCF_SUCCESS
  else
    ocf_log err "MaxDB x_server start failed: $output"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# x_server_stop: Stop the x_server
#
x_server_stop() {
  local lrc=$OCF_SUCCESS
  local output
  output=`echo "x_server stop" | su - $sidadm 2>&1`
  if [ $? -eq 0 ]
  then
    ocf_log info "MaxDB x_server stop: $output"
  else
    ocf_log err "MaxDB x_server stop failed: $output"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# x_server_status: is the x_server running?
#
x_server_status() {
  local lrc=$OCF_SUCCESS
  local sdbuser=`grep "^SdbOwner" /etc/opt/sdb | awk -F'=' '{print $2}'`
  # Note: ps cuts off it's output at column $COLUMNS, so "ps -ef" can not be used here
  # as the output might be to long.
  local cnt=`ps efo args --user $sdbuser | grep -c vserver`
  if [ $cnt -ge 1 ]
  then
    lrc=$OCF_SUCCESS
  else
    ocf_log info "x_server process not running"
    lrc=$OCF_ERR_GENERIC
  fi
  return $lrc
}

#
# oracle_stop: Stop the Oracle database without any condition
#
oracle_stop() {
echo '#!/bin/sh
LOG=$HOME/stopdb.log
date > $LOG

if [ -x "${ORACLE_HOME}/bin/sqlplus" ]
then
    SRVMGRDBA_EXE="${ORACLE_HOME}/bin/sqlplus"
else
   echo "Can not find executable sqlplus" >> $LOG
   exit 1
fi

$SRVMGRDBA_EXE /NOLOG >> $LOG << !
connect / as sysdba
shutdown immediate
exit
!
rc=$?
cat $LOG
exit $rc' > $TEMPFILE

chmod 700 $TEMPFILE
chown $sidadm $TEMPFILE

su - $sidadm -c $TEMPFILE
retcode=$?
rm -f $TEMPFILE

if [ $retcode -eq 0 ]; then
  sapdatabase_status
  if [ $? -ne $OCF_NOT_RUNNING ]; then
    retcode=1
  fi
fi

return $retcode
}

#
# maxdb_stop: Stop the MaxDB database without any condition
#
maxdb_stop() {

# x_Server must be running to stop database
x_server_status
if [ $? -ne $OCF_SUCCESS ]; then x_server_start; fi

if [ $DBJ2EE_ONLY -eq 1 ]; then
   userkey=c_J2EE
else
   userkey=c
fi

echo "#!/bin/sh
LOG=\$HOME/stopdb.log
date > \$LOG
echo \"Stop database with xuserkey >$userkey<\" >> \$LOG
dbmcli -U ${userkey} db_offline >> \$LOG 2>&1
exit \$?" > $TEMPFILE

chmod 700 $TEMPFILE
chown $sidadm $TEMPFILE

su - $sidadm -c $TEMPFILE
retcode=$?
rm -f $TEMPFILE

if [ $retcode -eq 0 ]; then
  sapdatabase_status
  if [ $? -ne $OCF_NOT_RUNNING ]; then
    retcode=1
  fi
fi

return $retcode
}

#
# db6udb_stop: Stop the DB2/UDB database without any condition
#
db6udb_stop() {
echo '#!/bin/sh
LOG=$HOME/stopdb.log
date > $LOG
echo "Shut down the database" >> $LOG
$INSTHOME/sqllib/bin/db2 deactivate database $DB2DBDFT |tee -a $LOG  2>&1
$INSTHOME/sqllib/adm/db2stop force |tee -a $LOG  2>&1
exit $?' > $TEMPFILE

chmod 700 $TEMPFILE
chown $sidadm $TEMPFILE

su - $sidadm -c $TEMPFILE
retcode=$?
rm -f $TEMPFILE

if [ $retcode -eq 0 ]; then
  sapdatabase_status
  if [ $? -ne $OCF_NOT_RUNNING ]; then
    retcode=1
  fi
fi

return $retcode
}

#
# oracle_recover: try to clean up oracle after a crash
#
oracle_recover() {
echo '#!/bin/sh
LOG=$HOME/recover.log
date > $LOG
echo "Logfile written by heartbeat SAPDatabase resource agent" >> $LOG

if [ -x "${ORACLE_HOME}/bin/sqlplus" ]
then
    SRVMGRDBA_EXE="${ORACLE_HOME}/bin/sqlplus"
else
   echo "Can not find executable sqlplus" >> $LOG
   exit 1
fi

$SRVMGRDBA_EXE /NOLOG >> $LOG << !
connect / as sysdba
shutdown abort
startup mount
alter database end backup;
alter database open;
exit
!
rc=$?
cat $LOG
exit $rc' > $TEMPFILE

  chmod 700 $TEMPFILE
  chown $sidadm $TEMPFILE

  su - $sidadm -c $TEMPFILE
  retcode=$?
  rm -f $TEMPFILE

  return $retcode
}

#
# maxdb_recover: try to clean up MaxDB after a crash
#
maxdb_recover() {
  # x_Server must be running to stop database
  x_server_status
  if [ $? -ne $OCF_SUCCESS ]; then x_server_start; fi

  if [ $DBJ2EE_ONLY -eq 1 ]; then
     userkey=c_J2EE
  else
     userkey=c
  fi

echo "#!/bin/sh
LOG=\$HOME/recover.log
date > \$LOG
echo \"Logfile written by heartbeat SAPDatabase resource agent\" >> \$LOG
echo \"Cleanup database with xuserkey >$userkey<\" >> \$LOG
echo \"db_stop\" >> \$LOG 2>&1
dbmcli -U ${userkey} db_stop >> \$LOG 2>&1
echo \"db_clear\" >> \$LOG 2>&1
dbmcli -U ${userkey} db_clear >> \$LOG 2>&1
echo \"db_online\" >> \$LOG 2>&1
dbmcli -U ${userkey} db_online >> \$LOG 2>&1
rc=\$?
cat \$LOG
exit \$rc" > $TEMPFILE

  chmod 700 $TEMPFILE
  chown $sidadm $TEMPFILE

  su - $sidadm -c $TEMPFILE
  retcode=$?
  rm -f $TEMPFILE

  return $retcode
}

#
# db6udb_recover: try to recover DB/2 after a crash
#
db6udb_recover() {
  db2sid="db2`echo $SID | tr '[:upper:]' '[:lower:]'`"

echo '#!/bin/sh
LOG=$HOME/recover.log
date > $LOG
echo "Logfile written by heartbeat SAPDatabase resource agent" >> $LOG
$INSTHOME/sqllib/bin/db2_kill >> $LOG  2>&1
$INSTHOME/sqllib/adm/db2start >> $LOG  2>&1
$INSTHOME/sqllib/bin/db2 activate database $DB2DBDFT >> $LOG  2>&1
rc=$?
cat $LOG
exit $rc' > $TEMPFILE

  chmod 700 $TEMPFILE
  chown $db2sid $TEMPFILE

  su - $db2sid -c $TEMPFILE
  retcode=$?
  rm -f $TEMPFILE

  return $retcode
}


#
# sapdatabase_start : Start the SAP database
#
sapdatabase_start() {
  sapuserexit PRE_START_USEREXIT "$OCF_RESKEY_PRE_START_USEREXIT"

  case $DBTYPE in
    ADA) x_server_start
         ;;
    ORA) listener_start
         ;;
  esac

  output=`su - $sidadm -c $SAPSTARTDB`
  rc=$?

  if [ $DBJ2EE_ONLY -eq 1 ]
  then
    sapdatabase_monitor 1
    rc=$?
  fi

  if [ $rc -ne 0 -a $OCF_RESKEY_AUTOMATIC_RECOVER -eq 1 ]
  then
    ocf_log warn "SAP database $SID start failed: $output"
    ocf_log warn "Try to recover database $SID"

    output=''
    sapdatabase_recover
    rc=$?
  fi

  if [ $rc -eq 0 ]
  then
    ocf_log info "SAP database $SID started: $output"
    rc=$OCF_SUCCESS
    sapuserexit POST_START_USEREXIT "$OCF_RESKEY_POST_START_USEREXIT"
  else
    ocf_log err "SAP database $SID start failed: $output"
    rc=$OCF_ERR_GENERIC
  fi

  return $rc
}

#
# sapdatabase_stop: Stop the SAP database
#
sapdatabase_stop() {

  sapuserexit PRE_STOP_USEREXIT "$OCF_RESKEY_PRE_STOP_USEREXIT"

  # use of the stopdb kernel script is not possible, because there are to may checks in that
  # script. We want to stop the database regardless of anything.
  #output=`su - $sidadm -c $SAPSTOPDB`

  case $DBTYPE in
    ORA) output=`oracle_stop`
         ;;
    ADA) output=`maxdb_stop`
         ;;
    DB6) output=`db6udb_stop`
         ;;
  esac

  if [ $? -eq 0 ]
  then
    ocf_log info "SAP database $SID stopped: $output"
    rc=$OCF_SUCCESS
  else
    ocf_log err "SAP database $SID stop failed: $output"
    rc=$OCF_ERR_GENERIC
  fi

  case $DBTYPE in
    ORA) listener_stop
         ;;
    ADA) x_server_stop
         ;;
  esac

  sapuserexit POST_STOP_USEREXIT "$OCF_RESKEY_POST_STOP_USEREXIT"

  return $rc
}


#
# sapdatabase_monitor: Can the given database instance do anything useful?
#
sapdatabase_monitor() {
  strict=$1

  sapdatabase_status
  rc=$?
  if [ $rc -ne $OCF_SUCCESS ]; then
    return $rc
  fi

  case $DBTYPE in
    ADA) x_server_status 
         if [ $? -ne $OCF_SUCCESS ]; then x_server_start; fi
         ;;
    ORA) listener_status
         if [ $? -ne $OCF_SUCCESS ]; then listener_start; fi
         ;;
  esac

  if [ $strict -eq 0 ]
  then
    return $rc
  else
    if [ $DBJ2EE_ONLY -eq 0 ]
    then
      output=`echo "$SAPDBCONNECT -d -w /dev/null" | su $sidadm 2>&1`
      if [ $? -le 4 ]
      then
        rc=$OCF_SUCCESS
      else
        rc=$OCF_NOT_RUNNING
      fi
    else
      MYCP=""
      EXECMD=""

      # WebAS Java 6.40+7.00
      IAIK_JCE="$SECSTORE"/iaik_jce.jar
      IAIK_JCE_EXPORT="$SECSTORE"/iaik_jce_export.jar
      EXCEPTION="$BOOTSTRAP"/exception.jar
      LOGGING="$BOOTSTRAP"/logging.jar
      OPENSQLSTA="$BOOTSTRAP"/opensqlsta.jar
      TC_SEC_SECSTOREFS="$BOOTSTRAP"/tc_sec_secstorefs.jar
      JDDI="$BOOTSTRAP"/../server0/bin/ext/jdbdictionary/jddi.jar
      ANTLR="$BOOTSTRAP"/../server0/bin/ext/antlr/antlr.jar
      FRAME="$BOOTSTRAP"/../server0/bin/system/frame.jar
  
      # only start jdbcconnect when all jars available
      if [ -f "$EXCEPTION" -a -f "$LOGGING" -a -f "$OPENSQLSTA" -a -f "$TC_SEC_SECSTOREFS" -a -f "$JDDI" -a -f "$ANTLR" -a -f "$FRAME" -a -f "$SAPDBCONNECT" ]
      then
        MYCP=".:$FRAME:$ANTLR:$JDDI:$IAIK_JCE_EXPORT:$IAIK_JCE:$EXCEPTION:$LOGGING:$OPENSQLSTA:$TC_SEC_SECSTOREFS:$DB_JARS:$SAPDBCONNECT" 
        EXECMD="com.sap.inst.jdbc.connect.JdbcCon -sec $SID:$SID"
      else
      # WebAS Java 7.10
        LAUNCHER=${BOOTSTRAP}/sap.com~tc~bl~offline_launcher~impl.jar

        if [ -f "$DB_JARS" -a -f "$SAPDBCONNECT" -a -f "$LAUNCHER" ]
        then
          MYCP="$LAUNCHER"
          EXECMD="com.sap.engine.offline.OfflineToolStart com.sap.inst.jdbc.connect.JdbcCon ${SAPDBCONNECT}:${SECSTORE}:${DB_JARS}:${BOOTSTRAP} -sec $SID:$SID"
        fi
      fi

      if [ -n "$EXECMD" ]
      then
        output=`${JAVA_HOME}/bin/java -cp $MYCP $EXECMD 2> /dev/null`
        if [ $? -le 0 ]
        then
          rc=$OCF_SUCCESS
        else
          rc=$OCF_NOT_RUNNING
        fi
      else
        output="Cannot find all jar files needed for database monitoring."
        rc=$OCF_ERR_GENERIC
      fi
    fi
  fi

  if [ $rc -ne $OCF_SUCCESS ]
  then
    ocf_log err "The SAP database $SID ist not running: $output"
  fi
  return $rc
}


#
# sapdatabase_status: Are there any database processes on this host ?
#
sapdatabase_status() {
  case $DBTYPE in
    ADA) SEARCH="$SID/db/pgm/kernel"
         SUSER=`grep "^SdbOwner" /etc/opt/sdb | awk -F'=' '{print $2}'`
         SNUM=2
         ;;
    ORA) SEARCH="ora_[a-z][a-z][a-z][a-z]_"
         SUSER="ora`echo $SID | tr '[:upper:]' '[:lower:]'`"
         SNUM=4
         ;;
    DB6) SEARCH="db2[a-z][a-z][a-z]"
         SUSER="db2`echo $SID | tr '[:upper:]' '[:lower:]'`"
         SNUM=2
         ;;
  esac

  # Note: ps cuts off it's output at column $COLUMNS, so "ps -ef" can not be used here
  # as the output might be to long.
  cnt=`ps efo args --user $SUSER 2> /dev/null | grep -c "$SEARCH"`
  if [ $cnt -ge $SNUM ]
  then
    rc=$OCF_SUCCESS
  else
    # ocf_log info "Database Instance $SID is not running on `hostname`"
    rc=$OCF_NOT_RUNNING
  fi
  return $rc
}


#
# sapdatabase_recover:
#
sapdatabase_recover() {

  case $DBTYPE in
    ORA) recoutput=`oracle_recover`
         ;;
    ADA) recoutput=`maxdb_recover`
         ;;
    DB6) recoutput=`db6udb_recover`
         ;;
  esac

  sapdatabase_monitor 1
  retcode=$?

  if [ $retcode -eq $OCF_SUCCESS ]
  then
    ocf_log info "Recover of SAP database $SID was successful: $recoutput"
  else
    ocf_log err "Recover of SAP database $SID failed: $recoutput"
  fi

  return $retcode
}


#
# sapdatabase_validate: Check the symantic of the input parameters 
#
sapdatabase_validate() {
  rc=$OCF_SUCCESS
  if [ `echo "$SID" | grep -c '^[A-Z][A-Z0-9][A-Z0-9]$'` -ne 1 ]
  then
    ocf_log err "Parsing parameter SID: '$SID' is not a valid system ID!"
    rc=$OCF_ERR_ARGS
  fi

  case "$DBTYPE" in
   ORA|ADA|DB6) ;;
   *) ocf_log err "Parsing parameter DBTYPE: '$DBTYPE' is not a supported database type!"
      rc=$OCF_ERR_ARGS ;;
  esac

  return $rc
}


#
# sapdatabase_init: initialize global variables at the beginning
#
sapdatabase_init() {

ocf_log warn "Usage of SAPDatabase resource agent without SAPHostAgent is deprecated. Please read documentation of SAPDatabase resource agent and follow SAP note 1031096 for the installation of SAPHostAgent."

# optional OCF parameters, we try to guess which directories are correct
EXESTARTDB="startdb"
EXESTOPDB="stopdb"
EXEDBCONNECT="R3trans"
if [ -z "$OCF_RESKEY_DBJ2EE_ONLY" ]; then
  DBJ2EE_ONLY=0
else
  case "$OCF_RESKEY_DBJ2EE_ONLY" in
   1|true|TRUE|yes|YES) DBJ2EE_ONLY=1
                        EXESTARTDB="startj2eedb"
                        EXESTOPDB="stopj2eedb"
                        EXEDBCONNECT="jdbcconnect.jar"
                        ;;
   0|false|FALSE|no|NO) DBJ2EE_ONLY=0;;
   *) ocf_log err "Parsing parameter DBJ2EE_ONLY: '$DBJ2EE_ONLY' is not a boolean value!"
      exit $OCF_ERR_ARGS ;;
  esac
fi

if [ -z "$OCF_RESKEY_NETSERVICENAME" ]; then
  case "$DBTYPE" in
    ORA|ora) NETSERVICENAME="LISTENER";;
    *)       NETSERVICENAME="";;
  esac
else
  NETSERVICENAME="$OCF_RESKEY_NETSERVICENAME"
fi

if [ -z "$OCF_RESKEY_STRICT_MONITORING" ]; then
  OCF_RESKEY_STRICT_MONITORING=0
else
  case "$OCF_RESKEY_STRICT_MONITORING" in
   1|true|TRUE|yes|YES) OCF_RESKEY_STRICT_MONITORING=1;;
   0|false|FALSE|no|NO) OCF_RESKEY_STRICT_MONITORING=0;;
   *)  ocf_log err "Parsing parameter STRICT_MONITORING: '$OCF_RESKEY_STRICT_MONITORING' is not a boolean value!"
       exit $OCF_ERR_ARGS ;;
  esac
fi

PATHLIST="
$OCF_RESKEY_DIR_EXECUTABLE
/usr/sap/$SID/*/exe
/usr/sap/$SID/SYS/exe/run
/sapmnt/$SID/exe
"
DIR_EXECUTABLE=""
for EXEPATH in $PATHLIST
do
  if [ -x $EXEPATH/$EXESTARTDB -a -x $EXEPATH/$EXESTOPDB -a -x $EXEPATH/$EXEDBCONNECT ]
  then
    DIR_EXECUTABLE=$EXEPATH
    SAPSTARTDB=$EXEPATH/$EXESTARTDB
    SAPSTOPDB=$EXEPATH/$EXESTOPDB
    SAPDBCONNECT=$EXEPATH/$EXEDBCONNECT
    break
  fi
done
if [ -z "$DIR_EXECUTABLE" ]
then
  ocf_log warn "Cannot find $EXESTARTDB,$EXESTOPDB and $EXEDBCONNECT executable, please set DIR_EXECUTABLE parameter!"
  exit $OCF_NOT_RUNNING
fi

if [ $DBJ2EE_ONLY -eq 1 ]
then
  if [ -n "$OCF_RESKEY_DIR_BOOTSTRAP" ]
  then
    BOOTSTRAP="$OCF_RESKEY_DIR_BOOTSTRAP"
  else
    BOOTSTRAP=`ls -1d /usr/sap/$SID/*/j2ee/cluster/bootstrap | head -1`
  fi

  if [ -n "$OCF_RESKEY_DIR_SECSTORE" ]
  then
    SECSTORE="$OCF_RESKEY_DIR_SECSTORE"
  else
    SECSTORE=/usr/sap/$SID/SYS/global/security/lib/tools
  fi

  if [ -n "$OCF_RESKEY_JAVA_HOME" ]
  then
    JAVA_HOME="$OCF_RESKEY_JAVA_HOME"
    PATH=$JAVA_HOME/bin:$PATH
  else
    if [ -n "$JAVA_HOME" ]
    then
      PATH=$JAVA_HOME/bin:$PATH
    else
      ocf_log err "Cannot find JAVA_HOME directory, please set JAVA_HOME parameter!"
      exit $OCF_NOT_RUNNING
    fi
  fi

  if [ -n "$OCF_RESKEY_DB_JARS" ]
  then
    DB_JARS=$OCF_RESKEY_DB_JARS
  else
    if [ -f "$BOOTSTRAP"/bootstrap.properties ]; then
      DB_JARS=`cat $BOOTSTRAP/bootstrap.properties | grep -i rdbms.driverLocation | sed -e 's/\\\:/:/g' | awk -F= '{print $2}'`
    fi
  fi
fi

if [ -z "$OCF_RESKEY_AUTOMATIC_RECOVER" ]
then
  OCF_RESKEY_AUTOMATIC_RECOVER=0
else
  case "$OCF_RESKEY_AUTOMATIC_RECOVER" in
   1|true|TRUE|yes|YES) OCF_RESKEY_AUTOMATIC_RECOVER=1;;
   0|false|FALSE|no|NO) OCF_RESKEY_AUTOMATIC_RECOVER=0;;
  esac
fi

# as root user we need the library path to the SAP kernel to be able to call executables
if [ `echo $LD_LIBRARY_PATH | grep -c "^$DIR_EXECUTABLE\>"` -eq 0 ]; then
  LD_LIBRARY_PATH=$DIR_EXECUTABLE${LD_LIBRARY_PATH:+:}$LD_LIBRARY_PATH
  export LD_LIBRARY_PATH
fi
sidadm="`echo $SID | tr '[:upper:]' '[:lower:]'`adm"
}

# Set a tempfile and make sure to clean it up again
TEMPFILE="/tmp/SAPDatabase.$$.tmp"
trap trap_handler INT TERM