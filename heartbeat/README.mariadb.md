Setting up the MariaDB resource agent
=====================================

This resource agent requires corosync version >= 2 and mariadb version > 10.2 .

Before embarking on this quest one should read the MariaDB pages on replication
and global transaction IDs, GTID. This will greatly help in understanding what
is going on and why.

Replication: https://mariadb.com/kb/en/mariadb/setting-up-replication/
GTID: https://mariadb.com/kb/en/mariadb/gtid/
semi-sync: https://mariadb.com/kb/en/mariadb/semisynchronous-replication/

Some reading on failures under enhanced semi-sync can be found here:
https://jira.mariadb.org/browse/MDEV-162

Part 1: MariaDB Setup
---------------------

It is best to initialize your MariaDB and do a failover before trying to use
Pacemaker to manage MariaDB. This will both verify the MariaDB configuration
and help you understand what is going on.

###Configuration Files

In your MariaDB config file for the server on node 1, place the following
entry (replacing my_database and other names as needed):
```
[mariadb]
log-bin
server_id=1
log-basename=master
binlog_do_db=my_database
```

Then for each other node create the same entry, but increment the server_id.

###Replication User

Now create the replication user (be sure to change the password!):
```
GRANT ALL PRIVILEGES ON *.* TO 'slave_user'@'%' IDENTIFIED BY 'password';
GRANT ALL PRIVILEGES ON *.* TO 'slave_user'@'localhost' IDENTIFIED BY 'password';
```

The second entry may not be necessary, but simplified other steps. Change
user name and password as needed.


###Intialize from a database backup

Initialize all nodes from an existing backup, or create a backup from the
first node if needed:

On the current database:
```
mysqldump -u root --master-data --databases my_database1 my_database2 > backup.sql
```

At the top of this file is a commented out line:
SET GLOBAL gtid_slave_pos='XXXX...'

uncomment his line.

On all new nodes:
```
mysqldump -u root < backup.sql
```

###Initialize replication

Choose a node as master, in this example node1.

On all slaves, execute:
```
RESET MASTER;

CHANGE MASTER TO master_host="node1", master_port=3306, \
       master_user="slave_user", master_password="password", \
       master_use_gtid=current_pos;

SET GLOBAL rpl_semi_sync_master_enabled='ON', rpl_semi_sync_slave_enabled='ON';

START SLAVE;

SHOW SLAVE STATUS\G
```

In an ideal world this will show that replication is now fully working.

Once replication is working, verify the configuration by doing some updates
and verifying that they are replicated.

Now try changing the master. On each slave perform:
```
STOP SLAVE
```

Choose a new master, node2 in our example. On all slave nodes execute:
```
CHANGE MASTER TO  master_host="node2", master_port=3306, \
       master_user="slave_user", master_password="password", \
       master_use_gtid=current_pos;

START SLAVE;
```

And again, check that replication is working and changes are synchronized.


Part 2: Pacemaker Setup
-----------------------

This is pretty straightforward. Example is using pcs.

```
# Dump the cib
pcs cluster cib mariadb_cfg

# Create the mariadb_server resource
pcs -f mariadb_cfg resource create mariadb_server mariadb \
   binary="/usr/sbin/mysqld" \
   replication_user="slave_user" \
   replication_passwd="password" \
   node_list="node1 node2 node3" \
   op start timeout=120 interval=0 \
   op stop timeout=120 interval=0 \
   op promote timeout=120 interval=0 \
   op demote timeout=120 interval=0 \
   op monitor role=Master timeout=30 interval=10 \
   op monitor role=Slave timeout=30 interval=20 \
   op notify  timeout="60s" interval="0s"

# Create the master slave resource
pcs -f mariadb_cfg resource master msMariadb mariadb_server \
    master-max=1 master-node-max=1 clone-max=3 clone-node-max=1 notify=true

# Avoid running this on some nodes, only if needed
pcs -f mariadb_cfg constraint location msMariadb avoids \
    node4=INFINITY node5=INFINITY

# Push the cib
pcs cluster cib-push mariadb_cfg
```

You should now have a running MariaDB cluster:
```
pcs status

...
 Master/Slave Set: msMariadb [mariadb_server]
      Masters: [ node1 ]
      Slaves: [ node2 node3 ]
...
```

