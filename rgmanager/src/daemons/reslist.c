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

#ifdef MDEBUG
#include <mallocdbg.h>

pthread_mutex_t tv_mutex = PTHREAD_MUTEX_INITIALIZER;
char *_tmp_val;

#define str_localize(x) \
do { \
	pthread_mutex_lock(&tv_mutex); \
	_tmp_val = strdup(x); \
	qfree(x); \
	x = _tmp_val; \
	pthread_mutex_unlock(&tv_mutex); \
} while(0)

#endif


char *attr_value(resource_node_t *node, char *attrname);


/**
   Find and determine an attribute's value.  Takes into account inherited
   attribute flag, and append attribute flag, which isn't implemented yet.

   @param node		Resource tree node to look examine
   @param attrname	Attribute to retrieve.
   @return 		value of attribute or NULL if not found
 */
char *
attr_value(resource_node_t *node, char *attrname)
{
	resource_t *res = node->rn_resource;
	resource_attr_t *ra;
	int x;

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {
		if (strcmp(attrname, res->r_attrs[x].ra_name))
			continue;

		ra = &res->r_attrs[x];

		if (ra->ra_flags & RA_INHERIT)
			return attr_value(node->rn_parent, ra->ra_value);

		return ra->ra_value;
	}

	return NULL;
}


char *
primary_attr_value(resource_t *res)
{
	int x;
	resource_attr_t *ra;

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {
		ra = &res->r_attrs[x];

		if (!(ra->ra_flags & RA_PRIMARY))
			continue;

		return ra->ra_value;
	}

	return NULL;
}



/**
   Compare two resources.

 */
int
rescmp(resource_t *left, resource_t *right)
{
	int x, y = 0, found;

	/* Completely different resource class... */
	if (strcmp(left->r_rule->rr_type, right->r_rule->rr_type)) {
		//printf("Er, wildly different resource type! ");
		return -1;
	}

	for (x = 0; left->r_attrs && left->r_attrs[x].ra_name; x++) {

		found = 0;
		for (y = 0; right->r_attrs && right->r_attrs[y].ra_name; y++) {
			if (!strcmp(right->r_attrs[y].ra_name,
			 	    left->r_attrs[x].ra_name))
				found = 1;
			else
				/* Different attribute name */
				continue;

			if (right->r_attrs[y].ra_flags !=
			    left->r_attrs[x].ra_flags) {
				/* Flags are different.  Change in
				   resource agents? */
				//printf("flags differ ");
				return 1;
			}

			if (strcmp(right->r_attrs[y].ra_value,
				   left->r_attrs[x].ra_value)) {
				/* Different attribute value. */
				//printf("different value for attr '%s'  ",
				       //right->r_attrs[y].ra_name);
				return 1;
			}
		}

		/* Attribute missing -> different attribute value. */
		if (!found) {
			//printf("Attribute %s deleted  ",
			       //left->r_attrs[x].ra_name);
			return 1;
		}
	}

	/* Different attribute count */
	if (x != y) {
		//printf("Attribute count differ (attributes added!) ");
		return 1;
	}

	/* All the same */
	return 0;
}


/**
   Find a resource given its reference.  A reference is the value of the
   primary attribute.

   @param reslist	List of resources to traverse.
   @param type		Type of resource to look for.
   @param ref		Reference
   @return		Resource matching type/ref or NULL if none.
 */   
resource_t *
find_resource_by_ref(resource_t **reslist, char *type, char *ref)
{
	resource_t *curr;
	int x;

	list_do(reslist, curr) {
		if (strcmp(curr->r_rule->rr_type, type))
			continue;

		/*
		   This should be one operation - the primary attr
		   is generally at the head of the array.
		 */
		for (x = 0; curr->r_attrs && curr->r_attrs[x].ra_name;
		     x++) {
			if (!(curr->r_attrs[x].ra_flags & RA_PRIMARY))
				continue;
			if (strcmp(ref, curr->r_attrs[x].ra_value))
				continue;

			return curr;
		}
	} while (!list_done(reslist, curr));

	return NULL;
}


