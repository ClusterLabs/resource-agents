/*
  Copyright Red Hat, Inc. 2002-2003

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
/** @file
 * Cluster Namespace Test Program.
 *
 * XXX Needs Doxygenification.
 */
#include <magma.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void
perform_op(char *op, char *data)
{
	cluster_member_list_t *nodelist;
	int x;

	if (strcmp(op, "null") == 0) {
		clu_null();
	} else if (strcmp(op, "members") == 0) {
		nodelist = clu_member_list(NULL);
		if (nodelist) {
			for (x=0; x<nodelist->cml_count; x++) {
				printf("Member ID %d: %s\n",
				       (int)nodelist->cml_members[x].cm_id,
				       nodelist->cml_members[x].cm_name);
			}
		}
	} else if (strcmp(op, "quorum") == 0) {
		printf("Quorum state = %d\n", clu_quorum_status(NULL));
	} else if (strcmp(op, "version") == 0) {
		printf("Version = %s\n", clu_plugin_version());
	} else {
		printf("not yet...\n");
	}
}


int
main(int argc, char **argv)
{
	cluster_plugin_t *cpp;
	int fd;

	if (argc < 2) {
		printf("usage: %s <libname> [<op> <data>]\n",
		       argv[0]);
		return 1;
	}

	cpp = cp_load(argv[1]);
	if (!cpp) {
		perror("cp_load");
		return 1;
	}

	cp_init(cpp, NULL, 0);

	clu_set_default(cpp);
	fd = clu_open();
	if (argc == 3) {
		perform_op(argv[2], NULL);
	} else if (argc == 4) {
		perform_op(argv[2], argv[3]);
	}

	clu_close(fd);
	cp_unload(cpp);

	return 0;
}
