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
 * Cluster Plugin Test Program.
 *
 * XXX Needs Doxygenification.
 */
#include <magma.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>


void
usage(char *arg0)
{
	printf(
"usage: %s <options>\n\n"
"Plugin debugging functions:\n\n"
"\tlist              List plugins and their status\n"
"\tenable <plugin>   Enable the specified plugin\n"
"\tdisable <plugin>  Disable the specified plugin (DANGEROUS)\n"
"\n"
"Cluster debugging functions.  These may be prepended by \"login\" to \n"
"force logging in to the specified service group.\n\n"
"\tlock [resource]   Obtain a cluster resource lock\n"
"\tlisten [group]    Listen for events (only useful with \"login\" \n"
"\t                  when using CMAN's Service Manager)\n"
"\tlocalname         Display this member's name\n"
"\tlocalid           Display this member's node ID\n"

"\tmembers [group]   List cluster members (optionally in [group]\n"
"\tquorum [group]    Display cluster/node quorum/group state\n"
"\n"
"Other options:\n"
"\tconfig <item>     Show library build configuration\n\n", arg0);
}


void
config_usage(char *arg0)
{
	printf(
"usage: %s config <item>\n"
"\tlibs              Show library link flags\n"
"\tlibs-nt           Show reentrant (non-pthread-encumbered) linker\n"
"\t                  flags\n"
"\tcflags            Show CFLAGS necessary to build\n"
"\tplugindir         Show where to install plugins\n"
"\tlibdir            Show where dynamic libraries are installed\n"
"\tslibdir           Show where static libraries are installed\n"
, arg0);
}



void
wait_for_events(int fd, char *groupname)
{
	cluster_member_list_t *membership;
	cluster_member_list_t *new, *lost, *gained;
	fd_set rfds;

	membership = clu_member_list(groupname);

	print_member_list(membership, 1);

	printf("=== Waiting for events.\n");

	while(1) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		select(fd+1, &rfds, NULL, NULL, NULL);
		
		switch(clu_get_event(fd)) {
		case CE_NULL:
			printf("-E- Spurious wakeup\n");
			break;
		case CE_SUSPEND:
			printf("*E* Suspend activities\n");
			break;
		case CE_MEMB_CHANGE:
			printf("*E* Membership change\n");

			new = clu_member_list(groupname);
			lost = memb_lost(membership, new);
			gained = memb_gained(membership, new);

			if (lost) {
				printf("<<< Begin Nodes lost\n");
				print_member_list(lost, 0);
				printf("<<< End Nodes Lost\n");
				free(lost);
			}

			if (gained) {
				printf(">>> Begin Nodes gained\n");
				print_member_list(gained, 0);
				printf(">>> End Nodes gained\n");
				free(gained);
			}

			free(membership);
			membership = new;

			break;
		case CE_QUORATE:
			printf("*E* Quorum formed\n");
			break;
		case CE_INQUORATE:
			printf("*E* Quorum dissolved\n");
			break;
		case CE_SHUTDOWN:
			printf("*E* Node shutdown\n");
			exit(0);
		}
	}
}


void
lock_something(char *lockname)
{
	void *lockp = NULL;
	int ret;

	if (!lockname)
		lockname = "Something";

	printf("Locking \"%s\"...",lockname);
	fflush(stdout);

	ret = clu_lock(lockname, CLK_EX, &lockp);

	if (ret != 0) {
		printf("FAILED: %d\n", ret);
		return;
	}

	printf("OK\n");
	printf("Press <ENTER> to unlock\n");
	getchar();

	printf("Unlocking \"%s\"...", lockname);
	fflush(stdout);
	if (clu_unlock(lockname, lockp) != 0) {
		printf("FAILED\n");
		return;
	}
	printf("OK\n");
}


void
perform_op(int fd, char *op, char *data)
{
	cluster_member_list_t *nodelist;
	int x;
	char *state;
	char name[256];
	uint64_t nid;

	if (strcmp(op, "null") == 0) {
		clu_null();
	} else if (strcmp(op, "localname") == 0) {
		if (clu_local_nodename(data, name, 256) == 0) {
			printf("Local node name = %s\n", name);
		}
	} else if (strcmp(op, "localid") == 0) {
		if (clu_local_nodeid(data, &nid) == 0) {
			printf("Local node id = 0x%x\n", (uint32_t)nid);
		}
	} else if (strcmp(op, "lock") == 0) {
		lock_something(data);
	} else if (strcmp(op, "members") == 0) {
		nodelist = clu_member_list(data);
		if (nodelist) {
			for (x=0; x<nodelist->cml_count; x++) {

				switch (nodelist->cml_members[x].cm_state) {
				case STATE_DOWN:
					state = "DOWN";
					break;
				case STATE_UP:
					state = "UP";
					break;
				default:
					state = "UNKNOWN";
					break;
				}
				printf("Member ID 0x%016llx: %s, state %s\n",
				       (unsigned long long)
					nodelist->cml_members[x].cm_id,
				       nodelist->cml_members[x].cm_name,
				       state);
			}
		}
	} else if (strcmp(op, "quorum") == 0) {
		switch (clu_quorum_status(data)) {
		case (QF_QUORATE|QF_GROUPMEMBER):
			state = "Quorate & Group Member";
			break;
		case (QF_QUORATE):
			state = "Quorate";
			break;
		default:
			state = "Inquorate";
			break;
		}
		printf("Quorum state = %s\n", state);
	} else if (strcmp(op, "listen") == 0) {
		printf("Listening for events (group %s)...\n", data?data:"ALL");
		wait_for_events(fd, data);
	}
}


/**
  ... uses internal magma library functions...
 */
int read_dirnames_sorted(char *dir, char ***filenames);
void free_dirnames(char **);


