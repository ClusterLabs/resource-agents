/*
  Copyright Red Hat, Inc. 2004

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
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>
#include <magma.h>
#include <ccs.h>
#include <stdlib.h>
#include <stdio.h>
#include <resgroup.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <list.h>
#include <reslist.h>
#include <pthread.h>
#include <clulog.h>
#include <assert.h>

#ifdef MDEBUG
#include <mallocdbg.h>
#endif
int _res_op(resource_node_t **tree, resource_t *first, char *type,
	    void * __attribute__((unused))ret, int op);
void print_env(char **env);

const char *res_ops[] = {
	"start",
	"stop",
	"status",
	"resinfo",
	"restart",
	"reload",
	"condrestart",		/* Unused */
	"monitor",
	"meta-data",		/* printenv */
	"validate-all"
};


const char *ocf_errors[] = {
	"success",				// 0
	"generic error",			// 1
	"invalid argument(s)",			// 2
	"function not implemented",		// 3
	"insufficient privileges",		// 4
	"program not installed",		// 5
	"program not configured",		// 6
	"not running",
	NULL
};


/**
   ocf_strerror
 */
const char *
ocf_strerror(int ret)
{
	if (ret < OCF_RA_MAX)
		return ocf_errors[ret];

	return "unspecified";
}



/**
   Destroys an environment variable array.

   @param env		Environment var to kill
   @see			build_env
 */
static void
kill_env(char **env)
{
	int x;

	for (x = 0; env[x]; x++)
		free(env[x]);
	free(env);
}


/**
   Adds OCF environment variables to a preallocated  environment var array

   @param res		Resource w/ additional variable stuff
   @param args		Preallocated environment variable array
   @see			build_env
 */
static void
add_ocf_stuff(resource_t *res, char **env)
{
	char ver[10];
	char *minor, *val;
	size_t n;

	if (!res->r_rule->rr_version)
		strncpy(ver, OCF_API_VERSION, sizeof(ver)-1);
	else 
		strncpy(ver, res->r_rule->rr_version, sizeof(ver)-1);

	minor = strchr(ver, '.');
	if (minor) {
		*minor = 0;
		minor++;
	} else
		minor = "0";
		
	/*
	   Store the OCF major version
	 */
	n = strlen(OCF_RA_VERSION_MAJOR_STR) + strlen(ver) + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=%s", OCF_RA_VERSION_MAJOR_STR, ver);
	*env = val; env++;

	/*
	   Store the OCF minor version
	 */
	n = strlen(OCF_RA_VERSION_MINOR_STR) + strlen(minor) + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=%s", OCF_RA_VERSION_MINOR_STR, minor);
	*env = val; env++;

	/*
	   Store the OCF root
	 */
	n = strlen(OCF_ROOT_STR) + strlen(OCF_ROOT) + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=%s", OCF_ROOT_STR, OCF_ROOT);
	*env = val; env++;

	/*
	   Store the OCF Resource Instance (primary attr)
	 */
	n = strlen(OCF_RESOURCE_INSTANCE_STR) +
		strlen(res->r_attrs[0].ra_value) + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=%s", OCF_RESOURCE_INSTANCE_STR,
		 res->r_attrs[0].ra_value);
	*env = val; env++;

	/*
	   Store the OCF Resource Type
	 */
	n = strlen(OCF_RESOURCE_TYPE_STR) +
		strlen(res->r_rule->rr_type) + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=%s", OCF_RESOURCE_TYPE_STR, 
		 res->r_rule->rr_type);
	*env = val; env++;

	/*
	   Store the OCF Check Level (0 for now)
	 */
	n = strlen(OCF_CHECK_LEVEL_STR) + strlen("0") + 2;
	val = malloc(n);
	if (!val)
		return;
	snprintf(val, n, "%s=0", OCF_CHECK_LEVEL_STR);
	*env = val; env++;
}


