#!@PYTHON@ -tt
import logging, re, time
import msrestazure.azure_exceptions

FENCE_SUBNET_NAME = "fence-subnet"
FENCE_INBOUND_RULE_NAME = "FENCE_DENY_ALL_INBOUND"
FENCE_INBOUND_RULE_DIRECTION = "Inbound"
FENCE_OUTBOUND_RULE_NAME = "FENCE_DENY_ALL_OUTBOUND"
FENCE_OUTBOUND_RULE_DIRECTION = "Outbound"
VM_STATE_POWER_PREFIX = "PowerState"
VM_STATE_POWER_DEALLOCATED = "deallocated"
VM_STATE_POWER_STOPPED = "stopped"
VM_STATE_POWER_RUNNING = "running"
VM_STATE_POWER_STARTING = "starting"
FENCE_STATE_OFF = "off"
FENCE_STATE_ON = "on"
FENCE_TAG_SUBNET_ID = "FENCE_TAG_SUBNET_ID"
FENCE_TAG_IP_TYPE = "FENCE_TAG_IP_TYPE"
FENCE_TAG_IP = "FENCE_TAG_IP"
IP_TYPE_DYNAMIC = "Dynamic"
MAX_RETRY = 10
RETRY_WAIT = 5

class AzureSubResource:
    Type = None
    Name = None

class AzureResource:
    Id = None
    SubscriptionId = None
    ResourceGroupName = None
    ResourceName = None
    SubResources = []

class AzureConfiguration:
    RGName = None
    VMName = None
    SubscriptionId = None
    Cloud = None
    UseMSI = None
    Tenantid = None
    ApplicationId = None
    ApplicationKey = None
    Verbose = None

def fail_usage(message):
    raise ValueError("%s Run with parameter help to get more information" % message)

# https://stackoverflow.com/questions/715417/converting-from-a-string-to-boolean-in-python
def ocf_is_true(strValue):
    return strValue and strValue.lower() in ("yes", "true", "1", "YES", "TRUE", "ja", "on", "ON")

def get_azure_resource(id):
    match = re.match('(/subscriptions/([^/]*)/resourceGroups/([^/]*))(/providers/([^/]*/[^/]*)/([^/]*))?((/([^/]*)/([^/]*))*)', id)
    if not match:
        fail_usage("{get_azure_resource}: cannot parse resource id %s" % id)

    logging.debug("{get_azure_resource} found %s matches for %s" % (len(match.groups()), id))
    iGroup = 0
    while iGroup < len(match.groups()):
        logging.debug("{get_azure_resource} group %s: %s" %(iGroup, match.group(iGroup)))
        iGroup += 1

    resource = AzureResource()
    resource.Id = id
    resource.SubscriptionId = match.group(2)
    resource.SubResources = []

    if len(match.groups()) > 3:        
        resource.ResourceGroupName = match.group(3)
        logging.debug("{get_azure_resource} resource group %s" % resource.ResourceGroupName)
    
    if len(match.groups()) > 6:
        resource.ResourceName = match.group(6)    
        logging.debug("{get_azure_resource} resource name %s" % resource.ResourceName)
    
    if len(match.groups()) > 7 and match.group(7):        
        splits = match.group(7).split("/")
        logging.debug("{get_azure_resource} splitting subtypes '%s' (%s)" % (match.group(7), len(splits)))
        i = 1 # the string starts with / so the first split is empty
        while i < len(splits) - 1:
            logging.debug("{get_azure_resource} creating subresource with type %s and name %s" % (splits[i], splits[i+1]))
            subRes = AzureSubResource()
            subRes.Type = splits[i]
            subRes.Name = splits[i+1]
            resource.SubResources.append(subRes)
            i += 2

    return resource

def get_fence_subnet_for_config(ipConfig, network_client):
    subnetResource = get_azure_resource(ipConfig.subnet.id)
    logging.debug("{get_fence_subnet_for_config} testing virtual network %s in resource group %s for a fence subnet" %(subnetResource.ResourceName, subnetResource.ResourceGroupName))
    vnet = network_client.virtual_networks.get(subnetResource.ResourceGroupName, subnetResource.ResourceName)
    return get_subnet(vnet, FENCE_SUBNET_NAME)

def get_subnet(vnet, subnetName):
    for avSubnet in vnet.subnets:
        logging.debug("{get_subnet} searching subnet %s testing subnet %s" % (subnetName, avSubnet.name))
        if (avSubnet.name.lower() == subnetName.lower()):
                logging.debug("{get_subnet} subnet found %s" % avSubnet)
                return avSubnet

