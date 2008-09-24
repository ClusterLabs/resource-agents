/** @file
 * CCS dependency parsing, based on failover domain parsing
 * Transition generation based on simple brute-force / best-first
 * tree search.
 * 
 * Allows specification of two types of rules concerning service states:
 * 
 * - requirement: A -> B means B must be running either for A to start or
 *                at all times (if B stops, A will be stopped)
 * - colocation: A -> B means that A must be run on the same node as B
 *               (note that colocation doesn't mean that B must be running;
 *               to do both, you must enable a requirement).
 *               A X> B means that A must be run on a different node from B.
 *
 * Known bugs:  If more than 1 error is introduced in the graph,
 *              the 'find-ideal-state' function sometimes will refuse to run
 */
#include <string.h>
#include <list.h>
#include <time.h>
#include <restart_counter.h>
#include <logging.h>
#include <resgroup.h>
#include <reslist.h>
#include <ccs.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <members.h>
#include <reslist.h>
#include <depends.h>
#include <ctype.h>

struct _try {
	dep_op_t *ops;
	int score;
	int depth;
};

void deconstruct_dep(dep_t *dep);
int dep_graph_validate(dep_t **deps);
fod_t *fod_find_domain(fod_t **domains, char *name);

//#define DEBUG

#ifdef NO_CCS
#define ccs_get(fd, query, ret) conf_get(query, ret)
#endif

/*
   <dependencies>
     <dependency name="service:bar">
       <target name="service:foo" colocate="always|never|unspec"
               require="start|always|unspec"/>
     </dependency>
   </dependencies>
 */

static dep_node_t *
get_dep_node(int ccsfd, char *base, int idx, dep_t *dep, int *_done)
{
	dep_node_t *dn;
	char xpath[256];
	char *ret;
	int c;

	*_done = 0;
	snprintf(xpath, sizeof(xpath), "%s/target[%d]/@name",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0) {
		*_done = 1;
		return NULL;
	}

	list_for(&dep->d_nodes, dn, c) {
		if (strcasecmp(ret, dn->dn_name))
			continue;

		printf("#XX: Target %s defined multiple times in "
		       "dependency block %s\n", ret, dep->d_name);
		free(ret);
		return NULL;
	}

	dn = malloc(sizeof(*dn));
	if (!dn)
		return NULL;
	memset(dn, 0, sizeof(*dn));

	/* Already malloc'd; simply store */
	dn->dn_name = ret;
	dn->dn_ptr = NULL;
	dn->dn_req = DEP_REQ_UNSPEC;
	dn->dn_colo = DEP_COLO_UNSPEC;

	snprintf(xpath, sizeof(xpath), "%s/target[%d]/@require",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0 && ret) {
		
		if (!strcasecmp(ret, "start")) {
			dn->dn_req = DEP_REQ_START;
		} else if (!strcasecmp(ret, "always")) {
			dn->dn_req = DEP_REQ_ALWAYS;
		} else if (!strcasecmp(ret, "unspec")) {
			dn->dn_req = DEP_REQ_UNSPEC;
		} else {
			dn->dn_req = atoi(ret);
		}
		
		free(ret);
	}
	
	snprintf(xpath, sizeof(xpath), "%s/target[%d]/@colocate",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0 && ret) {
		
		if (!strcasecmp(ret, "never")) {
			dn->dn_colo = DEP_COLO_NEVER;
		} else if (!strcasecmp(ret, "always")) {
			dn->dn_colo = DEP_COLO_ALWAYS;
		} else if (!strcasecmp(ret, "unspec")) {
			dn->dn_colo = DEP_COLO_UNSPEC;
		} else {
			dn->dn_colo = atoi(ret);
		}
		
		free(ret);
	}
	
	if (dn->dn_req == DEP_REQ_UNSPEC &&
	    dn->dn_colo == DEP_COLO_UNSPEC) {
		printf("Dropping dependency target %s: no rule in use\n",
		       dn->dn_name);
		free(dn->dn_name);
		free(dn);
		return NULL;
	}

	return dn;
}


static dep_t *
get_dep(int ccsfd, char *base, int idx, dep_t **deps, int *_done)
{
	dep_t *dep;
	dep_node_t *dn;
	char xpath[256];
	char *ret;
	int x = 1, c;
	int done;

	*_done = 0;
	snprintf(xpath, sizeof(xpath), "%s/dependency[%d]/@name",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0) {
		*_done = 1;
		return NULL;
	}

	list_for(deps, dep, c) {
		if (strcasecmp(dep->d_name, ret))
			continue;
		    
		printf("#XX: Dependency block %s defined multiple "
				"times\n", ret);
		free(ret);
		return NULL;
	}

	dep = malloc(sizeof(*dep));
	if (!dep)
		return NULL;
	memset(dep, 0, sizeof(*dep));
	dep->d_name = ret;
	dep->d_nodes = NULL;
	dep->d_flags = 0;
	dep->d_hits = 0;
	dep->d_deps = 0;

	snprintf(xpath, sizeof(xpath), "%s/dependency[%d]",
		 base, idx);

	do {
		dn = get_dep_node(ccsfd, xpath, x++, dep, &done);
		if (dn) {
			dep->d_deps++;
			list_insert(&dep->d_nodes, dn);
		}
	} while (!done);
	
	if (!dep->d_nodes) {
		printf("Dropping dependency block %s: No targets\n",
		       dep->d_name);
		free(dep->d_name);
		free(dep);
		return NULL;
	}

	return dep;
}


/**
 * Constructs links in the dependency tree
 */
static void
dep_construct_tree(dep_t **deps)
{
	dep_t *curr, *curr2, *dep;
	dep_node_t *dn;
	int done, found, a, b, c;
	
	do {
		done = 1;
		list_for(deps, curr, a) {
			list_for(&curr->d_nodes, dn, b) {
				found = 0;
				list_for(deps, curr2, c) {
					if (!strcasecmp(curr2->d_name,
							dn->dn_name)) {
						if (!dn->dn_ptr) {
							dn->dn_ptr = curr2;
							dn->dn_ptr->d_hits++;
						}
						found = 1;
					}
				}
				
				if (!found) {
					done=0;
					
					dep = malloc(sizeof(*dep));
					if (!dep)
						return;
					memset(dep, 0, sizeof(*dep));
					dep->d_name = strdup(dn->dn_name);
					if (!dep->d_name)
						return;
					dep->d_nodes = NULL;
					dep->d_flags = DEP_FLAG_IMPLIED;
					dep->d_hits = 1;
					dep->d_deps = 0;
					dn->dn_ptr = dep;
					
					list_insert(deps, dep);
					
					break;
				}
			}
			
			if (!done)
				break;
		}
	} while (!done);
}
	

/**
 * similar API to failover domain
 */
int
construct_depends(int ccsfd, dep_t **deps)
{
	char xpath[256];
	int x = 1, done, c;
	dep_t *dep, *curr;

	snprintf(xpath, sizeof(xpath),
		 RESOURCE_TREE_ROOT "/dependencies");

	do {
		dep = get_dep(ccsfd, xpath, x++, deps, &done);
		if (dep) {
			list_insert(deps, dep);
		}
	} while (!done);
	
	/* Insert terminal dependency nodes & construct tree */
	/* XXX optimize */
	dep_construct_tree(deps);

	dep_graph_validate(deps);
	
	do {
		done = 1;
		list_for(deps, curr, c) {
			if (curr->d_flags &
			    (DEP_FLAG_CYCLIC | DEP_FLAG_IMPOSSIBLE)) {
				printf("Removing dependency block %s: "
				       "Invalid\n", curr->d_name);
				done = 0;
				
				list_remove(deps, curr);
				deconstruct_dep(curr);
				break;
			}
		}
	} while (!done);
	
	return 0;
}


void
deconstruct_dep(dep_t *dep)
{
	dep_node_t *node;
	
	while ((node = dep->d_nodes)) {
		list_remove(&dep->d_nodes, node);
		if (node->dn_name)
			free(node->dn_name);
		free(node);
	}

	if (dep->d_name)
		free(dep->d_name);
	free(dep);
}


void
deconstruct_depends(dep_t **deps)
{
	dep_t *dep = NULL;
	
	while ((dep = *deps)) {
		list_remove(deps, dep);
		deconstruct_dep(dep);
	}
}


static inline dep_rs_t *
_find_state(char *name, dep_rs_t *sl, int slen)
{
	int x;

	for (x = 0; x < slen; x++)
		if (!strcasecmp(sl[x].rs_status.rs_name, name))
			return &sl[x];
	
	return NULL;
}


