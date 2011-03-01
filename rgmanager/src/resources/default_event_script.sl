%
% Copyright (C) 1997-2003 Sistina Software, Inc.  All rights reserved.
% Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
%
% This program is free software; you can redistribute it and/or
% modify it under the terms of the GNU General Public License
% as published by the Free Software Foundation; either version 2
% of the License, or (at your option) any later version.
%
% This program is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
% GNU General Public License for more details.
%
% You should have received a copy of the GNU General Public License
% along with this program; if not, write to the Free Software
% Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
%


define node_in_set(node_list, node)
{
	variable x, len;

	len = length(node_list);
	for (x = 0; x < len; x++) {
		if (node_list[x] == node)
			return 1;
	}

	return 0;
}


%
% Returns 3 node lists:
% (1) Nodes with no services
% (2) Nodes with non-exclusive services
% (3) Nodes with exclusive services
%
% NOTE: This function currently defenstrates failover domain rules
%
define separate_nodes(node_list)
{
	variable services = service_list();
	variable nodes_empty, nodes_services, nodes_excl;
	variable x, len;
	variable owner, state, excl, ns = 0, nx = 0;

	nodes_empty = node_list;

	% Most Awesome Initializer EVER!!!
	nodes_services = subtract([0], 0);
	nodes_excl = subtract([0], 0);

	len = length(services);
	for (x = 0; x < len; x++) {

		(,,, owner, state) = service_status(services[x]);
		if (owner < 0) {
			continue;
		}

		excl = atoi(service_property(services[x], "exclusive"));
		nodes_empty = subtract(nodes_empty, owner);
		if (excl) {
			nodes_excl = union(nodes_excl, owner);
		} else {
			nodes_services = union(nodes_services, owner);
		}
	}

	return (nodes_empty, nodes_services, nodes_excl);
}


define exclusive_prioritize(svc, node_list)
{
	variable services = service_list();
	variable len, x, y, owner, state, preferred_owner;
	variable svc_excl, other_excl;
	variable nodes_x, nodes_s, nodes_e;

	%
	% Not exclusive?  Don't care!
	%
	svc_excl = atoi(service_property(svc, "exclusive"));
	if (svc_excl == 0) {
		return node_list;
	}

	(nodes_e, nodes_s, nodes_x) = separate_nodes(node_list);
	debug("Nodes - Empty: ", nodes_e, " w/Services: ", nodes_s, " w/Excl: ", nodes_x);
	if (length(nodes_e) > 0) {
		%
		% If we've got an exclusive service, only allow it to start on 
		% empty nodes.
		%
		return nodes_e;
	}

	if (length(nodes_x) == 0) {
		%
		% If we've got NO nodes with other exclusive services
		% and no empty nodes, the service can not be started
		%
		notice("No empty / exclusive nodes available; cannot restart ", svc);
		return nodes_x;
	}

	%
	% Prioritization of exclusive services: pancake a service and replace it
	% with this service if this services is a higher priority.
	%
	len = length(services);
	for (x = 0; x < len; x++) {
		if (svc == services[x]) {
			% don't do anything to ourself! 
			continue;
		}

		(,,, owner, state) = service_status(services[x]);
		if (owner < 0) {
			continue;
		}

		if (node_in_set(node_list, owner) == 0) {
			continue;
		}

		other_excl = atoi(service_property(services[x], "exclusive"));
		if (other_excl == 0) {
			continue;
		}

		%
		% If we're a higher priority (lower #) exclusive
		% Stop the exclusive service that node and move that
		% node to the front.
		%
		if (svc_excl >= other_excl) {
			continue;
		}

		%
		% 
		%
		warning("STOPPING service ", services[x], " because ", svc, " is a higher priority.");
		() = service_stop(services[x]);

		%
		% Return just the one node.
		%
		node_list = subtract([0], 0);
		node_list = union(node_list, owner);
		return node_list;
	}

	return node_list;
}