/**
   Store a resource in the resource list if it's legal to do so.
   Otherwise, don't store it.
   Note: This function needs to be rewritten; it's way too long and way
   too indented.

   @param reslist	Resource list to store the new resource.
   @param newres	Resource to store
   @return 		0 on succes; nonzero on failure.
 */
int
store_resource(resource_t **reslist, resource_t *newres)
{
	resource_t *curr;
	int x, y;

	if (!*reslist) {
		/* first resource */
		list_insert(reslist, newres);
		return 0;
	}

	list_do(reslist, curr) {

		if (strcmp(curr->r_rule->rr_type, newres->r_rule->rr_type))
		    	continue;

		for (x = 0; newres->r_attrs && newres->r_attrs[x].ra_name;
		     x++) {
			/*
			   Look for conflicting primary/unique keys
			 */
			if (!newres->r_attrs[x].ra_flags &
			    (RA_PRIMARY | RA_UNIQUE))
				continue;

			for (y = 0; curr->r_attrs[y].ra_name; y++) {
				if (curr->r_attrs[y].ra_flags & RA_INHERIT)
					continue;

				if (strcmp(curr->r_attrs[y].ra_name,
					   newres->r_attrs[x].ra_name))
					continue;
				if (!strcmp(curr->r_attrs[y].ra_value,
					    newres->r_attrs[x].ra_value)) {
					/*
					   Unique/primary is not unique
					 */
					printf("Unique/primary not unique "
					       "type %s, %s=%s\n",
					       newres->r_rule->rr_type,
					       newres->r_attrs[x].ra_name,
					       newres->r_attrs[x].ra_value
					       );
					return -1;
				}
				break;
			}
		}
	} while (!list_done(reslist, curr));

	list_insert(reslist, newres);
	return 0;
}


/**
   Execute an XPath query, returning the first match.  Multiple matches are
   ignored.  Please be advised that this is quite inefficient.

   @param doc		Loaded XML document to search
   @param ctx		Predefined XML XPath context
   @param query		Query to execute.
   @return		newly allocated pointer to value or NULL if not found.
 */
char *
xpath_get_one(xmlDocPtr doc, xmlXPathContextPtr ctx, char *query)
{
	char *val = NULL, *ret = NULL;
	xmlXPathObjectPtr obj;

	obj = xmlXPathEvalExpression(query, ctx);
	if (!obj)
		return NULL;

	if (obj->nodesetval && obj->nodesetval->nodeNr >= 1) {
		val = obj->nodesetval->nodeTab[0]->children->content;
		if (strlen(val) >= 1)
			ret = strdup(val);
	}

	xmlXPathFreeObject(obj);

	return ret;
}


/**
   Obliterate a resource_t structure.

   @param res		Resource to free.
 */
void
destroy_resource(resource_t *res)
{
	int x;

	if (res->r_name)
		free(res->r_name);

	if (res->r_attrs) {
		for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {
			free(res->r_attrs[x].ra_name);
			free(res->r_attrs[x].ra_value);
		}

		free(res->r_attrs);
	}

	free(res);
}



/**
   Obliterate a resource_t list.

   @param list		Resource list to free.
 */
void
destroy_resources(resource_t **list)
{
	resource_t *res;

	while ((res = *list)) {
		list_remove(list, res);
		destroy_resource(res);
	}
}


/**
   Print a resource_t structure to stdout
   
   @param res		Resource to print.
 */
