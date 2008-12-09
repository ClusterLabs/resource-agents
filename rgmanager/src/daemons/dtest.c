#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>
#include <stdlib.h>
#include <stdio.h>
#include <restart_counter.h>
#include <resgroup.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <list.h>
#include <reslist.h>
#include <pthread.h>
#include <depends.h>
#include <ccs.h>
#include <readline/readline.h>
#include <readline/history.h>



#ifndef NO_CCS
#error "Can not be built with CCS support."
#endif

#ifdef NO_CCS
#define ccs_get(fd, query, ret) conf_get(query, ret)
#endif

void malloc_stats(void);
void malloc_dump_table(void);
 

resource_rule_t	*rules = NULL;
resource_t	*resources = NULL;
resource_node_t *restree = NULL;
dep_t		*depends = NULL;
dep_rs_t	*resource_states = NULL;
int	 	resource_state_count = 0;
fod_t		*domains = NULL;
int		*nodes_all = NULL;
int		nodes_all_count = 0;
int 		*nodes_online = NULL;
int		nodes_online_count = 0;


void
visualize_state(char *png_viewer)
{
	char cmd[1024];
	char tf_dot[128];
	char tf_png[128];
	FILE *fp;
	int fd, x;
		
	snprintf(tf_dot, sizeof(tf_dot), "/tmp/dtest.dot.XXXXXX");
	fd = mkstemp(tf_dot);
	if (fd < 0) {
		printf("Couldn't create temporary file: %s\n", strerror(errno));
		return;
	}
	
	fp = fdopen(fd, "w+");
	if (!fp) {
		printf("Couldn't init temporary file: %s\n", strerror(errno));
		return;
	}
	
	x = dep_check(&depends, resource_states,
		  resource_state_count, nodes_online,
		  nodes_online_count);
	printf("State score: %d\n", x);
	dep_cluster_state_dot(fp, &depends, resource_states,
			resource_state_count, nodes_online,
			nodes_online_count);
	
	fclose(fp);
	close(fd);
	
	snprintf(tf_png, sizeof(tf_png), "/tmp/dtest.png.XXXXXX");
	fd = mkstemp(tf_png);
	if (fd < 0) {
		printf("Couldn't init temporary file: %s\n", strerror(errno));
		return;
	}
	
	close(fd);
	
	snprintf(cmd, sizeof(cmd), "dot -Tpng -o %s %s", tf_png, tf_dot);
	if (system(cmd) != 0) {
		printf("Error: system('%s') failed\n", cmd);
		return;
	}
	
	snprintf(cmd, sizeof(cmd), "%s %s", png_viewer, tf_png);
	if (system(cmd) != 0) {
		printf("Error: system('%s') failed\n", cmd);
		return;
	}
	
	unlink(tf_png);
	unlink(tf_dot);
}


int
vis_dep_apply_trans(dep_op_t **op_list, char *pngviewer)
{
	dep_op_t *op;
	int x;
	int ops = 0;
	dep_rs_t *states = resource_states;
	int slen = resource_state_count;
	
	visualize_state(pngviewer);

	list_do(op_list, op) {
		
		++ops;
		
		for (x = 0; x < slen; x++) {
			if (strcasecmp(op->do_res, states[x].rs_status.rs_name))
				continue;
			if (op->do_op == RG_START) {
				printf("Start %s on %d\n", op->do_res, op->do_nodeid);
				states[x].rs_status.rs_state = RG_STATE_STARTED;
				states[x].rs_status.rs_owner = op->do_nodeid;
			} else {
				printf("Stop %s\n", op->do_res);
				states[x].rs_status.rs_state = RG_STATE_STOPPED;
				states[x].rs_status.rs_owner = 0;
				states[x].rs_flags &= ~(RS_ILLEGAL_NODE|
						        RS_DEAD_NODE);
			}
			break;
		}
		
		visualize_state(pngviewer);

	} while (!list_done(op_list, op));
	
	printf("Applied %d operations\n", ops);
	
	return 0;
}

