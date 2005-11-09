import gettext
_ = gettext.gettext



CLUSTER_CONF_FILE="cluster.conf"
CLUSTER_CONF_DIR_PATH="/etc/cluster/"
CLUSTER_CONF_PATH = CLUSTER_CONF_DIR_PATH + CLUSTER_CONF_FILE
ROTATE_BACKUP_EXT = ".bak."


DLM_TYPE = 0
GULM_TYPE = 1

CLUSTER_TYPE=1
CLUSTER_NODES_TYPE=2
CLUSTER_NODE_TYPE=3
FENCE_TYPE=4
FENCE_DEVICES_TYPE=5
FENCE_DEVICE_TYPE=6
MANAGED_RESOURCES_TYPE=7
FAILOVER_DOMAINS_TYPE=8
FAILOVER_DOMAIN_TYPE=9
RESOURCE_GROUPS_TYPE=10
RESOURCE_GROUP_TYPE=11
RESOURCES_TYPE=12
RESOURCE_TYPE=14
F_NODE_TYPE=15
F_LEVEL_TYPE=16
F_FENCE_TYPE=17
                                                                                
NAME_COL = 0
TYPE_COL = 1
OBJ_COL = 2

#DISPLAY COLORS
CLUSTER_COLOR="black"
CLUSTERNODES_COLOR="#0033FF"
CLUSTERNODE_COLOR="#0099FF"
FENCEDEVICES_COLOR="#990000"
FENCEDEVICE_COLOR="#CC0000"
FAILOVERDOMAINS_COLOR="#6600CC"
FAILOVERDOMAIN_COLOR="#9933CC"
RESOURCES_COLOR="#006600"
RESOURCE_COLOR="#00CC00"
RESOURCEGROUPS_COLOR="#FF6600"
RESOURCEGROUP_COLOR="#FF8800"

#Attribute Strings
VOTES_ATTR="votes"
NAME_ATTR="name"

ONE_VOTE=_("1")

#Fence Daemon Default Values
POST_JOIN_DEFAULT = "3"
POST_FAIL_DEFAULT = "0"
CLEAN_START_DEFAULT = "0"

SELECT_RC_TYPE=_("<span><b>Select a Resource Type:</b></span>")

RC_PROPS=_("Properties for %s Resource: %s")

MODIFIED_FILE=_("<modified>")
NEW_CONFIG=_("<New Configuration>")

#XML_CONFIG_ERROR=_("A problem was encountered while reading configuration file %s . Details or the error appear below. Click the \'Cancel\' button to quit the application. Click the \'New\' button to create a new configuration file. To continue anyway (Not Recommended!), click the \'Ok\' button.")
XML_CONFIG_ERROR=_("A problem was encountered while reading configuration file %s . Details or the error appear below. Click the \'New\' button to create a new configuration file. To continue anyway (Not Recommended!), click the \'Ok\' button.") 

SWITCH_TO_GULM=_("Change to GuLM Lockserver")
SWITCH_TO_DLM=_("Change to Distributed Lock Manager")
SWITCH_TO_BROADCAST=_("Change to Broadcast Mode for Cluster Manager")
SWITCH_TO_MULTICAST=_("Change to Multicast Mode for Cluster Manager")

DANGER_REBOOT_CLUSTER=_("This operation, if performed, will require the cluster to be completely stopped and restarted. Are you certain that you wish to proceed with this change? If so, press 'Yes'.")