static int
rs_running(char *name, dep_rs_t *sl, int slen)
{
	dep_rs_t *rs = NULL;
	
	if (name) {
		rs = _find_state(name, sl, slen);
	} else if (slen == 1) {
		rs = sl;
	}
	
	if (rs) {
		if (rs->rs_flags & RS_BROKEN)
			return -1;
		return (rs->rs_status.rs_state == RG_STATE_STARTING ||
			rs->rs_status.rs_state == RG_STATE_STARTED ||
			rs->rs_status.rs_state == RG_STATE_STOPPING );
	}
	
	return 0;
}


int
dep_tree_dup(dep_t **dest, dep_t **src)
{
	dep_t *dep, *dep_cpy;
	dep_node_t *dn, *dn_cpy;
	int a, b;
	
	list_for(src, dep, a) {
		while ((dep_cpy = malloc(sizeof(dep_t))) == NULL)
			usleep(10000);
		memset(dep_cpy, 0, sizeof(dep_t));
		while ((dep_cpy->d_name = strdup(dep->d_name)) == NULL)
			usleep(10000);
		
		dep_cpy->d_flags &= ~(DEP_FLAG_ALWAYS|DEP_FLAG_NEVER);
		
		list_for(&dep->d_nodes, dn, b) {
			while ((dn_cpy = malloc(sizeof(dep_node_t))) == NULL)
			 	usleep(10000);
			memset(dn_cpy, 0, sizeof(dep_node_t));
			while ((dn_cpy->dn_name = strdup(dn->dn_name)) == NULL)
				usleep(10000);
			dn_cpy->dn_colo = dn->dn_colo;
			dn_cpy->dn_req = dn->dn_req;
			
			/* Flags are all errors right now... */
			/* dn_cpy->dn_flags = dn->dn_flags; */
			list_insert(&dep_cpy->d_nodes, dn_cpy);
		}
		
		list_insert(dest, dep_cpy);
	}
	
	/* Construct internal tree */
	dep_construct_tree(dest);
	
	return 0;
}


static void
dep_tree_print(FILE *fp, dep_t *start, dep_t *dep, int level)
{
	dep_node_t *dn = NULL;
	int x, c;
	
	if (!dep)
		return;
	
	if ((dep == start) && level) {
		fprintf(fp, "[cycles to %s]\n", start->d_name);
		return;
	}
	
	fprintf(fp, "%s\n",dep->d_name);
	
	if (dep->d_nodes) {
		list_for(&dep->d_nodes, dn, c) {
			for (x = 0; x < level; x++)
				fprintf(fp, "         ");
			fprintf(fp, "|\n");
			for (x = 0; x < level; x++)
				fprintf(fp, "         ");
			fprintf(fp, "+-");
			switch(dn->dn_colo) {
			case DEP_COLO_UNSPEC:
				fprintf(fp, "--");
				break;
			case DEP_COLO_ALWAYS:
				fprintf(fp, "Ca");
				break;
			case DEP_COLO_NEVER:
				fprintf(fp, "Cn");
				break;
			}
			switch(dn->dn_req) {
			case DEP_REQ_UNSPEC:
				fprintf(fp, "--");
				break;
			case DEP_REQ_START:
				fprintf(fp, "Rs");
				break;
			case DEP_REQ_ALWAYS:
				fprintf(fp, "Ra");
				break;
			}
			fprintf(fp, "-> ");
			
			if (dn->dn_traversed) {
				fprintf(fp, "[cycles to %s]\n",
					dn->dn_ptr->d_name);
			} else {
				dn->dn_traversed = 1;
				dep_tree_print(fp, start, dn->dn_ptr, level+1);
				dn->dn_traversed = 0;
			}
			      
		}
	}
}


static void
_dot_clean_string(char *str)
{
	int x;
	
	/*
	if (strncmp(str, "service:", 8) == 0)
		memmove(str, str+8, strlen(str)-8+1);
	*/
	
	if (!isalpha(str[0]))
		str[0] = '_';
	
	for (x = 0; x < strlen(str); x++) {
		if (isalnum(str[x]))
			continue;
		if (str[x] == '_' || str[x] == '-')
			continue;
		str[x] = '_';
	}
}


void
print_dep_tree_dot(FILE *fp, dep_t *start, dep_t *dep, int level)
{
	dep_node_t *dn = NULL;
	char src[64], dest[64];
	int c;
	
	if (!dep)
		return;
	
	if ((dep == start) && level)
		return;
	
	if (dep->d_nodes) {
		list_for(&dep->d_nodes, dn, c) {
			if (dn->dn_traversed)
				continue;
			
			strncpy(src, dep->d_name, sizeof(src));
			strncpy(dest, dn->dn_name, sizeof(dest));
			_dot_clean_string(src);
			_dot_clean_string(dest);
		
			dn->dn_traversed = 1;
			switch(dn->dn_colo) {
			case DEP_COLO_UNSPEC:
				break;
			case DEP_COLO_ALWAYS:
				fprintf(fp, "\tedge [color=black, "
					    "arrowhead=diamond, label=\"Ca\"");
				if (dn->dn_flags & DN_BROKEN_COLO)
					fprintf(fp, ", style=dashed");
				else
					fprintf(fp, ", style=solid");
				fprintf(fp, "];\n");
				fprintf(fp, "\t%s -> %s;\n", src, dest);
				break;
			case DEP_COLO_NEVER:
				fprintf(fp, "\tedge [color=red, "
					    "arrowhead=diamond, label=\"Cn\"");
				if (dn->dn_flags & DN_BROKEN_NONCOLO)
					fprintf(fp, ", style=dashed");
				else
					fprintf(fp, ", style=solid");
				fprintf(fp, "];\n");
				fprintf(fp, "\t%s -> %s;\n", src, dest);
				break;
			}
			switch(dn->dn_req) {
			case DEP_REQ_UNSPEC:
				break;
			case DEP_REQ_START:
				fprintf(fp, "\tedge [color=green, "
					    "arrowhead=vee, label=\"Rs\"");
				if (dn->dn_flags & DN_BROKEN_REQ)
					fprintf(fp, ", style=dashed");
				else
					fprintf(fp, ", style=solid");
				fprintf(fp, "];\n");
				fprintf(fp, "\t%s -> %s;\n", src, dest);
				break;
			case DEP_REQ_ALWAYS:
				fprintf(fp, "\tedge [color=black, "
					    "arrowhead=vee, label=\"Ra\"");
				if (dn->dn_flags & DN_BROKEN_REQ)
					fprintf(fp, ", style=dashed");
				else
					fprintf(fp, ", style=solid");
				fprintf(fp, "];\n");
				fprintf(fp, "\t%s -> %s;\n", src, dest);
				break;
			}
			
			print_dep_tree_dot(fp, start, dn->dn_ptr, level+1);
			      
		}
	}
}


/**
 * Check for cyclic 'require' dependency.
 */
static int
find_cyclic_req(dep_t *dep, dep_t *what)
{
	dep_node_t *dn;
	int ret, c;
	
	if (dep == what)
		return 2;
	
	if (!dep->d_nodes)
		return 0;
	
	/* initialize */
	if (what == NULL)
		what = dep;
	
	list_for(&dep->d_nodes, dn, c) {
		
		if (!dn->dn_traversed &&
		    dn->dn_req != DEP_REQ_UNSPEC) {
			dn->dn_traversed = 1;
			ret = find_cyclic_req(dn->dn_ptr, what);
			dn->dn_traversed = 0;
			
			if (ret == 2) {
				printf("Error: Cyclic Requirement: \n");
				printf("   %s ... -> %s -> %s\n",
					what->d_name,
					dep->d_name,
					dn->dn_ptr->d_name);
			}
			if (ret)
				return 1;
		} else if (dn->dn_traversed)
			return 1;
	}
	
	return 0;
}


/**
 * Tag all nodes which have colocation requirements
 */
static int
tag_colo(dep_t *dep, dep_t *what, dep_colo_t colo)
{
	dep_node_t *dn;
	int ret = 0, c;
	
	/* How did we get here? */
	switch(colo) {
	case DEP_COLO_ALWAYS:
		if (dep->d_flags & DEP_FLAG_NEVER)
			ret = 1;
		dep->d_flags |= DEP_FLAG_ALWAYS;
		break;
	case DEP_COLO_NEVER:
		if (dep->d_flags & DEP_FLAG_ALWAYS)
			ret = 1;
		dep->d_flags |= DEP_FLAG_NEVER;
		break;
	default:
		break;
	}
	
	if (dep == what) 
		goto out;
	
	if (!dep->d_nodes)
		goto out;
	
	/* Start if this is the first call*/
	if (what == NULL)
		what = dep;
	
	list_for(&dep->d_nodes, dn, c) {
		
		if (!dn->dn_traversed) {
			dn->dn_traversed = 1;
			ret += tag_colo(dn->dn_ptr, what, dn->dn_colo);
			dn->dn_traversed = 0;
		}
		
	}
	
out:
	if (ret)
		dep->d_flags |= DEP_FLAG_IMPOSSIBLE;
	return ret;
}


