#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>
#include <stdlib.h>
#include <stdio.h>
#include <resgroup.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <list.h>
#include <restart_counter.h>
#include <reslist.h>
#include <pthread.h>
#include <depends.h>
#include <event.h>

#ifndef NO_CCS
#error "Can not be built with CCS support."
#endif

void res_build_name(char *, size_t, resource_t *);


/**
  Tells us if a resource group can be migrated.
 */
int
group_migratory(resource_t **resources, resource_node_t **tree, char *groupname)
{
	resource_node_t *rn;
	resource_t *res;
	int migrate = 0, x, ret = 0;

	res = find_root_by_ref(resources, groupname);
	if (!res) {
		/* Nonexistent or non-TL RG cannot be migrated */
		return 0;
	}

	for (x = 0; res->r_rule->rr_actions[x].ra_name; x++) {
		if (!strcmp(res->r_rule->rr_actions[x].ra_name,
		    "migrate")) {
			migrate = 1;
			break;
		}
	}

	if (!migrate)
		goto out_unlock;

	list_do(tree, rn) {
		if (rn->rn_resource == res && rn->rn_child) {
			/* TL service w/ children cannot be migrated */
			goto out_unlock;
		}
	} while (!list_done(tree, rn));


	/* Ok, we have a migrate option to the resource group,
	   the resource group has no children, and the resource
	   group exists.  We're all good */
	ret = 1;

out_unlock:
	return ret;
}

#define shift() {++argv; --argc;}

#define USAGE_TEST \
	"\ttest <configfile> [args..]\n" \
	"\t\tstart <type> <resource>\n" \
	"\t\tstatus <type> <resource>\n" \
	"\t\tstop <type> <resource>\n" \
	"\n"

#define USAGE_DELTA \
	"\tdelta <configfile1> <configfile2>\n\n"

#define USAGE_RULES \
	"\trules\n\n"


void _no_op_mode(int);
void malloc_dump_table(void);
char *agentpath = RESOURCE_ROOTDIR;


int
rules_func(int argc, char **argv)
{
	resource_rule_t *rulelist = NULL, *currule;
	int rules = 0;

	fprintf(stderr,"Running in rules mode.\n");

	load_resource_rules(agentpath, &rulelist);
	list_do(&rulelist, currule) {
		++rules;
	} while (!list_done(&rulelist, currule));
	fprintf(stderr, "Loaded %d resource rules\n",
		rules);
	list_do(&rulelist, currule) {
		print_resource_rule(currule);
	} while (!list_done(&rulelist, currule));

	destroy_resource_rules(&rulelist);

	return 0;
}


int
deps_func(int argc, char**argv)
{
	dep_t *depends = NULL;
	int ccsfd;

	conf_setconfig(argv[1]);
       	ccsfd = ccs_lock();
	if (ccsfd < 0) {
		printf("Error parsing %s\n", argv[1]);
		goto out;
	}

	construct_depends(ccsfd, &depends);
	if (depends) {
		print_depends(stdout, &depends);
	}
	
	deconstruct_depends(&depends);

out:
	ccs_unlock(ccsfd);
	return 0;
}



