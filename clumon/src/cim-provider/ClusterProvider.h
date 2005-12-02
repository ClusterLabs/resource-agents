/*
  Copyright Red Hat, Inc. 2005

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/*
 * Author: Stanko Kupcevic <kupcevic@redhat.com>
 */


#ifndef ClusterProvider_h
#define ClusterProvider_h

#include <Pegasus/Common/Config.h>
#include <Pegasus/Provider/CIMInstanceProvider.h>
#include <Pegasus/Provider/CIMAssociationProvider.h>

#include "Cluster.h"
#include "ClusterMonitor.h"


namespace ClusterMonitoring
{


#define CLUSTER_PROVIDER_CLASSNAME Pegasus::String("RedHatClusterProvider")
#define CLUSTER_SERVICE_CLASSNAME "RedHat_ClusterFailoverService"
#define CLUSTER_NODE_CLASSNAME "RedHat_ClusterNode"
#define CLUSTER_CLASSNAME "RedHat_Cluster"
//#define CLUSTER_PARTICIPATING_NODE_CLASSNAME "RedHat_ClusterParticipatingNode"
//#define CLUSTER_HOSTING_FAILOVER_SERVICE_CLASSNAME "RedHat_ClusterHostingFailoverService"
//#define CLUSTER_NODE_HOSTING_FAILOVER_SERVICE_CLASSNAME "RedHat_ClusterNodeHostingFailoverService"



class ClusterProvider : 
  public Pegasus::CIMInstanceProvider  //, public Pegasus::CIMAssociationProvider
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
  void log(const Pegasus::String& str);
  ClusterMonitor _monitor;
  
};  // class ClusterProvider
 
 
}; // namespace ClusterMonitoring


#endif
