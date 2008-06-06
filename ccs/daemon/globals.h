#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#define DEFAULT_CCSD_LOCKFILE "/var/run/cluster/ccsd.pid"

#define EXIT_MAGMA_PLUGINS 2  /* Magma plugins are not available */
#define EXIT_CLUSTER_FAIL  3  /* General failure to connect to cluster */
#define EXIT_LOCKFILE      4  /* Failed to create lock file */

extern int ppid;

extern char *config_file_location;
extern char *lockfile_location;

extern int frontend_port;
extern int backend_port;
extern int cluster_base_port;

extern int IPv6;
extern int use_local;
extern char *multicast_address;
extern int ttl;
#endif /* __GLOBALS_H__ */
