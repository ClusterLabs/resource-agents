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