int
dep_print_dep_errors(dep_t **deps)
{
	dep_t *dep;
	dep_node_t *dn;
	int a, b;
	
	list_for(deps, dep, a) {
		list_for(&dep->d_nodes, dn, b) {
			if (dn->dn_flags & DN_BROKEN_COLO) {
				printf("Resource %s must colocate with "
				       "resource %s\n", dep->d_name,
				       dn->dn_name);
			}
			if (dn->dn_flags & DN_BROKEN_NONCOLO) {
				printf("Resource %s must never colocate with "
					"resource %s\n", dep->d_name,
					dn->dn_name);
			}
			if (dn->dn_flags & DN_BROKEN_REQ) {
				printf("Resource %s depends on "
					"resource %s\n", dep->d_name,
					dn->dn_name);
			}
		}
	}
	
	return 0;
}


int
dep_print_state_errors(dep_rs_t *rs, int slen)
{
	int x;
	
	for (x = 0; x < slen; x++) {
		if (rs[x].rs_flags & RS_ILLEGAL_NODE)
			printf("Resource %s on illegal node %d\n",
				rs[x].rs_status.rs_name,
				rs[x].rs_status.rs_owner);
		if (rs[x].rs_flags & RS_DEAD_NODE)
			printf("Resource %s on dead/offline node %d\n",
				rs[x].rs_status.rs_name,
				rs[x].rs_status.rs_owner);
	}
	return 0;
}


/**
 * clear our loop detection
 */
int
dep_clear_dep_errors(dep_t **deps)
{
	dep_t *dep;
	dep_node_t *dn;
	int a, b;
	
	list_for(deps, dep, a) {
		dep->d_flags &= ~(DEP_FLAG_ALWAYS | DEP_FLAG_NEVER);
		
		list_for(&dep->d_nodes, dn, b) {
			dn->dn_traversed = 0;
			dn->dn_flags = 0;
		}
	}
	
	return 0;
}


int
dep_clear_traversed(dep_t **deps)
{
	dep_t *dep;
	dep_node_t *dn;
	int a, b;
	
	list_for(deps, dep, a) {
		list_for(&dep->d_nodes, dn, b) {
			dn->dn_traversed = 0;
		}
	}
	
	return 0;
}


int
dep_clear_state_errors(dep_rs_t *rs, int slen)
{
	int x;
	for (x = 0; x < slen; x++)
		rs[x].rs_flags &= ~(RS_ILLEGAL_NODE|RS_DEAD_NODE);
	return 0;
}


void
dep_reset(dep_t **deps, dep_rs_t *rs, int slen)
{
	dep_clear_dep_errors(deps);
	dep_clear_state_errors(rs, slen);
}


void
dep_print_errors(dep_t **deps, dep_rs_t *rs, int slen)
{
	dep_print_dep_errors(deps);
	dep_print_state_errors(rs, slen);
}


int
dep_graph_validate(dep_t **deps)
{
	dep_t *dep;
	dep_flag_t f;
	int a;
	
	list_for(deps, dep, a) {
		
		if (find_cyclic_req(dep, NULL)) {
			printf("Cyclic requirement dependency in block %s\n",
					dep->d_name);
			dep->d_flags |= DEP_FLAG_CYCLIC;
		}
		
		tag_colo(dep, NULL, 0);
	}
	
	f = (DEP_FLAG_ALWAYS | DEP_FLAG_NEVER);
	list_for(deps, dep, a) {
		if (((dep->d_flags & f) == f) ||
		    (dep->d_flags & DEP_FLAG_IMPOSSIBLE)) {
			printf("Graph %s: colocation conflict\n", dep->d_name);
			dep->d_flags |= DEP_FLAG_IMPOSSIBLE;
		}
	}
	
	return 0;
}


void
print_depends(FILE *fp, dep_t **deps)
{
	dep_t *dep;
	int a;

	list_for(deps, dep, a) {
		if (dep->d_nodes && !(dep->d_flags & DEP_FLAG_IMPLIED) &&
		    dep->d_hits == 0) {
			dep_tree_print(fp, dep, dep, 0); 
			fprintf(fp,"\n");
		}
	}
}


static void
_print_depends_dot(FILE *fp, dep_t **deps)
{
	dep_t *dep;
	int a;

	list_for(deps, dep, a) {
		print_dep_tree_dot(fp, dep, dep, 0); 
	}
	
	dep_clear_traversed(deps);
}


void
print_depends_dot(FILE *fp, dep_t **deps)
{
	fprintf(fp, "digraph G {\n");
	_print_depends_dot(fp, deps);
	fprintf(fp, "}\n");
}


/**
 * check to ensure a resource is on an allowed node.
 * Note - makes no assumption about the actual resource state; it could be
 * in the stopped state.
 */
int
dep_check_res_loc(dep_rs_t *state)
{
	int x;
	
	if (!rs_running(NULL, state, 1)) {
		/* Not running = location is perfectly fine ... sort of */
		return 0;
	}
		
	if (!state->rs_allowed || state->rs_allowed_len <= 0)
		return 0;
	
	for (x = 0; x < state->rs_allowed_len; x++) {
		if (state->rs_status.rs_owner == state->rs_allowed[x])
			return 0;
	}
	
	/* Didn't match a legal node -> fail */
	state->rs_flags |= RS_ILLEGAL_NODE;
	return 1;
}


static dep_t *
find_dep(dep_t **deps, char *name)
{
	dep_t *dep = NULL;
	int a;
	
	if (!deps)
		return NULL;
	
	list_for(deps, dep, a) {
		if (!strcasecmp(dep->d_name, name))
			return dep;
	}
	
	return NULL;
}


/**
 * traverses the requirements tree looking for 'name'
 * returns 1 if found, 0 if not
 */
static int
_tree_walk_req(dep_t *dep, dep_rs_t *state, dep_rs_t *sl, int slen)
{
	dep_node_t *dn;
	int errors = 0, ret, a;
	
	if (!dep  || !dep->d_nodes)
		return 0;
	
	list_for(&dep->d_nodes, dn, a) {
		
		/* If we have a specified requirement... */
		ret = 0;
		if (!dn->dn_traversed &&
		    dn->dn_req != DEP_REQ_UNSPEC) {
			
			/* see if our child is running.  If not, we're done */
			ret = rs_running(dn->dn_name, sl, slen);
			
			/* XXX check for req-start vs. req-always */
			if (ret <= 0) {
				dn->dn_flags |= DN_BROKEN_REQ;
				state->rs_flags |= RS_BROKEN;
				ret = 1;
			} else {
				dn->dn_traversed = 1;
				ret = _tree_walk_req(dn->dn_ptr,
						_find_state(dn->dn_name, sl, slen), sl, slen);
				dn->dn_traversed = 0;
			}
			
			if (ret) {
				errors += ret;
				dn->dn_flags |= DN_BROKEN_REQ;
				state->rs_flags |= RS_BROKEN;
			}
		}
	}
	
	return errors;
}

/*
 * Returns nonzero if something requires this resource, 0 if not
 */
int
dep_check_requires(dep_t **deps, dep_rs_t *state, dep_rs_t *states, int slen)
{
	dep_t *dep;
	int errors = 0, a;
	
	/* Check to see if anything depends on this (not-running) resource */
	list_for(deps, dep, a) {
		errors += _tree_walk_req(dep, NULL, /*state->rs_status.rs_name,*/
					 states, slen);
	}
	
	/* Flag requirements on any broken-dep resources as broken, too... */
	dep_clear_traversed(deps);
	
	return errors;
}


/**
 * Check to see if this resource is causing a conflict.  It can cause a
 * conflict if it is stopped but other resources depend on it.
 * If it is running, it can not cause a requirement conflict, but it might
 * cause a colocation conflict.
 */
static int
_dep_check_requires(dep_t **deps, dep_rs_t *state, dep_rs_t *states, int slen)
{
	/* if not running, it has no dependencies */
	if (!rs_running(NULL, state, 1))
		return 0;
	
	return _tree_walk_req(find_dep(deps, state->rs_status.rs_name),
			state, states, slen);
	
	//return dep_check_requires(deps, state, states, slen);
}

/**
 * Checks the tree to see if there's a colocation requiremet on the given
 * resource.  Returns 1 if found, 0 if not.
 * returns 1 if found, 0 if not  We don't need to recurse on this one.
 */
