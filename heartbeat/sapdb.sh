#
# sapdb.sh - for systems having SAPHostAgent installed
# (sourced by SAPDatabase)
#
# Description:	This code is separated from the SAPDatabase agent to
#               introduce new functions for systems which having
#               SAPHostAgent installed.
#               Someday it might be merged back into SAPDatabase agein.
#
# Author:       Alexander Krauth, September 2010
# Support:      linux@sap.com
# License:      GNU General Public License (GPL)
# Copyright:    (c) 2010, 2012 Alexander Krauth
#


#
# check_saphostexec : Before using saphostctrl we make sure that the saphostexec is running on the current node.
#
check_saphostexec() {
  restart=0
  runninginst=""
  chkrc=$OCF_SUCCESS

  output=`$SAPHOSTEXEC -status 2>&1`
  if [ $? -ne 0 ]
  then
    ocf_log warn "saphostexec is not running on node `hostname`, it will be started now"
    output=`$SAPHOSTEXEC -restart 2>&1`
    
    # now make sure the daemon has been started and is able to respond
    srvrc=1
    while [ $srvrc -ne 0 -a `pgrep -f "$SAPHOSTEXEC" | wc -l` -gt 0 ]
    do
      sleep 1
      output=`$SAPHOSTEXEC -status 2>&1`
      srvrc=$?
    done

    if [ $srvrc -eq 0 ]
    then
      ocf_log info "saphostexec on node `hostname` was restarted !"
      chkrc=$OCF_SUCCESS
    else
      ocf_log error "saphostexec on node `hostname` could not be started! - $output"
      chkrc=$OCF_ERR_GENERIC
    fi
  fi
  
  return $chkrc
}


#
# sapdatabase_start : Start the SAP database
#
sapdatabase_start() {

  check_saphostexec
  rc=$?
  
  if [ $rc -eq $OCF_SUCCESS ]
  then
    sapuserexit PRE_START_USEREXIT "$OCF_RESKEY_PRE_START_USEREXIT"

    DBINST=""
    if [ -n "$OCF_RESKEY_DBINSTANCE" ]
    then
      DBINST="-dbinstance $OCF_RESKEY_DBINSTANCE "
    fi
    FORCE=""
    if ocf_is_true $OCF_RESKEY_AUTOMATIC_RECOVER
    then
      FORCE="-force"
    fi
    output=`$SAPHOSTCTRL -function StartDatabase -dbname $SID -dbtype $DBTYPE $DBINST $FORCE -service`

    sapdatabase_monitor 1
    rc=$?

    if [ $rc -eq 0 ]
    then
      ocf_log info "SAP database $SID started: $output"
      rc=$OCF_SUCCESS
    
      sapuserexit POST_START_USEREXIT "$OCF_RESKEY_POST_START_USEREXIT"
    else
      ocf_log err "SAP database $SID start failed: $output"
      rc=$OCF_ERR_GENERIC
    fi
  fi
  
  return $rc
}

#
# sapdatabase_stop: Stop the SAP database
#
sapdatabase_stop() {

  check_saphostexec
  rc=$?
  
  if [ $rc -eq $OCF_SUCCESS ]
  then
    sapuserexit PRE_STOP_USEREXIT "$OCF_RESKEY_PRE_STOP_USEREXIT"

    DBINST=""
    if [ -n "$OCF_RESKEY_DBINSTANCE" ]
    then
      DBINST="-dbinstance $OCF_RESKEY_DBINSTANCE "
    fi
    output=`$SAPHOSTCTRL -function StopDatabase -dbname $SID -dbtype $DBTYPE $DBINST -force -service`

    if [ $? -eq 0 ]
    then
      ocf_log info "SAP database $SID stopped: $output"
      rc=$OCF_SUCCESS
    else
      ocf_log err "SAP database $SID stop failed: $output"
      rc=$OCF_ERR_GENERIC
    fi
  fi

  sapuserexit POST_STOP_USEREXIT "$OCF_RESKEY_POST_STOP_USEREXIT"
  
  return $rc
}


#
# sapdatabase_monitor: Can the given database instance do anything useful?
#
sapdatabase_monitor() {
  strict=$1
  rc=$OCF_SUCCESS

  if ! ocf_is_true $strict
  then
    sapdatabase_status
    rc=$?
  else
    check_saphostexec
    rc=$?
  
    if [ $rc -eq $OCF_SUCCESS ]
    then
      count=0
      
      DBINST=""
      if [ -n "$OCF_RESKEY_DBINSTANCE" ]
      then
        DBINST="-dbinstance $OCF_RESKEY_DBINSTANCE "
      fi
      output=`$SAPHOSTCTRL -function GetDatabaseStatus -dbname $SID -dbtype $DBTYPE $DBINST`

      # we have to parse the output, because the returncode doesn't tell anything about the instance status
      for SERVICE in `echo "$output" | grep -i 'Component[ ]*Name *[:=] [A-Za-z][A-Za-z0-9_]* (' | sed 's/^.*Component[ ]*Name *[:=] *\([A-Za-z][A-Za-z0-9_]*\).*$/\1/i'`
      do 
        COLOR=`echo "$output" | grep -i "Component[ ]*Name *[:=] *$SERVICE (" | sed 's/^.*Status *[:=] *\([A-Za-z][A-Za-z0-9_]*\).*$/\1/i'`
        STATE=0

        case $COLOR in
          Running)       STATE=$OCF_SUCCESS;;
          *)             STATE=$OCF_NOT_RUNNING;;
        esac 

        SEARCH=`echo "$OCF_RESKEY_MONITOR_SERVICES" | sed 's/\+/\\\+/g' | sed 's/\./\\\./g'`
        if [ `echo "$SERVICE" | egrep -c "$SEARCH"` -eq 1 ]
        then
            if [ $STATE -eq $OCF_NOT_RUNNING ]
            then
              ocf_log err "SAP database service $SERVICE is not running with status $COLOR !"
              rc=$STATE
            fi
            count=1
        fi
      done

      if [ $count -eq 0 -a $rc -eq $OCF_SUCCESS ]
      then
        ocf_log err "The resource does not run any services which this RA could monitor!"
        rc=$OCF_ERR_ARGS
      fi
      
      if [ $rc -ne $OCF_SUCCESS ]
      then
        ocf_log err "The SAP database $SID ist not running: $output"
      fi
    fi
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

  cnt=`ps -u $SUSER -o command 2> /dev/null | grep -c $SEARCH`
  [ $cnt -ge $SNUM ] && return $OCF_SUCCESS
  return $OCF_NOT_RUNNING
}


#
# sapdatabase_recover:
#
sapdatabase_recover() {
  OCF_RESKEY_AUTOMATIC_RECOVER=1
  sapdatabase_stop
  sapdatabase_start
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
OCF_RESKEY_AUTOMATIC_RECOVER_default=0
: ${OCF_RESKEY_AUTOMATIC_RECOVER=${OCF_RESKEY_AUTOMATIC_RECOVER_default}}

if [ -z "$OCF_RESKEY_MONITOR_SERVICES" ]
then
  case $DBTYPE in
    ORA) export OCF_RESKEY_MONITOR_SERVICES="Instance|Database|Listener"
         ;;
    ADA) export OCF_RESKEY_MONITOR_SERVICES="Database"
         ;;
    DB6) db2sid="db2`echo $SID | tr '[:upper:]' '[:lower:]'`"
         export OCF_RESKEY_MONITOR_SERVICES="${SID}|${db2sid}"
         ;;
  esac
fi
}