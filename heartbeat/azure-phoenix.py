#!/usr/bin/python

import sys, re, os, subprocess, time
import logging

OCF_SUCCESS = 0
OCF_ERR_GENERIC = 1
OCF_ERR_UNIMPLEMENTED = 3
OCF_ERR_CONFIGURED = 6
OCF_NOT_RUNNING = 7

AZR_VM_FENCED = 1
AZR_VM_NOT_FENCED = 2

OCF_RESOURCE_INSTANCE = None
PROCESS_EXEC_NAME = None
PROCESS_EXEC_ARG = None
PID_FILE = "azure-phoenix-{}.pid"

class AzureConfiguration:
    RGName = None
    VMName = None
    SubscriptionId = None
    Cloud = None
    UseMSI = None
    Tenantid = None
    ApplicationId = None
    ApplicationKey = None

def get_azure_config():
    config = AzureConfiguration()

    config.RGName = os.environ.get("OCF_RESKEY_resourceGroup")
    config.VMName = os.environ.get("OCF_RESKEY_vmName")
    config.SubscriptionId = os.environ.get("OCF_RESKEY_subscriptionId")
    config.Cloud = os.environ.get("OCF_RESKEY_cloud")
    config.UseMSI = os.environ.get("OCF_RESKEY_useMSI")
    config.Tenantid = os.environ.get("OCF_RESKEY_tenantId")
    config.ApplicationId = os.environ.get("OCF_RESKEY_applicationId")
    config.ApplicationKey = os.environ.get("OCF_RESKEY_applicationKey")

    if len(sys.argv) > 2:
        for x in range(2, len(sys.argv)):
            argument = sys.argv[x]
            if (argument.startswith("resourceGroup=")):
                config.RGName = argument.replace("resourceGroup=", "")
            elif (argument.startswith("vmName=")):
                config.VMName = argument.replace("vmName=", "")
            elif (argument.startswith("subscriptionId=")):
                config.SubscriptionId = argument.replace("subscriptionId=", "")
            elif (argument.startswith("cloud=")):
                config.Cloud = argument.replace("cloud=", "")
            elif (argument.startswith("useMSI=")):
                config.UseMSI = argument.replace("useMSI=", "")
            elif (argument.startswith("tenantId=")):
                config.Tenantid = argument.replace("tenantId=", "")
            elif (argument.startswith("applicationId=")):
                config.ApplicationId = argument.replace("applicationId=", "")
            elif (argument.startswith("applicationKey=")):
                config.ApplicationKey = argument.replace("applicationKey=", "")
            else:
                fail_usage("Unkown argument %s" % argument)
    
    return config

