/**
  @file S/Lang event handling & intrinsic functions + vars
 */
#include <platform.h>
#include <resgroup.h>
#include <list.h>
#include <restart_counter.h>
#include <reslist.h>
#include <logging.h>
#include <members.h>
#include <assert.h>
#include <event.h>

#include <stdio.h>
#include <string.h>
#include <slang.h>
#include <sys/syslog.h>
#include <malloc.h>
#include <logging.h>
#include <sets.h>

static int __sl_initialized = 0;

static char **_service_list = NULL;
static int _service_list_len = 0;

char **get_service_names(int *len); /* from groups.c */
int get_service_property(char *rg_name, char *prop, char *buf, size_t buflen);
void push_int_array(int *stuff, int len);


/* ================================================================
 * Node states 
 * ================================================================ */
static const int
   _ns_online = 1,
   _ns_offline = 0;

/* ================================================================
 * Event information 
 * ================================================================ */
static const int
   _ev_none = EVENT_NONE,
   _ev_node = EVENT_NODE,
   _ev_service = EVENT_RG,
   _ev_config = EVENT_CONFIG,
   _ev_user = EVENT_USER;

static const int
   _rg_fail = RG_EFAIL,
   _rg_success = RG_ESUCCESS,
   _rg_edomain = RG_EDOMAIN,
   _rg_edepend = RG_EDEPEND,
   _rg_eabort = RG_EABORT,
   _rg_einval = RG_EINVAL,
   _rg_erun = RG_ERUN;

static int
   _stop_processing = 0,
   _my_node_id = 0,
   _node_state = 0,
   _node_id = 0,
   _node_clean = 0,
   _service_owner = 0,
   _service_last_owner = 0,
   _user_request = 0,
   _user_arg1 = 0,
   _user_arg2 = 0,
   _user_return = 0,
   _rg_err = 0,
   _event_type = 0;

static char
   *_node_name = NULL,
   *_service_name = NULL,
   *_service_state = NULL,
   *_rg_err_str = "No Error";

static int
   _user_enable = RG_ENABLE,
   _user_disable = RG_DISABLE,
   _user_stop = RG_STOP_USER,		/* From clusvcadm */
   _user_relo = RG_RELOCATE,
   _user_restart = RG_RESTART,
   _user_migrate = RG_MIGRATE,
   _user_freeze = RG_FREEZE,
   _user_unfreeze = RG_UNFREEZE;