int
toggle_plugin(int argc, char **argv)
{
	struct stat sb;
	char filename[1024];

	if (argc < 3) {
		printf("No file specified\n");
		usage(basename(argv[0]));
		return 1;
	}

	snprintf(filename, sizeof(filename), "%s/%s",
		 PLUGINDIR, argv[2]);

	if (stat(filename, &sb) < 0) {
		fprintf(stderr, "Failed to %s %s: %s\n", argv[1], 
			argv[2], strerror(errno));
		return 1;
	}

	if (S_ISDIR(sb.st_mode)) {
		errno = EISDIR;
		fprintf(stderr, "Failed to %s %s: %s\n", argv[1],
			argv[2], strerror(errno));
		return 1;
	}

	if (!strcmp(argv[1], "disable")) {
		if (!(sb.st_mode & S_IRUSR)) {
			fprintf(stderr,
				"Failed to disable %s: Already disabled\n",
				argv[2]);
			return 1;
		}
		sb.st_mode &= (mode_t)(~S_IRUSR);
	} else {
		if (sb.st_mode & S_IRUSR) {
			fprintf(stderr,
				"Failed to enable %s: Already enabled\n",
				argv[2]);
			return 1;
		}
		sb.st_mode |= S_IRUSR;
	}

	if (chmod(filename, sb.st_mode) < 0) {
		fprintf(stderr, "Failed to %s %s: %s\n", argv[1], 
			argv[2], strerror(errno));
		return 1;
	}

	return 0;
}


int
show_config(int argc, char **argv)
{
	/* XXX hardcoded ;( */
	if (argc < 3) {
		config_usage(basename(argv[0]));
		return 1;
	}

	if (!strcmp(argv[2], "cflags")) {
		printf("-I%s -DPLUGINDIR=\"%s\"\n", INCDIR,
		       PLUGINDIR);
		return 0;
	}

	if (!strcmp(argv[2], "libs")) {
		printf("-L%s -lmagma -lmagmamsg -lpthread -ldl\n", LIBDIR);
		return 0;
	}

	if (!strcmp(argv[2], "libs-nt")) {
		printf("-L%s -lmagma_nt -ldl\n", LIBDIR);
		return 0;
	}

	if (!strcmp(argv[2], "libdir")) {
		printf("%s\n", LIBDIR);
		return 0;
	}

	if (!strcmp(argv[2], "slibdir")) {
		printf("%s\n", SLIBDIR);
		return 0;
	}

	if (!strcmp(argv[2], "plugindir")) {
		printf("%s\n", PLUGINDIR);
		return 0;
	}

	config_usage(basename(argv[0]));
	return 1;
}


int
list_plugins(void)
{
	int found = 0;
	cluster_plugin_t *cp;
	char **filenames;
	int fcount = 0, l;
	char *bname, *tab;

	printf("Magma: Checking plugins in %s\n", PLUGINDIR);

	if (read_dirnames_sorted(PLUGINDIR, &filenames) != 0) {
		printf("Error reading %s: %s\n", PLUGINDIR,
		       strerror(errno));
		return -1;
	}

	printf("\n");
	printf("File\t\tStatus\tMessage\n");
	printf("----\t\t------\t-------\n");

	for (fcount = 0; filenames[fcount]; fcount++) {

		cp = cp_load(filenames[fcount]);

		bname = basename(filenames[fcount]);
		l = strlen(bname);
		if (l < 8)
			tab = "\t\t";
		else
			tab = "\t";
		

		if (cp == NULL) {
			if (errno == EISDIR)
				continue;
			printf("%s%s[FAIL]\t%s\n", bname, tab,
			       cp_load_error(errno));
			continue;
		}
	
		if (cp_init(cp, NULL, 0) < 0) {
			printf("%s%s[FAIL]\t%s\n", bname, tab,
			       strerror(errno));
			cp_unload(cp);
			cp = NULL;
			continue;
		}

		++found;

		printf("%s%s[OK]\t%s\n", bname, tab, cp_plugin_version(cp));

		cp_unload(cp);
		cp = NULL;
	}

	printf("\n");
	free_dirnames(filenames);

	if (!found) {
		printf("Magma: No usable plugins found.\n");
		errno = ELIBACC;
		return -1;
	}

	printf("Magma: %d plugins available\n", found);

	return 0;
}


int
main(int argc, char **argv)
{
	int fd, login=0;
	char *arg0 = basename(argv[0]);

	signal(SIGPIPE, SIG_IGN);

	if (argc < 2 || (strcmp(argv[1], "-h") == 0)) {
		usage(arg0);
		return 1;
	}

	if (!strcmp(argv[1], "login")) {
		login = 1;
		--argc;
		++argv;
	}

	if (!strcmp(argv[1], "list")) {
		return list_plugins();
	}

	if (!strcmp(argv[1], "enable")) {
		return toggle_plugin(argc, argv);
	}

	if (!strcmp(argv[1], "disable")) {
		return toggle_plugin(argc, argv);
	}

	if (!strcmp(argv[1], "config")) {
		return show_config(argc, argv);
	}


	fd = clu_connect("cluster::usrm", login);
	
	if (fd < 0) {
		switch(errno) {
		case ESRCH:
			fprintf(stderr,
			        "Connect failure: No cluster running?\n");
			break;
		case ELIBACC:
			fprintf(stderr, "Connect failure: No plugins "
				"available (try '%s list')\n",
				arg0);
			break;
		default:
			perror("Connect failure");
			break;
		}
		return -1;
	}

	printf("Connected via: %s\n",clu_plugin_version());

	if (argc == 2) {
		perform_op(fd, argv[1], NULL);
	} else if (argc == 3) {
		perform_op(fd, argv[1], argv[2]);
	}

	clu_disconnect(fd);

	return 0;
}