def test_fence_subnet(fenceSubnet, nic, network_client):
    logging.info("{test_fence_subnet}")
    testOk = True
    if not fenceSubnet:
        testOk = False
        logging.info("{test_fence_subnet} No fence subnet found for virtual network of network interface %s" % nic.id)
    else:
        if not fenceSubnet.network_security_group:
            testOk = False
            logging.info("{test_fence_subnet} Fence subnet %s has not network security group" % fenceSubnet.id)
        else:
            nsgResource = get_azure_resource(fenceSubnet.network_security_group.id)
            logging.info("{test_fence_subnet} Getting network security group %s in resource group %s" % (nsgResource.ResourceName, nsgResource.ResourceGroupName))
            nsg = network_client.network_security_groups.get(nsgResource.ResourceGroupName, nsgResource.ResourceName)
            inboundRule = get_inbound_rule_for_nsg(nsg)
            outboundRule = get_outbound_rule_for_nsg(nsg)
            if not outboundRule:
                testOk = False
                logging.info("{test_fence_subnet} Network Securiy Group %s of fence subnet %s has no outbound security rule that blocks all traffic" % (nsgResource.ResourceName, fenceSubnet.id))
            elif not inboundRule:
                testOk = False
                logging.info("{test_fence_subnet} Network Securiy Group %s of fence subnet %s has no inbound security rule that blocks all traffic" % (nsgResource.ResourceName, fenceSubnet.id))
    
    return testOk

def get_inbound_rule_for_nsg(nsg):
    return get_rule_for_nsg(nsg, FENCE_INBOUND_RULE_NAME, FENCE_INBOUND_RULE_DIRECTION)

def get_outbound_rule_for_nsg(nsg):
    return get_rule_for_nsg(nsg, FENCE_OUTBOUND_RULE_NAME, FENCE_OUTBOUND_RULE_DIRECTION)

def get_rule_for_nsg(nsg, ruleName, direction):
    logging.info("{get_rule_for_nsg} Looking for security rule %s with direction %s" % (ruleName, direction))
    if not nsg:
        logging.info("{get_rule_for_nsg} Network security group not set")
        return None

    for rule in nsg.security_rules:
        logging.info("{get_rule_for_nsg} Testing a %s securiy rule %s" % (rule.direction, rule.name))
        if (rule.access == "Deny") and (rule.direction == direction)  \
                and (rule.source_port_range == "*") and (rule.destination_port_range == "*") \
                and (rule.protocol == "*") and (rule.destination_address_prefix == "*") \
                and (rule.source_address_prefix == "*") and (rule.provisioning_state == "Succeeded") \
                and (rule.priority == 100) and (rule.name == ruleName):
            logging.info("{get_rule_for_nsg} %s rule found" % direction)
            return rule

    return None

def get_vm_state(vm, prefix):
    for status in vm.instance_view.statuses:
        if status.code.startswith(prefix):
            return status.code.replace(prefix + "/", "").lower()

    return None

def get_vm_power_state(vm):
    return get_vm_state(vm, VM_STATE_POWER_PREFIX)

def get_power_status_impl(compute_client, network_client, rgName, vmName):
    result = FENCE_STATE_ON

    try:
        logging.info("{get_power_status_impl} Testing VM state")
        vm = compute_client.virtual_machines.get(rgName, vmName, "instanceView")
        powerState = get_vm_power_state(vm)
        if powerState == VM_STATE_POWER_DEALLOCATED:
            return FENCE_STATE_OFF
        if powerState == VM_STATE_POWER_STOPPED:
            return FENCE_STATE_OFF
        
        allNICOK = True
        for nicRef in vm.network_profile.network_interfaces:
            nicresource = get_azure_resource(nicRef.id)
            nic = network_client.network_interfaces.get(nicresource.ResourceGroupName, nicresource.ResourceName)
            for ipConfig in nic.ip_configurations:
                logging.info("{get_power_status_impl} Testing ip configuration %s" % ipConfig.name)
                fenceSubnet = get_fence_subnet_for_config(ipConfig, network_client)
                testOk = test_fence_subnet(fenceSubnet, nic, network_client)
                if not testOk:
                    allNICOK = False
                elif fenceSubnet.id.lower() != ipConfig.subnet.id.lower():
                    logging.info("{get_power_status_impl} IP configuration %s is not in fence subnet (ip subnet: %s, fence subnet: %s)" % (ipConfig.name, ipConfig.subnet.id.lower(), fenceSubnet.id.lower()))
                    allNICOK = False
        if allNICOK:
            logging.info("{get_power_status_impl} All IP configurations of all network interfaces are in the fence subnet. Declaring VM as off")
            result = FENCE_STATE_OFF
    except Exception as e:
        fail_usage("{get_power_status_impl} Failed: %s" % e)
    
    return result

