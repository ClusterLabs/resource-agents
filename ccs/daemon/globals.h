/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#define DEFAULT_CONFIG_LOCATION "/etc/cluster/cluster.conf"
#define DEFAULT_CCSD_LOCKFILE "/var/run/cluster/ccsd.pid"

extern char *config_file_location;
extern char *lockfile_location;

extern int frontend_port;
extern int backend_port;
extern int cluster_base_port;

extern int IPv6;
extern char *multicast_address;
extern int ttl;
#endif /* __GLOBALS_H__ */