def check_azure_config(config):
    
    if not config.RGName:
        fail_usage("Parameter resourceGroup required.")
    if not config.VMName:
        fail_usage("Parameter vmName required.")
    if not config.SubscriptionId:
        fail_usage("Parameter subscriptionId required.")

    if not ocf_is_true(config.UseMSI):
        if not config.Tenantid:
            fail_usage("Parameter tenantId required if Service Principal should be used.")
        if not config.ApplicationId:
            fail_usage("Parameter applicationId required if Service Principal should be used.")
        if not config.ApplicationKey:
            fail_usage("Parameter applicationKey required if Service Principal should be used.")

    if config.Cloud and not (config.Cloud.lower() in ("china", "germany", "usgov")):
        fail_usage("Value %s for cloud parameter not supported. Supported values are china, germany and usgov" % config.Cloud)

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
    if ocf_is_true(config.UseMSI):
        from msrestazure.azure_active_directory import MSIAuthentication            
        credentials = MSIAuthentication(cloud_environment=cloud_environment)
    else:
        from azure.common.credentials import ServicePrincipalCredentials            
        credentials = ServicePrincipalCredentials(
            client_id = config.ApplicationId,
            secret = config.ApplicationKey,
            tenant = config.Tenantid,
            cloud_environment=cloud_environment
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


# https://stackoverflow.com/questions/715417/converting-from-a-string-to-boolean-in-python
def ocf_is_true(strValue):
    return strValue and strValue.lower() in ("yes", "true", "1", "YES", "TRUE", "ja", "on", "ON")

# https://stackoverflow.com/questions/32295395/how-to-get-the-process-name-by-pid-in-linux-using-python
def get_pname(id):
    p = subprocess.Popen(["ps -o cmd= {}".format(id)], stdout=subprocess.PIPE, shell=True)
    return str(p.communicate()[0]).strip()

# https://stackoverflow.com/questions/568271/how-to-check-if-there-exists-a-process-with-a-given-pid-in-python
def check_pid(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True

def get_pid_file():
    return PID_FILE.format(OCF_RESOURCE_INSTANCE)

def print_help():
    print "This resource agent is part of the fencing solution for Azure."
    print "    It implements the on operation of the Azure fencing."
    print ""
    print "Usage:"
    print "  azure-phoenix <action> resourceGroup=<val> vmName=<val> subscriptionId=<val>"
    print "    cloud=<val> useMSI=<val> tenantId=<val> applicationId=<val> applicationKey=<val>"
    print ""
    print "  action (required): Supported values are: start, stop, "
    print "                     monitor, meta-data, validate-all"
    print "  resourceGroup (required): Name of the resource group"
    print "  vmName (required): Name of the virtual machine that this"
    print "                     resource agent instance should unfence"
    print "  subscriptionId (required): Id of the Azure subscription"
    print "  cloud (optional): Name of the cloud you want to use. Supported values are"
    print "                    china, germany or usgov. Do not use this parameter if"
    print "                    you want to use public Azure."
    print "  useMSI (optional): Determines if Managed Service Identity should be used"
    print "                     instead of username and password (Service Principal)."
    print "                     If this parameter is specified, parameters"
    print "                     tenantId, applicationId and applicationKey are ignored."
    print "  tenantId (optional): Id of the Azure Active Directory tenant."
    print "                       Only required if a Service Principal should be used"
    print "  applicationId (optional): Application ID of the Service Principal."
    print "                            Only required if a Service Principal should be used"
    print "  applicationKey (optional): Authentication key of the Service Principal."
    print "                             Only required if a Service Principal should be used"

def print_metadata():
    print "<?xml version=\"1.0\"?>"
    print "<!DOCTYPE resource-agent SYSTEM \"ra-api-1.dtd\">"
    print "<resource-agent name=\"azure-phoenix\">"
    print "  <version>0.1</version>"
    print "  <longdesc lang=\"en\">"
    print "    This resource agent is part of the fencing solution for Azure. It implements the on operation of the Azure fencing."
    print "  </longdesc>"
    print "  <shortdesc lang=\"en\">Azure resource agent for fencing on</shortdesc>"
    print "  <parameters>"

    print "    <parameter name=\"resourceGroup\" unique=\"0\" required=\"1\">"
    print "      <longdesc lang=\"en\">"
    print "        Name of the resource group"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Name of the resource group</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"vmName\" unique=\"0\" required=\"1\">"
    print "      <longdesc lang=\"en\">"
    print "        Name of the virtual machine that this resource agent instance should unfence"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Name of the virtual machine</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"subscriptionId\" unique=\"0\" required=\"1\">"
    print "      <longdesc lang=\"en\">"
    print "        Id of the Azure subscription"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Id of the Azure subscription</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"cloud\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "        Name of the cloud you want to use. Supported values are china, germany or usgov. Do not use this parameter if you want to use public Azure."
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Name of the cloud you want to use.</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"useMSI\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "        Determines if Managed Service Identity should be used instead of username and password (Service Principal). If this parameter is specified, parameters tenantId, applicationId and applicationKey are ignored."
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Determines if Managed Service Identity should be used.</shortdesc>"
    print "      <content type=\"boolean\"/>"
    print "    </parameter>"

    print "    <parameter name=\"tenantId\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "        Id of the Azure Active Directory tenant. Only required if a Service Principal should be used"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Id of the Azure Active Directory tenant</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"applicationId\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "        Application ID of the Service Principal. Only required if a Service Principal should be used"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Application Id</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "    <parameter name=\"applicationKey\" unique=\"0\" required=\"0\">"
    print "      <longdesc lang=\"en\">"
    print "        Authentication key of the Service Principal. Only required if a Service Principal should be used"
    print "      </longdesc>"
    print "      <shortdesc lang=\"en\">Authentication key</shortdesc>"
    print "      <content type=\"string\"/>"
    print "    </parameter>"

    print "  </parameters>"
    print "  <actions>"
    print "    <action name=\"start\"        timeout=\"600\" />"
    print "    <action name=\"stop\"         timeout=\"20\" />"
    print "    <action name=\"monitor\"      timeout=\"20\" interval=\"10\" depth=\"0\" />"
    print "    <action name=\"meta-data\"    timeout=\"5\" />"
    print "  </actions>"
    print "</resource-agent>"

    return OCF_SUCCESS

def fail_usage(message):
    logging.error("%s Run with parameter help to get more information" % message)
    sys.exit(OCF_ERR_CONFIGURED)

def action_start(config):

    if (action_monitor() == OCF_SUCCESS):
        logging.info("Resource is already running")
        return OCF_SUCCESS
    
    if os.path.exists(get_pid_file()):
        os.remove(get_pid_file())

    file = open(get_pid_file(), "w")
    file.close()

    logging.info("Starting new process. Using pid file %s" % get_pid_file())
    p = subprocess.Popen(PROCESS_EXEC_ARG, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    logging.info("process started. pid %s" % p.pid)   
    file = open(get_pid_file(), "w") 
    file.write(str(p.pid))
    file.close()

    if get_fence_status(config) == AZR_VM_FENCED:
        return set_power_status(config)
    
    return OCF_SUCCESS

def action_stop():

    logging.info("action_stop: Testing if resource is running")
    if (action_monitor() == OCF_NOT_RUNNING):
        logging.info("action_stop: Resource is not running")
        return OCF_SUCCESS
    
    logging.info("action_stop: Resource is running. Removing pid file")
    if os.path.exists(get_pid_file()):
        os.remove(get_pid_file())
    else:
        logging.error("action_stop: pid file does not exist")
        return OCF_ERR_GENERIC

    logging.info("action_stop: pid file removed. Testing status")
    if (action_monitor() == OCF_NOT_RUNNING):
        logging.info("action_stop: Resource is not running")
        return OCF_SUCCESS
    else:
        logging.error("action_stop: stop failed. Resource still running")
        return OCF_ERR_GENERIC

def action_monitor():

    if os.path.exists(get_pid_file()):
        logging.info("action_monitor: pid file exist")
        file = open(get_pid_file(), "r") 
        strpid = file.read()
        file.close()

        if strpid and check_pid(int(strpid)):
            logging.info("action_monitor: process with pid %s is running" % strpid)
            name = get_pname(strpid)
            logging.info("action_monitor: process with pid %s has name '%s'" % (strpid, name))

            if (name == PROCESS_EXEC_NAME):
                return OCF_SUCCESS
            else:
                logging.info("action_monitor: no process with name '%s'" % PROCESS_EXEC_NAME)
                return OCF_NOT_RUNNING
        else:
            logging.info("action_monitor: no process for pid %s" % strpid)
            return OCF_NOT_RUNNING
    else:
        logging.info("action_monitor: pid file does not exist")
        return OCF_NOT_RUNNING

def get_fence_status(config):
    logging.info("{get_power_status} getting fence status for virtual machine %s" % config.VMName)
    result = AZR_VM_NOT_FENCED
        
    try:
        compute_client = get_azure_compute_client(config)
        network_client = get_azure_network_client(config)

        logging.info("{get_power_status} Testing VM state")            
        vm = compute_client.virtual_machines.get(config.RGName, config.VMName, "instanceView")
        
        allNICOK = True
        for nic in vm.network_profile.network_interfaces:
            match = re.match('(/subscriptions/([^/]*)/resourceGroups/([^/]*))(/providers/([^/]*/[^/]*)/([^/]*))?', nic.id)
            
            if match:
                logging.info("{get_power_status} Getting network interface.")
                nic = network_client.network_interfaces.get(match.group(3), match.group(6))
                logging.info("{get_power_status} Getting network interface done.")
                if nic.network_security_group:
                    nsgmatch = re.match('(/subscriptions/([^/]*)/resourceGroups/([^/]*))(/providers/([^/]*/[^/]*)/([^/]*))?', nic.network_security_group.id)
                    if nsgmatch:
                        logging.info("{get_power_status} Getting NSG.")
                        nsg = network_client.network_security_groups.get(nsgmatch.group(3), nsgmatch.group(6))                                
                        logging.info("{get_power_status} Getting NSG done.")

                        if len(nsg.network_interfaces) == 1 and ((not nsg.subnets) or len(nsg.subnets) == 0):
                            inboundOk = False
                            outboundOk = False
                            for rule in nsg.security_rules:                                    
                                if (rule.access == "Deny") and (rule.direction == "Inbound")  \
                                    and (rule.source_port_range == "*") and (rule.destination_port_range == "*") \
                                    and (rule.protocol == "*") and (rule.destination_address_prefix == "*") \
                                    and (rule.source_address_prefix == "*") and (rule.provisioning_state == "Succeeded") \
                                    and (rule.priority == 100) and (rule.name == "FENCE_DENY_ALL_INBOUND"):
                                    logging.info("{get_power_status} Inbound rule found.")
                                    inboundOk = True
                                elif (rule.access == "Deny") and (rule.direction == "Outbound")  \
                                    and (rule.source_port_range == "*") and (rule.destination_port_range == "*") \
                                    and (rule.protocol == "*") and (rule.destination_address_prefix == "*") \
                                    and (rule.source_address_prefix == "*") and (rule.provisioning_state == "Succeeded") \
                                    and (rule.priority == 100) and (rule.name == "FENCE_DENY_ALL_OUTBOUND"):
                                    logging.info("{get_power_status} Outbound rule found.")
                                    outboundOk = True
                            
                            nicOK = outboundOk and inboundOk
                            allNICOK = allNICOK & nicOK

                        elif len(nsg.network_interfaces) != 1:
                            fail_usage("{get_power_status} Network security group %s of network interface %s is used by multiple network interfaces. Virtual Machine %s cannot be fenced" % (nic.network_security_group.id, nic.id, vm.id ))
                        else:
                            fail_usage("{get_power_status} Network security group %s of network interface %s is also used by a subnet. Virtual Machine %s cannot be fenced" % (nic.network_security_group.id, nic.id, vm.id ))                        
                    else:
                        fail_usage("{get_power_status} Network Security Group id %s could not be parsed. Contact support" % nic.network_security_group.id)
                else:            
                    fail_usage("{get_power_status} Network interface id %s does not have a network security group." % nic.id)
            else:
                fail_usage("{get_power_status} Network interface id %s could not be parsed. Contact support" % nic.id)
    except Exception as e:
        fail_usage("{get_power_status} Failed: %s" % e)

    if allNICOK:
        logging.info("{get_power_status} All network interface have inbound and outbound deny all rules. Declaring VM as off")
        result = AZR_VM_FENCED
    
    logging.info("{get_power_status} result is %s (AZR_VM_FENCED: %s, AZR_VM_NOT_FENCED: %s)" % (result, AZR_VM_FENCED, AZR_VM_NOT_FENCED))
    return result

def set_power_status(config):    
    logging.info("set_power_status: unfencing virtual machine %s in resource group %s" % (config.VMName, config.RGName))
    
    try:
        compute_client = get_azure_compute_client(config)
        network_client = get_azure_network_client(config)

        while True:
            powerState = "unknown"
            provState = "unknown"
            vm = compute_client.virtual_machines.get(config.RGName, config.VMName, "instanceView")
            for status in vm.instance_view.statuses:
                if status.code.startswith("PowerState"):
                    powerState = status.code.replace("PowerState/", "")
                if status.code.startswith("ProvisioningState"):
                    provState = status.code.replace("ProvisioningState/", "")
        
            logging.info("Testing VM state: ProvisioningState %s, PowerState %s" % (provState, powerState))
            
            if (provState.lower() == "succeeded" and (powerState.lower() == "deallocated" or powerState.lower() == "stopped")):
                break
            elif (provState.lower() == "succeeded" and not (powerState.lower() == "deallocated" or powerState.lower() == "stopped")):
                fail_usage("Virtual machine %s needs to be deallocated or stopped to be unfenced" % (vm.id))                        
            elif (provState.lower() == "failed" or provState.lower() == "canceled"):
                fail_usage("Virtual machine operation %s failed or canceled. Virtual machine %s needs to be deallocated or stopped to be unfenced" % (vm.id))
            else:
                time.sleep(10)

        for nic in vm.network_profile.network_interfaces:
            match = re.match('(/subscriptions/([^/]*)/resourceGroups/([^/]*))(/providers/([^/]*/[^/]*)/([^/]*))?', nic.id)
            
            if match:
                logging.info("Getting network interface.")
                nic = network_client.network_interfaces.get(match.group(3), match.group(6))
                logging.info("Getting network interface done.")
                if nic.network_security_group:
                    nsgmatch = re.match('(/subscriptions/([^/]*)/resourceGroups/([^/]*))(/providers/([^/]*/[^/]*)/([^/]*))?', nic.network_security_group.id)
                    if nsgmatch:
                        logging.info("Getting NSG.")
                        nsg = network_client.network_security_groups.get(nsgmatch.group(3), nsgmatch.group(6))                                
                        logging.info("Getting NSG done.")

                        if len(nsg.network_interfaces) == 1 and ((not nsg.subnets) or len(nsg.subnets) == 0):
                            rulesCountBefore = len(nsg.security_rules)
                            logging.info("Rules cound before %s" % rulesCountBefore)
                            nsg.security_rules[:] = [rule for rule in nsg.security_rules if \
                                rule.name != "FENCE_DENY_ALL_INBOUND" and rule.name != "FENCE_DENY_ALL_OUTBOUND"]
                            rulesCountAfter = len(nsg.security_rules)
                            logging.info("Rules cound after %s" % rulesCountAfter)                                

                            if (rulesCountBefore != rulesCountAfter):
                                logging.info("Updating %s" % nsg.name)                               
                                op = network_client.network_security_groups.create_or_update(nsgmatch.group(3), nsg.name, nsg)
                                logging.info("Updating of %s started - waiting" % nsg.name)
                                op.wait()
                                logging.info("Updating of %s done" % nsg.name)
                            else:
                                logging.info("No update of NSG since nothing changed")

                        elif len(nsg.network_interfaces) != 1:
                            fail_usage("Network security group %s of network interface %s is used by multiple network interfaces. Virtual Machine %s cannot be fenced" % (nic.network_security_group.id, nic.id, vm.id ))
                        else:
                            fail_usage("Network security group %s of network interface %s is also used by a subnet. Virtual Machine %s cannot be fenced" % (nic.network_security_group.id, nic.id, vm.id ))                        
                    else:
                        fail_usage("Network Security Group id %s could not be parsed. Contact support" % nic.network_security_group.id)
                else:            
                    fail_usage("Network interface id %s does not have a network security group." % nic.id)
            else:
                fail_usage("Network interface id %s could not be parsed. Contact support" % nic.id)

        logging.info("Starting virtual machine %s in resource group %s" % (config.VMName, config.RGName))
        waitOp = compute_client.virtual_machines.start(config.RGName, config.VMName, raw=True)
        if waitOp.response.status_code < 200 or waitOp.response.status_code > 202:
            fail_usage("Response code is %s. Must be 200, 201 or 202" % waitOp.response.status_code)

        logging.info("Virtual machine starting up. Status is %s" % (waitOp.response.status_code))
    except ImportError as ie:
        logging.error("Azure Resource Manager Python SDK not found or not accessible: %s" % re.sub("^, ", "", str(ie)))
        return OCF_ERR_GENERIC
    except Exception as e:
        logging.error("Failed: %s" % re.sub("^, ", "", str(e)))
        return OCF_ERR_GENERIC

    return OCF_SUCCESS

def action_validate_all(config):
    logging.info("action_validate_all")

    try:
        compute_client = get_azure_compute_client(config)
        vm = compute_client.virtual_machines.get(config.RGName, config.VMName, "instanceView")
    except Exception as e:
        fail_usage("{get_fence_status} Failed: %s" % e)
    
    return OCF_SUCCESS

def main():
    logging.basicConfig(filename='example.log',level=logging.INFO)

    global OCF_RESOURCE_INSTANCE
    global PROCESS_EXEC_NAME
    global PROCESS_EXEC_ARG

    OCF_RESOURCE_INSTANCE = os.environ.get("OCF_RESOURCE_INSTANCE")
    if not OCF_RESOURCE_INSTANCE:
        OCF_RESOURCE_INSTANCE = "unknown"
    
    PROCESS_EXEC_NAME = 'tailf {}'.format(get_pid_file())
    PROCESS_EXEC_ARG = ['tailf', get_pid_file()]

    action = None
    if len(sys.argv) > 1:
            action = sys.argv[1]
    else:
        print_help()

    logging.info("action is %s" % action)

    result = OCF_ERR_UNIMPLEMENTED
    if action == "meta-data":
        result = print_metadata()
    else:
        config = get_azure_config()
        check_azure_config(config)

        if action == "monitor":
            result = action_monitor()
        elif action == "stop":
            result = action_stop()
        elif action == "start":
            result = action_start(config)
        elif action == "validate-all":
            result = action_validate_all(config)
        elif action:
            result = OCF_ERR_UNIMPLEMENTED 

    logging.info("Done %s" % result)
    sys.exit(result)

if __name__ == "__main__":
    main()