def set_power_status_off(compute_client, network_client, rgName, vmName):
    logging.info("{set_power_status_off} Fencing %s in resource group %s" % (vmName, rgName))
                          
    vm = compute_client.virtual_machines.get(rgName, vmName, "instanceView")
    
    operations = []
    for nicRef in vm.network_profile.network_interfaces:
        nicresource = get_azure_resource(nicRef.id)
        nic = network_client.network_interfaces.get(nicresource.ResourceGroupName, nicresource.ResourceName)
        if not nic.tags:
            nic.tags = {}

        for ipConfig in nic.ip_configurations: 
            fenceSubnet = get_fence_subnet_for_config(ipConfig, network_client)
            testOk = test_fence_subnet(fenceSubnet, nic, network_client)
            if testOk:
                logging.info("{set_power_status_off} Changing subnet of ip config of nic %s" % nic.id)
                nic.tags[("%s_%s" % (FENCE_TAG_SUBNET_ID, ipConfig.name))] = ipConfig.subnet.id
                nic.tags[("%s_%s" % (FENCE_TAG_IP_TYPE, ipConfig.name))] = ipConfig.private_ip_allocation_method
                nic.tags[("%s_%s" % (FENCE_TAG_IP, ipConfig.name))] = ipConfig.private_ip_address
                ipConfig.subnet = fenceSubnet
                ipConfig.private_ip_allocation_method = IP_TYPE_DYNAMIC
            else:            
                fail_usage("{set_power_status_off} Network interface id %s does not have a network security group." % nic.id)
        
        op = network_client.network_interfaces.create_or_update(nicresource.ResourceGroupName, nicresource.ResourceName, nic)
        operations.append(op)
    
    # while True:
    #     vm = compute_client.virtual_machines.get(rgName, vmName, "instanceView")
    #     if get_vm_power_state(vm) == VM_STATE_POWER_STARTING:
    #         break
    #     else:
    #         logging.info("{set_power_status_off} waiting for starting state")
    #         time.sleep(5)     

    iCount = 1
    for waitOp in operations:
       logging.info("{set_power_status} Waiting for network update operation (%s/%s)" % (iCount, len(operations)))                    
       waitOp.wait()
       logging.info("{set_power_status} Waiting for network update operation (%s/%s) done" % (iCount, len(operations)))
       iCount += 1

def set_power_status_on(compute_client, network_client, rgName, vmName):
    logging.info("{set_power_status_on} Unfencing %s in resource group %s" % (vmName, rgName))

    vm = None          
    while True:
        vm = compute_client.virtual_machines.get(rgName, vmName, "instanceView")
        if get_vm_power_state(vm) == VM_STATE_POWER_RUNNING:
            break
        else:
            logging.info("{set_power_status_on} waiting for running state")
            time.sleep(10)                
    
    operations = []
    for nicRef in vm.network_profile.network_interfaces:
        attempt = 0
        while attempt < MAX_RETRY:
            attempt += 1
            try:
                nicresource = get_azure_resource(nicRef.id)
                nic = network_client.network_interfaces.get(nicresource.ResourceGroupName, nicresource.ResourceName)
                logging.info("{set_power_status_on} Searching for tags required to unfence this virtual machine")
                for ipConfig in nic.ip_configurations:

                    if not nic.tags:
                        fail_usage("{set_power_status_on}: IP configuration %s is missing the required resource tags (empty)" % ipConfig.name)

                    subnetId = nic.tags.pop("%s_%s" % (FENCE_TAG_SUBNET_ID, ipConfig.name))
                    ipType = nic.tags.pop("%s_%s" % (FENCE_TAG_IP_TYPE, ipConfig.name))
                    ipAddress = nic.tags.pop("%s_%s" % (FENCE_TAG_IP, ipConfig.name))

                    if (subnetId and ipType and (ipAddress or (ipType.lower() == IP_TYPE_DYNAMIC.lower()))):
                        logging.info("{set_power_status_on} tags found (subnetId: %s, ipType: %s, ipAddress: %s)" % (subnetId, ipType, ipAddress))

                        subnetResource = get_azure_resource(subnetId)
                        vnet = network_client.virtual_networks.get(subnetResource.ResourceGroupName, subnetResource.ResourceName)
                        logging.info("{set_power_status_on} looking for subnet %s" % len(subnetResource.SubResources))
                        oldSubnet = get_subnet(vnet, subnetResource.SubResources[0].Name)
                        if not oldSubnet:
                            fail_usage("{set_power_status_on}: subnet %s not found" % subnetId)

                        ipConfig.subnet = oldSubnet
                        ipConfig.private_ip_allocation_method = ipType
                        if ipAddress:
                            ipConfig.private_ip_address = ipAddress
                    else:
                        fail_usage("{set_power_status_on}: IP configuration %s is missing the required resource tags(subnetId: %s, ipType: %s, ipAddress: %s)" % (ipConfig.name, subnetId, ipType, ipAddress))
                
                logging.info("{set_power_status_on} updating nic %s" % (nic.id))
                op = network_client.network_interfaces.create_or_update(nicresource.ResourceGroupName, nicresource.ResourceName, nic)
                operations.append(op)
                break
            except msrestazure.azure_exceptions.CloudError as cex:
                logging.error("{set_power_status_on} CloudError in attempt %s '%s'" % (attempt, cex))
                if cex.error and cex.error.error and cex.error.error.lower() == "PrivateIPAddressIsBeingCleanedUp":
                    logging.error("{set_power_status_on} PrivateIPAddressIsBeingCleanedUp")
                time.sleep(RETRY_WAIT)

            except Exception as ex:
                logging.error("{set_power_status_on} Exception of type %s: %s" % (type(ex).__name__, ex))
                break

    while True:
        vm = compute_client.virtual_machines.get(rgName, vmName, "instanceView")
        if get_vm_power_state(vm) == VM_STATE_POWER_STARTING:
            break
        else:
            logging.info("{set_power_status_on} waiting for starting state")
            time.sleep(5)