static int
_tree_walk_colo(dep_t *dep, dep_t *start, char *name, dep_rs_t *sl, int slen)
{
	dep_node_t *dn;
	dep_rs_t *state;
	int errors = 0, a;
	
	if (dep == start) 
		return 0;
	
	if (!dep->d_nodes)
		return 0;
	
	/* Start if this is the first call*/
	if (start == NULL)
		start = dep;
	
	state = _find_state(dep->d_name, sl, slen);
	if (!state)
		return 0;
	
	list_for(&dep->d_nodes, dn, a) {
		
		/* If we have a specified requirement... */
		if (!dn->dn_traversed &&
		    dn->dn_colo == DEP_COLO_ALWAYS) {
			
			if (!strcasecmp(dn->dn_name, name) &&
			    !(dn->dn_flags & DN_BROKEN_COLO)) {
				/* Broken colocation req */
				state->rs_flags |= RS_BROKEN;
				dn->dn_flags |= DN_BROKEN_COLO;
				++errors;
			}
			dn->dn_traversed = 1;
			errors += _tree_walk_colo(dn->dn_ptr, start, name,
						  sl, slen);
			dn->dn_traversed = 0;
		}
	}
	
	return errors;
}


/**
 * Checks the immediate children of the given dependency to see if there's a
 * non-colocation requiremet on the given resource.  Returns 1 if found,
 * 0 if not.
 */
static int
_tree_walk_noncolo(dep_t *dep, dep_t *start, char *name, dep_rs_t *sl,
		   int slen)
{
	dep_node_t *dn;
	dep_rs_t *state;
	int errors = 0, a;
	
	if (dep == start) 
		return 0;
	
	if (!dep->d_nodes)
		return 0;
	
	/* Start if this is the first call*/
	if (start == NULL)
		start = dep;
	
	state = _find_state(dep->d_name, sl, slen);
	if (!state)
		return 0;
	
	list_for(&dep->d_nodes, dn, a) {
		
		/* If we have a specified requirement... */
		if (dn->dn_colo == DEP_COLO_NEVER) {
			
			if (!strcasecmp(dn->dn_name, name) &&
			    !(dn->dn_flags & DN_BROKEN_COLO)) {
				/* Broken noncolo req */
				state->rs_flags |= RS_BROKEN;
				dn->dn_flags |= DN_BROKEN_NONCOLO;
				++errors;
			}
		}
	}
	
	return errors;
}


int
dep_check_colo(dep_t **deps, dep_rs_t *states, int slen,
	       char *name, int nodeid)
{
	dep_t *dep;
	int x, errors = 0;
	
	if (!deps)
		return 0;
	dep = find_dep(deps, name);
	if (!dep)
		return 0;
	
	for (x = 0; x < slen; x++) {
		
		if (!rs_running(NULL, &states[x], 1)) {
			/*
			 * Non-running resources don't cause colo/noncolo
			 * conflicts
			 */
			continue;
		}
		
		/*
		 * Two resources on different nodes... see if the specified
		 * one is causing a colocation conflict (e.g. another one
		 * must colocate with it)
		 */
		if (states[x].rs_status.rs_owner != nodeid) {
			errors += _tree_walk_colo(dep, NULL,
					states[x].rs_status.rs_name, states,
					slen);
		}
		
		/*
		 * two resources on the same node... see if the specified one
		 * is causing a colocation conflict (e.g. another one must
		 * NOT colocate with it)
		 */
		if (states[x].rs_status.rs_owner == nodeid) {
			errors += _tree_walk_noncolo(dep, NULL,
					states[x].rs_status.rs_name,
					states, slen);
		}
	}
	
	dep_clear_traversed(deps);
	
	return errors;
}


/**
 * Check to see if this resource is causing a colocation conflict.  It can
 * not cause if it is stopped.  If it is running, it can cause a colocation
 * conflict if something must not colocate with it.
 */
static int
_dep_check_colo(dep_t **deps, dep_rs_t *states, int slen, dep_rs_t *state)
{
	if (!rs_running(NULL, state, 1)) {
		/* not running : cannot be causing a colo conflict */
		return 0;
	}
	
	return dep_check_colo(deps,
			      states,
			      slen,
			      state->rs_status.rs_name,
			      state->rs_status.rs_owner);
}


int
dep_check_online(dep_rs_t *rs, int *nodes, int nlen)
{
	int x;
	
	if (!rs_running(NULL, rs, 1)) {
		return 0;
	}
	for (x = 0; x < nlen; x++){
		if (rs->rs_status.rs_owner == nodes[x])
			return 0;
	}
	
	rs->rs_flags |= RS_DEAD_NODE;
	return 1;
}


/**
 * Validate the current state of the cluster.  This traverses the dependency
 * graph looking for conflicts, as well as obvious errors (e.g. resource
 * outside failover domain constraint, for example.
 * 
 * @return		-1 for invalid, 0 for ideal, or the # of services 
 * 			which need to be started to become ideal.
 */
int
dep_check(dep_t **deps, dep_rs_t *states, int slen, int *nodes, int nlen)
{
	int x, ret = 0, errors = 0;
	
	dep_clear_dep_errors(deps);
	dep_clear_state_errors(states, slen);
	
	for (x = 0; x < slen; x++) {
		
		if (states[x].rs_status.rs_state == RG_STATE_STOPPED)
			++ret;
			
		/*
		 * Pass 1: Check for services started on dead nodes
		 */
		errors += dep_check_online(&states[x], nodes, nlen);
	
		/*
		 * Pass 2: Check obvious legal-targets for resources
		 * (failover domain)
		 */
		errors += dep_check_res_loc(&states[x]);
	
		/*
		 * Pass 3: Check to see if this resource has all of its a 
		 * resource dependencies satisfied.
		 */
		errors += _dep_check_requires(deps, &states[x], states, slen);
		
		/*
		 * Pass 4: Check to see if this resource is causing a
		 * colocation conflict (e.g. is cohabiting with a resource
		 * which must run apart from it)
		 */
		errors += _dep_check_colo(deps, states, slen, &states[x]);
	}
	
	if (errors)
		return -errors;
	
	return ret;
}


