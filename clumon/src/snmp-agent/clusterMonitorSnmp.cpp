
#include "clusterMonitorSnmp.h"
#include "clusterMIB.h"
#include "nodesMIB.h"
#include "servicesMIB.h"


ClusterMonitoring::ClusterMonitor monitor("/tmp/cluster_monitor_sock");

void
init_redhatClusterMIB(void)
{
  try {
    initialize_clusterMIB();
    initialize_nodesMIB();
    initialize_servicesMIB();
    
    // TODO: initialize others
  } catch ( ... ) 
    {}
}