def get_azure_cloud_environment(config):
    cloud_environment = None
    if config.Cloud:
        if (config.Cloud.lower() == "china"):
            from msrestazure.azure_cloud import AZURE_CHINA_CLOUD
            cloud_environment = AZURE_CHINA_CLOUD
        elif (config.Cloud.lower() == "germany"):
            from msrestazure.azure_cloud import AZURE_GERMAN_CLOUD
            cloud_environment = AZURE_GERMAN_CLOUD
        elif (config.Cloud.lower() == "usgov"):
            from msrestazure.azure_cloud import AZURE_US_GOV_CLOUD
            cloud_environment = AZURE_US_GOV_CLOUD

    return cloud_environment

def get_azure_credentials(config):
    credentials = None
    cloud_environment = get_azure_cloud_environment(config)
    if ocf_is_true(config.UseMSI) and cloud_environment:
        from msrestazure.azure_active_directory import MSIAuthentication            
        credentials = MSIAuthentication(cloud_environment=cloud_environment)
    elif ocf_is_true(config.UseMSI):
        from msrestazure.azure_active_directory import MSIAuthentication            
        credentials = MSIAuthentication()
    elif cloud_environment:
        from azure.common.credentials import ServicePrincipalCredentials            
        credentials = ServicePrincipalCredentials(
            client_id = config.ApplicationId,
            secret = config.ApplicationKey,
            tenant = config.Tenantid,
            cloud_environment=cloud_environment
        )
    else:
        from azure.common.credentials import ServicePrincipalCredentials            
        credentials = ServicePrincipalCredentials(
            client_id = config.ApplicationId,
            secret = config.ApplicationKey,
            tenant = config.Tenantid
        )

    return credentials

def get_azure_compute_client(config):
    from azure.mgmt.compute import ComputeManagementClient

    cloud_environment = get_azure_cloud_environment(config)
    credentials = get_azure_credentials(config)

    if cloud_environment:
        compute_client = ComputeManagementClient(
            credentials,
            config.SubscriptionId,
            base_url=cloud_environment.endpoints.resource_manager
        )
    else:
        compute_client = ComputeManagementClient(
            credentials,
            config.SubscriptionId
        )
    return compute_client

def get_azure_network_client(config):
    from azure.mgmt.network import NetworkManagementClient

    cloud_environment = get_azure_cloud_environment(config)
    credentials = get_azure_credentials(config)

    if cloud_environment:
        network_client = NetworkManagementClient(
            credentials,
            config.SubscriptionId,
            base_url=cloud_environment.endpoints.resource_manager
        )
    else:
        network_client = NetworkManagementClient(
            credentials,
            config.SubscriptionId
        )
    return network_client