int
test_func(int argc, char **argv)
{
	fod_t *domains = NULL;
	dep_t *depends = NULL;
	resource_rule_t *rulelist = NULL, *currule;
	resource_t *reslist = NULL, *curres;
	resource_node_t *tree = NULL, *tmp, *rn = NULL;
	int ccsfd, ret = 0, rules = 0;
	event_table_t *events = NULL;

	fprintf(stderr,"Running in test mode.\n");

	conf_setconfig(argv[1]);
       	ccsfd = ccs_lock();
	if (ccsfd < 0) {
		printf("Error parsing %s\n", argv[1]);
		goto out;
	}

	load_resource_rules(agentpath, &rulelist);
	construct_domains(ccsfd, &domains);
	construct_events(ccsfd, &events);
	construct_depends(ccsfd, &depends);
	load_resources(ccsfd, &reslist, &rulelist);
	build_resource_tree(ccsfd, &tree, &rulelist, &reslist);

	shift();

	if (argc == 1) {
		/*
		printf("=== Resource XML Rules ===\n");
		list_do(&rulelist, currule) {
			print_resource_rule(currule);
		} while (!list_done(&rulelist, currule));
		 */
		list_do(&rulelist, currule) {
			++rules;
		} while (!list_done(&rulelist, currule));
		fprintf(stderr, "Loaded %d resource rules\n",
			rules);

		if (reslist) {
			printf("=== Resources List ===\n");
			list_do(&reslist, curres) {
				print_resource(curres);
			} while (!list_done(&reslist, curres));
		}

		if (tree) {
			printf("=== Resource Tree ===\n");
			print_resource_tree(&tree);
		}

		if (domains) {
			printf("=== Failover Domains ===\n");
			print_domains(&domains);
		}
		
		if (depends) {
			printf("=== Dependencies ===\n");
			print_depends(stdout, &depends);
		}

		if (events) {
			printf("=== Event Triggers ===\n");
			print_events(events);
		}
	}

	ccs_unlock(ccsfd);

	if (argc < 4)
		goto out;

	curres = find_resource_by_ref(&reslist, argv[2], argv[3]);
	if (!curres) {
		printf("No resource %s of type %s found\n",
		       argv[3], argv[2]);
		goto out;
	}

	list_do(&tree, tmp) {
		if (tmp->rn_resource == curres) {
			rn = tmp;
			break;
		}
	} while (!list_done(&tree, tmp));

	if (!strcmp(argv[1], "start")) {
		printf("Starting %s...\n", argv[3]);

		if (res_start(&tree, curres, NULL)) {
			printf("Failed to start %s\n", argv[3]);
			ret = -1;
			goto out;
		}
		printf("Start of %s complete\n", argv[3]);
		goto out;
	} else if (!strcmp(argv[1], "stop")) {
		printf("Stopping %s...\n", argv[3]);

		if (res_stop(&tree, curres, NULL)) {
			ret = -1;
			goto out;
		}
		printf("Stop of %s complete\n", argv[3]);
		goto out;
	} else if (!strcmp(argv[1], "migrate")) {
		printf("Migrating %s to %s...\n", argv[3], argv[4]);

	#if 0
		if (!group_migratory(curres)) {
			printf("No can do\n");
			ret = -1;
			goto out;
		}
	#endif

		if (res_exec(rn, RS_MIGRATE, argv[4], 0)) {
			ret = -1;
			goto out;
		}
		printf("Migration of %s complete\n", argv[3]);
		goto out;
	} else if (!strcmp(argv[1], "status")) {
		printf("Checking status of %s...\n", argv[3]);

		ret = res_status(&tree, curres, NULL);
		if (ret) {
			printf("Status check of %s failed\n", argv[3]);
			goto out;
		}
		printf("Status of %s is good\n", argv[3]);
		goto out;
	}

out:
	deconstruct_depends(&depends);
	deconstruct_events(&events);
	deconstruct_domains(&domains);
	destroy_resource_tree(&tree);
	destroy_resources(&reslist);
	destroy_resource_rules(&rulelist);

	return ret;
}


