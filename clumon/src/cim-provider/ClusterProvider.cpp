#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <algorithm>
#include <functional>
#include <vector>


#include "ClusterProvider.h"
#include "SmartHandler.h"
#include "ClusterMonitor.h"
#include "Cluster.h"


using namespace std;
using namespace Pegasus;
using namespace ClusterMonitoring;


static CIMInstance 
buildClusterInstance(Cluster& cluster, Boolean qual, Boolean orig);
static CIMInstance 
buildNodeInstance(Node& node, Boolean qual, Boolean orig);
static CIMInstance 
buildServiceInstance(Service& service, Boolean qual, Boolean orig);


static CIMObjectPath 
buildClusterInstancePath(Cluster& cluster, const CIMNamespaceName& nameSpace);
static CIMObjectPath 
buildNodeInstancePath(Node& node, const CIMNamespaceName& nameSpace);
static CIMObjectPath 
buildServiceInstancePath(Service& service, const CIMNamespaceName& nameSpace);


static String
hostname(void);




ClusterProvider::ClusterProvider(void) throw()
  //  clusterMonitor("/tmp/cluster_monitor_sock")
{
  log_handle = open("/var/log/ClusterProvider.log", O_WRONLY | O_CREAT | O_APPEND);
  log("ClusterProvider Created");
}

ClusterProvider::~ClusterProvider(void) throw()
{
  
}
  


// CIMProvider interface
void 
ClusterProvider::initialize (CIMOMHandle &cimom)
{
  log("ClusterProvider::initialize called");
}

void 
ClusterProvider::terminate (void)
{
  log("ClusterProvider::terminate called");
  delete this;
}


