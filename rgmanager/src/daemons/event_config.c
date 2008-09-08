/** @file
 * CCS event parsing, based on failover domain parsing
 */
#include <string.h>
#include <list.h>
#include <clulog.h>
#include <resgroup.h>
#include <restart_counter.h>
#include <reslist.h>
#include <ccs.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <members.h>
#include <reslist.h>
#include <ctype.h>
#include <event.h>

#define CONFIG_NODE_ID_TO_NAME \
   "/cluster/clusternodes/clusternode[@nodeid=\"%d\"]/@name"
#define CONFIG_NODE_NAME_TO_ID \
   "/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid"

void deconstruct_events(event_table_t **);
void print_event(event_t *ev);

//#define DEBUG

#ifdef DEBUG
#define ENTER() clulog(LOG_DEBUG, "ENTER: %s\n", __FUNCTION__)
#define RETURN(val) {\
	clulog(LOG_DEBUG, "RETURN: %s line=%d value=%d\n", __FUNCTION__, \
	       __LINE__, (val));\
	return(val);\
}
#else
#define ENTER()
#define RETURN(val) return(val)
#endif

#ifdef NO_CCS
#define ccs_get(fd, query, ret) conf_get(query, ret)
#endif

/*
   <events>
     <event name="helpful_name_here" class="node"
            node="nodeid|nodename" nodestate="up|down">
	    slang_script_stuff();
	    start_service();
     </event>
   </events>
 */
int
event_match(event_t *pattern, event_t *actual)
{
	if (pattern->ev_type != EVENT_NONE &&
	    actual->ev_type != pattern->ev_type)
		return 0;

	/* If there's no event class specified, the rest is
	   irrelevant */
	if (pattern->ev_type == EVENT_NONE)
		return 1;

	switch(pattern->ev_type) {
	case EVENT_NODE:
		if (pattern->ev.node.ne_nodeid >= 0 &&
		    actual->ev.node.ne_nodeid !=
				pattern->ev.node.ne_nodeid) {
			return 0;
		}
		if (pattern->ev.node.ne_local >= 0 && 
		    actual->ev.node.ne_local !=
				pattern->ev.node.ne_local) {
			return 0;
		}
		if (pattern->ev.node.ne_state >= 0 && 
		    actual->ev.node.ne_state !=
				pattern->ev.node.ne_state) {
			return 0;
		}
		if (pattern->ev.node.ne_clean >= 0 && 
		    actual->ev.node.ne_clean !=
				pattern->ev.node.ne_clean) {
			return 0;
		}
		return 1; /* All specified params match */
	case EVENT_RG:
		if (pattern->ev.group.rg_name[0] &&
		    strcasecmp(actual->ev.group.rg_name, 
			       pattern->ev.group.rg_name)) {
			return 0;
		}
		if (pattern->ev.group.rg_state != (uint32_t)-1 && 
		    actual->ev.group.rg_state !=
				pattern->ev.group.rg_state) {
			return 0;
		}
		if (pattern->ev.group.rg_owner >= 0 && 
		    actual->ev.group.rg_owner !=
				pattern->ev.group.rg_owner) {
			return 0;
		}
		return 1;
	case EVENT_CONFIG:
		if (pattern->ev.config.cfg_version >= 0 && 
		    actual->ev.config.cfg_version !=
				pattern->ev.config.cfg_version) {
			return 0;
		}
		if (pattern->ev.config.cfg_oldversion >= 0 && 
		    actual->ev.config.cfg_oldversion !=
				pattern->ev.config.cfg_oldversion) {
			return 0;
		}
		return 1;
	case EVENT_USER:
		if (pattern->ev.user.u_name[0] &&
		    strcasecmp(actual->ev.user.u_name, 
			       pattern->ev.user.u_name)) {
			return 0;
		}
		if (pattern->ev.user.u_request != 0 && 
		    actual->ev.user.u_request !=
				pattern->ev.user.u_request) {
			return 0;
		}
		if (pattern->ev.user.u_target != 0 && 
		    actual->ev.user.u_target !=
				pattern->ev.user.u_target) {
			return 0;
		}
		return 1;
	default:
		break;
	}
			
	return 0;
}


char *
#ifndef NO_CCS
ccs_node_id_to_name(int ccsfd, int nodeid)
#else
ccs_node_id_to_name(int __attribute__ ((unused)) ccsfd, int nodeid)
#endif
{
	char xpath[256], *ret = 0;

	snprintf(xpath, sizeof(xpath), CONFIG_NODE_ID_TO_NAME,
		 nodeid);
	if (ccs_get(ccsfd, xpath, &ret) == 0)
		return ret;
	return NULL;
}