define move_or_start(service, node_list)
{
	variable len;
	variable state, owner;
	variable depends;

	depends = service_property(service, "depend");
	if (depends != "") {
		(,,, owner, state) = service_status(depends);
		if (owner < 0) {
			debug(service, " is not runnable; dependency not met");
			return ERR_DEPEND;
		}
	}

	(,,, owner, state) = service_status(service);
	debug("Evaluating ", service, " state=", state, " owner=", owner);
	if ((event_type == EVENT_NODE) and (node_id == owner) and
	    (node_state == NODE_OFFLINE)) {
		info("Marking service ", service, " on down member ",
		     owner, " as stopped");
		if (service_stop(service) < 0) {
			return ERR_ABORT;
		}
	}

	len = length(node_list);
	if (len == 0) {
		notice(service, " is not runnable - restricted domain offline");
		()=service_stop(service);
		return ERR_DOMAIN;
	}

	if (((event_type != EVENT_USER) and (state == "disabled")) or
            ((state == "failed") or (state == "frozen"))) {
		%
		% Commenting out this block will -not- allow you to
		% recover failed services from event scripts.  Sorry.
		% All it will get you is a false log message about
		% starting this service.
		%
		% You may enable disabled services, but I recommend
		% against it.
		%
		debug(service, " is not runnable");
		return -1;
	}

	if (node_list[0] == owner) {
		debug(service, " is already running on best node");
		return ERR_RUNNING;
	}

	if ((owner >= 0) and (node_in_set(node_list, owner) == 1)) {
		notice("Moving ", service, " from ", owner,
		       " to ", node_list);
		if (service_stop(service) < 0) {
			return ERR_ABORT;
		}
	} else {
		node_list = exclusive_prioritize(service, node_list);
		notice("Starting ", service, " on ", node_list);
	}

	if (length(node_list) == 0) {
		return ERR_DOMAIN; 
	}
	return service_start(service, node_list);
}


%
% Returns the set of online nodes in preferred/shuffled order which
% are allowed to run this service.  Gives highest preference to current
% owner if nofailback is specified.
% 
define allowed_nodes(service)
{
	variable anodes;
	variable online;
	variable nodes_domain;
	variable ordered, restricted, nofailback;
	variable state, owner;
	variable depends;

	(nofailback, restricted, ordered, nodes_domain) =
			service_domain_info(service);

	(,,, owner, state) = service_status(service);

	anodes = nodes_online();

	% Shuffle the array so we don't start all services on the same
	% node.  TODO - add RR, Least-services, placement policies...
	online = shuffle(anodes);

	if (restricted == 1) {
		anodes = intersection(nodes_domain, online);
	} else {
		% Ordered failover domains (nodes_domain) unioned with the
		% online nodes basically just reorders the online node list
		% according to failover domain priority rules.
		anodes = union(intersection(nodes_domain, online),
			       online);
	}

	if ((nofailback == 1) or (ordered == 0)) {
		
		if ((owner < 0) or (node_in_set(anodes, owner) == 0)) {
			return anodes;
		}
		
		% Because union takes left as priority, we can
		% return the union of the current owner with the
		% allowed node list.  This means the service will
		% remain on the same node it's currently on.
		return union(owner, anodes);
	}

	return anodes;
}

%
% Returns the set of online nodes in preferred/shuffled order which
% are allowed to run this service.  Gives highest preference to current
% owner if nofailback is specified.
% 
define allowed_nodes(service)
{
	variable anodes;
	variable online;
	variable nodes_domain;
	variable ordered, restricted, nofailback;
	variable state, owner;
	variable depends;

	(nofailback, restricted, ordered, nodes_domain) =
			service_domain_info(service);
	(,,, owner, state) = service_status(service);

	anodes = nodes_online();

	% Shuffle the array so we don't start all services on the same
	% node.  TODO - add RR, Least-services, placement policies...
	online = shuffle(anodes);

	if (restricted == 1) {
		anodes = intersection(nodes_domain, online);
	} else {
		% Ordered failover domains (nodes_domain) unioned with the
		% online nodes basically just reorders the online node list
		% according to failover domain priority rules.
		anodes = union(intersection(nodes_domain, online),
			       online);
	}

	if ((nofailback == 1) or (ordered == 0)) {
		
		if ((owner < 0) or (node_in_set(anodes, owner) == 0)) {
			return anodes;
		}
		
		% Because union takes left as priority, we can
		% return the union of the current owner with the
		% allowed node list.  This means the service will
		% remain on the same node it's currently on.
		return union(owner, anodes);
	}

	return anodes;
}