void
print_help(void)
{
	printf("nodes                     view all nodes\n");
	printf("online [id1 id2...|none]  set or view online nodes\n");
	printf("res [resource]            view a resource state (or all)\n");
	printf("start <res> <id>          start res on id\n");
	printf("dep [res]                 print dependency tree(s)\n");
	printf("stop <res1> [res2...]     stop res\n");
	printf("disable <res1> [res2...]  disable res\n");
	printf("check                     check cluster state against deps\n");
	printf("calc                      calculate transition to valid state\n");
	printf("apply [pngviewer]         Apply previous transition list\n");
	printf("                          If pngviewer is specified, send\n");
	printf("                          graphviz & bring up viewer\n");
	printf("state [pngviewer]         dump cluster state in DOT format\n");
	printf("                          If pngviewer is specified, send\n");
	printf("                          graphviz & bring up viewer\n");
	printf("quit, exit                exit\n");
}


void
show_rs_t(dep_rs_t *rs)
{
	int *allowed, cnt, x;
	
	printf("Resource %s\n  State: %s\n  Owner: %d\n",
	       rs->rs_status.rs_name,
	       rg_state_str(rs->rs_status.rs_state),
	       rs->rs_status.rs_owner);
	
	if (rs->rs_allowed) {
		allowed = rs->rs_allowed;
		cnt = rs->rs_allowed_len;
	} else {
		printf("  Allowed Nodes = [ all ]\n");
		return;
	}
		
	printf("  Allowed Nodes = [ ");
	
	for (x = 0; x < cnt; x++) {
		printf("%d ", allowed[x]);
	}
	printf("]\n");
}


int 
show_resources(char *name)
{
	int x, found = 0;
	
	for (x = 0; x < resource_state_count; x++) {
		if (!name || !strcmp(name,
		    resource_states[x].rs_status.rs_name)) {
			found = 1;
			
			show_rs_t(&resource_states[x]);
		}
		
		if (!name)
			printf("\n");
	}
	
	return !found;
}


void
show_calc_result(int final_score, dep_op_t **ops, int iter)
{
	int x = 0;
	dep_op_t *op;
	
	list_do(ops, op) {
		++x;
		if (op->do_op == RG_START) {
			printf("Start %s on %d [%d]\n", op->do_res, op->do_nodeid, op->do_iter);
		} else {
			printf("Stop %s [%d]\n", op->do_res, op->do_iter);
		}
	} while (!list_done(ops, op));
	
	if (iter >= 0)
		printf("%d operations reduced in %d iterations; final score = %d\n", x, iter,
			final_score);
	else
		printf("%d operations\n", x);
}
		


dep_rs_t *
get_rs_byname(char *name)
{
	int x, found = 0;
	
	if (!name)
		return NULL;
	
	for (x = 0; x < resource_state_count; x++) {
		if (!name || !strcmp(name,
		    resource_states[x].rs_status.rs_name)) {
			found = 1;
			
			return &resource_states[x];
		}
		
	}
	
	return NULL;
}