void
print_resource(resource_t *res)
{
	int x;

	printf("Resource type: %s", res->r_rule->rr_type);
	if (res->r_rule->rr_root)
		printf(" [ROOT]");
	if (res->r_flags & RF_INLINE)
		printf(" [INLINE]");
	if (res->r_flags & RF_NEEDSTART)
		printf(" [NEEDSTART]");
	if (res->r_flags & RF_NEEDSTOP)
		printf(" [NEEDSTOP]");
	if (res->r_flags & RF_COMMON)
		printf(" [COMMON]");
	printf("\n");

	if (res->r_rule->rr_maxrefs)
		printf("Instances: %d/%d\n", res->r_refs,
		       res->r_rule->rr_maxrefs);
	if (res->r_rule->rr_agent)
		printf("Agent: %s\n", res->r_rule->rr_agent);
	
	printf("Attributes:\n");
	if (!res->r_attrs) {
		printf("  - None -\n\n");
		return;
	}

	for (x = 0; res->r_attrs[x].ra_name; x++) {

		if (!(res->r_attrs[x].ra_flags & RA_INHERIT)) {
			printf("  %s = %s", res->r_attrs[x].ra_name,
			       res->r_attrs[x].ra_value);
		} else {
			printf("  %s", res->r_attrs[x].ra_name);
		}

		if (!res->r_attrs[x].ra_flags) {
			printf("\n");
			continue;
		}

		printf(" [");
		if (res->r_attrs[x].ra_flags & RA_PRIMARY)
			printf(" primary");
		if (res->r_attrs[x].ra_flags & RA_UNIQUE)
			printf(" unique");
		if (res->r_attrs[x].ra_flags & RA_REQUIRED)
			printf(" required");
		if (res->r_attrs[x].ra_flags & RA_INHERIT)
			printf(" inherit(\"%s\")", res->r_attrs[x].ra_value);
		printf(" ]\n");
	}

	printf("\n");
}


void *
act_dup(resource_act_t *acts)
{
	int x;
	resource_act_t *newacts;

	for (x = 0; acts[x].ra_name; x++);

	++x;
	x *= sizeof(resource_act_t);

	newacts = malloc(x);
	if (!newacts)
		return NULL;

	memcpy(newacts, acts, x);

	return newacts;
}



/**
   Try to load all the attributes in our rule set.  If none are found,
   or an error occurs, return NULL and move on to the next one.

   @param ccsfd		File descriptor connected to CCS
   @param rule		Resource rule set to use when looking for data
   @param base		Base XPath path to start with.
   @return		New resource if legal or NULL on failure/error
 */