SLang_Intrin_Var_Type rgmanager_vars[] =
{
	/* Log levels (constants) */

	/* Node state information */
	MAKE_VARIABLE("NODE_ONLINE",	&_ns_online,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("NODE_OFFLINE",	&_ns_offline,	SLANG_INT_TYPE, 1),

	/* Node event information */
	MAKE_VARIABLE("node_self",	&_my_node_id,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("node_state",	&_node_state,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("node_id",	&_node_id,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("node_name",	&_node_name,	SLANG_STRING_TYPE,1),
	MAKE_VARIABLE("node_clean",	&_node_clean,	SLANG_INT_TYPE, 1),

	/* Service event information */
	MAKE_VARIABLE("service_name",	&_service_name,	SLANG_STRING_TYPE,1),
	MAKE_VARIABLE("service_state",	&_service_state,SLANG_STRING_TYPE,1),
	MAKE_VARIABLE("service_owner",	&_service_owner,SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("service_last_owner", &_service_last_owner,
		      					SLANG_INT_TYPE, 1),

	/* User event information */
	MAKE_VARIABLE("user_request",	&_user_request,	SLANG_INT_TYPE,1),
	MAKE_VARIABLE("user_arg1",	&_user_arg1,	SLANG_INT_TYPE,1),
	MAKE_VARIABLE("user_arg2",	&_user_arg2,	SLANG_INT_TYPE,1),
	MAKE_VARIABLE("user_service",	&_service_name, SLANG_STRING_TYPE,1),
	MAKE_VARIABLE("user_target",	&_service_owner,SLANG_INT_TYPE, 1),
	/* Return code to user requests; i.e. clusvcadm */
	MAKE_VARIABLE("user_return",	&_user_return,	SLANG_INT_TYPE, 0),

	/* General event information */
	MAKE_VARIABLE("event_type",	&_event_type,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("EVENT_NONE",	&_ev_none,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("EVENT_NODE",	&_ev_node,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("EVENT_CONFIG",	&_ev_config,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("EVENT_SERVICE",	&_ev_service,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("EVENT_USER",	&_ev_user,	SLANG_INT_TYPE, 1),

	/* User request constants */
	MAKE_VARIABLE("USER_ENABLE",	&_user_enable,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_DISABLE",	&_user_disable,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_STOP",	&_user_stop,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_RELOCATE",	&_user_relo,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_RESTART",	&_user_restart,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_MIGRATE",	&_user_migrate,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_FREEZE",	&_user_freeze,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("USER_UNFREEZE",	&_user_unfreeze,SLANG_INT_TYPE, 1),

	/* Errors */
	MAKE_VARIABLE("rg_error",	&_rg_err,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("rg_error_string",&_rg_err_str,	SLANG_STRING_TYPE,1),

	/* From constants.c */
	MAKE_VARIABLE("FAIL",		&_rg_fail,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("SUCCESS",	&_rg_success,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("ERR_ABORT",	&_rg_eabort,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("ERR_INVALID",	&_rg_einval,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("ERR_DEPEND",	&_rg_edepend,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("ERR_DOMAIN",	&_rg_edomain,	SLANG_INT_TYPE, 1),
	MAKE_VARIABLE("ERR_RUNNING",	&_rg_erun,	SLANG_INT_TYPE, 1),

	SLANG_END_INTRIN_VAR_TABLE
};


#define rg_error(errortype) \
do { \
	_rg_err = errortype; \
	_rg_err_str = ##errortype; \
} while(0)


int
get_service_state_internal(char *svcName, rg_state_t *svcStatus)
{
	struct dlm_lksb lock;
	char buf[32];

	get_rg_state_local(svcName, svcStatus);
	if (svcStatus->rs_state == RG_STATE_UNINITIALIZED) {
		if (rg_lock(svcName, &lock) < 0) {
			errno = ENOLCK;
			return -1;
		}

		if (get_rg_state(svcName, svcStatus) < 0) {
			errno = ENOENT;
			rg_unlock(&lock);
			return -1;
		}

		if (get_service_property(svcName, "autostart",
					 buf, sizeof(buf)) == 0) {
			if (buf[0] == '0' || !strcasecmp(buf, "no")) {
				svcStatus->rs_state = RG_STATE_DISABLED;
			} else {
				svcStatus->rs_state = RG_STATE_STOPPED;
			}
		}

		set_rg_state(svcName, svcStatus);

		rg_unlock(&lock);
	}

	return 0;
}


/*
   (restarts, last_owner, owner, state) = get_service_status(servicename)
 */
void
sl_service_status(char *svcName)
{
	rg_state_t svcStatus;
	char *state_str;

	if (get_service_state_internal(svcName, &svcStatus) < 0) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to get status for %s",
			     __FUNCTION__,
			     svcName);
		return;
	}

	if (SLang_push_integer(svcStatus.rs_restarts) < 0) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to push restarts for %s",
			     __FUNCTION__,
			     svcName);
		return;
	}

	if (SLang_push_integer(svcStatus.rs_last_owner) < 0) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to push last owner of %s",
			     __FUNCTION__,
			     svcName);
		return;
	}

	switch(svcStatus.rs_state) {
	case RG_STATE_DISABLED:
	case RG_STATE_STOPPED:
	case RG_STATE_FAILED:
	case RG_STATE_RECOVER:
	case RG_STATE_ERROR:
		/* There is no owner for these states.  Ever.  */
		svcStatus.rs_owner = -1;
	}

	if (SLang_push_integer(svcStatus.rs_owner) < 0) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to push owner of %s",
			     __FUNCTION__,
			     svcName);
		return;
	}

	if (svcStatus.rs_flags & RG_FLAG_FROZEN) {
		/* Special case: "frozen" is a flag, but user scripts should
		   treat it as a state. */
		state_str = strdup(rg_flag_str(RG_FLAG_FROZEN));
	} else {
		state_str = strdup(rg_state_str(svcStatus.rs_state));
	}

	if (!state_str) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to duplicate state of %s",
			     __FUNCTION__,
			     svcName);
		return;
	}

	if (SLang_push_malloced_string(state_str) < 0) {
		SLang_verror(SL_RunTime_Error,
			     "%s: Failed to push state of %s",
			     __FUNCTION__,
			     svcName);
		free(state_str);
	}
}


/* These can be done by the master node */
int
sl_service_freeze(char *svcName)
{
	return svc_freeze(svcName);
}


int
sl_service_unfreeze(char *svcName)
{
	return svc_unfreeze(svcName);
}


/**
  (nofailback, restricted, ordered, nodelist) = service_domain_info(svcName);
 */
void
sl_domain_info(char *svcName)
{
	int *nodelist = NULL, listlen;
	char buf[64];
	int flags = 0;

	if (get_service_property(svcName, "domain", buf, sizeof(buf)) < 0) {
		/* no nodes */
		SLang_push_integer(0);

		/* no domain? */
/*
		str = strdup("none");
		if (SLang_push_malloced_string(str) < 0) {
			free(state_str);
			return;
		}
*/

		/* not ordered */
		SLang_push_integer(0);
		/* not restricted */
		SLang_push_integer(0);
		/* nofailback not set */
		SLang_push_integer(0);
	}

	if (node_domain_set_safe(buf, &nodelist, &listlen, &flags) < 0) {
		SLang_push_integer(0);
		SLang_push_integer(0);
		SLang_push_integer(0);
		SLang_push_integer(0);
		return;
	}

	SLang_push_integer(!!(flags & FOD_NOFAILBACK));
	SLang_push_integer(!!(flags & FOD_RESTRICTED));
	SLang_push_integer(!!(flags & FOD_ORDERED));

	push_int_array(nodelist, listlen);
	free(nodelist);

/*
	str = strdup(buf);
	if (SLang_push_malloced_string(str) < 0) {
		free(state_str);
		return;
	}
*/
}


static int
get_int_array(int **nodelist, int *len)
{
	SLang_Array_Type *a = NULL;
	SLindex_Type i;
	int *nodes = NULL, t, ret = -1;

	if (!nodelist || !len)
		return -1;

	t = SLang_peek_at_stack();
	if (t == SLANG_INT_TYPE) {

		nodes = malloc(sizeof(int) * 1);
		if (!nodes)
			goto out;
		if (SLang_pop_integer(&nodes[0]) < 0)
			goto out;

		*len = 1;
		ret = 0;

	} else if (t == SLANG_ARRAY_TYPE) {
		if (SLang_pop_array_of_type(&a, SLANG_INT_TYPE) < 0)
			goto out;
		if (a->num_dims > 1)
			goto out;
		if (a->dims[0] < 0)
			goto out;
		nodes = malloc(sizeof(int) * a->dims[0]);
		if (!nodes)
			goto out;
		for (i = 0; i < a->dims[0]; i++)
			SLang_get_array_element(a, &i, &nodes[i]);

		*len = a->dims[0];
		ret = 0;
	}

out:
	if (a)
		SLang_free_array(a);
	if (ret == 0) {
		*nodelist = nodes;
	} else {
		if (nodes)
			free(nodes);
	}
	
	return ret;
}


/**
  get_service_property(service_name, property)
 */
char *
sl_service_property(char *svcName, char *prop)
{
	char buf[96];

	if (get_service_property(svcName, prop, buf, sizeof(buf)) < 0)
		return NULL;

	/* does this work or do I have to push a malloce'd string? */
	return strdup(buf);
}


/**
  usage:

  stop_service(name, disable_flag);
 */
int
sl_stop_service(void)
{
	char *svcname = NULL;
	int nargs, t, ret = -1;
	int do_disable = 0;

	nargs = SLang_Num_Function_Args;

	/* Takes one or two args */
	if (nargs <= 0 || nargs > 2) {
		SLang_verror(SL_Syntax_Error,
		     "%s: Wrong # of args (%d), must be 1 or 2\n",
		     __FUNCTION__,
		     nargs);
		return -1;
	}

	if (nargs == 2) {
		t = SLang_peek_at_stack();
		if (t != SLANG_INT_TYPE) {
			SLang_verror(SL_Syntax_Error,
				     "%s: expected type %d got %d\n",
				     __FUNCTION__, SLANG_INT_TYPE, t);
			goto out;
		}

		if (SLang_pop_integer(&do_disable) < 0) {
			SLang_verror(SL_Syntax_Error,
			    "%s: Failed to pop integer from stack!\n",
			    __FUNCTION__);
			goto out;
		}

		--nargs;
	}

	if (nargs == 1) {
		t = SLang_peek_at_stack();
		if (t != SLANG_STRING_TYPE) {
			SLang_verror(SL_Syntax_Error,
				     "%s: expected type %d got %d\n",
				     __FUNCTION__,
				     SLANG_STRING_TYPE, t);
			goto out;
		}

		if (SLpop_string(&svcname) < 0) {
			SLang_verror(SL_Syntax_Error,
			    "%s: Failed to pop string from stack!\n",
			    __FUNCTION__);
			goto out;
		}
	}

	/* TODO: Meat of function goes here */
	ret = service_op_stop(svcname, do_disable, _event_type);
out:
	if (svcname)
		free(svcname);
	_user_return = ret;
	return ret;
}


/**
  usage:

  start_service(name, <array>ordered_node_list_allowed,
  		      <array>node_list_illegal)
 */
int
sl_start_service(void)
{
	char *svcname = NULL;
	int *pref_list = NULL, pref_list_len = 0;
	int *illegal_list = NULL, illegal_list_len = 0;
	int nargs, t, newowner = 0, ret = -1;

	nargs = SLang_Num_Function_Args;

	/* Takes one, two, or three */
	if (nargs <= 0 || nargs > 3) {
		SLang_verror(SL_Syntax_Error,
		     "%s: Wrong # of args (%d), must be 1 or 2\n",
		     __FUNCTION__, nargs);
		return -1;
	}

	if (nargs == 3) {
		if (get_int_array(&illegal_list, &illegal_list_len) < 0)
			goto out;
		--nargs;
	}

	if (nargs == 2) {
		if (get_int_array(&pref_list, &pref_list_len) < 0)
			goto out;
		--nargs;
	}

	if (nargs == 1) {
		/* Just get the service name */
		t = SLang_peek_at_stack();
		if (t != SLANG_STRING_TYPE) {
			SLang_verror(SL_Syntax_Error,
				     "%s: expected type %d got %d\n",
				     __FUNCTION__,
				     SLANG_STRING_TYPE, t);
			goto out;
		}

		if (SLpop_string(&svcname) < 0)
			goto out;
	}

	/* TODO: Meat of function goes here */
	ret = service_op_start(svcname, pref_list,
			       pref_list_len, &newowner);

	if (ret == 0 && newowner > 0)
		ret = newowner;
out:
	if (svcname)
		free(svcname);
	if (illegal_list)
		free(illegal_list);
	if (pref_list)
		free(pref_list);
	_user_return = ret;
	return ret;
}


/* Take an array of integers given its length and
   push it on to the S/Lang stack */
void
push_int_array(int *stuff, int len)
{
	SLindex_Type arrlen, x;
	SLang_Array_Type *arr;
	int i;

	arrlen = len;
	arr = SLang_create_array(SLANG_INT_TYPE, 0, NULL, &arrlen, 1);
	if (!arr)
		return;

	x = 0;
	for (x = 0; x < len; x++) {
		i = stuff[x];
		SLang_set_array_element(arr, &x, &i);
	}
	SLang_push_array(arr, 1);
}


/*
   Returns an array of rgmanager-visible nodes online.  How cool is that?
 */
void
sl_nodes_online(void)
{
	int x, *nodes = NULL, nodecount = 0;

	x = member_online_set(&nodes, &nodecount);
	if (x < 0 || !nodes || !nodecount)
		return;

	push_int_array(nodes, nodecount);
	free(nodes);
}


/*
   Returns an array of rgmanager-defined services, in type:name format
   We allocate/kill this list *once* per event to ensure we don't leak
   memory
 */
void
sl_service_list(void)
{
	SLindex_Type svccount = _service_list_len, x = 0;
	SLang_Array_Type *svcarray;

	svcarray = SLang_create_array(SLANG_STRING_TYPE, 0, NULL, &svccount, 1);
	if (!svcarray)
		return;

	for (; x < _service_list_len; x++) 
		SLang_set_array_element(svcarray, &x, &_service_list[x]);

	SLang_push_array(svcarray, 1);
}


/* s_union hook (see sets.c) */
void
sl_union(void)
{
	int *arr1 = NULL, a1len = 0;
	int *arr2 = NULL, a2len = 0;
	int *ret = NULL, retlen = 0;
	int nargs = SLang_Num_Function_Args;

	if (nargs != 2)
		return;
		
	/* Remember: args on the stack are reversed */
	get_int_array(&arr2, &a2len);
	get_int_array(&arr1, &a1len);
	s_union(arr1, a1len, arr2, a2len, &ret, &retlen);
	push_int_array(ret, retlen);
	if (arr1)
		free(arr1);
	if (arr2)
		free(arr2);
	if (ret)
		free(ret);
	return;
}


/* s_intersection hook (see sets.c) */
void
sl_intersection(void)
{
	int *arr1 = NULL, a1len = 0;
	int *arr2 = NULL, a2len = 0;
	int *ret = NULL, retlen = 0;
	int nargs = SLang_Num_Function_Args;

	if (nargs != 2)
		return;
		
	/* Remember: args on the stack are reversed */
	get_int_array(&arr2, &a2len);
	get_int_array(&arr1, &a1len);
	s_intersection(arr1, a1len, arr2, a2len, &ret, &retlen);
	push_int_array(ret, retlen);
	if (arr1)
		free(arr1);
	if (arr2)
		free(arr2);
	if (ret)
		free(ret);
	return;
}


/* s_delta hook (see sets.c) */
void
sl_delta(void)
{
	int *arr1 = NULL, a1len = 0;
	int *arr2 = NULL, a2len = 0;
	int *ret = NULL, retlen = 0;
	int nargs = SLang_Num_Function_Args;

	if (nargs != 2)
		return;
		
	/* Remember: args on the stack are reversed */
	get_int_array(&arr2, &a2len);
	get_int_array(&arr1, &a1len);
	s_delta(arr1, a1len, arr2, a2len, &ret, &retlen);
	push_int_array(ret, retlen);
	if (arr1)
		free(arr1);
	if (arr2)
		free(arr2);
	if (ret)
		free(ret);
	return;
}


/* s_subtract hook (see sets.c) */
void
sl_subtract(void)
{
	int *arr1 = NULL, a1len = 0;
	int *arr2 = NULL, a2len = 0;
	int *ret = NULL, retlen = 0;
	int nargs = SLang_Num_Function_Args;

	if (nargs != 2)
		return;
		
	/* Remember: args on the stack are reversed */
	get_int_array(&arr2, &a2len);
	get_int_array(&arr1, &a1len);
	s_subtract(arr1, a1len, arr2, a2len, &ret, &retlen);
	push_int_array(ret, retlen);
	if (arr1)
		free(arr1);
	if (arr2)
		free(arr2);
	if (ret)
		free(ret);
	return;
}


/* Shuffle array (see sets.c) */
void
sl_shuffle(void)
{
	int *arr1 = NULL, a1len = 0;
	int nargs = SLang_Num_Function_Args;

	if (nargs != 1)
		return;
		
	/* Remember: args on the stack are reversed */
	get_int_array(&arr1, &a1len);
	s_shuffle(arr1, a1len);
	push_int_array(arr1, a1len);
	if (arr1)
		free(arr1);
	return;
}


/* Converts an int array to a string so we can log it in one shot */
static int
array_to_string(char *buf, int buflen, int *array, int arraylen)
{
	char intbuf[16];
	int x, len, remain = buflen;

	memset(intbuf, 0, sizeof(intbuf));
	memset(buf, 0, buflen);
	len = snprintf(buf, buflen - 1, "[ ");
	if (len == buflen)
		return -1;

	remain -= len;
	for (x = 0; x < arraylen; x++) {
		len = snprintf(intbuf, sizeof(intbuf) - 1, "%d ", array[x]);
		remain -= len;
		if (remain > 0) {
			strncat(buf, intbuf, len);
		} else {
			return -1;
		}
	}

	len = snprintf(intbuf, sizeof(intbuf) - 1 ,  "]");
	remain -= len;
	if (remain > 0) {
		strncat(buf, intbuf, len);
	} else {
		return -1;
	}
	return (buflen - remain);
}


/**
  Start at the end of the arg list and work backwards, prepending a string.
  This does not support standard log_printf / printf formattting; rather, we 
  just allow integers / strings to be mixed on the stack, figure out the
  type, convert it to the right type, and prepend it on to our log message

  The last must be a log level, as specified above:
     LOG_DEBUG
     ...
     LOG_EMERG

  This matches up with log_printf / syslog mappings in the var table; the above
  are constants in the S/Lang interpreter.  Any number of arguments may
  be provided.  Examples are:

    log(LOG_INFO, "String", 1, "string2");

  Result:  String1string2

    log(LOG_INFO, "String ", 1, " string2");

  Result:  String 1 string2

 */
void
sl_log_printf(int level)
{
	int t, nargs, len;
	//int level;
	int s_intval;
	char *s_strval;
	int *nodes = 0, nlen = 0;
	char logbuf[512];
	char tmp[256];
	int need_free;
	int remain = sizeof(logbuf)-2;

	nargs = SLang_Num_Function_Args;
	if (nargs < 1)
		return;

	memset(logbuf, 0, sizeof(logbuf));
	memset(tmp, 0, sizeof(tmp));
	logbuf[sizeof(logbuf)-1] = 0;
	logbuf[sizeof(logbuf)-2] = '\n';

	while (nargs && (t = SLang_peek_at_stack()) >= 0 && remain) {
		switch(t) {
		case SLANG_ARRAY_TYPE:
			if (get_int_array(&nodes, &nlen) < 0)
				return;
			len = array_to_string(tmp, sizeof(tmp),
					      nodes, nlen);
			if (len < 0) {
				free(nodes);
				return;
			}
			free(nodes);
			break;
		case SLANG_INT_TYPE:
			if (SLang_pop_integer(&s_intval) < 0)
				return;
			len=snprintf(tmp, sizeof(tmp) - 1, "%d", s_intval);
			break;
		case SLANG_STRING_TYPE:
			need_free = 0;
			if (SLpop_string(&s_strval) < 0)
				return;
			len=snprintf(tmp, sizeof(tmp) - 1, "%s", s_strval);
			SLfree(s_strval);
			break;
		default:
			need_free = 0;
			len=snprintf(tmp, sizeof(tmp) - 1,
				     "{UnknownType %d}", t);
			break;
		}

		--nargs;

		if (len > remain)
			return;
		remain -= len;

		memcpy(&logbuf[remain], tmp, len);
	}

#if 0
	printf("<%d> %s\n", level, &logbuf[remain]);
#endif
	log_printf(level, "%s", &logbuf[remain]);
	return;
}


/* Logging functions */
void
sl_log_debug(void)
{
	sl_log_printf(LOG_DEBUG);
}


void
sl_log_info(void)
{
	sl_log_printf(LOG_INFO);
}


void
sl_log_notice(void)
{
	sl_log_printf(LOG_NOTICE);
}


void
sl_log_warning(void)
{
	sl_log_printf(LOG_WARNING);
}


void
sl_log_err(void)
{
	sl_log_printf(LOG_ERR);
}


void
sl_log_crit(void)
{
	sl_log_printf(LOG_CRIT);
}


void
sl_log_alert(void)
{
	sl_log_printf(LOG_ALERT);
}


void
sl_log_emerg(void)
{
	sl_log_printf(LOG_EMERG);
}


void
sl_die(void)
{
	_stop_processing = 1;
	return;
}


SLang_Intrin_Fun_Type rgmanager_slang[] =
{
	MAKE_INTRINSIC_0("nodes_online", sl_nodes_online, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("service_list", sl_service_list, SLANG_VOID_TYPE),

	MAKE_INTRINSIC_SS("service_property", sl_service_property,
			  SLANG_STRING_TYPE),
	MAKE_INTRINSIC_S("service_domain_info", sl_domain_info, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("service_stop", sl_stop_service, SLANG_INT_TYPE),
	MAKE_INTRINSIC_0("service_start", sl_start_service, SLANG_INT_TYPE),
	MAKE_INTRINSIC_S("service_status", sl_service_status,
			 SLANG_VOID_TYPE),
	MAKE_INTRINSIC_S("service_freeze", sl_service_freeze,
			 SLANG_INT_TYPE),
	MAKE_INTRINSIC_S("service_unfreeze", sl_service_unfreeze,
			 SLANG_INT_TYPE),

	/* Node list manipulation */
	MAKE_INTRINSIC_0("union", sl_union, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("intersection", sl_intersection, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("delta", sl_delta, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("subtract", sl_subtract, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("shuffle", sl_shuffle, SLANG_VOID_TYPE),

	/* Logging */
	MAKE_INTRINSIC_0("debug", sl_log_debug, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("info", sl_log_info, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("notice", sl_log_notice, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("warning", sl_log_warning, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("err", sl_log_err, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("crit", sl_log_crit, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("alert", sl_log_alert, SLANG_VOID_TYPE),
	MAKE_INTRINSIC_0("emerg", sl_log_emerg, SLANG_VOID_TYPE),

	MAKE_INTRINSIC_0("stop_processing", sl_die, SLANG_VOID_TYPE),

	SLANG_END_INTRIN_FUN_TABLE
};


/* Hook for when we generate a script error */
void
rgmanager_slang_error_hook(char *errstr)
{
	/* Don't just send errstr, because it might contain
	   "%s" for example which would result in a crash!
	   plus, we like the newline :) */
	log_printf(LOG_ERR, "[S/Lang] %s\n", errstr);
}



/* ================================================================
 * S/Lang initialization
 * ================================================================ */
int
do_init_slang(void)
{
	SLang_init_slang();
	SLang_init_slfile();

	if (SLadd_intrin_fun_table(rgmanager_slang, NULL) < 0)
		return 1;
    	if (SLadd_intrin_var_table (rgmanager_vars, NULL) < 0)
		return 1;

	/* TODO: Make rgmanager S/Lang conformant.  Though, it
	   might be a poor idea to provide access to all the 
	   S/Lang libs */
	SLpath_set_load_path(RESOURCE_ROOTDIR);

	_my_node_id = my_id();
	__sl_initialized = 1;

	SLang_Error_Hook = rgmanager_slang_error_hook;

	return 0;
}


/*
   Execute a script / file and return the result to the caller
   Log an error if we receive one.
 */
int
do_slang_run(const char *file, const char *script)
{
	int ret = 0;

	if (file) 
		ret = SLang_load_file((char *)file);
	else
		ret = SLang_load_string((char *)script);

	if (ret < 0) {
		log_printf(LOG_ERR, "[S/Lang] Script Execution Failure\n");
		SLang_restart(1);
	}

	return ret;
}


int
S_node_event(const char *file, const char *script, int nodeid,
	     int state, int clean)
{
	int ret;
	cluster_member_list_t *membership = member_list();

	_node_name = strdup(memb_id_to_name(membership, nodeid));
	_node_state = state;
	_node_clean = clean;
	_node_id = nodeid;
	free_member_list(membership);

	ret = do_slang_run(file, script);

	_node_state = 0;
	_node_clean = 0;
	_node_id = 0;
	if (_node_name)
		free(_node_name);
	_node_name = NULL;

	return ret;
}


int
S_service_event(const char *file, const char *script, char *name,
	        int state, int owner, int last_owner)
{
	int ret;

	_service_name = name;
	_service_state = (char *)rg_state_str(state);
	_service_owner = owner;
	_service_last_owner = last_owner;

	switch(state) {
	case RG_STATE_DISABLED:
	case RG_STATE_STOPPED:
	case RG_STATE_FAILED:
	case RG_STATE_RECOVER:
	case RG_STATE_ERROR:
		/* There is no owner for these states.  Ever.  */
		_service_owner = -1;
	}

	ret = do_slang_run(file, script);

	_service_name = NULL;
	_service_state = 0;
	_service_owner = 0;
	_service_last_owner = 0;

	return ret;
}


int
S_user_event(const char *file, const char *script, char *name,
	     int request, int arg1, int arg2, int target, msgctx_t *ctx)
{
	int ret = RG_SUCCESS;

	_service_name = name;
	_service_owner = target;
	_user_request = request;
	_user_arg1 = arg1;
	_user_arg2 = arg2;
	_user_return = 0;

	ret = do_slang_run(file, script);
	if (ret < 0) {
		_user_return = RG_ESCRIPT;
	}

	_service_name = NULL;
	_service_owner = 0;
	_user_request = 0;
	_user_arg1 = 0;
	_user_arg2 = 0;

	/* XXX Send response code to caller - that 0 should be the
	   new service owner, if there is one  */
	if (ctx) {
		if (_user_return > 0) {
			/* sl_start_service() squashes return code and
			   node ID into one value.  <0 = error, >0 =
			   success, return-value == node id running
			   service */
			send_ret(ctx, name, 0, request, _user_return);
		} else {
			/* return value < 0 ... pass directly back;
			   don't transpose */
			send_ret(ctx, name, _user_return, request, 0);
		}
		msg_close(ctx);
		msg_free_ctx(ctx);
	}
	_user_return = 0;
	return ret;
}


int
slang_do_script(event_t *pattern, event_t *ev)
{
	_event_type = ev->ev_type;
	int ret = 0;

	switch(ev->ev_type) {
	case EVENT_NODE:
		ret = S_node_event(
				pattern->ev_script_file,
				pattern->ev_script,
				ev->ev.node.ne_nodeid,
				ev->ev.node.ne_state,
				ev->ev.node.ne_clean);
		break;
	case EVENT_RG:
		ret = S_service_event(
				pattern->ev_script_file,
				pattern->ev_script,
				ev->ev.group.rg_name,
				ev->ev.group.rg_state,
				ev->ev.group.rg_owner,
				ev->ev.group.rg_last_owner);
		break;
	case EVENT_USER:
		ret = S_user_event(
				pattern->ev_script_file,
				pattern->ev_script,
				ev->ev.user.u_name,
				ev->ev.user.u_request,
				ev->ev.user.u_arg1,
				ev->ev.user.u_arg2,
				ev->ev.user.u_target,
				ev->ev.user.u_ctx);
		break;
	default:
		break;
	}

	_event_type = EVENT_NONE;
	return ret;
}



/**
  Process an event given our event table and the event that
  occurred.  Note that the caller is responsible for freeing the
  event - do not free (ev) ...
 */
int
slang_process_event(event_table_t *event_table, event_t *ev)
{
	int x, y;
	event_t *pattern;

	if (!__sl_initialized)
		do_init_slang();

	/* Get the service list once before processing events */
	if (!_service_list || !_service_list_len)
		_service_list = get_service_names(&_service_list_len);

	_stop_processing = 0;
	for (x = 1; x <= event_table->max_prio; x++) {
		list_for(&event_table->entries[x], pattern, y) {
			if (event_match(pattern, ev))
				slang_do_script(pattern, ev);
			if (_stop_processing)
				goto out;
		}
	}

	/* Default level = 0 */
	list_for(&event_table->entries[0], pattern, y) {
		if (event_match(pattern, ev))
			slang_do_script(pattern, ev);
		if (_stop_processing)
			goto out;
	}

out:
	/* Free the service list */
	if (_service_list) {
		for(x = 0; x < _service_list_len; x++) {
			free(_service_list[x]);
		}
		free(_service_list);
		_service_list = NULL;
		_service_list_len = 0;
	}

	return 0;
}