void
dtest_shell(void)
{
	resource_t *res;
	char name[64];
	char *cmd = NULL;
	int done = 0;
	char *save, *curr, *tmp;
	int cnt, err;
	int *nodes;
	int x;
	dep_rs_t *rs;
	dep_t *depcpy;
	dep_op_t *ops = NULL, *op;
	
	nodes = malloc(sizeof(int)*nodes_all_count);
	// FIXME: handle failed malloc
	
	while (!done) {
		
		if (cmd)
			free(cmd);
		cmd = readline("> ");
		if (!cmd || !strlen(cmd)) {
			printf("\n");
		}
		if (!cmd) {
			break;
		}
		
		/*
		if (cmd && cmd[0])
			add_history(cmd);
		 */
		
		if (!cmd[0])
			continue;
		
		curr = strtok_r(cmd, " ", &save);
		err = 0;
		
		if (!strcmp(curr, "online")) {
			cnt = 0;
			err = 0;
			while ((curr = strtok_r(NULL, " ", &save))) {
				nodes[cnt] = atoi(curr);
				if (nodes[cnt] <= 0) {
					printf("Error: Node '%s' invalid\n",
					       curr);
					err = 1;
					break;
				}
				
				err = 1;
				for (x = 0; x < nodes_all_count; x++) {
					if (nodes_all[x] == nodes[cnt]) {
						err = 0;
						break;
					}
				}
				++cnt;
			}
			
			if (cnt && !err) {
				if (nodes_online)
					free(nodes_online);
				
				nodes_online = malloc(sizeof(int) * cnt);
				// FIXME: handle failed malloc
				
				for (x = 0; x < cnt; x++)
					nodes_online[x] = nodes[x];
				nodes_online_count = cnt;
			}
			
			if (!err) {
				printf("Online = [ ");
				for (x = 0; x < nodes_online_count; x++) {
					printf("%d ", nodes_online[x]);
				}
				printf("]\n");
			}
		} else if (!strcmp(curr, "start")) {
			
			curr = strtok_r(NULL, " ", &save);
			
			if (!curr) {
				printf("usage: start <resource> <nodeid>"
					" [test]\n");
				continue;
			}
			
			if (!strchr(curr,':')) {
				snprintf(name, sizeof(name), "service:%s",
						curr);
				curr = name;
			}
			
			rs = get_rs_byname(curr);
			if (!rs) {
				printf("Error: Resource '%s' not found\n", curr);
				++err;
			}
			
			curr = strtok_r(NULL, " ", &save);
			cnt = 0;
			if (!curr) {
				printf("Error: No node ID specified\n");
				++err;
			} else {
				cnt = atoi(curr);
				x = 0;
				if (cnt <= 0) {
					printf("Error: Node '%s' invalid\n",
					       curr);
					++err;
				} else {
					for (x = 0; x < nodes_online_count; x++) {
						if (nodes_online[x] == cnt) {
							x = -1;
							break;
						}
					}
				}
				
				if (x != -1) {
					printf("Error: Node '%s' not online\n",
					       curr);
					++err;
				}
			}
			
			if (err)
				continue;
			
			curr = strtok_r(NULL, " ", &save);
			if (curr) {
				if (strcmp(curr, "test")) 
					printf("Error: start ... %s\n", curr);
			
				while ((op = ops)) {
					list_remove(&ops, op);
					free(op);
				}
				/*
int
dep_check_operation(char *res, int operation, int target, 
		    dep_t **deps, dep_rs_t *_states,
		    int slen, int *nodes, int nlen, dep_op_t **oplist)
		    */
				if (dep_check_operation(rs->rs_status.rs_name,
						RG_START,
						cnt,
						&depends,
						resource_states,
						resource_state_count,
						nodes_online,
						nodes_online_count,
						&ops) < 0) {
					printf("No, thanks.\n");
				} else
					show_calc_result(0, &ops, 0);
				continue;
			} 
			
			
			rs->rs_status.rs_owner = cnt;
			rs->rs_status.rs_state = RG_STATE_STARTED;
			printf("%s is started on %d\n", rs->rs_status.rs_name,
				cnt);
			
		} else if (!strcmp(curr, "domains")) {
			print_domains(&domains);
			
		} else if (!strcmp(curr, "calc")) {
			
			while ((op = ops)) {
				list_remove(&ops, op);
				free(op);
			}
			
			err = dep_calc_trans(&depends, resource_states,
					     resource_state_count,
					     nodes_online, nodes_online_count,
					     &ops, &x);
			show_calc_result(err, &ops, x);
			
		} else if (!strcmp(curr, "apply")) {
		
			curr = strtok_r(NULL, " ", &save);
			dep_check(&depends, resource_states,
				  resource_state_count, nodes_online,
				  nodes_online_count);
			if (!curr) {
				dep_apply_trans(&depends, resource_states,
						resource_state_count,
						&ops);
			} else {
				vis_dep_apply_trans(&ops, curr);
			}
			
			while ((op = ops)) {
				list_remove(&ops, op);
				free(op);
			}
		} else if (!strcmp(curr, "state")) {
			
			x = dep_check(&depends, resource_states,
				      resource_state_count,
				      nodes_online, nodes_online_count);
			
			if (x < 0) {
				printf("Cluster state is invalid (%d errors)\n",
					-x);
				dep_print_errors(&depends,
						 resource_states,
						 resource_state_count);
			} else if (x > 0) {
				printf("Cluster state is valid, "
				       "but not ideal (%d stopped)\n",x);
			} else {
				printf("Cluster state is ideal\n");
			}	
			
			curr = strtok_r(NULL, " ", &save);
			if (!curr) {
				dep_cluster_state(stdout, &depends,
						resource_states,
						resource_state_count, nodes_online,
						nodes_online_count);
			} else {
				visualize_state(curr);
			}
			
			dep_reset(&depends, resource_states,
					resource_state_count);
			
		} else if (!strcmp(curr, "reslist")) {
			list_do(&resources, res) {
				print_resource(res);
			} while (!list_done(&resources, res));
			
		} else if (!strcmp(curr, "nodes")) {
			
			printf("Nodes = [ ");
			for (x = 0; x < nodes_all_count; x++) {
				printf("%d ", nodes_all[x]);
			}
			printf("]\n");
		} else if (!strcmp(curr, "stop") || !strcmp(curr, "disable")) {
			
			tmp = curr;
			
			curr = strtok_r(NULL, " ", &save);
			
			if (!curr) {
				printf("usage: %s <resource>\n", tmp);
				continue;
			}
		#if 0	
			do {
				if (!strchr(curr,':')) {
					snprintf(name, sizeof(name), "service:%s",
							curr);
					curr = name;
				}
				rs = get_rs_byname(curr);
				if (!rs) {
					printf("Error: Resource '%s' not found\n",curr);
					break;
				}
			
				rs->rs_status.rs_owner = 0;
				if (!strcmp(cmd, "stop")) {
					rs->rs_status.rs_state = RG_STATE_STOPPED;
					printf("%s is stopped\n",
						rs->rs_status.rs_name);
				} else {
					rs->rs_status.rs_state = RG_STATE_DISABLED;
					printf("%s is disabled\n",
						rs->rs_status.rs_name);
				}
				curr = strtok_r(NULL, " ", &save);
			} while (curr);
			
			curr = strtok_r(NULL, " ", &save);
			
			if (!curr) {
				printf("usage: start <resource> <nodeid>"
					" [test]\n");
				continue;
			}
		#endif
			
			if (!strchr(curr,':')) {
				snprintf(name, sizeof(name), "service:%s",
						curr);
				curr = name;
			}
			
			rs = get_rs_byname(curr);
			if (!rs) {
				printf("Error: Resource '%s' not found\n", curr);
				++err;
			}
			
			if (err)
				continue;
			
			curr = strtok_r(NULL, " ", &save);
			if (curr) {
				if (strcmp(curr, "test")) 
					printf("Error: stop ... %s\n", curr);
			
				while ((op = ops)) {
					list_remove(&ops, op);
					free(op);
				}
				
				if(dep_check_operation(rs->rs_status.rs_name,
						!strcmp(tmp,"stop")?RG_STOP:RG_DISABLE,
						-1,
						&depends,
						resource_states,
						resource_state_count,
						nodes_online,
						nodes_online_count,
						&ops) < 0) {
					printf("No.\n");
				} else 
				show_calc_result(0, &ops, 0);
				continue;
			} 
			
			rs->rs_status.rs_owner = 0;
			if (!strcmp(cmd, "stop")) {
				rs->rs_status.rs_state = RG_STATE_STOPPED;
				printf("%s is stopped\n",
					rs->rs_status.rs_name);
			} else {
				rs->rs_status.rs_state = RG_STATE_DISABLED;
				printf("%s is disabled\n",
					rs->rs_status.rs_name);
			}
			
			
		} else if (!strcmp(curr, "res")) {
			
			curr = strtok_r(NULL, " ", &save);
			
			if (curr && !strchr(curr,':')) {
				snprintf(name, sizeof(name), "service:%s",
						curr);
				curr = name;
			}
			err = show_resources(curr);
			if (err) {
				printf("Error: Invalid resource '%s'", curr);
			}
			
		} else if (!strcmp(curr, "dep")) {
			
			curr = strtok_r(NULL, " ", &save);
			
			if (!curr) {
				print_depends(stdout, &depends);
				continue;
			}
			
			if (!strcmp(curr, "dot")) {
				print_depends_dot(stdout, &depends);
				continue;
			} else if (!strcmp(curr, "copy")) {
				printf("Copying tree...");
				depcpy = NULL;
				dep_tree_dup(&depcpy, &depends);
				printf("Done\n");
				print_depends(stdout, &depcpy);
				deconstruct_depends(&depcpy);
				depcpy = NULL;
			} else {
				printf("Error: Invalid command 'dep %s'\n",
					curr);
			}
				
		} else if (!strcmp(curr, "check")) {
			dep_reset(&depends, resource_states,
				  resource_state_count);
			
			x = dep_check(&depends, resource_states,
				      resource_state_count,
				      nodes_online, nodes_online_count);
			
			if (x < 0) {
				printf("Cluster state is invalid (%d errors)\n",
					-x);
				dep_print_errors(&depends,
						 resource_states,
						 resource_state_count);
				dep_reset(&depends, resource_states,
					  resource_state_count);
			} else if (x > 0) {
				printf("Cluster state is valid, "
				       "but not ideal (%d stopped)\n",x);
			} else {
				printf("Cluster state is ideal\n");
			}
		} else if (!strcmp(curr, "?") || !strcmp(curr,"help")) {
			print_help();
		} else if (!strcmp(curr, "quit") || !strcmp(curr,"exit")) {
			done = 1;
		} else if (!strcmp(curr, "mem")) {
			
			tmp = curr;
			curr = strtok_r(NULL, " ", &save);
			if (!curr) {
				malloc_stats();
				continue;
			}
			
			if (!strcmp(curr, "table")) {
				malloc_dump_table();
			} else {
				printf("Unknown command '%s %s'\n", tmp , curr);
			}
		} else {
			printf("Unknown command '%s'\n", curr);
		}
	}
	if (cmd)
		free(cmd);
}


