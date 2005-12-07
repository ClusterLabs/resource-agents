#!/bin/bash

usage()
{
    echo "Usage:"
    echo "       $0 <host> <pretty|raw>"
}
if [ $# -ne 2 ]; then
    usage
    exit 1
fi

HOSTNAME=$1
FORMAT=$2

if [ $FORMAT = "raw" ]; then
    snmpwalk -v 2c -c cluster $HOSTNAME REDHAT-CLUSTER-MIB::RedHatCluster
    exit $?
fi

if [ $FORMAT = "pretty" ]; then
    snmpwalk -v 2c -c cluster $HOSTNAME REDHAT-CLUSTER-MIB::rhcMIBInfo
    snmpwalk -t 5 -v 2c -c cluster $HOSTNAME REDHAT-CLUSTER-MIB::rhcCluster
    snmptable -t 5 -v 2c -c cluster $HOSTNAME REDHAT-CLUSTER-MIB::rhcNodesTable
    snmptable -t 5 -v 2c -c cluster $HOSTNAME REDHAT-CLUSTER-MIB::rhcServicesTable
    exit $?
fi

usage
exit 1