// CIMInstanceProvider interface
void 
ClusterProvider::getInstance(const OperationContext &context,
			     const CIMObjectPath &ref,
			     const Boolean includeQualifiers,
			     const Boolean includeClassOrigin,
			     const CIMPropertyList &propertyList,
			     InstanceResponseHandler &handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<InstanceResponseHandler> t(handler);
  
  CIMName className(ref.getClassName());
  
  log("getInstance(... " + className.getString() + " ...) called");
  
  if(className.equal(CLUSTER_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      CIMObjectPath path = buildClusterInstancePath(*cluster, 
						    ref.getNameSpace());
      if(path.identical(ref))
	{
	  CIMInstance inst = buildClusterInstance(*cluster,
						  includeQualifiers,
						  includeClassOrigin);
	  handler.deliver(inst);
	}
    }
  else if(className.equal(CLUSTER_NODE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Node>& nodes = cluster->getNodes();
      list<Node>::iterator iter = nodes.begin();
      for( ; iter != nodes.end(); iter++)
	{
	  CIMObjectPath path = buildNodeInstancePath(*iter, 
						     ref.getNameSpace());
	  if(path.identical(ref))
	    {
	      CIMInstance inst = buildNodeInstance(*iter, 
						   includeQualifiers,
						   includeClassOrigin);
	      handler.deliver(inst);
	    }
	}
    }
  else if(className.equal(CLUSTER_SERVICE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Service>& services = cluster->getServices();
      list<Service>::iterator iter = services.begin();
      for( ; iter != services.end(); iter++)
	{
	  CIMObjectPath path = buildServiceInstancePath(*iter, 
							ref.getNameSpace());
	  if(path.identical(ref))
	    {
	      CIMInstance inst = buildServiceInstance(*iter, 
							  includeQualifiers,
						      includeClassOrigin);
	      handler.deliver(inst);
	    }
	}
    }
  else
    throw CIMInvalidParameterException(ref.toString());
}

void 
ClusterProvider::enumerateInstances(const OperationContext &context,
				    const CIMObjectPath &ref,
				    const Boolean includeQualifiers,
				    const Boolean includeClassOrigin,
				    const CIMPropertyList &propertyList,
				    InstanceResponseHandler &handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<InstanceResponseHandler> t(handler);
  
  CIMName className(ref.getClassName());
  
  log("enumerateInstances(... " + className.getString() + " ...) called");
  
  if(className.equal(CLUSTER_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      CIMInstance inst = buildClusterInstance(*cluster,
					      includeQualifiers,
					      includeClassOrigin);
      handler.deliver(inst);
    }
  else if(className.equal(CLUSTER_NODE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Node>& nodes = cluster->getNodes();
      list<Node>::iterator iter = nodes.begin();
      for( ; iter != nodes.end(); iter++)
	{
	  CIMInstance inst = buildNodeInstance(*iter, 
					       includeQualifiers,
					       includeClassOrigin);
	  handler.deliver(inst);
	}
    }
  else if(className.equal(CLUSTER_SERVICE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Service>& services = cluster->getServices();
      list<Service>::iterator iter = services.begin();
      for( ; iter != services.end(); iter++)
	{
	  CIMInstance inst = buildServiceInstance(*iter, 
						  includeQualifiers,
						  includeClassOrigin);
	  handler.deliver(inst);
	}
    }
  else
    throw CIMInvalidParameterException(ref.toString());
}

void 
ClusterProvider::enumerateInstanceNames(const OperationContext &context,
					const CIMObjectPath &classRef,
					ObjectPathResponseHandler &handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<ObjectPathResponseHandler> t(handler);
  
  CIMName className(classRef.getClassName());
  
  log("enumerateInstanceNames(... " + className.getString() + " ...) called");
  
  if(className.equal(CLUSTER_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      CIMObjectPath path = buildClusterInstancePath(*cluster,
						    classRef.getNameSpace());
      handler.deliver(path);
    }
  else if(className.equal(CLUSTER_NODE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Node>& nodes = cluster->getNodes();
      list<Node>::iterator iter = nodes.begin();
      for( ; iter != nodes.end(); iter++)
	{
	  CIMObjectPath path = buildNodeInstancePath(*iter, 
						     classRef.getNameSpace());
	  handler.deliver(path);
	}
    }
  else if(className.equal(CLUSTER_SERVICE_CLASSNAME))
    {
      if(cluster.get() == NULL)
	return;
      list<Service>& services = cluster->getServices();
      list<Service>::iterator iter = services.begin();
      for( ; iter != services.end(); iter++)
	{
	  CIMObjectPath path = buildServiceInstancePath(*iter, 
							classRef.getNameSpace());
	  handler.deliver(path);
	}
    }
  else
    throw CIMInvalidParameterException(classRef.toString());
}


void 
ClusterProvider::createInstance(const OperationContext &context,
				const CIMObjectPath &ref,
				const CIMInstance &obj,
				ObjectPathResponseHandler &handler)
{
  throw CIMNotSupportedException(CLUSTER_PROVIDER_CLASSNAME + "::createInstance");
}

void 
ClusterProvider::modifyInstance(const OperationContext &context,
				const CIMObjectPath &ref,
				const CIMInstance &obj,
				const Boolean includeQualifiers,
				const CIMPropertyList &propertyList,
				ResponseHandler &handler)
{
  throw CIMNotSupportedException(CLUSTER_PROVIDER_CLASSNAME + "::modifyInstance");
}

void 
ClusterProvider::deleteInstance(const OperationContext &context,
				const CIMObjectPath &ref,
				ResponseHandler &handler)
{
  throw CIMNotSupportedException(CLUSTER_PROVIDER_CLASSNAME + "::deleteInstance");
}




// private

void 
ClusterProvider::log(const String str)
{
  String s = str + "\n";
  if(log_handle != -1)
    write(log_handle, s.getCString(), s.size());
}






CIMInstance
buildClusterInstance(Cluster& cluster, Boolean qual, Boolean orig)
{
  CIMInstance inst(CIMName(CLUSTER_CLASSNAME));
  
  // Name
  inst.addProperty(CIMProperty(
			       CIMName("Name"),
			       CIMValue(String(cluster.name.c_str()))));
  
  // Caption
  //inst.addProperty(CIMProperty(
  //			       CIMName("Caption"),
  //			       CIMValue(String(cluster.name.c_str()))));
  // Description
  //inst.addProperty(CIMProperty(
  //			       CIMName("Description"),
  //			       CIMValue(String(cluster.name.c_str()))));
  
  // *** Votes ***
  
  inst.addProperty(CIMProperty(
			       CIMName("Votes"),
			       CIMValue(Uint16(cluster.getVotes()))));
  inst.addProperty(CIMProperty(
			       CIMName("VotesNeededForQuorum"),
			       CIMValue(Uint16(cluster.getMinQuorum()))));
  
  
  // *** Nodes ***
  
  list<Node>& nodes = cluster.getNodes();
  Array<String> names;
  Array<String> namesA;
  Array<String> namesU;
  list<Node>::iterator iterN = nodes.begin();
  for( ; iterN != nodes.end(); iterN++)
    {
      String name(iterN->name.c_str());
      names.append(name);
      if(iterN->clustered)
	namesA.append(name);
      else
	namesU.append(name);
    }
  inst.addProperty(CIMProperty(
			       CIMName("MaxNumberOfNodes"),
			       CIMValue(Uint32(0)))); // unlimited
  inst.addProperty(CIMProperty(
			       CIMName("NodesNumber"),
			       CIMValue(Uint16(nodes.size()))));
  inst.addProperty(CIMProperty(
			       CIMName("AvailableNodesNumber"),
			       CIMValue(Uint16(cluster.getClusteredNodes().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("UnavailableNodesNumber"),
			       CIMValue(Uint16(cluster.getUnclusteredNodes().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("NodesNames"), 
			       CIMValue(names)));
  inst.addProperty(CIMProperty(
			       CIMName("AvailableNodesNames"), 
			       CIMValue(namesA)));
  inst.addProperty(CIMProperty(
			       CIMName("UnavailableNodesNames"), 
			       CIMValue(namesU)));
  
  // *** services ***
  
  list<Service>& services = cluster.getServices();
  names.clear();
  Array<String> namesR;
  Array<String> namesF;
  Array<String> namesS;
  list<Service>::iterator iterS = services.begin();
  for( ; iterS != services.end(); iterS++)
    {
      String name(iterS->name.c_str());
      names.append(name);
      if(iterS->running())
	namesR.append(name);
      else 
	namesS.append(name);
      if(iterS->failed())
	namesF.append(name);
    }
  inst.addProperty(CIMProperty(
			       CIMName("ServicesNumber"),
			       CIMValue(Uint16(cluster.getServices().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("RunningServicesNumber"),
			       CIMValue(Uint16(cluster.getRunningServices().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("StoppedServicesNumber"),
			       CIMValue(Uint16(cluster.getStoppedServices().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("FailedServicesNumber"),
			       CIMValue(Uint16(cluster.getFailedServices().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("ServicesNames"),
			       CIMValue(names)));
  inst.addProperty(CIMProperty(
			       CIMName("RunningServicesNames"),
			       CIMValue(namesR)));
  inst.addProperty(CIMProperty(
			       CIMName("StoppedServicesNames"),
			       CIMValue(namesS)));
  inst.addProperty(CIMProperty(
			       CIMName("FailedServicesNames"),
			       CIMValue(namesF)));
  
  
  // *** status begin ***
  
  bool online = cluster.getClusteredNodes().size() > 0;
  bool failedServices = cluster.getFailedServices().size() != 0;
  bool stoppedServices = cluster.getStoppedServices().size() != 0;
  bool unclusteredNodes = cluster.getUnclusteredNodes().size() != 0;
  bool quorumed = cluster.quorumed();
  Array<Uint16> Ostatus; // OperationalStatus
  Array<String> statusD; // StatusDescription
  if(online)
    {
      if(quorumed)
	{
	  if(!unclusteredNodes && !failedServices && !stoppedServices)
	    {
	      // OK
	      Ostatus.append(2);
	      statusD.append("All services and nodes functional");
	    }
	  else
	    {
	      if(failedServices)
		{
		  // Error
		  Ostatus.append(6);
		  statusD.append("Some services failed");
		}
	      if(stoppedServices)
		{
		  // Stressed
		  Ostatus.append(4);
		  statusD.append("Some services not running");
		}
	      if(unclusteredNodes)
		{
		  // Stressed
		  Ostatus.append(4);
		  statusD.append("Some nodes unavailable");
		}
	    }
	}
      else
	{
	  // Degraded
	  Ostatus.append(3);
	  statusD.append("All services stopped, not quorumed");
	}
    }
  else
    {
      // Stopped
      Ostatus.append(10);
      statusD.append("Cluster stopped");
    }
  
  //inst.addProperty(CIMProperty(
  //			       CIMName("Status"),
  //			       CIMValue(status)));
  inst.addProperty(CIMProperty(
			       CIMName("OperationalStatus"),
			       CIMValue(Ostatus)));
  inst.addProperty(CIMProperty(
			       CIMName("StatusDescriptions"),
			       CIMValue(statusD)));
  
  CIMValue clusterState;
  if(online)
    clusterState = CIMValue(Uint16(2)); // online
  else
    clusterState = CIMValue(Uint16(3)); // offline
  inst.addProperty(CIMProperty(
			       CIMName("ClusterState"),
			       clusterState));
  
  // *** status done ***
  
  
  // Types
  Array<Uint16> types;
  types.append(2); // failover
  inst.addProperty(CIMProperty(
			       CIMName("Types"),
			       CIMValue(types)));
  
  
  // CreationClassName
  inst.addProperty(CIMProperty(
			       CIMName("CreationClassName"),
			       CIMValue(String(CLUSTER_CLASSNAME))));
  
  // ResetCapability
  //inst.addProperty(CIMProperty(
  //			       CIMName("ResetCapability"),
  //			       CIMValue(Uint16(5)))); // not implemented - cluster :)
  // PowerManagementCapabilities
  
  // ElementName
  // InstallDate
  
  // EnabledState
  // OtherEnabledState
  // EnabledDefault
  
  // RequestedState
  // TimeOfLastStateChange
  
  // Roles
  
  // NameFormat
  
  // PrimaryOwnerContact
  // PrimaryOwnerName
  // OtherIdentifingInfo
  // IdentifyingDescription
  
  // Dedicated
  // OtherDedicatedDescription
  
  
  // Interconnect
  // InterconnectAddress
  
  return inst;
}

CIMInstance
buildNodeInstance(Node& node, Boolean qual, Boolean orig)
{
  CIMInstance inst(CIMName(CLUSTER_NODE_CLASSNAME));
  /*
  [ Key, Description("Name of cluster this node participates in.") ]
    string ClusterName;
  
  [ Description("Number of services running on this node") ]
    uint16 RunningServicesNumber;
  
  [ Description("Services running on this node") ]
    uint16 RunningServicesNames;
  */
  
  // ClusterName
  inst.addProperty(CIMProperty(
  			       CIMName("ClusterName"),
  			       CIMValue(String(node.cluster->name.c_str()))));
  
  // Name
  inst.addProperty(CIMProperty(
			       CIMName("Name"),
			       CIMValue(String(node.name.c_str()))));
  
  // Caption
  //inst.addProperty(CIMProperty(
  //			       CIMName("Caption"),
  //			       CIMValue(String(cluster.name.c_str()))));
  // Description
  //inst.addProperty(CIMProperty(
  //			       CIMName("Description"),
  //			       CIMValue(String(cluster.name.c_str()))));
  
  
  // *** Votes ***
  
  inst.addProperty(CIMProperty(
			       CIMName("Votes"),
			       CIMValue(Uint16(node.votes))));
  
  // *** services ***
  
  list<Service*> services = node.getServices();
  Array<String> names;
  list<Service*>::iterator iter = services.begin();
  for( ; iter != services.end(); iter++)
    {
      String name((*iter)->name.c_str());
      names.append(name);
    }
  inst.addProperty(CIMProperty(
			       CIMName("RunningServicesNumber"),
			       CIMValue(Uint16(node.getServices().size()))));
  inst.addProperty(CIMProperty(
			       CIMName("RunningServicesNames"),
			       CIMValue(names)));
  
  
  // *** status begin ***
  
  Array<Uint16> Ostatus; // OperationalStatus
  Array<String> statusD; // StatusDescription
  if(node.online && node.clustered)
    {
      // OK
      Ostatus.append(2);
      statusD.append("Node available to cluster");
    }
  else if(node.online)
    {
      // Error
      Ostatus.append(6);
      statusD.append("Node running, but unavailable to cluster");
    }
  else
    {
      // Stopped
      Ostatus.append(10);
      statusD.append("Node not running");
    }
  
  //inst.addProperty(CIMProperty(
  //			       CIMName("Status"),
  //			       CIMValue(status)));
  inst.addProperty(CIMProperty(
			       CIMName("OperationalStatus"),
			       CIMValue(Ostatus)));
  inst.addProperty(CIMProperty(
			       CIMName("StatusDescriptions"),
			       CIMValue(statusD)));
  
  
  // CreationClassName
  inst.addProperty(CIMProperty(
			       CIMName("CreationClassName"),
			       CIMValue(String(CLUSTER_NODE_CLASSNAME))));
  
  // ResetCapability
  //inst.addProperty(CIMProperty(
  //			       CIMName("ResetCapability"),
  //			       CIMValue(Uint16(5)))); // not implemented - cluster :)
  // PowerManagementCapabilities
  
  // ElementName
  // InstallDate
  
  // EnabledState
  // OtherEnabledState
  // EnabledDefault
  
  // RequestedState
  // TimeOfLastStateChange
  
  // Roles
  
  // NameFormat
  
  // PrimaryOwnerContact
  // PrimaryOwnerName
  // OtherIdentifingInfo
  // IdentifyingDescription
  
  // Dedicated
  // OtherDedicatedDescription
  
  return inst;
}

CIMInstance
buildServiceInstance(Service& service, Boolean qual, Boolean orig)
{
  CIMInstance inst(CIMName(CLUSTER_SERVICE_CLASSNAME));
  
  // Name
  inst.addProperty(CIMProperty(
			       CIMName("Name"),
			       CIMValue(String(service.name.c_str()))));
  
  // Caption
  //inst.addProperty(CIMProperty(
  //			       CIMName("Caption"),
  //			       CIMValue(String(cluster.name.c_str()))));
  // Description
  //inst.addProperty(CIMProperty(
  //			       CIMName("Description"),
  //			       CIMValue(String(cluster.name.c_str()))));
  
  // ClusterName
  inst.addProperty(CIMProperty(
  			       CIMName("ClusterName"),
  			       CIMValue(String(service.cluster->name.c_str()))));
  
  // Started
  inst.addProperty(CIMProperty(
			       CIMName("Started"),
			       CIMValue(service.running())));
  // StartMode
  String autostart;
  if(service.autostart())
    autostart = "Automatic";
  else
    autostart = "Manual";
  inst.addProperty(CIMProperty(
			       CIMName("StartMode"),
			       CIMValue(autostart)));
  
  // NodeName
  if(service.running())
    {
      Node* node = service.getNode();
      String nodeName = String(node->name.c_str());
      inst.addProperty(CIMProperty(
				   CIMName("NodeName"),
				   CIMValue(nodeName)));
    }
  
  // *** status begin ***
  
  Array<Uint16> Ostatus; // OperationalStatus
  Array<String> statusD; // StatusDescription
  if(service.failed())
    {
      // Error
      Ostatus.append(6);
      statusD.append("Failed");
    }
  else if(!service.running())
    {
      // Stopped
      Ostatus.append(10);
      statusD.append("Stopped");
    }
  else
    {
      // OK
      Ostatus.append(2);
      statusD.append("Running");
    }
  //inst.addProperty(CIMProperty(
  //			       CIMName("Status"),
  //			       CIMValue(status)));
  inst.addProperty(CIMProperty(
			       CIMName("OperationalStatus"),
			       CIMValue(Ostatus)));
  inst.addProperty(CIMProperty(
			       CIMName("StatusDescriptions"),
			       CIMValue(statusD)));
  // *** status done ***
  
  // CreationClassName
  inst.addProperty(CIMProperty(
			       CIMName("CreationClassName"),
			       CIMValue(String(CLUSTER_SERVICE_CLASSNAME))));
  
  // SystemCreationClassName
  inst.addProperty(CIMProperty(
			       CIMName("SystemCreationClassName"),
			       CIMValue(String(CLUSTER_CLASSNAME))));
  
  // SystemName
  inst.addProperty(CIMProperty(
  			       CIMName("SystemName"),
  			       CIMValue(String(service.cluster->name.c_str()))));
  
  
  // ElementName
  // InstallDate
  
  // EnabledState
  // OtherEnabledState
  // EnabledDefault
  
  // RequestedState
  // TimeOfLastStateChange
  
  // PrimaryOwnerContact
  // PrimaryOwnerName
  // OtherIdentifingInfo
  // IdentifyingDescription
  
  return inst;
}




CIMObjectPath 
buildClusterInstancePath(Cluster& cluster, const CIMNamespaceName& nameSpace)
{
  Array<CIMKeyBinding> keys;
  keys.append(CIMKeyBinding("CreationClassName", String(CLUSTER_CLASSNAME), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("Name", String(cluster.name.c_str()), CIMKeyBinding::STRING));
  return CIMObjectPath(hostname(), nameSpace, CLUSTER_CLASSNAME, keys);
}

CIMObjectPath 
buildNodeInstancePath(Node& node, const CIMNamespaceName& nameSpace)
{
  string clustername = node.cluster->name;
  Array<CIMKeyBinding> keys;
  keys.append(CIMKeyBinding("CreationClassName", String(CLUSTER_NODE_CLASSNAME), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("Name", String(node.name.c_str()), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("ClusterName", String(clustername.c_str()), CIMKeyBinding::STRING));
  return CIMObjectPath(hostname(), nameSpace, CLUSTER_NODE_CLASSNAME, keys);
}

CIMObjectPath 
buildServiceInstancePath(Service& service, const CIMNamespaceName& nameSpace)
{
  string clustername = service.cluster->name;
  Array<CIMKeyBinding> keys;
  keys.append(CIMKeyBinding("CreationClassName", String(CLUSTER_SERVICE_CLASSNAME), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("Name", String(service.name.c_str()), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("SystemCreationClassName", String(CLUSTER_CLASSNAME), CIMKeyBinding::STRING));
  keys.append(CIMKeyBinding("SystemName", String(clustername.c_str()), CIMKeyBinding::STRING));
  return CIMObjectPath(hostname(), nameSpace, CLUSTER_SERVICE_CLASSNAME, keys);
}


String
hostname()
{
  String hostname;
  struct utsname uts;
  
  if (uname(&uts) == 0)
    hostname = uts.nodename;
  else
    hostname = "unknown";
  
  return hostname;
}




























/*
  ### Implement later ###




class Association
{ 
public:
  Association(CIMInstance assoc, CIMObjectPath assocPath,
	      CIMInstance ante, CIMObjectPath antePath,
	      CIMInstance dep, CIMObjectPath depPath) :
    assoc(assoc), assocPath(assocPath),
    ante(ante), antePath(antePath),
    dep(dep), depPath(depPath) {}
  
  CIMInstance assoc;
  CIMObjectPath assocPath;
  CIMInstance ante;
  CIMObjectPath antePath;
  CIMInstance dep;
  CIMObjectPath depPath;
};     

vector<Association> 
buildAssociations(Cluster& cluster, 
		  const CIMNamespaceName& nameSpace, 
		  Boolean quals, 
		  Boolean orig);



static CIMInstance 
buildClusterParticipatingNodeInstance(Cluster& cluster, 
				      Node& node, 
				      const CIMNamespaceName& nameSpace,
				      Boolean qual, 
				      Boolean orig);
static CIMObjectPath 
buildClusterParticipatingNodeInstancePath(Cluster& cluster,
					  Node& node,
					  const CIMNamespaceName& nameSpace);



// CIMAssociationProvider interface
void 
ClusterProvider::associatorNames(const OperationContext& context,
				 const CIMObjectPath& objectName,
				 const CIMName& associationClass,
				 const CIMName& resultClass,
				 const String& role,
				 const String& resultRole,
				 ObjectPathResponseHandler& handler) 
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<ObjectPathResponseHandler> t(handler);
  
  log("associatorNames(" + objectName.toString() + ", " + 
      associationClass.getString() + ", " +
      resultClass.getString() + ", " +
      role + ", " + 
      resultRole + ", ...) called");
  return;
  
}
  
void 
ClusterProvider::associators(const OperationContext& context,
			     const CIMObjectPath& objectName,
			     const CIMName& associationClass,
			     const CIMName& resultClass,
			     const String& role,
			     const String& resultRole,
			     const Boolean includeQualifiers,
			     const Boolean includeClassOrigin,
			     const CIMPropertyList& propertyList,
			     ObjectResponseHandler& handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<ObjectResponseHandler> t(handler);
  
  log("associatorNames(" + objectName.toString() + ", " + 
      associationClass.getString() + ", " +
      resultClass.getString() + ", " +
      role + ", " + 
      resultRole + ", ...) called");
  return;
  
}
  
void 
ClusterProvider::referenceNames(const OperationContext& context,
				const CIMObjectPath& objectName,
				const CIMName& resultClass,
				const String& role,
				ObjectPathResponseHandler& handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<ObjectPathResponseHandler> t(handler);
  
  CIMName className(objectName.toString());
  
  log("referenceNames(" + objectName.toString() + ", " + 
      resultClass.getString() + ", " +
      role + ", ...) called");
  return;
  
  
  if(! className.equal(CLUSTER_CLASSNAME) || 
     ! className.equal(CLUSTER_NODE_CLASSNAME) ||
     ! className.equal(CLUSTER_SERVICE_CLASSNAME))
    throw CIMInvalidParameterException(className.getString());
  if(role.size()!=0 &&
     !role.equalNoCase(role, "Dependent") && 
     !role.equalNoCase(role, "Antecedent"))
    throw CIMInvalidParameterException(className.getString());
  
  
  if(cluster.get() == NULL)
    return;
  
  vector<Association> assocs = buildAssociations(*cluster, 
						 objectName.getNameSpace(),
						 false, 
						 false);
  
  for(unsigned int i=0; i<assocs.size(); i++)
    {
      Association& assoc = assocs[i];
      
      if(!objectName.identical(assoc.antePath) && !objectName.identical(assoc.depPath))
	continue;
      
      if(!resultClass.isNull() && !resultClass.equal(assoc.assocPath.getClassName()))
	continue;
      
      if(role.size()!=0 &&
	 !(role.equalNoCase(role, "Dependent") && objectName.identical(assoc.depPath)) &&
	 !(role.equalNoCase(role, "Antecedent") && objectName.identical(assoc.antePath)))
	continue;
      
      handler.deliver(assoc.assocPath);
    }
}
  
void 
ClusterProvider::references(const OperationContext& context,
			    const CIMObjectPath& objectName,
			    const CIMName& resultClass,
			    const String& role,
			    const Boolean includeQualifiers,
			    const Boolean includeClassOrigin,
			    const CIMPropertyList& propertyList,
			    ObjectResponseHandler& handler)
{
  counting_auto_ptr<Cluster> cluster = ClusterMonitoring::getCluster("/tmp/cluster_monitor_sock");
  
  SmartHandler<ObjectResponseHandler> t(handler);
  
  CIMName className(objectName.toString());
  
  log("references(" + objectName.toString() + ", " + 
      resultClass.getString() + ", " +
      role + ", ...) called");
  return;
  
  
  if(! className.equal(CLUSTER_CLASSNAME) || 
     ! className.equal(CLUSTER_NODE_CLASSNAME) ||
     ! className.equal(CLUSTER_SERVICE_CLASSNAME))
    throw CIMInvalidParameterException(className.getString());
  if(role.size()!=0 &&
     !role.equalNoCase(role, "Dependent") && 
     !role.equalNoCase(role, "Antecedent"))
    throw CIMInvalidParameterException(className.getString());
  
  if(cluster.get() == NULL)
    return;
  
  
  vector<Association> assocs = buildAssociations(*cluster, 
						 objectName.getNameSpace(),
						 includeQualifiers,
						 includeClassOrigin);
  
  for(unsigned int i=0; i<assocs.size(); i++)
    {
      Association& assoc = assocs[i];
      
      if(!objectName.identical(assoc.antePath) && !objectName.identical(assoc.depPath))
	continue;
      
      if(!resultClass.isNull() && !resultClass.equal(assoc.assocPath.getClassName()))
	continue;
      
      if(role.size()!=0 &&
	 !(role.equalNoCase(role, "Dependent") && objectName.identical(assoc.depPath)) &&
	 !(role.equalNoCase(role, "Antecedent") && objectName.identical(assoc.antePath)))
	continue;
      
      handler.deliver(assoc.assoc);
    }
}

CIMInstance
buildClusterParticipatingNodeInstance(Cluster& cluster, 
				      Node& node, 
				      const CIMNamespaceName& nameSpace,
				      Boolean qual, 
				      Boolean orig)
{
  CIMInstance inst(CIMName(CLUSTER_PARTICIPATING_NODE_CLASSNAME));
  
  // *** localy defined ***
  
  inst.addProperty(CIMProperty(
			       CIMName("QuorumVotes"),
			       CIMValue(Uint16(node.votes))));
  
  // *** inherited ***
  
  inst.addProperty(CIMProperty(
			       CIMName("Antecedent"),
			       CIMValue(buildNodeInstancePath(node, nameSpace))));
  
  inst.addProperty(CIMProperty(
			       CIMName("Dependent"),
			       CIMValue(buildClusterInstancePath(cluster, nameSpace))));
  
  inst.addProperty(CIMProperty(
			       CIMName("RoleOfNode"),
			       CIMValue(Uint16(2)))); // peers
  
  // StateOfNode
  int state;
  if(node.clustered)
    state = 4; // clustered
  else
    state = 5; // unclustered
  inst.addProperty(CIMProperty(CIMName("StateOfNode"),
			       CIMValue(Uint16(state))));
  
  return inst;
}

CIMObjectPath 
buildClusterParticipatingNodeInstancePath(Cluster& cluster,
					  Node& node,
					  const CIMNamespaceName& nameSpace)
{
  Array<CIMKeyBinding> keys;
  keys.append(CIMKeyBinding("Dependent", 
			    String(buildClusterInstancePath(cluster, nameSpace).toString()), 
			    CIMKeyBinding::REFERENCE));
  keys.append(CIMKeyBinding("Antecedent", 
			    String(buildNodeInstancePath(node, nameSpace).toString()), 
			    CIMKeyBinding::REFERENCE));
  return CIMObjectPath(hostname(), nameSpace, CLUSTER_PARTICIPATING_NODE_CLASSNAME, keys);
}

vector<Association> 
buildAssociations(Cluster& cluster, 
		  const CIMNamespaceName& nameSpace, 
		  Boolean quals, 
		  Boolean orig)
{
  vector<Association> assocs;
  
  // cluster - node
  list<Node>::iterator iter = cluster.getNodes().begin();
  for( ; iter != cluster.getNodes().end(); iter++)
    {
      // association
      CIMInstance inst = buildClusterParticipatingNodeInstance(cluster, 
							       *iter,
							       nameSpace,
							       quals,
							       orig);
      CIMObjectPath instPath = buildClusterParticipatingNodeInstancePath(cluster,
									 *iter,
									 nameSpace);
      // ante
      CIMInstance ante = buildNodeInstance(*iter,
					   quals,
					   orig);
      CIMObjectPath antePath = buildNodeInstancePath(*iter,
						     nameSpace);
      // dep
      CIMInstance dep = buildClusterInstance(cluster, 
					     quals,
					     orig);
      CIMObjectPath depPath = buildClusterInstancePath(cluster,
						       nameSpace);
      
      Association assoc(inst, instPath, 
			ante, antePath, 
			dep, depPath);
      
      assocs.push_back(assoc);
    }
  
  // cluster - service
  
  
  // node - service
  
  
  
  return assocs;
}
*/