resource_t *
load_resource(int ccsfd, resource_rule_t *rule, char *base)
{
	resource_t *res;
	char ccspath[1024];
	char *attrname, *attr;
	int x, found = 0, flags;

	res = malloc(sizeof(*res));
	if (!res) {
		printf("Out of memory\n");
		return NULL;
	}

	memset(res, 0, sizeof(*res));
	res->r_rule = rule;

	for (x = 0; res->r_rule->rr_attrs &&
	     res->r_rule->rr_attrs[x].ra_name; x++) {

		flags = rule->rr_attrs[x].ra_flags;
		attrname = strdup(rule->rr_attrs[x].ra_name);
		if (!attrname) {
			destroy_resource(res);
			return NULL;
		}

		/*
		   Ask CCS for the respective attribute
		 */
		attr = NULL;
		snprintf(ccspath, sizeof(ccspath), "%s/@%s", base, attrname);

#ifndef NO_CCS
		if (ccs_get(ccsfd, ccspath, &attr) != 0) {
#else
		if (conf_get(ccspath, &attr) != 0) {
#endif

			if (flags & (RA_REQUIRED | RA_PRIMARY)) {
				/* Missing required attribute.  We're done. */
				free(attrname);
				destroy_resource(res);
				return NULL;
			}

			if (!(flags & RA_INHERIT)) {
				free(attrname);
				continue;
			}
		}

#ifdef MDEBUG /* We don't record allocs from ccs */
		if (attr)
			str_localize(attr);
#endif
		found = 1;

		/*
		   If we are supposed to inherit and we don't have an
		   instance of the specified attribute in CCS, then we
		   keep the inherit flag and use it as the attribute.

		   However, if we _do_ have the attribute for this instance,
		   we drop the inherit flag and use the attribute.
		 */
		if (flags & RA_INHERIT) {
		       	if (attr) {
				flags &= ~RA_INHERIT;
			} else {
				attr = strdup(rule->rr_attrs[x].ra_value);
				if (!attr) {
					destroy_resource(res);
					free(attrname);
					return NULL;
				}
			}
		}

		/*
		   Store the attribute.  We'll ensure all required
		   attributes are present soon.
		 */
		if (attrname && attr)
			store_attribute(&res->r_attrs, attrname, attr, flags);
	}

	if (!found) {
		//printf("No attributes found for %s\n", base);
		destroy_resource(res);
		return NULL;
	}

	res->r_actions = act_dup(rule->rr_actions);

	return res;
}


/**
   Read all resources in the resource manager block in CCS.

   @param ccsfd		File descriptor connected to CCS.
   @param reslist	Empty list to fill with resources.
   @param rulelist	List of rules to use when searching CCS.
   @return		0 on success, nonzero on failure.
 */
int
load_resources(int ccsfd, resource_t **reslist, resource_rule_t **rulelist)
{
	int resID = 0;
	resource_t *newres;
	resource_rule_t *currule;
	char tok[256];

	list_do(rulelist, currule) {

		for (resID = 1; ; resID++) {
			snprintf(tok, sizeof(tok), RESOURCE_BASE "/%s[%d]",
				 currule->rr_type, resID);

			newres = load_resource(ccsfd, currule, tok);
			if (!newres)
				break;

		       if (store_resource(reslist, newres) != 0) {
	       		       printf("Error storing %s resource\n",
				      newres->r_rule->rr_type);
			       destroy_resource(newres);
		       }

		       /* Just information */
		       newres->r_flags = RF_DEFINED;
		}
	} while (!list_done(rulelist, currule));

	return 0;
}


int
test_func(int argc, char **argv)
{
	fod_t *domains = NULL;
	resource_rule_t *rulelist = NULL, *currule;
	resource_t *reslist = NULL, *curres;
	resource_node_t *tree = NULL;
	int ccsfd;

	printf("Running in test mode.\n");

	load_resource_rules(&rulelist);

       	ccsfd = ccs_lock();

	if (ccsfd == FAIL) {
		printf("Couldn't connect to ccs\n");
		return 0;
	}

	construct_domains(ccsfd, &domains);
	load_resources(ccsfd, &reslist, &rulelist);
	build_resource_tree(ccsfd, &tree, &rulelist, &reslist);

	if (argc == 1) {
		printf("=== Resource XML Rules ===\n");
		list_do(&rulelist, currule) {
			print_resource_rule(currule);
		} while (!list_done(&rulelist, currule));

		printf("=== Resources from CCS ===\n");
		list_do(&reslist, curres) {
			print_resource(curres);
		} while (!list_done(&reslist, curres));

		printf("=== Resource Tree ===\n");
		print_resource_tree(&tree);

		printf("=== Failover Domains ===\n");
		print_domains(&domains);
	}

	ccs_unlock(ccsfd);

	if (argc < 4) {
		printf("to play with resources:\n");
		printf("   %s <start|stop|status> <type> <reference>\n",
		       argv[0]);
		return 0;
	}

	curres = find_resource_by_ref(&reslist, argv[2], argv[3]);
	if (!curres) {
		printf("No resource %s of type %s found\n",
		       argv[3], argv[2]);
		return 0;
	}

	if (!strcmp(argv[1], "start")) {
		printf("Starting %s...\n", argv[3]);

		if (res_start(&tree, curres, NULL)) {
			printf("Failed to start %s\n", argv[3]);
			return 1;
		}
		printf("Start of %s complete\n", argv[3]);
		return 0;
	} else if (!strcmp(argv[1], "stop")) {
		printf("Stopping %s...\n", argv[3]);

		if (res_stop(&tree, curres, NULL)) {
			return 1;
		}
		printf("Stop of %s complete\n", argv[3]);
		return 0;
	} else if (!strcmp(argv[1], "status")) {
		printf("Checking status of %s...\n", argv[3]);

		if (res_status(&tree, curres, NULL)) {
			printf("Status check of %s failed\n", argv[3]);
			return 1;
		}
		printf("Status of %s is good\n", argv[3]);
		return 0;
	}

	destroy_resource_tree(&tree);
	destroy_resources(&reslist);
	destroy_resource_rules(&rulelist);

#ifdef MDEBUG
	dump_mem_table();
#endif

	return 0;
}