int
#ifndef NO_CCS
ccs_node_name_to_id(int ccsfd, char *name)
#else
ccs_node_name_to_id(int __attribute__((unused)) ccsfd, char *name)
#endif
{
	char xpath[256], *ret = 0;
	int rv = 0;

	snprintf(xpath, sizeof(xpath), CONFIG_NODE_NAME_TO_ID,
		 name);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		rv = atoi(ret);
		free(ret);
		return rv;
	}
	return 0;
}


static void 
deconstruct_event(event_t *ev)
{
	if (ev->ev_script)
		free(ev->ev_script);
	if (ev->ev_name)
		free(ev->ev_name);
	free(ev);
}


static int
get_node_event(int ccsfd, char *base, event_t *ev)
{
	char xpath[256], *ret = NULL;

	/* Clear out the possibilitiies */
	ev->ev.node.ne_nodeid = -1;
	ev->ev.node.ne_local = -1;
	ev->ev.node.ne_state = -1;
	ev->ev.node.ne_clean = -1;

	snprintf(xpath, sizeof(xpath), "%s/@node_id", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		ev->ev.node.ne_nodeid = atoi(ret);
		free(ret);
		if (ev->ev.node.ne_nodeid <= 0)
			return -1;
	} else {
		/* See if there's a node name */
		snprintf(xpath, sizeof(xpath), "%s/@node", base);
		if (ccs_get(ccsfd, xpath, &ret) == 0) {
			ev->ev.node.ne_nodeid =
				ccs_node_name_to_id(ccsfd, ret);
			free(ret);
			if (ev->ev.node.ne_nodeid <= 0)
				return -1;
		}
	}

	snprintf(xpath, sizeof(xpath), "%s/@node_state", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (!strcasecmp(ret, "up")) {
			ev->ev.node.ne_state = 1;
		} else if (!strcasecmp(ret, "down")) {
			ev->ev.node.ne_state = 0;
		} else {
			ev->ev.node.ne_state = !!atoi(ret);
		}
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/@node_clean", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		ev->ev.node.ne_clean = !!atoi(ret);
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/@node_local", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		ev->ev.node.ne_local = !!atoi(ret);
		free(ret);
	}

	return 0;
}


static int
get_rg_event(int ccsfd, char *base, event_t *ev)
{
	char xpath[256], *ret = NULL;

	/* Clear out the possibilitiies */
	ev->ev.group.rg_name[0] = 0;
	ev->ev.group.rg_state = (uint32_t)-1;
	ev->ev.group.rg_owner = -1;

	snprintf(xpath, sizeof(xpath), "%s/@service", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		strncpy(ev->ev.group.rg_name, ret,
			sizeof(ev->ev.group.rg_name));
		free(ret);
		if (!strlen(ev->ev.group.rg_name)) {
			return -1;
		}
	}

	snprintf(xpath, sizeof(xpath), "%s/@service_state", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (!isdigit(ret[0])) {
			ev->ev.group.rg_state =
			       	rg_state_str_to_id(ret);
		} else {
			ev->ev.group.rg_state = atoi(ret);
		}	
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/@service_owner", base);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (!isdigit(ret[0])) {
			ev->ev.group.rg_owner =
			       	ccs_node_name_to_id(ccsfd, ret);
		} else {
			ev->ev.group.rg_owner = !!atoi(ret);
		}	
		free(ret);
	}

	return 0;
}


static int
get_config_event(int __attribute__((unused)) ccsfd,
		 char __attribute__((unused)) *base,
		 event_t __attribute__((unused)) *ev)
{
	errno = ENOSYS;
	return -1;
}