int
dep_cluster_state_dot(FILE *fp, dep_t **deps, dep_rs_t *states, int slen,
		      int *nodes, int nlen)
{
	int x, y, z, tmp, off = 0;
	char str[64];
	
	fprintf(fp, "digraph G {\n");
	
	/* Print out node states */
	for (x = 0; x < nlen; x++) {
		fprintf(fp, "\tsubgraph cluster_%d {\n", x);
		fprintf(fp, "\t\tlabel=node%d;\n", nodes[x]);
		fprintf(fp, "\t\tstyle=filled;\n");
		fprintf(fp, "\t\tcolor=lightgrey;\n");
		fprintf(fp, "\t\tfontcolor=black;\n");
		fprintf(fp, "\t\tnode [style=filled, color=lightgrey, "
			"fontcolor=lightgrey];\n");
		
		z = 0;
		
		for (y = 0; y < slen; y++) {
			if (rs_running(NULL, &states[y], 1) &&
			    states[y].rs_status.rs_owner == nodes[x]) {
				tmp = 0;
				if (!states[y].rs_allowed_len)
					tmp = 1;
				else {
					for (z = 0; z<states[y].rs_allowed_len;
					     z++) {
						if (states[y].rs_allowed[z] == 
						    states[y].rs_status.rs_owner) {
							tmp = 1;
							break;
						}
					}
				}
				
				++z;
				
				if (!tmp) {
					fprintf(fp, "\t\tnode [style=filled, "
						    "color=\"#ff6060\", "
						    "shape=box, "
						    "fontcolor=black];\n");
				} else {
					fprintf(fp, "\t\tnode [style=filled, "
						    "color=white, shape=box, "
						    "fontcolor=black];\n");
				}
				
				strncpy(str, states[y].rs_status.rs_name,
					sizeof(str));
				_dot_clean_string(str);
				fprintf(fp, "\t\t%s;\n", str);
			}
		}
		
		if (!z) {
			snprintf(str, sizeof(str), "node%d", x);
			fprintf(fp, "\t\t%s;\n", str);
		}
		fprintf(fp, "\t}\n");
	}
	
	/* Show all 'started' on dead node resources in a red "node" */
	off = 0;
	for (x = 0; x < slen; x++) {
		
		if (!(states[x].rs_flags & RS_DEAD_NODE))
			continue;
		
		if (!off) {
			off = 1;
			fprintf(fp, "\tsubgraph cluster_%d {\n", nlen+1);
			fprintf(fp, "\t\tlabel=Dead;\n");
			fprintf(fp, "\t\tstyle=filled;\n");
			fprintf(fp, "\t\tcolor=\"#ff6060\";\n");
			fprintf(fp, "\t\tfontcolor=black;\n");
			fprintf(fp, "\t\tnode [color=white, style=filled, "
				    "shape=box, fontcolor=black];\n");
		}
			
		strncpy(str, states[x].rs_status.rs_name, sizeof(str));
		_dot_clean_string(str);
		fprintf(fp, "\t\t%s;\n", str);
	}
	
	if (off)
		fprintf(fp, "\t}\n");
	
	/* Show all 'stopped' in a 'stopped' "node" */
	fprintf(fp, "\tsubgraph cluster_%d {\n", nlen+2);
	fprintf(fp, "\t\tstyle=filled;\n");
	fprintf(fp, "\t\tcolor=\"#60ff60\";\n");
	fprintf(fp, "\t\tfontcolor=black;\n");
	fprintf(fp, "\t\tlabel=Stopped;\n");
	fprintf(fp, "\t\tnode [color=white, style=filled, shape=box, "
		    "fontcolor=black];\n");
	
	z = 0;
	for (x = 0; x < slen; x++) {
		
		if (states[x].rs_status.rs_state != RG_STATE_STOPPED)
			continue;
		
		strncpy(str, states[x].rs_status.rs_name, sizeof(str));
		_dot_clean_string(str);
		
		++z;
		fprintf(fp, "\t\t%s;\n", str);
	}
	
	if (!z) {
		fprintf(fp, "\t\tnode [style=filled, color=\"#60ff60\", "
			    "fontcolor=\"#60ff60\"];\n");
		fprintf(fp, "\t\tStopped;\n");
	}
	
	fprintf(fp, "\t}\n");
	
	/* Show all 'disabled' in a 'disabled' "node" */
	fprintf(fp, "\tsubgraph cluster_%d {\n", nlen+3);
	fprintf(fp, "\t\tlabel=Disabled;\n");
	fprintf(fp, "\t\tstyle=filled;\n");
	fprintf(fp, "\t\tcolor=\"#6060ff\";\n");
	fprintf(fp, "\t\tfontcolor=black;\n");
	fprintf(fp, "\t\tnode [color=white, style=filled, shape=box, "
		    "fontcolor=black];\n");
	
	z = 0;
	for (x = 0; x < slen; x++) {
		
		if (states[x].rs_status.rs_state != RG_STATE_DISABLED)
			continue;
		
		++z;
		strncpy(str, states[x].rs_status.rs_name, sizeof(str));
		_dot_clean_string(str);
		fprintf(fp, "\t\t%s;\n", str);
	}
	
	if (!z) {
		fprintf(fp, "\t\tnode [style=filled, color=\"#6060ff\", "
			    "fontcolor=\"#6060ff\"];\n");
		fprintf(fp, "\t\tDisabled;\n");
	}
		
	fprintf(fp, "\t}\n");
			
	_print_depends_dot(fp, deps);
	
	fprintf(fp, "}\n");
	
	return 0;
}


int
dep_cluster_state(FILE *fp, dep_t **deps, dep_rs_t *states, int slen,
		  int *nodes, int nlen)
{
	int x, y, z, tmp, off = 0;
	
	/* Print out node states */
	for (x = 0; x < nlen; x++) {
		fprintf(fp, "Node %d\n", nodes[x]);
		
		for (y = 0; y < slen; y++) {
			if (rs_running(NULL, &states[y], 1) &&
			    states[y].rs_status.rs_owner == nodes[x]) {
				tmp = 0;
				if (!states[y].rs_allowed_len)
					tmp = 1;
				else {
					for (z = 0; z<states[y].rs_allowed_len;
					     z++) {
						if (states[y].rs_allowed[z] == 
						    states[y].rs_status.rs_owner) {
							tmp = 1;
							break;
						}
					}
				}
				
				if (!tmp) {
					fprintf(fp, "\t[ILLEGAL] ");
				} else {
					fprintf(fp, "\t");
				}
				
				fprintf(fp, "%s\n",
					states[y].rs_status.rs_name);
			}
		}
		fprintf(fp, "\n");
	}
	
	/* Show all 'started' on dead node resources in a red "node" */
	off = 0;
	for (x = 0; x < slen; x++) {
		
		if (!(states[x].rs_flags & RS_DEAD_NODE))
			continue;
		
		if (!off) {
			off = 1;
			fprintf(fp, "Resources on dead nodes:\n");
		}
			
		fprintf(fp, "\t%s\n", states[x].rs_status.rs_name);
	}
	
	if (off)
		fprintf(fp, "\n");
	
	/* Show all 'stopped' in a 'stopped' "node" */
	fprintf(fp, "Stopped resources:\n");
	
	z = 0;
	for (x = 0; x < slen; x++) {
		
		if (states[x].rs_status.rs_state != RG_STATE_STOPPED)
			continue;
		
		fprintf(fp, "\t%s;\n", states[x].rs_status.rs_name);
	}
	
	fprintf(fp, "\n");
	
	/* Show all 'disabled' */
	fprintf(fp, "Disabled resources:\n");
	
	z = 0;
	for (x = 0; x < slen; x++) {
		
		if (states[x].rs_status.rs_state != RG_STATE_DISABLED)
			continue;
		
		fprintf(fp, "\t%s;\n", states[x].rs_status.rs_name);
	}
	
	fprintf(fp, "\n");
	
	return 0;
}

/**
   Gets an attribute of a resource group.

   @param res		Resource
   @param property	Name of property to check for
   @param ret		Preallocated return buffer
   @param len		Length of buffer pointed to by ret
   @return		0 on success, -1 on failure.
 */
int
res_property(resource_t *res, char *property, char *ret, size_t len)
{
	int x = 0;

	if (!res)
		return -1;

	for (; res->r_attrs[x].ra_name; x++) {
		if (strcasecmp(res->r_attrs[x].ra_name, property))
			continue;
		strncpy(ret, res->r_attrs[x].ra_value, len);
		return 0;
	}

	return 1;
}


static void
_set_allowed(dep_rs_t *state, fod_t *domain, int *nodes, int nlen)
{
	fod_node_t *fdn;
	int cnt = 0, allowed = 0, x, y, z, tmp;
	
	if (!domain) {
		state->rs_allowed = malloc(sizeof(int)*nlen);
		state->rs_allowed_len = nlen;
		
		if (!state->rs_allowed)
			return;
		memcpy(state->rs_allowed, nodes, sizeof(int)*nlen);
		return;
	}
	
	if (domain->fd_flags & FOD_RESTRICTED) {
		list_for(&domain->fd_nodes, fdn, allowed);
	} else {
		allowed = nlen;
	}
	
	state->rs_allowed = malloc(sizeof(int)*allowed);
	state->rs_allowed_len = allowed;
	state->rs_flags = 0;
	
	if (!state->rs_allowed)
		return;
	
	memset(state->rs_allowed, 0, sizeof(int)*allowed);
	
	cnt = 0;
	/* Failover domain prios are 1..100 */
	if (!(domain->fd_flags & FOD_ORDERED)) {
		
		/* Non-prio failover domain */
		list_for(&domain->fd_nodes, fdn, x) {
			state->rs_allowed[cnt++] = fdn->fdn_nodeid;
		}
	} else {
		for (x = 0; x <= 100; x++) {
			list_for(&domain->fd_nodes, fdn, y) {
				if (fdn->fdn_prio != x)
					continue;
				state->rs_allowed[cnt++] = fdn->fdn_nodeid;
			}
		}
		
		state->rs_flags |= RS_ORDERED;
		
		if (!(domain->fd_flags & FOD_NOFAILBACK))
			state->rs_flags |= RS_FAILBACK;
	}
	
	if (domain->fd_flags & FOD_RESTRICTED)
		return;
	
	/* allowed == nlen */
	/* find missing nodes and fill them in */
	for (x = 0; x < allowed; x++) {
		if (state->rs_allowed[x] != 0)
			continue;
		for (y = 0; y < nlen; y++) {
			
			tmp = 0;
			for (z = 0; z < x; z++) {
				if (nodes[y] == state->rs_allowed[z]) {
					tmp = 1;
					break;
				}
			}
			
			if (!tmp) {
				state->rs_allowed[x] = nodes[y];
			}
		}
	}
}


