#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ccs.h"

MODULE = Cluster::CCS PACKAGE = Cluster::CCS

PROTOTYPES: ENABLE

void
fullxpath(self, value)
    int value;
    CODE:
	fullxpath = value;

int
connect(self)
    CODE:
        RETVAL = ccs_connect();
    OUTPUT:
    RETVAL

int
force_connect(self, cluster_name, blocking)
    const char *cluster_name;
    int blocking;
    CODE:
	RETVAL = ccs_force_connect(cluster_name, blocking);
    OUTPUT:
    RETVAL

int
disconnect(self, desc)
    int desc;
    CODE:
	RETVAL = ccs_disconnect(desc);
    OUTPUT:
    RETVAL

int
get(self, desc, query, rtn)
    int desc;
    const char *query;
    char *rtn;
    CODE:
	RETVAL = ccs_get(desc, query, &rtn);
    OUTPUT:
    RETVAL
    rtn

int
get_list(self, desc, query, rtn)
    int desc;
    const char *query;
    char *rtn;
    CODE:
	RETVAL = ccs_get_list(desc, query, &rtn);
    OUTPUT:
    RETVAL
    rtn

int
set(self, desc, path, val)
    int desc;
    char *path;
    char *val;
    CODE:
	RETVAL = ccs_set(desc, path, val);
    OUTPUT:
    RETVAL

int
lookup_nodename(self, desc, nodename, rtn)
    int desc;
    const char *nodename;
    char *rtn;
    CODE:
	RETVAL = ccs_lookup_nodename(desc, nodename, &rtn);
    OUTPUT:
    RETVAL
    rtn