/**
   Allocate and fill an environment variable array.

   @param node		Node in resource tree to use for parameters
   @param op		Operation (start/stop/status/monitor/etc.)
   @return		Newly allocated environment array or NULL if
   			one could not be formed.
   @see			kill_env res_exec add_ocf_stuff
 */
static char **
build_env(resource_node_t *node, int op)
{
	resource_t *res = node->rn_resource;
	char **env;
	char *val;
	int x, n;

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++);
	x += 7; /*
		   Leave space for:
		   OCF_RA_VERSION_MAJOR
		   OCF_RA_VERSION_MINOR
		   OCF_ROOT
		   OCF_RESOURCE_INSTANCE
		   OCF_RESOURCE_TYPE
		   OCF_CHECK_LEVEL
		   (null terminator)
		 */

	env = malloc(sizeof(char *) * x);
	if (!env)
		return NULL;

	memset(env, 0, sizeof(char *) * x);

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {

		val = attr_value(node, res->r_attrs[x].ra_name);

		/* Strlen of both + '=' + 'OCF_RESKEY' + '\0' terminator' */
		n = strlen(res->r_attrs[x].ra_name) + strlen(val) + 2 +
			strlen(OCF_RES_PREFIX);

		env[x] = malloc(n);
		if (!env[x]) {
			kill_env(env);
			return NULL;
		}

		/* Prepend so we don't conflict with normal shell vars */
		snprintf(env[x], n, "%s%s=%s", OCF_RES_PREFIX,
			 res->r_attrs[x].ra_name, val);
		
#if 0
		/* Don't uppercase; OCF-spec */
		for (n = 0; env[x][n] != '='; n++)
			env[x][n] &= ~0x20; /* Convert to uppercase */
#endif
	}

	add_ocf_stuff(res, &env[x]);

	return env;
}


/**
   Print info in an environment array

   @param env		Environment to print.
 */
void
print_env(char **env)
{
	int x;

	for (x = 0; env[x]; x++) {
		printf("%s\n", env[x]);
	}
}


/**
   Set all signal handlers to default for exec of a script.
   ONLY do this after a fork().
 */
void
restore_signals(void)
{
	sigset_t set;
	int x;

	for (x = 1; x < _NSIG; x++)
		signal(x, SIG_DFL);

	sigfillset(&set);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}


/**
   Execute a resource-specific agent within its associated handler for a
   resource node in the tree.

   @param node		Resource tree node we're dealing with
   @param op		Operation to perform (stop/start/etc.)
   @return		Return value of script.
   @see			build_env
 */
int
res_exec(resource_node_t *node, int op)
{
	int childpid, pid;
	int ret = 0;
	char **env = NULL;
	resource_t *res = node->rn_resource;
	char fullpath[2048];

	if (!res->r_rule->rr_agent)
		return 0;

#ifdef DEBUG
	env = build_env(node, op);
	if (!env)
		return -errno;
#endif

	childpid = fork();
	if (childpid < 0)
		return -errno;

	if (!childpid) {
		/* Child */ 
#if 0
		printf("Exec of script %s, action %s type %s\n",
			res->r_rule->rr_agent, res_ops[op],
			res->r_rule->rr_type);
#endif

#ifndef DEBUG
		env = build_env(node, op);
#endif

		if (!env)
			exit(-ENOMEM);

		if (!res->r_rule->rr_handler)
			res->r_rule->rr_handler = "/bin/bash";

		if (res->r_rule->rr_agent[0] != '/')
			snprintf(fullpath, sizeof(fullpath), "%s/%s",
				 RESOURCE_ROOTDIR, res->r_rule->rr_agent);
		else
			snprintf(fullpath, sizeof(fullpath), "%s",
				 res->r_rule->rr_agent);

		restore_signals();

		execle(res->r_rule->rr_handler, res->r_rule->rr_handler, 
		       fullpath, res_ops[op], NULL, env);
	}

#ifdef DEBUG
	kill_env(env);
#endif

	do {
		pid = waitpid(childpid, &ret, 0);
		if ((pid < 0) && (errno == EINTR))
			continue;
	} while (0);

	if (WIFEXITED(ret)) {

		ret = WEXITSTATUS(ret);

		if (ret) {
			clulog(LOG_NOTICE,
			       "%s on %s \"%s\" returned %d (%s)\n",
			       res_ops[op], res->r_rule->rr_type,
			       res->r_attrs->ra_value, ret,
			       ocf_strerror(ret));
		}

		return ret;
	}

	if (!WIFSIGNALED(ret))
		assert(0);

	return -EFAULT;
}


