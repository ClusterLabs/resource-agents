#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>
#include <stdlib.h>
#include <stdio.h>
#include <resgroup.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <list.h>
#include <reslist.h>
#include <pthread.h>

#ifndef NO_CCS
#error "Can not be built with CCS support."
#endif

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
test_func(int argc, char **argv)
{
	fod_t *domains = NULL;
	resource_rule_t *rulelist = NULL, *currule;
	resource_t *reslist = NULL, *curres;
	resource_node_t *tree = NULL;
	int ccsfd, ret = 0, rules = 0;

	fprintf(stderr,"Running in test mode.\n");

	conf_setconfig(argv[1]);
       	ccsfd = ccs_lock();
	if (ccsfd == FAIL) {
		printf("Error parsing %s\n", argv[1]);
		goto out;
	}

	load_resource_rules(agentpath, &rulelist);
	construct_domains(ccsfd, &domains);
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
	} else if (!strcmp(argv[1], "status")) {
		printf("Checking status of %s...\n", argv[3]);

		if (res_status(&tree, curres, NULL)) {
			printf("Status check of %s failed\n", argv[3]);
			ret = -1;
			goto out;
		}
		printf("Status of %s is good\n", argv[3]);
		goto out;
	}

out:
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
	int ccsfd, ret = 0;

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

	curres = find_resource_by_ref(&reslist, "resourcegroup", "oracle");

	resource_tree_delta(&tree, &tree2);
	printf("=== Old Resource Tree ===\n");
	print_resource_tree(&tree);
	printf("=== New Resource Tree ===\n");
	print_resource_tree(&tree2);

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
		} else if (!strcmp(argv[1], "rules")) {
			shift();
			ret = rules_func(argc, argv);
			goto out;
		} else if (!strcmp(argv[1], "delta")) {
			shift();
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
	return 0;
}
