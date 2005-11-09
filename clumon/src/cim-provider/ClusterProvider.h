#ifndef ClusterProvider_h
#define ClusterProvider_h

#include <Pegasus/Common/Config.h>
#include <Pegasus/Provider/CIMInstanceProvider.h>
#include <Pegasus/Provider/CIMAssociationProvider.h>

#include <list>
#include <string>

#include "Cluster.h"


namespace ClusterMonitoring
{


#define CLUSTER_PROVIDER_CLASSNAME Pegasus::String("RedHatClusterProvider")
#define CLUSTER_SERVICE_CLASSNAME "RedHat_ClusterFailoverService"
#define CLUSTER_NODE_CLASSNAME "RedHat_ClusterNode"
#define CLUSTER_CLASSNAME "RedHat_Cluster"
//#define CLUSTER_PARTICIPATING_NODE_CLASSNAME "RedHat_ClusterParticipatingNode"
//#define CLUSTER_HOSTING_FAILOVER_SERVICE_CLASSNAME "RedHat_ClusterHostingFailoverService"
//#define CLUSTER_NODE_HOSTING_FAILOVER_SERVICE_CLASSNAME "RedHat_ClusterNodeHostingFailoverService"



class ClusterProvider : public Pegasus::CIMInstanceProvider//, public Pegasus::CIMAssociationProvider
{
 public:
  ClusterProvider (void) throw ();
  virtual ~ClusterProvider (void) throw ();
  
  
  
  // CIMProvider interface
  virtual void initialize (Pegasus::CIMOMHandle& cimom);
  virtual void terminate (void);
  
  // CIMInstanceProvider interface
  virtual void getInstance(
			   const Pegasus::OperationContext & context,
			   const Pegasus::CIMObjectPath & ref,
			   const Pegasus::Boolean includeQualifiers,
			   const Pegasus::Boolean includeClassOrigin,
			   const Pegasus::CIMPropertyList & propertyList,
			   Pegasus::InstanceResponseHandler & handler);
    
  virtual void enumerateInstances(
				  const Pegasus::OperationContext & context,
				  const Pegasus::CIMObjectPath & ref,
				  const Pegasus::Boolean includeQualifiers,
				  const Pegasus::Boolean includeClassOrigin,
				  const Pegasus::CIMPropertyList & propertyList,
				  Pegasus::InstanceResponseHandler & handler);

  virtual void enumerateInstanceNames(
				      const Pegasus::OperationContext & context,
				      const Pegasus::CIMObjectPath & ref,
				      Pegasus::ObjectPathResponseHandler & handler);

  virtual void modifyInstance(
			      const Pegasus::OperationContext & context,
			      const Pegasus::CIMObjectPath & ref,
			      const Pegasus::CIMInstance & obj,
			      const Pegasus::Boolean includeQualifiers,
			      const Pegasus::CIMPropertyList & propertyList,
			      Pegasus::ResponseHandler & handler);

  virtual void createInstance(
			      const Pegasus::OperationContext & context,
			      const Pegasus::CIMObjectPath & ref,
			      const Pegasus::CIMInstance & obj,
			      Pegasus::ObjectPathResponseHandler & handler);

  virtual void deleteInstance(
			      const Pegasus::OperationContext & context,
			      const Pegasus::CIMObjectPath & ref,
			      Pegasus::ResponseHandler & handler);
  
  /*
  // CIMAssociationProvider
  virtual void associatorNames(
			       const Pegasus::OperationContext& context,
			       const Pegasus::CIMObjectPath& objectName,
			       const Pegasus::CIMName& associationClass,
			       const Pegasus::CIMName& resultClass,
			       const Pegasus::String& role,
			       const Pegasus::String& resultRole,
			       Pegasus::ObjectPathResponseHandler& handler) ;
  
  virtual void associators(const Pegasus::OperationContext& context,
			   const Pegasus::CIMObjectPath& objectName,
			   const Pegasus::CIMName& associationClass,
			   const Pegasus::CIMName& resultClass,
			   const Pegasus::String& role,
			   const Pegasus::String& resultRole,
			   const Pegasus::Boolean includeQualifiers,
			   const Pegasus::Boolean includeClassOrigin,
			   const Pegasus::CIMPropertyList& propertyList,
			   Pegasus::ObjectResponseHandler& handler);
  
  virtual void referenceNames(const Pegasus::OperationContext& context,
			      const Pegasus::CIMObjectPath& objectName,
			      const Pegasus::CIMName& resultClass,
			      const Pegasus::String& role,
			      Pegasus::ObjectPathResponseHandler& handler);
  
  virtual void references(const Pegasus::OperationContext& context,
			  const Pegasus::CIMObjectPath& objectName,
			  const Pegasus::CIMName& resultClass,
			  const Pegasus::String& role,
			  const Pegasus::Boolean includeQualifiers,
			  const Pegasus::Boolean includeClassOrigin,
			  const Pegasus::CIMPropertyList& propertyList,
			  Pegasus::ObjectResponseHandler& handler);
  */
  
 private:
  int log_handle;
  void log(const Pegasus::String str);
  
};
 
}; // namespace ClusterMonitoring


#endif