/**
   Build the resource tree.  If a new resource is defined inline, add it to
   the resource list.  All rules, however, must have already been read in.

   @param ccsfd		File descriptor connected to CCS
   @param tree		Tree to modify/insert on to
   @param parent	Parent node, if one exists.
   @param rule		Rule surrounding the new node
   @param rulelist	List of all rules allowed in the tree.
   @param reslist	List of all currently defined resources
   @param base		Base CCS path.
   @see			destroy_resource_tree
 */
static int
build_tree(int ccsfd, resource_node_t **tree,
	   resource_node_t *parent,
	   resource_rule_t *rule,
	   resource_rule_t **rulelist,
	   resource_t **reslist, char *base)
{
	char tok[512];
	resource_rule_t *childrule;
	resource_node_t *node;
	resource_t *curres;
	char *ref;
	int x, y;

	for (x = 1; ; x++) {

		/* Search for base/type[x]/@ref - reference an existing
		   resource */
		snprintf(tok, sizeof(tok), "%s/%s[%d]/@ref", base,
			 rule->rr_type, x);

		if (ccs_get(ccsfd, tok, &ref) != 0) {
			/* There wasn't an existing resource. See if there
			   is one defined inline */
			snprintf(tok, sizeof(tok), "%s/%s[%d]", base, 
				 rule->rr_type, x);

			curres = load_resource(ccsfd, rule, tok);
			if (!curres)
				/* No ref and no new one inline == 
				   no more of the selected type */
				break;

		       	if (store_resource(reslist, curres) != 0) {
		 		printf("Error storing %s resource\n",
		 		       curres->r_rule->rr_type);
		 		destroy_resource(curres);
				continue;
		 	}

			curres->r_flags = RF_INLINE;

		} else {

			curres = find_resource_by_ref(reslist, rule->rr_type,
						      ref);
			if (!curres) {
				printf("Error: Reference to nonexistent "
				       "resource %s (type %s)\n", ref,
				       rule->rr_type);
				continue;
			}

			if (curres->r_flags & RF_INLINE) {
				printf("Error: Reference to inlined "
				       "resource %s (type %s) is illegal\n",
				       ref, rule->rr_type);
				continue;
			}
		}

		/* Load it if its max refs hasn't been exceeded */
		if (rule->rr_maxrefs && (curres->r_refs >= rule->rr_maxrefs)){
			printf("Warning: Max references exceeded for resource"
			       " %s (type %s)", curres->r_attrs[0].ra_name,
			       rule->rr_type);
			continue;
		}

		node = malloc(sizeof(*node));
		if (!node)
			continue;

		memset(node, 0, sizeof(*node));

		node->rn_child = NULL;
		node->rn_parent = parent;
		node->rn_resource = curres;
		node->rn_state = RES_STOPPED;
		curres->r_refs++;

		list_insert(tree, node);

		/* TODO: Search for children with new-fangled CCS stuff */
		for (y = 0; rule->rr_childtypes &&
		     rule->rr_childtypes[y].rc_name; y++) {
			childrule = find_rule_by_type(rulelist,
					rule->rr_childtypes[y].rc_name);
			if (!childrule) {
				/*
				printf("Error: Reference to nonexistent "
				       "rule type %s in type %s\n",
				       rule->rr_childtypes[y].rc_name,
				       rule->rr_type);
				 */
				continue;
			}

			snprintf(tok, sizeof(tok), "%s/%s[%d]", base,
				 rule->rr_type, x);

			/* Kaboom */
			build_tree(ccsfd, &node->rn_child, node, childrule,
				   rulelist, reslist, tok);
		}
	}

	return 0;
}


