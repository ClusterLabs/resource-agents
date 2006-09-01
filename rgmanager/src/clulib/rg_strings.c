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
#include <resgroup.h>

struct { int val; char *str; } rg_error_strings[] = {
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


char *rg_strerror(int err)
{
	int x;

	for (x = 0; rg_error_strings[x].str != NULL; x++) {
		if (rg_error_strings[x].val == err) {
			return rg_error_strings[x].str;
		}
	}

	return "Unknown";
}
	

const char *rg_state_strings[] = {
	"stopped",
	"starting",
	"started",
	"stopping",
	"failed",
	"uninitialized",
	"checking",
	"recoverable",
	"recovering",
	"disabled",
	""
};

const char *rg_req_strings[] = {
	"success",
	"fail",
	"start",
	"stop",
	"status",
	"disable",
	"stop (recovery)",
	"start (recovery)",
	"restart",
	"exiting",
	"initialize",
	"enable",
	"status inquiry",
	"relocate",
	"conditional stop",
	"conditional start",
	"remote start",
	"user stop",
	""
};