static event_t *
get_event(int ccsfd, char *base, int idx, int *_done)
{
	event_t *ev;
	char xpath[256];
	char *ret = NULL;

	*_done = 0;
	snprintf(xpath, sizeof(xpath), "%s/event[%d]/@name",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0) {
		*_done = 1;
		return NULL;
	}

	ev = malloc(sizeof(*ev));
	if (!ev)
		return NULL;
	memset(ev, 0, sizeof(*ev));
	ev->ev_name = ret;

	/* Get the script file / inline from config */
	ret = NULL;
	snprintf(xpath, sizeof(xpath), "%s/event[%d]/@file",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		ev->ev_script_file = ret;
	} else {
		snprintf(xpath, sizeof(xpath), "%s/event[%d]",
		         base, idx);
		if (ccs_get(ccsfd, xpath, &ret) == 0) {
			ev->ev_script = ret;
		} else {
			goto out_fail;
		}
	}

	/* Get the priority ordering (must be nonzero) */
	ev->ev_prio = 99;
	ret = NULL;
	snprintf(xpath, sizeof(xpath), "%s/event[%d]/@priority",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		ev->ev_prio = atoi(ret);
		if (ev->ev_prio <= 0 || ev->ev_prio > EVENT_PRIO_COUNT) {
			clulog(LOG_ERR,
			       "event %s: priority %s invalid\n",
			       ev->ev_name, ret);
			goto out_fail;
		}
		free(ret);
	}

	/* Get the event class */
	snprintf(xpath, sizeof(xpath), "%s/event[%d]/@class",
		 base, idx);
	ret = NULL;
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		snprintf(xpath, sizeof(xpath), "%s/event[%d]",
		 	 base, idx);
		if (!strcasecmp(ret, "node")) {
			ev->ev_type = EVENT_NODE;
			if (get_node_event(ccsfd, xpath, ev) < 0)
				goto out_fail;
		} else if (!strcasecmp(ret, "service") ||
			   !strcasecmp(ret, "resource") ||
			   !strcasecmp(ret, "rg") ) {
			ev->ev_type = EVENT_RG;
			if (get_rg_event(ccsfd, xpath, ev) < 0)
				goto out_fail;
		} else if (!strcasecmp(ret, "config") ||
			   !strcasecmp(ret, "reconfig")) {
			ev->ev_type = EVENT_CONFIG;
			if (get_config_event(ccsfd, xpath, ev) < 0)
				goto out_fail;
		} else {
			clulog(LOG_ERR,
			       "event %s: class %s unrecognized\n",
			       ev->ev_name, ret);
			goto out_fail;
		}

		free(ret);
		ret = NULL;
	}

	return ev;
out_fail:
	if (ret)
		free(ret);
	deconstruct_event(ev);
	return NULL;
}


static event_t *
get_default_event(void)
{
	event_t *ev;
	char xpath[1024];

	ev = malloc(sizeof(*ev));
	if (!ev)
		return NULL;
	memset(ev, 0, sizeof(*ev));
	ev->ev_name = strdup("Default");

	/* Get the script file / inline from config */
	snprintf(xpath, sizeof(xpath), "%s/default_event_script.sl",
		 RESOURCE_ROOTDIR);

	ev->ev_prio = 100;
	ev->ev_type = EVENT_NONE;
	ev->ev_script_file = strdup(xpath);
	if (!ev->ev_script_file || ! ev->ev_name) {
		deconstruct_event(ev);
		return NULL;
	}

	return ev;
}


/**
 * similar API to failover domain
 */
int
construct_events(int ccsfd, event_table_t **events)
{
	char xpath[256];
	event_t *ev;
	int x = 1, done = 0;

	/* Allocate the event list table */
	*events = malloc(sizeof(event_table_t) +
			 sizeof(event_t) * (EVENT_PRIO_COUNT+1));
	if (!*events)
		return -1;
	memset(*events, 0, sizeof(event_table_t) +
	       		   sizeof(event_t) * (EVENT_PRIO_COUNT+1));
	(*events)->max_prio = EVENT_PRIO_COUNT;

	snprintf(xpath, sizeof(xpath),
		 RESOURCE_TREE_ROOT "/events");

	do {
		ev = get_event(ccsfd, xpath, x++, &done);
		if (ev)
			list_insert(&((*events)->entries[ev->ev_prio]), ev);
	} while (!done);

	ev = get_default_event();
	if (ev)
		list_insert(&((*events)->entries[ev->ev_prio]), ev);
	
	return 0;
}


void
print_event(event_t *ev)
{
	printf("  Name: %s\n", ev->ev_name);

	switch(ev->ev_type) {
	case EVENT_NODE:
		printf("    Node %d State %d\n", ev->ev.node.ne_nodeid,
		       ev->ev.node.ne_state);
		break;
	case EVENT_RG:
		printf("    RG %s State %s\n", ev->ev.group.rg_name,
		       rg_state_str(ev->ev.group.rg_state));
		break;
	case EVENT_CONFIG:
		printf("    Config change - unsupported\n");
		break;
	default:
		printf("    (Any event)\n");
		break;
	}
	
	if (ev->ev_script) {
		printf("    Inline script.\n");
	} else {
		printf("    File: %s\n", ev->ev_script_file);
	}
}


void
print_events(event_table_t *events)
{
	int x, y;
	event_t *ev;

	for (x = 0; x <= events->max_prio; x++) {
		if (!events->entries[x])
			continue;
		printf("Event Priority Level %d:\n", x);
		list_for(&(events->entries[x]), ev, y) {
			print_event(ev);
		}
	}
}


void
deconstruct_events(event_table_t **eventsp)
{
	int x;
	event_table_t *events = *eventsp;
	event_t *ev = NULL;

	if (!events)
		return;

	for (x = 0; x <= events->max_prio; x++) {
		while ((ev = (events->entries[x]))) {
			list_remove(&(events->entries[x]), ev);
			deconstruct_event(ev);
		}
	}

	free(events);
	*eventsp = NULL;
}


