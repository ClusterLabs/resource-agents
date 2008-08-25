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

define move_or_start(service, node_list)
{
	variable len;
	variable state, owner;
	variable depends;

	depends = service_property(service, "depend");
	if (depends != "") {
		(owner, state) = service_status(depends);
		if (owner < 0) {
			debug(service, " is not runnable; dependency not met");
			return ERR_DEPEND;
		}
	}

	(owner, state) = service_status(service);
	debug("Evaluating ", service, " state=", state, " owner=", owner);

	len = length(node_list);
	if (len == 0) {
		debug(service, " is not runnable");
		return ERR_DOMAIN;
	}

	if (((event_type != EVENT_USER) and (state == "disabled")) or (state == "failed")) {
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
		notice("Starting ", service, " on ", node_list);
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

	(owner, state) = service_status(service);

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


define default_node_event_handler()
{
	variable services = service_list();
	variable x;
	variable nodes;

	% debug("Executing default node event handler");
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

	% debug("Executing default service event handler");

	if (service_state == "recovering") {

		policy = service_property(service_name, "recovery");
		debug("Recovering",
		      " Service: ", service_name,
		      " Last owner: ", service_last_owner,
		      " Policy: ", policy);

		if (policy == "disable") {
			() = service_stop(service_name, 1);
			return;
		}

		nodes = allowed_nodes(service_name);
		if (policy == "restart") {
			tmp = union(service_last_owner, nodes);
		} else {
			% relocate 
			tmp = subtract(nodes, service_last_owner);
			nodes = tmp;
			tmp = union(nodes, service_last_owner);
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

		(owner, state) = service_status(services[x]);
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
	% debug("Executing default config event handler");
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
	(owner, state) = service_status(service_name);

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

	}

	%
	% todo - migrate
	%

	return ret;
}

if (event_type == EVENT_NODE)
	default_node_event_handler();
if (event_type == EVENT_SERVICE)
	default_service_event_handler();
if (event_type == EVENT_CONFIG)
	default_config_event_handler();
if (event_type == EVENT_USER)
	user_return=default_user_event_handler();