dep_rs_t *
dep_rstate_alloc(resource_node_t **restree, fod_t **domains, int *nodes,
		 int nlen, int *rs_cnt)
{
	dep_rs_t *rstates;
	resource_node_t *rn;
	int tl_res_count = 0, x;
	char dom[64];
	fod_t *fod;
	
	list_for(restree, rn, tl_res_count);
	
	rstates = malloc(sizeof(dep_rs_t) * tl_res_count);
	if (!rstates)
		return NULL;
	
	memset(rstates, 0, (sizeof(dep_rs_t) * tl_res_count));
	
	list_for(restree, rn, x) {
		snprintf(rstates[x].rs_status.rs_name,
			 sizeof(rstates[x].rs_status.rs_name),
			 "%s:%s", rn->rn_resource->r_rule->rr_type,
			 rn->rn_resource->r_attrs[0].ra_value);
		
		rstates[x].rs_status.rs_last_owner = 0;
		rstates[x].rs_status.rs_owner = 0;
		rstates[x].rs_status.rs_state = RG_STATE_STOPPED;
		
		fod = NULL;
		if (!res_property(rn->rn_resource, "domain", dom,
				sizeof(dom))) {
			
			fod = fod_find_domain(domains, dom);
		} 
		_set_allowed(&rstates[x], fod, nodes, nlen);
	}
	
	*rs_cnt = tl_res_count;
	
	return rstates;
}


void
dep_rstate_free(dep_rs_t *states, int slen)
{
	int x;
	
	if (!states)
		return;
	if (!slen)
		return;
	
	for (x = 0; x < slen; x++) {
		if (states[x].rs_allowed && states[x].rs_allowed_len) {
			free(states[x].rs_allowed);
			states[x].rs_allowed = NULL;
			states[x].rs_allowed_len = 0;
		}
	}
	
	free(states);
}


dep_rs_t *
dep_rstate_dup(dep_rs_t *states, int slen)
{
	dep_rs_t *states_new;
	int x;
	
	while ((states_new = malloc(sizeof(dep_rs_t) * slen)) == NULL)
		usleep(10000);
	
	memcpy(states_new, states, sizeof(dep_rs_t) * slen);
	
	for (x = 0; x < slen; x++) {
		if (!states[x].rs_allowed || !states[x].rs_allowed_len) {
			states_new[x].rs_allowed = NULL;
			continue;
		}
		
		while ((states_new[x].rs_allowed =
			malloc(sizeof(int) * states[x].rs_allowed_len))==NULL)
			usleep(10000);
		
		memcpy(states_new[x].rs_allowed,
		       states[x].rs_allowed,
		       sizeof(int) * states[x].rs_allowed_len);
	}
		
	return states_new;
}	


/**
 * copy everything from src into dest except for the allowed pointer arrays
 */
void
dep_rstate_copy(dep_rs_t *dest, dep_rs_t *src, int slen)
{
	int x, *ap;
	
	for (x = 0; x < slen; x ++) {
		ap = dest[x].rs_allowed;
		memcpy(&dest[x], &src[x], sizeof(dep_rs_t));
		dest[x].rs_allowed = ap;
	}
}	


static int
dep_srt_cmp(dep_t *l, dep_rs_t *ls, dep_t *r, dep_rs_t *rs)
{
	
	if (ls->rs_status.rs_state == RG_STATE_STOPPED &&
	    rs->rs_status.rs_state != RG_STATE_STOPPED)
		return 1;
	
	if (ls->rs_status.rs_state != RG_STATE_STOPPED &&
	    rs->rs_status.rs_state == RG_STATE_STOPPED)
		return -1;
	
	if (ls->rs_status.rs_state != RG_STATE_STOPPED &&
	    rs->rs_status.rs_state != RG_STATE_STOPPED) {
		
		if (!l && r)
			return -1;
		if (r && !l)
			return 1;
		if (!r && !l)
			return 0;
		
		/* running resource */
		/*
		 * Left is greater if the hits exceed, or left's
		 * hits are zero
		 */
		if (l->d_hits == 0 && r->d_hits != 0)
			return -1;
		if (l->d_hits != 0 && r->d_hits == 0)
			return 1;
		
		/* Otherwise, more hits, the farther away */
		if (l->d_hits > r->d_hits)
			return -1;
		if (l->d_hits < r->d_hits)
			return 1;
		return 0;
	}
	
	/* both states are stopped */
	if (!l && r)
		return 1;
	if (l && !r)
		return -1;
	if (!r && !l)
		return 0;
	if (l->d_deps > r->d_deps)
		return -1;
	if (l->d_deps < r->d_deps)
		return 1;
	if (l->d_hits > r->d_hits)
		return -1;
	if (l->d_hits < r->d_hits)
		return 1;
	return 0;
}


void
dep_rstate_sort(dep_t **deps, dep_rs_t *states, int slen)
{
	dep_t *xd, *yd;
	int x, y;
	dep_rs_t tmp;
	
	/* brute force sort */
	for (x = 0; x < slen; x++) {
		for (y = 0; y < x; y++) {
			xd = find_dep(deps, states[x].rs_status.rs_name);
			yd = find_dep(deps, states[y].rs_status.rs_name);
			
			if (dep_srt_cmp(yd, &states[y], xd, &states[x]) < 0) {
				memcpy(&tmp, &states[y], sizeof(dep_rs_t));
				memcpy(&states[y], &states[x],
				       sizeof(dep_rs_t));
				memcpy(&states[x], &tmp, sizeof(dep_rs_t));
			}
		}
	}
}


static inline int
_alter_state_start(dep_t **deps, dep_rs_t *state, int newowner)
{
	/* Try starting a resource */
	if (state->rs_flags & RS_BEEN_STARTED)
		return 0;
				
	/* we just stopped this on this node */
	if (state->rs_status.rs_last_owner == newowner)
		return 0;
				
	state->rs_status.rs_state = RG_STATE_STARTED;
	state->rs_status.rs_owner = newowner;
				
	/*
	 * Optimization: don't allow start+stop+start.
	 * of the same resource, unless it was in an
	 * error state.
	 */
	state->rs_flags |= (RS_BEEN_STARTED | RS_BEEN_STOPPED);
	
	return 1;
}


static inline int
_alter_state_stop(dep_t **deps, dep_rs_t *state)
{
	dep_t *dep;
	dep_node_t *dn;
	int x;
	
	if (state->rs_flags & RS_BEEN_STOPPED)
		return 0;
	
	dep = find_dep(deps, state->rs_status.rs_name);
	if (!dep) {
		printf("Dependency missing for %s\n",
			state->rs_status.rs_name);
		return 0;
	}
	
#if 0
	if (dep->d_hits == 0) {
		/* If nothing depends on this resource, stopping it is 
		 * pointless
		 */
		return 0;
	}
#endif
		
	state->rs_status.rs_state = RG_STATE_STOPPED;
	state->rs_status.rs_last_owner = state->rs_status.rs_owner;
	state->rs_status.rs_owner = 0;
	state->rs_flags |= RS_BEEN_STOPPED;
	
	/* We can clear the state now.  Set the last owner to nobody */
	if (state->rs_flags & (RS_ILLEGAL_NODE | 
			       RS_DEAD_NODE    )) {
		state->rs_status.rs_last_owner = 0;
		state->rs_flags &= ~(RS_ILLEGAL_NODE | RS_DEAD_NODE);
		return 1;
	}

	/*
	 * If we stopped it because of a broken dependency, then we can
	 * restart it on the same node. 
	 */
	list_for(&dep->d_nodes, dn, x) {
		if (dn->dn_flags & (DN_BROKEN_COLO   |
				    DN_BROKEN_REQ    |
				    DN_BROKEN_NONCOLO)) {
			state->rs_status.rs_last_owner = 0;
			return 1;
		}
	}
	
	return 1;
}


static inline int
_alter_state(dep_t **deps, dep_rs_t *state, dep_op_t *op)
{
	if (!op) {
		printf("No operation\n");
		return 0;
	}
	
	if (op->do_op == RG_START)
		return _alter_state_start(deps, state, op->do_nodeid);
	return _alter_state_stop(deps, state);
}	


static int *
build_try_indices(struct _try *tries, int try_cnt, int idx_cnt)
{
	int *indices, x, y;
	/* Build our indices according to score */
	
	while ((indices = malloc(sizeof(int)*idx_cnt)) == 0)
		usleep(10000);
	y = 0;
	for (x = 0; x < try_cnt; x++) {
		if (!tries[x].ops)
			continue;
		indices[y] = x;
		++y;
	}
	
	return indices;
}


static void
nuke_op_list(dep_op_t **ops)
{
	dep_op_t *op;
	
	while ((op = *ops) != NULL) {
		list_remove(ops, op);
		free(op);
	}
}


