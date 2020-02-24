Setting up the oraasmdg resource agent
======================================

This resource agent requires Oracle GI installed and working, in a multi-node cluster.
Diskgroups should be defined and created through the GI set of tools (asmca/asmcmd/sqlplus).
The diskgroup should be able to mount. However - due to the way Oracle GI works,
the Diskgroup might not mount automatically. For RAC databases, this is not a problem, 
because oracle db resources in GI register their ASM Diskgroup as requirement.
When PCS manages Oracle instances, this is not the case.

The agent is rather simple, however, it assumes that the administrator is familiar with
Oracle ASM Diskgroups, and is capable of managing them through Oracle ASM tools.


Part 1: Pacemaker Setup
-----------------------

This is pretty straightforward. Note that in a cluster, ASM Diskgroup is not a unique resource, 
but it is required to mount on all nodes. This implies that the resource is to be created as a clone.
Example of using pcs.

```
# Create the oraasmdg resource
pcs resource create datadg oraasmdg \
   user="oracle" \
   home="/oracle/app/19.3.0/grid" \
   diskgroup="DATADG" \
   clone

```

You should now have a running ASM Diskgroup in the cluster:
```
pcs status

...
 Master/Slave Set: msMariadb [mariadb_server]
      Masters: [ node1 ]
      Slaves: [ node2 node3 ]
...
```

If you want to create a group of resources, you will have to create them uncloned, and then clone the whole group:
```
# Create a non-cloned resournce
pcs resource create datadg oraasmdg \
   user="oracle" \
   home="/oracle/app/19.3.0/grid" \
   diskgroup="DATADG" --group oracle-diskgroups

pcs resource create archivedg oraasmdg \
   user="oracle" \
   home="/oracle/app/19.3.0/grid" \
   diskgroup="ARCHIVEDG" --group oracle-diskgroups

pcs resource clone oracle-diskgroups
```


Part 2: Why was it written
--------------------------

This agent was written after the existing oraasm agent failed to work for me. The original agent 
(from which this work is derived, to a certain degree) just started Oracle GI process (ohasd), and left it at that.
This is not enough for a multi-node cluster. Oracle GI takes time to start, and during that time, ASM Diskgroups are
not available. This leads to a cluster failure, or taking down Oracle GI at all (running stop operations on ohasd).

The main purpose of this agent is to make sure that when both cluster infrastructures co-exist, PCS resources will
wait for ASM Diskgroup to finish starting up, without using sensless timeouts. The main purpose of this agent is to
wait for ASM Diskgroup, and only if it is still down, ask Oracle GI nicely to mount it.


Part 3: How does it work
------------------------

This agent performs the following tasks:
1. Start: It runs a preliminary check of Oracle CRS agent using the command:
```
${OCF_RESKEY_home}/crsctl status res -t
```
This is enough to trigger a simple test. If the return value is $OCF_SUCCESS, then the GI is running. If not, we 
need to wait for it to start, so we rerun this query every second until it is up, or until resource agent start timeout reached.
If this query succeeds, we run monitoring of the ASM diskgroup status *on this node* like this:
```
${OCF_RESKEY_home}/crsctl status res ora.${OCF_RESKEY_diskgroup}.dg | grep -q `hostname -s`
```
If this command fails to run, we will attempt to run it for 50 seconds, every 5 seconds. If by then the resource is not up, 
then we need to start it manually. If it is up, we just return $OCF_SUCCESS, and we are done here.
To start the resource, we call the 'srvctl' command, like this:
```
${OCF_RESKEY_home}/start diskgroup -diskgroup ${OCF_RESKEY_diskgroup} -node `hostname -s`
```
We run it as the grid user. If we fail to run the command, we return $OCF_ERR_GENERIC. If we succeeded, we check again, using
the command mentioned above that the diskgroup is mounted on this node, after 5 second wait.

We do not perform any action when stop is called. Since it is managed by Oracle GI, and other tools might run on this ASM 
Diskgroup. We just leave it be, assuming Oracle GI can handle its own resources.

Part 4: Timing and timeouts
---------------------------

Agent start timeout defaults to 180 seconds, because Oracle GI takes some time to start and mount. 

Agent monitor/status timeout defaults to 90 seconds. Normally - 'crsctl status res' command responds very fast, however, during
cluster node unplanned failure, CRS takes a wile to recover and respond to query commands. During this time, we need to keep PCS
working, and not responding in timeouts.