int *
load_node_ids(int ccsfd, int *count)
{
	int ncount, x;
	int *nodes;
	char *val;
	char xpath[256];
	
	for (ncount = 1; ; ncount++) {
		snprintf(xpath, sizeof(xpath),
			 "/cluster/clusternodes/clusternode[%d]/@nodeid",
			 ncount);
		
		if (ccs_get(ccsfd, xpath, &val)!=0) {
			--ncount;
			break;
		}
	}
	
	if (!ncount)
		return NULL;
	
	nodes = malloc(sizeof(int) * ncount);
	if (!nodes) {
		fprintf(stderr, "out of memory?\n");
		return NULL;
	}
	
	for (x = 1; x <= ncount; x++) {
		snprintf(xpath, sizeof(xpath),
			 "/cluster/clusternodes/clusternode[%d]/@nodeid", x);
		
		if (ccs_get(ccsfd, xpath, &val)!=0) {
			fprintf(stderr,
				"Code path error: # of nodes changed\n");
			free(nodes);
			return NULL;
		}
		
		nodes[x-1] = atoi(val);
		free(val);
	}
	
	*count = ncount;
	return nodes;
}


int
main(int argc, char **argv)
{
	int ccsfd, ret = 0;
	char *agentpath;
	char *config;
	
	if (argc < 3) {
		printf("usage: %s <agentpath> <config>\n", argv[0]);
		return -1;
	}
	
	agentpath = argv[1];
	config = argv[2];

	conf_setconfig(config);
	
       	ccsfd = ccs_lock();
	if (ccsfd < 0) {
		printf("Error parsing %s\n", argv[1]);
		return -1;
	}

	load_resource_rules(agentpath, &rules);
	construct_domains(ccsfd, &domains);
	load_resources(ccsfd, &resources, &rules);
	build_resource_tree(ccsfd, &restree, &rules, &resources);
	construct_depends(ccsfd, &depends);
	
	if (argc >= 4) {
		if (!strcmp(argv[3], "dot")) {
			print_depends_dot(stdout, &depends);
		} else {
			fprintf(stderr,"Invalid command: %s\n", argv[3]);
			ret = 1;
		}
		goto out;
	}
	
	nodes_all = load_node_ids(ccsfd, &nodes_all_count);
		
	ccs_unlock(ccsfd);
	
	printf("Nodes = [ ");
	for (ret = 0; ret < nodes_all_count; ret++) {
		printf("%d ", nodes_all[ret]);
	}
	printf("]\n");
	
	if ((resource_states = dep_rstate_alloc(&restree, &domains,
			nodes_all,
			nodes_all_count,
			&resource_state_count)) == NULL) {
		printf("oops\n");
		return -1;
	}
	
	/* Ok!  We have it all! */
	dtest_shell();
			
out:
	
	deconstruct_depends(&depends);
	destroy_resource_tree(&restree);
	destroy_resources(&resources);
	deconstruct_domains(&domains);
	destroy_resource_rules(&rules);
	
	malloc_dump_table();

	return ret;
}