define string_list(thelist, delimiter)
{
	variable index;
	variable output="";

	if (length(thelist) == 0) {
		return output;
	}
  
	for (index=0; index < length(thelist)-1; index++) {
		output=output+string(thelist[index])+delimiter;
	}
	return output+thelist[index];
}

% this function gets the smallest property from a given list of services
% if the list only exists of one element the property itself is returned
% if the given property is not found 0 is returned
define services_min_attribute(services, property)
{
	variable x;
	variable min_property=-1;
	variable tmp_property;

	for (x = 0; x < length(services); x++) {
		tmp_property=service_property(services[x], property);
		if (tmp_property == NULL) {
			tmp_property=0;
		} else {
			tmp_property=atoi(tmp_property);
		}
		if ((min_property < 0) or (tmp_property < min_property)) {
			min_property=tmp_property;
		}
		%debug("services_min_attribute: ",services[x]," attribute: ",min_property, "tmp: ", tmp_property, " min: ", min_property);
	}

	%debug("services_min_attribute: (", string_list(services, ", "),")[",property,"]: ",min_property);

	return min_property;
}

% This function will sort a given service_list by the given attribute name and
% return the list
define sorted_service_list(services, attribute)
{
	variable work_queue={};
	variable sorted_list={}, tmp, tmp2;
	variable x, y;
	variable cur_min_prop=0;
	variable service_prop=0;

	y=0;
	%debug("sorted_service_list: ", strjoin(services, ", "));
	for (x=0; x<length(services); x++) {
		list_append(work_queue, string(services[x]));
	}

	%debug("sorted_service_list: work_queue ", string_list(work_queue, ", "));
	while (length(work_queue) > 0) {
		cur_min_prop=services_min_attribute(work_queue, attribute);
		%debug("sorted_service_list sorting services list for attribute ", attribute, " cur_min: ",cur_min_prop);
		for (x = 0; x < length(work_queue); x++) {
			service_prop=service_property(work_queue[x], "priority");
			if (service_prop == NULL) {
				service_prop=0;
			} else {
				service_prop=atoi(service_prop);
			}
			%debug("sorted_service_list: ",work_queue[x], " property[", attribute,"]: ",service_prop);
			if (cur_min_prop==service_prop) {
				%debug("sorted_service_list: adding service ",work_queue[x]," to sorted. work_queue: ", string_list(work_queue, ", "));
				list_append(sorted_list, work_queue[x]);
				%debug("sorted_service_list: sorted_list: ", string_list(sorted_list, ", "));
				%debug("sorted_service_list: removing service ",work_queue[x], " from work_queue ", string_list(work_queue, ", "));
				list_delete(work_queue, x);
				x=x-1;
				%debug("sorted_service_list: work_queue: ",string_list(work_queue, ", "));
				y=y+1;
			}
		}
	}

	debug("sorted_service_list ", string_list(sorted_list, ", "));
	return sorted_list;
}

define sortedservices_node_event_handler(services, attribute) {
	variable x;
	variable nodes;

	services=sorted_service_list(services, attribute);
	for (x = 0; x < length(services); x++) {
		debug("Executing sortedservices node event handler for service: ", services[x]);
		nodes = allowed_nodes(services[x]);
		()=move_or_start(services[x], nodes);
	}
}

define default_node_event_handler()
{
	variable services = service_list();
	variable x;
	variable nodes;

	debug("Executing default node event handler");
	for (x = 0; x < length(services); x++) {
		nodes = allowed_nodes(services[x]);
		()=move_or_start(services[x], nodes);
	}
}


