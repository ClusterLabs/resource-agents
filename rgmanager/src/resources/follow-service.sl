% follow-service.sl
%
% Description:	Implements the "follow service" mechanism based on the Red Hat RIND event 
%               scripting mechanism.
%
% Author:       Marc Grimme, Mark Hlawatschek, October 2008
% Support:      support@atix.de
% License:      GNU General Public License (GPL), version 2 or later
% Copyright:    (c) 2008-2010 ATIX AG


debug("*** follow-service.sl");


%
% Returns a list of nodes for the given service that are online and in the failoverdomain.
%
define nodelist_online(service_name) {
   variable nodes, nofailback, restricted, ordered, node_list;
   nodes=nodes_online();
   
   (nofailback, restricted, ordered, node_list) = service_domain_info(service_name);
   
   return intersection(nodes, node_list);
}

%
% Idea: 
%   General purpose function of a construct when Service(svc1) and Service(svc2) 
%   should not be running on the same node even after failover.
%   There are to options to influence the behaviour. If both services have to be 
%   running on the same node (only one node is left in the failovergroup) what 
%   service is the master and should both services be running or only the master
%   service survives. If master is not svc1 or svc2 both service might run on the 
%   same node. If master is either svc1 or svc2 the specified one will be the 
%   surviving service.
%   If followslave is not 0 the svc1 always follows svc2. That means it will be 
%   started on on the same node as svc1. And if available svc2 will be relocated
%   to any other node.
%
define follow_service(svc1, svc2, master) %, followslave)
{
	variable state_svc1, state_svc2, owner_svc1, owner_svc2;
	variable nodes1, nodes2, allowed;

	debug("*** FOLLOW_SERVICE: follow_service(",svc1,", ",svc2,", ", master, ")");
	debug("*** FOLLOW_SERVICE: event_type: ", event_type, ", service_name: ", service_name, ", service_state: ", service_state);

	%
	% setup the master
	%
	if ((master != svc1) and (master != svc2)) {
		debug("*** FOLLOW_SERVICE: master=NULL");
		master=NULL;
	}

	% get infos we need to decide further
	(,,, owner_svc1, state_svc1) = service_status(svc1);
	(,,, owner_svc2, state_svc2) = service_status(svc2);
	nodes1 = nodelist_online(svc1);
	nodes2 = nodelist_online(svc2);
	debug("*** FOLLOW_SERVICE: service_status(",svc1,"): ", state_svc1);
	debug("*** FOLLOW_SERVICE: owner_svc1: ", owner_svc1, ", owner_svc2: ", owner_svc2, ", nodes1: ", nodes1, ", nodes2: ", nodes2);

	if (((event_type == EVENT_NODE)    and (owner_svc1 == node_id) and (node_state == NODE_OFFLINE) and (owner_svc2 >=0)) or 
		((event_type == EVENT_SERVICE) and (service_name == svc1)  and (service_state == "recovering" ) and (owner_svc2 >= 0))) {
		%
		% uh oh, the owner of the master server died.  Restart it
		% on the node running the slave server or if we should not 
		% follow the slave start it somewhere else.
		% We should end up here if svc1 has to be restarted

		%
		% If this was a service event, don't execute the default event
		% script trigger after this script completes.
		%
		if (event_type == EVENT_SERVICE) {
			stop_processing();
		}
		% were to start svc2
		allowed=subtract(nodes2, owner_svc2);
		if (length(allowed) > 1) {
			allowed=subtract(allowed, service_last_owner);
		}
		debug("*** FOLLOW SERVICE: service event triggered following svc2 to ",owner_svc2, " svc2 on : ",allowed);

		% either svc1 is the master or there are node were to start svc2
		if ((master == svc1) or (length(allowed) > 0)) {
			()=service_start(svc1, owner_svc2);
		}
		% either svc2 is the master or there are node were to start svc2
		if ((master == svc2) or (length(allowed) > 0)) {
			()=service_stop(svc2);
			()=service_start(svc2, allowed);
		} 
	}
	else if (((event_type == EVENT_NODE) and (owner_svc2 == node_id) and (node_state == NODE_OFFLINE) and (owner_svc2 >=0)) or 
		((event_type == EVENT_SERVICE) and (service_name == svc2) and (service_state == "recovering" ) and (owner_svc1 >= 0))) {
		%
		% uh oh, the owner of the svc2 died.  Restart it
		% on any other node but not the one running the svc1.
		% If svc1 is the only one left only start it there 
		% if master==svc2
		%
		% Just relocate svc2 or if svc2 is master stop svc1 and start svc2 on owner_svc1

		%
		% If this was a service event, don't execute the default event
		% script trigger after this script completes.
		%
	  
		if (event_type == EVENT_SERVICE) {
			stop_processing();
		}

		allowed=subtract(nodes2, owner_svc1);
		if (length(allowed) > 1) {
			allowed=subtract(allowed, service_last_owner);
		}

		debug("*** FOLLOW SERVICE: service event triggered relocating svc2 to ",allowed, " svc1 on : ",owner_svc1);

		if (length(allowed) > 0) {
			()=service_stop(svc2);
			()=service_start(svc2, allowed);
		} else if (master == svc2) {
			()=service_stop(svc1);
			()=service_start(svc2, owner_svc1);
		}
	}
	else if (((event_type == EVENT_SERVICE) and (service_state == "started") and (owner_svc2 == owner_svc1) and (owner_svc1 > 0) and (owner_svc2 > 0)) or
    		((event_type == EVENT_CONFIG) and (owner_svc2 == owner_svc1))) {
		allowed=subtract(nodes2, owner_svc1);
		debug("*** FOLLOW SERVICE: service event both running on same node triggered.", allowed);
		if (length(allowed) > 0) {
			%()=service_stop(svc1);
			%()=service_start(svc1, owner_svc2);
			()=service_stop(svc2);
			()=service_start(svc2, allowed);
		} else if ((master == svc2) and (owner_svc2 > 0)){
			debug("*** FOLLOW SERVICE: will stop service .", svc1); 
			()=service_stop(svc1);
		} else if ((master == svc1) and (owner_svc1 > 0)) {
			debug("*** FOLLOW SERVICE: will stop service .", svc2);
			()=service_stop(svc2);
		} else {
			debug("*** FOLLOW SERVICE: both services running on the same node or only one is running.", allowed, ", ", master);
		}
	}
	return;
}