static void
sort_try_indices(struct _try *tries, int *indices, int idx_cnt,
		 dep_rs_t *states, int slen)
{
	int x, y, z, swp;
	dep_rs_t *cstate;

	/*
	 * Got our list of indices into our 'tries' branch array, 
	 * now sort.  Brutish sort..  Really should take into account error
	 * states.; e.g. 0 1 2 3 4 5 -1 -2 -3... 
	 */
	for (x = 0; x < idx_cnt; x++) {
		for (y = 0; y < x; y++) {
			
			if (tries[indices[x]].score < tries[indices[y]].score){
				swp = indices[x];
				indices[x] = indices[y];
				indices[y] = swp;
			}
			
			if (tries[indices[x]].score != tries[indices[y]].score)
				continue;
				
			/* See if we're ordered */
			cstate = NULL;
			for (z = 0; z < slen; z++) {
				if (!strcmp(tries[indices[y]].ops->do_res,
					    states[z].rs_status.rs_name)) {
					    cstate = &states[z];
					    break;
				}
			}
			
			if (!cstate || !(cstate->rs_flags & RS_ORDERED))
				continue;
			
			swp = 0;
			for (z = 0; z < cstate->rs_allowed_len; z++) {
				if (tries[indices[x]].ops->do_nodeid ==
				    cstate->rs_allowed[z]) {
					swp = 1;
					break;
				} else if (tries[indices[y]].ops->do_nodeid ==
				    cstate->rs_allowed[z]) {
					break;
				}
			}
			
			if (!swp)
				continue;
			
			swp = indices[x];
			indices[x] = indices[y];
			indices[y] = swp;
		}
	}
}


static void
free_try_list(struct _try *tries, int len)
{
	int x;
		
	/* Clean up all remaining branch lists */
	for (x = 0; x < len; x++) {
		if (!tries[x].ops)
			continue;
		nuke_op_list(&(tries[x].ops));
	}
	
	/* Clean up branch list array */
	free(tries);
}


static int
check_online(int n, int *nodes, int nlen, int *nidx)
{
	for (*nidx = 0; *nidx < nlen; (*nidx)++)
		if (n == nodes[*nidx])
			return nodes[*nidx];
	return -1;
}


static void
insert_try(struct _try *tries, int idx, dep_op_t *op, int score, int iter)
{
	dep_op_t *newop;
	
	while ((newop = malloc(sizeof(dep_op_t))) == NULL)
		usleep(10000);
	memcpy(newop, op, sizeof(dep_op_t));
	newop->do_iter = iter;
	list_insert(&(tries[idx].ops), newop);
	tries[idx].score = score;
}


/**
 * Calculate all of the possible branches from this point.
 * Returns the number of possible branches.
 */
static int
build_all_possible_tries(dep_t **deps, dep_rs_t *states_cpy, int slen,
			 int *nodes, int nlen, int start_score,
			 struct _try *tries, int iter, int depth)
{
	int x, y, nidx, err;
	dep_rs_t tmp;
	dep_op_t op;
	int idx_cnt = 0;
	
	for (x = 0; x < slen; x++) {
		
		/* Immutable resources can't be moved */
		if (states_cpy[x].rs_flags & RS_IMMUTABLE)
			continue;
		
		/* can't bother with non-running resources */
		if (states_cpy[x].rs_status.rs_state == RG_STATE_DISABLED ||
		    states_cpy[x].rs_status.rs_state == RG_STATE_FAILED)
			continue;
		
		/*
		 * see if this set has already tried to manipulate this
		 * service
		 */
		if ((states_cpy[x].rs_flags & 
		    (RS_BEEN_STARTED|RS_BEEN_STOPPED)) ==
		    (RS_BEEN_STARTED|RS_BEEN_STOPPED))
			continue;
		
		/*
		 * Since we are operating on the same allowed list (and
		 * will not be freeing tmp), it's ok to just use memcpy here.
		 */
		memcpy(&tmp, &states_cpy[x], sizeof(tmp));
		
		op.do_op = RG_START; /* XXX the loop below must happen
		 		        at least once...*/
		
		/* try for each nodeid that's a legal target */
		for (y = 0; (op.do_op == RG_START) &&
		            y < states_cpy[x].rs_allowed_len; y++) {
			
			memcpy(&states_cpy[x], &tmp, sizeof(tmp));
			/* 
			 * Find next node online in our allowed list
			 * nidx is the offset into the online nodes array
			 * where this node was found - since the online list
			 * of nodes might be smaller than the allowed list
			 * for the resource, we need to keep track of it for
			 * later.
			 * 
			 * XXX Since we sort later in the alg., we can probably
			 * reverse these loops to get rid of nidx.
			 */
			if ((err = check_online(states_cpy[x].rs_allowed[y],
						nodes, nlen, &nidx)) < 0)
				continue;
			
			/* Reset flags, etc. */
			memset(&op, 0, sizeof(dep_op_t));
			
			if (rs_running(NULL, &states_cpy[x], 1)) {
			
				/* Try stopping a resource */
				op.do_op = RG_STOP;
				strncpy(op.do_res,
					states_cpy[x].rs_status.rs_name,
					sizeof(op.do_res));
				/*printf("stop %s %d %d %d ", op.do_res,
					depth, iter, start_score);*/
				
			} else if (states_cpy[x].rs_status.rs_state ==
				   RG_STATE_STOPPED &&
				   start_score >= 0) {
			
				/* Try starting a resource.  We can only
				 * do this after we clear the errors (e.g. stop)
				 * resources */
				op.do_op = RG_START;
				op.do_nodeid = err;
				strncpy(op.do_res,
					states_cpy[x].rs_status.rs_name,
					sizeof(op.do_res));
				/*printf("start %s %d %d %d ", op.do_res,
					depth, iter, start_score);*/
			} else {
				/* Other states = can't do anything */
				continue;
			}
			
			/*
			 * Try to apply our operation.  If it was an invalid,
			 * operation, we just move on
			 */
			if (_alter_state(deps, &states_cpy[x], &op) == 0)
				continue;
	
			/* Check each possible move at this level */
			err = dep_check(deps, states_cpy, slen, nodes, nlen);
			//printf(" [score: %d]\n", err);
		
			/* 
			 * Make sure the operation does not introduce an
			 * error
			 */
			if ((start_score < 0 && err <= start_score) ||
			    (start_score >= 0 && err < 0)) {
				/*
				 * Introduce new error = bad; drop this
				 * operation.
				 */
				/*
				printf("%s of %s introduced new err %d %d\n",
					op.do_op==RG_START?"start":"stop",
					op.do_res,
					start_score, err);
				 */
				continue;
			}
			
			/* No new error - this op is a valid thing to do */
			/* Insert the op on to the try list and move on */
			insert_try(tries, x * nlen + nidx, &op, err, iter);
			
			/*
			 * Keep track of how many places we actually
			 * found a possible branch for returning
			 */
			++idx_cnt;
		}
		memcpy(&states_cpy[x], &tmp, sizeof(tmp));
	}
	
	return idx_cnt;
}

			
/**
 * Breadth-first-search.
 */