/**
   Set up to call build_tree.  Hides the nastiness from the user.

   @param ccsfd		File descriptor connected to CCS
   @param tree		Tree pointer.  Should start as a pointer to NULL.
   @param rulelist	List of all rules allowed
   @param reslist	List of all currently defined resources
   @return 		0
   @see			build_tree destroy_resource_tree
 */
int
build_resource_tree(int ccsfd, resource_node_t **tree,
		    resource_rule_t **rulelist,
		    resource_t **reslist)
{
	resource_rule_t *curr;
	resource_node_t *root = NULL;
	char tok[512];

	snprintf(tok, sizeof(tok), "%s", RESOURCE_TREE_ROOT);

	/* Find and build the list of root nodes */
	list_do(rulelist, curr) {

		if (!curr->rr_root)
			continue;

		build_tree(ccsfd, &root, NULL, curr, rulelist, reslist, tok);

	} while (!list_done(rulelist, curr));

	if (root)
		*tree = root;

	return 0;
}


/**
   Deconstruct a resource tree.

   @param tree		Tree to obliterate.
   @see			build_resource_tree
 */
void
destroy_resource_tree(resource_node_t **tree)
{
	resource_node_t *node;

	while ((node = *tree)) {
		if ((*tree)->rn_child)
			destroy_resource_tree(&(*tree)->rn_child);

		list_remove(tree, node);
		free(node);
	}
}


void
_print_resource_tree(resource_node_t **tree, int level)
{
	resource_node_t *node;
	int x, y;

	list_do(tree, node) {
		for (x = 0; x < level; x++)
			printf("  ");

		printf("%s {\n", node->rn_resource->r_rule->rr_type);

		for (x = 0; node->rn_resource->r_attrs &&
		     node->rn_resource->r_attrs[x].ra_value; x++) {
			for (y = 0; y < level+1; y++)
				printf("  ");
			printf("%s = \"%s\";\n",
			       node->rn_resource->r_attrs[x].ra_name,
			       attr_value(node, node->rn_resource->r_attrs[x].ra_name)
			      );
		}

		_print_resource_tree(&node->rn_child, level + 1);

		for (x = 0; x < level; x++)
			printf("  ");
		printf("}\n");
	} while (!list_done(tree, node));
}


void
print_resource_tree(resource_node_t **tree)
{
	_print_resource_tree(tree, 0);
}


/**
   Nasty codependent function.  Perform an operation by numerical level
   at some point in the tree.  This allows indirectly-dependent resources
   (such as IP addresses and user scripts) to have ordering without requiring
   a direct dependency.

   @param tree		Resource tree to search/perform operations on
   @param first		Resource we're looking to perform the operation on,
   			if one exists.
   @param ret		Unused, but will be used to store status information
   			such as resources consumed, etc, in the future.
   @param op		Operation to perform if either first is found,
   			or no first is declared (in which case, all nodes
			in the subtree).
   @see			_res_op res_exec
 */
int
_res_op_by_level(resource_node_t **tree, resource_t *first, void *ret,
		 int op)
{
	resource_node_t *node = *tree;
	resource_t *res = node->rn_resource;
	resource_rule_t *rule = res->r_rule;
	int l, x, rv = 0, lev;

	if (!rule->rr_childtypes)
		return _res_op(&node->rn_child, first, NULL, ret, op);

	for (l = 1; l <= RESOURCE_MAX_LEVELS; l++) {

		for (x = 0; rule->rr_childtypes &&
		     rule->rr_childtypes[x].rc_name; x++) {

			lev = rule->rr_childtypes[x].rc_startlevel;
			if (!lev || lev != l)
				continue;

#if 0
			printf("%s children of %s type %s (level %d)\n",
			       res_ops[op],
			       node->rn_resource->r_rule->rr_type,
			       rule->rr_childtypes[x].rc_name, l);
#endif

			/* Do op on all children at our level */
			rv = _res_op(&node->rn_child, first,
			     	     rule->rr_childtypes[x].rc_name, 
		     		     ret, op);
			if (rv != 0)
				return rv;
		}

		if (rv != 0)
			return rv;
	}

	for (x = 0; rule->rr_childtypes &&
	     rule->rr_childtypes[x].rc_name; x++) {
		lev = rule->rr_childtypes[x].rc_startlevel;

		if (lev)
			continue;

		/*
		printf("%s children of %s type %s (default level)\n",
		       res_ops[op],
		       node->rn_resource->r_rule->rr_type,
		       rule->rr_childtypes[x].rc_name);
		 */

		rv = _res_op(&node->rn_child, first,
			     rule->rr_childtypes[x].rc_name, 
			     ret, op);
		if (rv != 0)
			return rv;
	}

	return 0;
}


