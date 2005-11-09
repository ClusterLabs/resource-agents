#include <Pegasus/Common/Config.h>
#include <Pegasus/Common/String.h>

#include "ClusterProvider.h"


using namespace Pegasus;

extern "C" PEGASUS_EXPORT Pegasus::CIMProvider* PegasusCreateProvider(const String &providerName)
{
  if (String::equalNoCase(providerName, "RedHatClusterProvider"))
    {
      return new ClusterMonitoring::ClusterProvider();
    }
  
  return (0);
}
