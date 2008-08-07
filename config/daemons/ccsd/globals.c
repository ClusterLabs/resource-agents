#include <stdio.h>

int ppid = 0;

char *config_file_location = NULL;
char *lockfile_location = NULL;

int frontend_port = 50006;
int backend_port  = 50007;
int cluster_base_port = 50008;

/* -1 = no preference, 0 = IPv4, 1 = IPv6 */
int IPv6=-1;

/* 1 = allow and use UNIX domain sockets for local ccs queries */
int use_local = 1;

char *multicast_address = NULL;
int ttl=1;