static int
_dep_calc_trans(dep_t **deps, dep_rs_t *states, int slen, int *nodes, int nlen,
		int depth, int errors, dep_op_t **oplist, int *iter)
{
	int x, y, err;
	struct _try *tries;
	dep_rs_t *states_cpy;
	dep_op_t *newop;
	int best_idx = -1, best_score, start_score;
	int *indices = NULL, idx_cnt = 0;
	
	x = dep_check(deps, states, slen, nodes, nlen);
	/* Ideal? */
	if (x == 0)
		return 0;
	/* Introduced a new error? */
	if ((*iter) && errors < 0 && x <= errors)
		return x;
		
	/* Went from sub-optimal to outright broken? */
	if (errors > 0 && x < 0)
		return x;
	
	/*
	 * Impossible to try branch more than 2*slen times: we can only 
	 * on the outside stop/start every resource in the cluster.
	 */
	if (depth > (2*slen))
		return errors;
	
	/* Set up */
	best_score = start_score = x;
	++(*iter);
	/*
	printf("[%d] start depth %d errors %d init %d\n",
		*iter, depth, errors, start_score);
	*/
	
	while ((tries = malloc(sizeof(*tries) * slen * nlen)) == NULL)
		usleep(10000);
	memset(tries, 0, sizeof(*tries) * slen * nlen);
	
	/* Copy our states (don't alter parent's states) */
	states_cpy = dep_rstate_dup(states, slen);
	
	/* Find all possible things to try */
	idx_cnt = build_all_possible_tries(deps, states_cpy, slen, nodes, nlen,
					   start_score, tries, *iter, depth);

	/* Build our indices into an array */
	indices = build_try_indices(tries, nlen*slen, idx_cnt);
	
	/* Sort array indices */
	sort_try_indices(tries, indices, idx_cnt, states_cpy, slen);
	
	/*
	 * Now, do a recurse on the tree in order of best-score-first...
	 * (that's why we sorted above)
	 */
	dep_rstate_copy(states_cpy, states, slen);
	for (x = 0; x < idx_cnt; x++) {
		
		/* state index = tries_index/node count len */
		y = indices[x] / nlen;
		
		/* Back this up for later */
		dep_rstate_copy(&states_cpy[y], &states[y], 1);
		
		/* Flip our state. */
		if (_alter_state(deps, &states_cpy[y],
				 tries[indices[x]].ops) == 0) {
			/* :( */
			printf("Error: Logic error\n");
			continue;
		}
	
		/* Recurse */
		err = _dep_calc_trans(deps, states_cpy, slen, nodes, nlen,
				      depth + 1, start_score,
				      &(tries[indices[x]].ops), iter);
		
		//dep_rstate_copy(&states_cpy[y], &states[y], 1);
		
		/* Store the score for branching on this operation */
		tries[indices[x]].score = err;
		
		/* ... and the depth.  XXX  not done yet; someday, we should
		 * select the shortest path... 
		tries[indices[x]].depth = 0;
		list_for_count(&tries[indices[x]].ops, newop,
			       tries[indices[x]].depth);
		 */
		
		/* Keep track of our best score */
		if ((best_score < 0 && err > best_score) ||
		    (best_score >= 0 && err >= 0 && err < best_score))  {
			best_idx = indices[x];
			best_score = err;
		}
	}
	
	/* Free our index array; we don't need it anymore */
	free(indices);
	
	/* We don't need our temporary space anymore */
	dep_rstate_free(states_cpy, slen);
	
	if ((errors < 0 && best_score < errors) ||
	    (errors >= 0 && best_score > errors)) {
		/* No good branch to take from here */
		best_idx = -1;
		best_score = start_score;
	}
	
	/* Append the so-called best list on to our passed-in list of
	 * operations (this goes on tries[...] above) */
	if (best_idx != -1) {
		while ((newop = tries[best_idx].ops) != NULL) {
			list_remove(&(tries[best_idx].ops), newop);
			list_insert(oplist, newop);
		}
		
		/* Get the best score */
		best_score = tries[best_idx].score;
	}
	
	free_try_list(tries, nlen * slen);
	
	/*
	printf("[%d] end start score: %d, bestscore: %d  depth: %d\n", *iter,
		start_score, best_score, depth);
	 */
	
	return best_score;
}


int
dep_calc_trans(dep_t **deps, dep_rs_t *_states, int slen,
		int *nodes, int nlen, dep_op_t **op_list, int *iter)
{
	int x, my_i, ops = 0, err = 0;
	dep_op_t *newop;
	dep_rs_t *states = NULL;
	
	if (iter)
		*iter = 0;
	else {
		my_i = 0;
		iter = &my_i;
	}
	
	x = dep_check(deps, _states, slen, nodes, nlen);
	if (x == 0)
		return 0;
	
	states = dep_rstate_dup(_states, slen);
	for (x = 0; x < slen; x++) {
		if (states[x].rs_flags & (RS_DEAD_NODE|RS_ILLEGAL_NODE)) {
			states[x].rs_status.rs_state = RG_STATE_STOPPED;
			while ((newop = malloc(sizeof(*newop))) == NULL)
				sleep(1);
			memset(newop, 0, sizeof(*newop));
			newop->do_op = RG_STOP;
			strncpy(newop->do_res, states[x].rs_status.rs_name,
					sizeof(newop->do_res));
			list_insert(op_list, newop);
			++ops;
		}
	}
	
	/*
	 * Sort: 
	 * low...
	 * (a) stopped resources which are not depended on (starting these
	 *     will not affect any other resources)
	 * (b) stopped resources which are depended on - where the # of
	 *     deps is increasing 
	 * (c) started resources w/ deps, ordered from lowest to highest
	 * (d) started resources w/ no deps (transitioning these will have
	 *     -no- effect.)
	 * ... high
	 */
	dep_rstate_sort(deps, states, slen);
	err = dep_check(deps, states, slen, nodes, nlen);
	if (err) {
		err = _dep_calc_trans(deps, states, slen, nodes, nlen, 0,
				      err, op_list, iter);
	}
	
	dep_rstate_free(states, slen);
	return err;
}


int
dep_apply_trans(dep_t **deps, dep_rs_t *states, int slen, dep_op_t **op_list)
{
	dep_op_t *op;
	int ops = 0, x;

	list_for(op_list, op, ops) {
		for (x = 0; x < slen; x++) {
			if (strcasecmp(op->do_res, states[x].rs_status.rs_name))
				continue;
			if (op->do_op == RG_START) {
				printf("Start %s on %d [%d]\n",
				       op->do_res, op->do_nodeid, op->do_iter);
				states[x].rs_status.rs_state = RG_STATE_STARTED;
				states[x].rs_status.rs_owner = op->do_nodeid;
			} else {
				printf("Stop %s [%d]\n", op->do_res,
				       op->do_iter);
				states[x].rs_status.rs_state = RG_STATE_STOPPED;
				states[x].rs_status.rs_owner = 0;
				states[x].rs_flags &= ~(RS_ILLEGAL_NODE|
						        RS_DEAD_NODE);
			}
		}
	}
	
	printf("Applied %d operations\n", ops);
	
	return 0;
}


void
reverse_list(dep_op_t **oplist)
{
	dep_op_t *new_ol = NULL;
	dep_op_t *op;

	if (!*oplist)
		return;
	
	while ((op = *oplist)) {
		list_remove(oplist, op);
		list_prepend(&new_ol, op);
	}
	
	*oplist = new_ol;
}


void
insert_after_stops(dep_op_t **oplist, dep_op_t *newop)
{
	dep_op_t *new_ol = NULL;
	dep_op_t *op;

	if (!*oplist)
		return;
	
	while ((op = *oplist) && op->do_op == RG_STOP) {
		list_remove(oplist, op);
		list_insert(&new_ol, op);
	}
	
	list_insert(&new_ol, newop);
	
	while ((op = *oplist)) {
		list_remove(oplist, op);
		list_insert(&new_ol, op);
	}
		
	*oplist = new_ol;
}


/**
 * Generate the list of operations which would satisfy a requested (user)
 * operation (e.g. start, relocate, disable)
 */
int
dep_check_operation(char *res, int operation, int target, 
		    dep_t **deps, dep_rs_t *_states,
		    int slen, int *nodes, int nlen, dep_op_t **oplist)
{
	int ret = -1;
	dep_rs_t *state = NULL, *states = NULL;
	dep_op_t *newop = NULL;
	int start_score, score;
	
	states = dep_rstate_dup(_states, slen);
	start_score = dep_check(deps, _states, slen, nodes, nlen);
	
	/* Find the state dealing with */
	if (!(state = _find_state(res, states, slen))) {
		printf("No record of that service...\n");
		dep_rstate_free(states, slen);
		return -1;
	}
	
	switch(operation) {
	case RG_DISABLE:
	case RG_STOP:
	case RG_START:
		while ((newop = malloc(sizeof(dep_op_t))) == NULL)
			usleep(10000);
		memset(newop, 0, sizeof(dep_op_t));
	
		/* Set it up */
		strncpy(newop->do_res, res, sizeof(newop->do_res));
		newop->do_op = operation;
		newop->do_nodeid = target;
		
		if (_alter_state(deps, state, newop) == 0)
			break;
		
		state->rs_flags |= RS_IMMUTABLE;
		
		start_score = dep_check(deps, states, slen, nodes, nlen);
		score = dep_calc_trans(deps, states, slen, nodes, nlen,
					oplist, NULL);
		if (start_score < 0 && score <= start_score)
			break;
		
		/* Reverse the list if we breake dependencies */
		if (operation == RG_STOP || operation == RG_DISABLE) {
			/* Append the operation */
			reverse_list(oplist);
			list_insert(oplist, newop);
		} else {
			/* append after stops... */
			insert_after_stops(oplist, newop);
		}
		
		ret = 0; /* Woot */
		break;
	case RG_RELOCATE:
		if (dep_check_operation(res, RG_STOP, -1, deps, states, slen, nodes,
					nlen, oplist) < 0)
			break;
		if (dep_check_operation(res, RG_START, target, deps, states, slen,
				        nodes, nlen, oplist) < 0)
			break;
		ret = 0; /* Woot */
		break;
	case RG_MIGRATE:
	default:
		break;
	}
	
	/* Ret will be -1 unless we succeeded */
	if (ret < 0) {
		if (newop)
			free(newop);
			
		while ((newop = *oplist)) {
			list_remove(oplist, newop);
			free(newop);
		}
	}

	if (states)
		dep_rstate_free(states, slen);
	return ret;
}