int
tree_delta_test(int argc, char **argv)
{
	resource_rule_t *rulelist = NULL, *currule, *rulelist2 = NULL;
	resource_t *reslist = NULL, *curres, *reslist2 = NULL;
	resource_node_t *tree = NULL, *tree2 = NULL;
	resource_node_t *tn;
	int ccsfd, ret = 0, need_init, need_kill;
	char rg[64];

	if (argc < 2) {
		printf("Operation requires two arguments\n");
		printf(USAGE_DELTA);
		return -1;
	}

	currule = NULL;
	curres = NULL;

	fprintf(stderr,"Running in resource tree delta test mode.\n");

	conf_setconfig(argv[1]);

       	ccsfd = ccs_lock();
	if (ccsfd < 0) {
		printf("Error parsing %s\n", argv[1]);
		ret = 1;
		goto out;
	}

	load_resource_rules(agentpath, &rulelist);
	load_resources(ccsfd, &reslist, &rulelist);
	build_resource_tree(ccsfd, &tree, &rulelist, &reslist);
	ccs_unlock(ccsfd);

	conf_setconfig(argv[2]);

       	ccsfd = ccs_lock();
	if (ccsfd < 0) {
		printf("Error parsing %s\n", argv[2]);
		ret = 1;
		goto out;
	}

	load_resource_rules(agentpath, &rulelist2);
	load_resources(ccsfd, &reslist2, &rulelist2);
	build_resource_tree(ccsfd, &tree2, &rulelist2, &reslist2);
	ccs_unlock(ccsfd);

	resource_delta(&reslist, &reslist2);

	printf("=== Old Resource List ===\n");
	list_do(&reslist, curres) {
		print_resource(curres);
	} while (!list_done(&reslist, curres));
	printf("=== New Resource List ===\n");
	list_do(&reslist2, curres) {
		print_resource(curres);
	} while (!list_done(&reslist2, curres));

	resource_tree_delta(&tree, &tree2);
	printf("=== Old Resource Tree ===\n");
	print_resource_tree(&tree);
	printf("=== New Resource Tree ===\n");
	print_resource_tree(&tree2);
	printf("=== Operations (down-phase) ===\n");
	list_do(&tree, tn) {
		res_build_name(rg, sizeof(rg), tn->rn_resource);
		/* Set state to uninitialized if we're killing a RG */
		need_init = 0;

		/* Set state to uninitialized if we're killing a RG */
		need_kill = 0;
		if (tn->rn_resource->r_flags & RF_NEEDSTOP) {
			need_kill = 1;
			printf("[kill] ");
		}

		if (!tn->rn_child && ((tn->rn_resource->r_rule->rr_flags &
		    RF_DESTROY) == 0) && group_migratory(&reslist, &tree, rg) &&
		    need_kill == 1) {
			/* Do something smart here: flip state? */
			printf("[no-op] %s was removed from the config, but I am not stopping it.\n",
			       rg);
			continue;
		}

		res_condstop(&tn, tn->rn_resource, NULL);
	} while (!list_done(&tree, tn));
	printf("=== Operations (up-phase) ===\n");
	list_do(&tree2, tn) {
		res_build_name(rg, sizeof(rg), tn->rn_resource);
		/* New RG.  We'll need to initialize it. */
		need_init = 0;
		if (!(tn->rn_resource->r_flags & RF_RECONFIG) &&
		    (tn->rn_resource->r_flags & RF_NEEDSTART))
			need_init = 1;

		if (need_init) {
			printf("[init] ");
		}

		if (!tn->rn_child && ((tn->rn_resource->r_rule->rr_flags &
		    RF_INIT) == 0) && group_migratory(&reslist2, &tree2, rg) &&
		    need_init == 1) {
			/* Do something smart here? */
			printf("[noop] %s was added, but I am not initializing it\n", rg);
			continue;
		}

		if (need_init) {
			res_stop(&tn, tn->rn_resource, NULL);
		} else {
			res_condstart(&tn, tn->rn_resource, NULL);
		}
	} while (!list_done(&tree2, tn));

out:
	destroy_resource_tree(&tree2);
	destroy_resources(&reslist2);
	destroy_resource_rules(&rulelist2);

	destroy_resource_tree(&tree);
	destroy_resources(&reslist);
	destroy_resource_rules(&rulelist);

	return ret;
}


int
usage(char *arg0)
{
	printf("usage: %s [agent_path] <args..>\n\n", arg0);
	printf(USAGE_TEST);
	printf(USAGE_DELTA);
	printf(USAGE_RULES);

	exit(1);
}


int
main(int argc, char **argv)
{
	char *arg0 = basename(argv[0]);
	int ret;
	struct stat st;

	if (argc < 2) {
		usage(arg0);
		return 1;
	}

	xmlInitParser();
	while (argc > 1) {
		if (!strcmp(argv[1], "test")) {
			shift();
			ret = test_func(argc, argv);
			goto out;
		} else if (!strcmp(argv[1], "depends")) {
			shift();
			ret = deps_func(argc, argv);
			goto out;
		} else if (!strcmp(argv[1], "noop")) {
			shift();
			_no_op_mode(1);
			ret = test_func(argc, argv);
			goto out;
		} else if (!strcmp(argv[1], "rules")) {
			shift();
			ret = rules_func(argc, argv);
			goto out;
		} else if (!strcmp(argv[1], "delta")) {
			shift();
			_no_op_mode(1);
			ret = tree_delta_test(argc, argv);
			goto out;
		} else {
			ret = stat(argv[1], &st);
			if (ret == -1 || !S_ISDIR(st.st_mode)) {
				break;
			}
			fprintf(stderr,
				"Using %s as resource agent path\n",
			       argv[1]);
			agentpath = argv[1];
			shift();
		}
	}

	usage(arg0);
	xmlCleanupParser();
	malloc_dump_table();
	return 1;

out:
	xmlCleanupParser();
	malloc_dump_table();
	return ret;
}