/**
   Nasty codependent function.  Perform an operation by type for all siblings
   at some point in the tree.  This allows indirectly-dependent resources
   (such as IP addresses and user scripts) to have ordering without requiring
   a direct dependency.

   @param tree		Resource tree to search/perform operations on
   @param first		Resource we're looking to perform the operation on,
   			if one exists.
   @param type		Type to look for.
   @param ret		Unused, but will be used to store status information
   			such as resources consumed, etc, in the future.
   @param op		Operation to perform if either first is found,
   			or no first is declared (in which case, all nodes
			in the subtree).
   @see			_res_op_by_level res_exec
 */
int
_res_op(resource_node_t **tree, resource_t *first, char *type,
	   void * __attribute__((unused))ret, int op)
{
	int rv, me;
	resource_node_t *node;

	list_do(tree, node) {

		/* If we're starting by type, do that funky thing. */
		if (type && strlen(type) &&
		    strcmp(node->rn_resource->r_rule->rr_type, type))
			continue;

		/* If the resource is found, all nodes in the subtree must
		   have the operation performed as well. */
		me = !first || (node->rn_resource == first);

/*
		printf("begin %s: %s\n", res_ops[op],
		       node->rn_resource->r_rule->rr_type); */

		/* Start starts before children */
		if (me && (op == RS_START)) {
			rv = res_exec(node, op);
			if (rv != 0)
				return rv;
		}

		if (node->rn_child) {
			rv = _res_op_by_level(&node, me?NULL:first, ret, op);
			if (rv != 0)
				return rv;
		}

		/* Stop/status/etc stops after children have stopped */
		if (me && (op != RS_START)) {
			rv = res_exec(node, op);

			if (rv != 0)
				return rv;
		}

		/*
		printf("end %s: %s\n", res_ops[op],
		       node->rn_resource->r_rule->rr_type);
		 */
	} while (!list_done(tree, node));

	return 0;
}


/**
   Start all occurrences of a resource in a tree

   @param tree		Tree to search for our resource.
   @param res		Resource to start/stop
   @param ret		Unused
 */
int
res_start(resource_node_t **tree, resource_t *res, void *ret)
{
	return _res_op(tree, res, NULL, ret, RS_START);
}


/**
   Stop all occurrences of a resource in a tree

   @param tree		Tree to search for our resource.
   @param res		Resource to start/stop
   @param ret		Unused
 */
int
res_stop(resource_node_t **tree, resource_t *res, void *ret)
{
	return _res_op(tree, res, NULL, ret, RS_STOP);
}


/**
   Check status of all occurrences of a resource in a tree

   @param tree		Tree to search for our resource.
   @param res		Resource to start/stop
   @param ret		Unused
 */
int
res_status(resource_node_t **tree, resource_t *res, void *ret)
{
	return _res_op(tree, res, NULL, ret, RS_STATUS);
}


/**
   Grab resource info for all occurrences of a resource in a tree

   @param tree		Tree to search for our resource.
   @param res		Resource to start/stop
   @param ret		Unused
 */
int
res_resinfo(resource_node_t **tree, resource_t *res, void *ret)
{
	return _res_op(tree, res, NULL, ret, RS_RESINFO);
}
