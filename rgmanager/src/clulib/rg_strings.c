/*
  Copyright Red Hat, Inc. 2004-2006

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
#include <res-ocf.h>
#include <resgroup.h>

struct string_val {
	int val;
	char *str;
};


const struct string_val rg_error_strings[] = {
	{ RG_ERUN,      "Service is already running" },
	{ RG_EQUORUM,	"Operation requires quorum" },
	{ RG_EINVAL,	"Invalid operation for resource" },
	{ RG_EDEPEND,	"Operation violates dependency rule" },
	{ RG_EAGAIN,	"Temporary failure; try again" },
	{ RG_EDEADLCK,	"Operation would cause a deadlock" },
	{ RG_ENOSERVICE,"Service does not exist" },
	{ RG_EFORWARD,	"Service not mastered locally" },
	{ RG_EABORT,	"Aborted; service failed" },
	{ RG_EFAIL,	"Failure" },
	{ RG_ESUCCESS,	"Success" },
	{ RG_YES,	"Yes" },
	{ RG_NO, 	"No" },
	{ 0,		NULL }
};


const struct string_val rg_req_strings[] = {
	{RG_SUCCESS, "success" },
	{RG_FAIL, "fail"},
	{RG_START, "start"},
	{RG_STOP, "stop"},
	{RG_STATUS, "status"},
	{RG_DISABLE, "disable"},
	{RG_STOP_RECOVER, "stop (recovery)"},
	{RG_START_RECOVER, "start (recovery)"},
	{RG_RESTART, "restart"},
	{RG_EXITING, "exiting"},
	{RG_INIT, "initialize"},
	{RG_ENABLE, "enable"},
	{RG_STATUS_NODE, "status inquiry"},
	{RG_RELOCATE, "relocate"},
	{RG_CONDSTOP, "conditional stop"},
	{RG_CONDSTART, "conditional start"},
	{RG_START_REMOTE,"remote start"},
	{RG_STOP_USER, "user stop"},
	{RG_STOP_EXITING, "stop (shutdown)"},
	{RG_LOCK, "locking"},
	{RG_UNLOCK, "unlocking"},
	{RG_QUERY_LOCK, "lock status inquiry"},
	{RG_MIGRATE, "migrate"},
	{RG_NONE, "none"},
	{0, NULL}
};


const struct string_val rg_state_strings[] = {
	{RG_STATE_STOPPED, "stopped"},
	{RG_STATE_STARTING, "starting"},
	{RG_STATE_STARTED, "started"},
	{RG_STATE_STOPPING, "stopping"},
	{RG_STATE_FAILED, "failed"},
	{RG_STATE_UNINITIALIZED, "uninitialized"},
	{RG_STATE_CHECK, "checking"},
	{RG_STATE_ERROR, "recoverable"},
	{RG_STATE_RECOVER, "recovering"},
	{RG_STATE_DISABLED, "disabled"},
	{RG_STATE_MIGRATE, "migrating"},
	{0, NULL}
};


const struct string_val agent_ops[] = {
	{RS_START, "start"},
	{RS_STOP, "stop"},
	{RS_STATUS, "status"},
	{RS_RESINFO, "resinfo"},
	{RS_RESTART, "restart"},
	{RS_RELOAD, "reload"},
	{RS_CONDRESTART, "condrestart"},		/* Unused */
	{RS_RECOVER, "recover"},		
	{RS_CONDSTART, "condstart"},
	{RS_CONDSTOP, "condstop"},
	{RS_MONITOR, "monitor"},
	{RS_META_DATA, "meta-data"},		/* printenv */
	{RS_VALIDATE, "validate-all"},
	{RS_MIGRATE, "migrate"},
	{0 , NULL}
};


static inline const char *
rg_search_table(const struct string_val *table, int val)
{
	int x;

	for (x = 0; table[x].str != NULL; x++) {
		if (table[x].val == val) {
			return table[x].str;
		}
	}

	return "Unknown";
}


const char *
rg_strerror(int val)
{
	return rg_search_table(rg_error_strings, val);
}
	
const char *
rg_state_str(int val)
{
	return rg_search_table(rg_state_strings, val);
}


const char *
rg_req_str(int val)
{
	return rg_search_table(rg_req_strings, val);
}


const char *
agent_op_str(int val)
{
	return rg_search_table(agent_ops, val);
}
