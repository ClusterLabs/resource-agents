#include "clusterCIM_test.h"

#include <Pegasus/Common/SSLContext.h>


PEGASUS_USING_PEGASUS;
PEGASUS_USING_STD;


int 
main(int argc, char** argv)
{
  try
    {
      CIMClient               client;
      string hostname;
      
      if (argc == 1) {
	cout << "Enter hostname: ";
	cin >> hostname;
      }
      else if (argc == 2)
	hostname = argv[1];
      else {
	cout << "Usage:" << endl;
	cout << "      " << argv[0] << " <hostname>" << endl;
	return 1;
      }
      
      // connect
      
      if (hostname == "localhost")
	client.connectLocal();
      else {
	string username, password;
	cout << "Enter username: ";
	cin >> username;
	cout << "Enter password for user \"" + username + "\" on \"" + hostname + "\": ";
	cin >> password;
	
	client.connect(hostname.c_str(), 
		       PEGASUS_PORT,
		       SSLContext("", NULL),
		       username.c_str(),
		       password.c_str());
      }
      
      // print 
      
      printClusters(client);
      printNodes(client);
      printServices(client);
    }
  catch(Exception& e)
    {
      cerr << "Error: " << e.getMessage() << endl;
      exit(1);
    }
  
  return 0;
}


void 
printClusters(CIMClient& client)
{
  const CIMNamespaceName NAMESPACE = CIMNamespaceName("root/cimv2");
  const CIMName CLASSNAME = CIMName("RedHat_Cluster");
  
  Boolean                 deepInheritance = true;
  Boolean                 localOnly = true;
  Boolean                 includeQualifiers = false;
  Boolean                 includeClassOrigin = false;
  Array<CIMInstance>      cimInstances;
  
  //
  // Enumerate Instances.
  //
  cimInstances = client.enumerateInstances(NAMESPACE,
					   CLASSNAME,
					   deepInheritance,
					   localOnly,
					   includeQualifiers,
					   includeClassOrigin );
  int size = cimInstances.size();
  if (size == 0)
    cout << "Cluster info is not accessible" << endl;
  for (int i=0; i<size; i++) {
    cout << "Cluster: " << endl;
    printInstance("\t", cimInstances[i]);
  }
}

void 
printNodes(CIMClient& client)
{
  const CIMNamespaceName NAMESPACE = CIMNamespaceName("root/cimv2");
  const CIMName CLASSNAME = CIMName("RedHat_ClusterNode");
  
  Boolean                 deepInheritance = true;
  Boolean                 localOnly = true;
  Boolean                 includeQualifiers = false;
  Boolean                 includeClassOrigin = false;
  Array<CIMInstance>      cimInstances;
  
  //
  // Enumerate Instances.
  //
  cimInstances = client.enumerateInstances(NAMESPACE,
					   CLASSNAME,
					   deepInheritance,
					   localOnly,
					   includeQualifiers,
					   includeClassOrigin );
  int size = cimInstances.size();
  for (int i=0; i<size; i++) {
    cout << "\tNode: " << endl;
    printInstance("\t\t", cimInstances[i]);
  }
}

void 
printServices(CIMClient& client)
{
  const CIMNamespaceName NAMESPACE = CIMNamespaceName("root/cimv2");
  const CIMName CLASSNAME = CIMName("RedHat_ClusterFailoverService");
  
  Boolean                 deepInheritance = true;
  Boolean                 localOnly = true;
  Boolean                 includeQualifiers = false;
  Boolean                 includeClassOrigin = false;
  Array<CIMInstance>      cimInstances;
  
  //
  // Enumerate Instances.
  //
  cimInstances = client.enumerateInstances(NAMESPACE,
					   CLASSNAME,
					   deepInheritance,
					   localOnly,
					   includeQualifiers,
					   includeClassOrigin );
  int size = cimInstances.size();
  for (int i=0; i<size; i++) {
    cout << "\tService: " << endl;
    printInstance("\t\t", cimInstances[i]);
  }
}

void
printInstance(string tab, CIMInstance& inst)
{
  int size = inst.getPropertyCount();
  for (int i=0; i<size; i++) {
    CIMProperty prop = inst.getProperty(i);
    
    cout << tab << prop.getName().getString() << ": ";
    cout << prop.getValue().toString() << endl;
    
  }
}