define default_service_event_handler()
{
	variable services = service_list();
	variable x;
	variable depends;
	variable depend_mode;
	variable policy;
	variable nodes;
	variable tmp;
	variable owner;
	variable state;

	debug("Executing default service event handler");

	if (service_state == "recovering") {

		policy = service_property(service_name, "recovery");
		debug("Recovering",
		      " Service: ", service_name,
		      " Last owner: ", service_last_owner,
		      " Policy: ", policy,
		      " RTE: ", service_restarts_exceeded);

		if (policy == "disable") {
			() = service_stop(service_name, 1);
			return;
		}

		nodes = allowed_nodes(service_name);
		if (policy == "restart" and service_restarts_exceeded == 0) {
			nodes = union(service_last_owner, nodes);
		} else {
			% relocate 
			tmp = subtract(nodes, service_last_owner);
			if (length(tmp) == 0) {
				() = service_stop(service_name,0);
				return;
			}

			nodes = union(tmp, service_last_owner);
		}

		()=move_or_start(service_name, nodes);

		return;
	}

	for (x = 0; x < length(services); x++) {
		if (service_name == services[x]) {
			% don't do anything to ourself! 
			continue;
		}

		%
		% Simplistic dependency handling
		%
		depends = service_property(services[x], "depend");
		depend_mode = service_property(services[x], "depend_mode");

		% No dependency; do nothing
		if (depends != service_name) {
			continue;
		}

		(,,, owner, state) = service_status(services[x]);
		if ((service_state == "started") and (owner < 0) and
		    (state == "stopped")) {
			info("Dependency met; starting ", services[x]);
			nodes = allowed_nodes(services[x]);
			()=move_or_start(services[x], nodes);
		}

		% service died - stop service(s) that depend on the dead
		if ((service_owner < 0) and (owner >= 0) and
		    (depend_mode != "soft")) {
			info("Dependency lost; stopping ", services[x]);
			()=service_stop(services[x]);
		}
	}
}

define default_config_event_handler()
{
	debug("Executing default config event handler");
}

define default_user_event_handler()
{
	variable ret;
	variable nodes;
	variable reordered;
	variable x;
	variable target = user_target;
	variable found = 0;
	variable owner, state;

	nodes = allowed_nodes(service_name);
	(,,, owner, state) = service_status(service_name);

	if (user_request == USER_RESTART) {

		if (owner >= 0) {
			reordered = union(owner, nodes);
			nodes = reordered;
		}

		notice("Stopping ", service_name, " for relocate to ", nodes);

		found = service_stop(service_name);
		if (found < 0) {
			return ERR_ABORT;
		}

		ret = move_or_start(service_name, nodes);

	} else if ((user_request == USER_RELOCATE) or 
		   (user_request == USER_ENABLE)) {

		if (user_target > 0) {
			for (x = 0; x < length(nodes); x++) {
				%
				% Put the preferred node at the front of the 
				% list for a user-relocate operation
				%
				if (nodes[x] == user_target) {
					reordered = union(user_target, nodes);
					nodes = reordered;
					found = 1;
				}
			}
	
			if (found == 0) {
				warning("User specified node ", user_target,
					" is offline");
			}
		}

		if ((owner >= 0) and (user_request == USER_RELOCATE)) {
			if (service_stop(service_name) < 0) {
				return ERR_ABORT;
			}

			%
			% The current owner shouldn't be the default
			% for a relocate operation
			%
			reordered = subtract(nodes, owner);
			nodes = union(reordered, owner);
		}

		ret = move_or_start(service_name, nodes);

	} else if (user_request == USER_DISABLE) {

		ret = service_stop(service_name, 1);

	} else if (user_request == USER_STOP) {

		ret = service_stop(service_name);

	} else if (user_request == USER_FREEZE) {

		ret = service_freeze(service_name);

	} else if (user_request == USER_UNFREEZE) {

		ret = service_unfreeze(service_name);

	} else if (user_request == USER_MIGRATE) {

		ret = service_migrate(service_name, user_target);

	} else if (user_request == USER_CONVALESCE) {

		ret = service_convalesce(service_name);

	}

	return ret;
}

if (event_type == EVENT_NODE)
	sortedservices_node_event_handler(service_list(), "priority");
if (event_type == EVENT_SERVICE)
	default_service_event_handler();
if (event_type == EVENT_CONFIG)
	default_config_event_handler();
if (event_type == EVENT_USER)
	user_return=default_user_event_handler();

