#ifndef ClusterClient_h
#define ClusterClient_h

#include <Pegasus/Common/Config.h>
#include <Pegasus/Common/CIMInstance.h>
#include <Pegasus/Client/CIMClient.h>


PEGASUS_USING_PEGASUS;
PEGASUS_USING_STD;


#define PEGASUS_PORT 5989


void printClusters(CIMClient& client);
void printNodes(CIMClient& client);
void printServices(CIMClient& client);

void printInstance(string tab, CIMInstance& inst);


#endif
