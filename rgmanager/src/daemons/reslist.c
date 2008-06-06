#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>
#include <ccs.h>
#include <stdlib.h>
#include <stdio.h>
#include <resgroup.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <list.h>
#include <restart_counter.h>
#include <reslist.h>
#include <pthread.h>
#ifndef NO_CCS
#include <clulog.h>
#endif


char *attr_value(resource_node_t *node, char *attrname);
char *rg_attr_value(resource_node_t *node, char *attrname);

void
res_build_name(char *buf, size_t buflen, resource_t *res)
{
	snprintf(buf, buflen, "%s:%s", res->r_rule->rr_type,
		 res->r_attrs[0].ra_value);
}

/**
   Find and determine an attribute's value. 

   @param res		Resource node to look examine
   @param attrname	Attribute to retrieve.
   @return 		value of attribute or NULL if not found
 */
char *
res_attr_value(resource_t *res, char *attrname)
{
	resource_attr_t *ra;
	int x;

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {
		if (strcmp(attrname, res->r_attrs[x].ra_name))
			continue;

		ra = &res->r_attrs[x];

		if (ra->ra_flags & RA_INHERIT)
			/* Can't check inherited resources */
			return NULL;

		return ra->ra_value;
	}

	return NULL;
}


/**
   Find and determine an attribute's value.  Takes into account inherited
   attribute flag, and append attribute flag, which isn't implemented yet.

   @param node		Resource tree node to look examine
   @param attrname	Attribute to retrieve.
   @param ptype		Resource type to look for (if inheritance)
   @return 		value of attribute or NULL if not found
 */
static char *
_attr_value(resource_node_t *node, char *attrname, char *ptype)
{
	resource_t *res;
	resource_attr_t *ra;
	char *c, p_type[32];
	ssize_t len;
	int x;

	if (!node)
		return NULL;

	res = node->rn_resource;

	/* Go up the tree if it's not the right parent type */
	if (ptype && strcmp(res->r_rule->rr_type, ptype))
		return _attr_value(node->rn_parent, attrname, ptype);

	for (x = 0; res->r_attrs && res->r_attrs[x].ra_name; x++) {
		if (strcmp(attrname, res->r_attrs[x].ra_name))
			continue;

		ra = &res->r_attrs[x];

		if (!(ra->ra_flags & RA_INHERIT))
			return ra->ra_value;
		/* 
		   Handle resource_type%field to be more precise, so we
		   don't have to worry about this being a child
		   of an unexpected type.  E.g. lots of things have the
		   "name" attribute.
		 */
		c = strchr(ra->ra_value, '%');
		if (!c) {
			/* Someone doesn't care or uses older
			   semantics on inheritance */
			return _attr_value(node->rn_parent, ra->ra_value,
					   NULL);
		}
		
		len = (c - ra->ra_value);
		memset(p_type, 0, sizeof(p_type));
		memcpy(p_type, ra->ra_value, len);
		
		/* Skip the "%" and recurse */
		return _attr_value(node->rn_parent, ++c, p_type);
	}

	return NULL;
}


char *
attr_value(resource_node_t *node, char *attrname)
{
	return _attr_value(node, attrname, NULL);
}


/**
  Run to the top of the tree.  Used to determine certain attributes of the
  resource group in-line, during resource tree operations.
 */
char *
rg_attr_value(resource_node_t *node, char *attrname)
{
	for (; node->rn_parent; node = node->rn_parent);
	return res_attr_value(node->rn_resource, attrname);
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

  @param left	Left resource
  @param right	Right resource	
  @return	-1 on different resource, 0 if the same, 1 if different,
		2 if different, but only safe resources are different

 */
int
rescmp(resource_t *left, resource_t *right)
{
	int x, y = 0, found = 0, ret = 0;


	/* Completely different resource class... */
	if (strcmp(left->r_rule->rr_type, right->r_rule->rr_type)) {
		return -1;
	}

	/*
	printf("Comparing %s:%s to %s:%s\n",
	       left->r_rule->rr_type, left->r_attrs[0].ra_value,
	       right->r_rule->rr_type, right->r_attrs[0].ra_value)
	 */

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
				/*
				printf("* flags differ %08x vs %08x\n",
				       left->r_attrs[x].ra_flags,
				       right->r_attrs[y].ra_flags);
				 */
				return 1;
			}

			if (strcmp(right->r_attrs[y].ra_value,
				   left->r_attrs[x].ra_value)) {
				/* Different attribute value. */
				/*
				printf("* different value for attr '%s':"
				       " '%s' vs '%s'",
				       right->r_attrs[y].ra_name,
				       left->r_attrs[x].ra_value,
				       right->r_attrs[y].ra_value);
				 */
				if (left->r_attrs[x].ra_flags & RA_RECONFIG) {
					/* printf(" [SAFE]\n"); */
					ret = 2;
			 	} else {
					/* printf("\n"); */
					return 1;
				}
			}
		}

		/* Attribute missing -> different attribute value. */
		if (!found) {
			/*
			printf("* Attribute '%s' deleted\n",
			       left->r_attrs[x].ra_name);
			 */
			return 1;
		}
	}

	/* Different attribute count */
	if (x != y) {
		/* printf("* Attribute count differ (attributes added!) "); */
		return 1;
	}

	/* All the same */
	return ret;
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
   Find a root resource by ref (service, usually).  No name is required.
   Only one type of root resource may exist because of the primary
   attribute flag

   @param reslist	List of resources to traverse.
   @param ref		Reference
   @return		Resource matching type/ref or NULL if none.
 */   
resource_t *
find_root_by_ref(resource_t **reslist, char *ref)
{
	resource_t *curr;
	char ref_buf[128];
	char *type;
	char *name = ref;
	int x;

	snprintf(ref_buf, sizeof(ref_buf), "%s", ref);

	type = ref_buf;
	if ((name = strchr(ref_buf, ':'))) {
		*name = 0;
		name++;
	} else {
		/* Default type */
		type = "service";
		name = ref;
	}

	list_do(reslist, curr) {

		/*
		   This should be one operation - the primary attr
		   is generally at the head of the array.
		 */
		for (x = 0; curr->r_attrs && curr->r_attrs[x].ra_name;
		     x++) {
			if (strcmp(type, curr->r_rule->rr_type))
				continue;
			if (!(curr->r_attrs[x].ra_flags & RA_PRIMARY))
				continue;
			if (strcmp(name, curr->r_attrs[x].ra_value))
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
			if (!(newres->r_attrs[x].ra_flags &
			    (RA_PRIMARY | RA_UNIQUE)))
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
#ifdef NO_CCS
					printf("Error: "
                                               "%s attribute collision. "
                                               "type=%s attr=%s value=%s\n",
					       (newres->r_attrs[x].ra_flags&
                                                RA_PRIMARY)?"Primary":
                                               "Unique",
					       newres->r_rule->rr_type,
					       newres->r_attrs[x].ra_name,
					       newres->r_attrs[x].ra_value
					       );
#else 
					clulog(LOG_ERR,
                                               "%s attribute collision. "
                                               "type=%s attr=%s value=%s\n",
					       (newres->r_attrs[x].ra_flags&
                                                RA_PRIMARY)?"Primary":
                                               "Unique",
					       newres->r_rule->rr_type,
					       newres->r_attrs[x].ra_name,
					       newres->r_attrs[x].ra_value
					       );
#endif
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
	xmlNodePtr node;
	size_t size = 0;
	int nnv = 0;

	obj = xmlXPathEvalExpression((unsigned char *)query, ctx);
	if (!obj)
		return NULL;
	if (!obj->nodesetval)
		goto out;
	if (obj->nodesetval->nodeNr <= 0)
		goto out;

	node = obj->nodesetval->nodeTab[0];
	if(!node)
		goto out;

	if (((node->type == XML_ATTRIBUTE_NODE) && strstr(query, "@*")) ||
	    ((node->type == XML_ELEMENT_NODE) && strstr(query, "child::*"))){
		if (node->children && node->children->content)
	  		size = strlen((char *)node->children->content)+
				      strlen((char *)node->name)+2;
		else 
			size = strlen((char *)node->name)+2;
		nnv = 1;
	} else {
		if (node->children && node->children->content) {
			size = strlen((char *)node->children->content)+1;
		} else {
			goto out;
		}
	}

	val = (char *)malloc(size);
	if(!val)
		goto out;
	memset(val, 0, size);
	if (nnv) {
		sprintf(val, "%s=%s", node->name, node->children ?
			(char *)node->children->content:"");
	} else {
		sprintf(val, "%s", node->children ? node->children->content :
			node->name);
	}

	ret = val;
out:
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

	if (res->r_actions) {
		/* Don't free the strings; they're part of the rule */
		free(res->r_actions);
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
	if (res->r_flags & RF_INLINE)
		printf(" [INLINE]");
	if (res->r_flags & RF_NEEDSTART)
		printf(" [NEEDSTART]");
	if (res->r_flags & RF_NEEDSTOP)
		printf(" [NEEDSTOP]");
	if (res->r_flags & RF_COMMON)
		printf(" [COMMON]");
	if (res->r_flags & RF_RECONFIG)
		printf(" [RECONFIG]");
	printf("\n");

	if (res->r_rule->rr_maxrefs)
		printf("Instances: %d/%d\n", res->r_refs,
		       res->r_rule->rr_maxrefs);
	if (res->r_rule->rr_agent)
		printf("Agent: %s\n", basename(res->r_rule->rr_agent));
	
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
		if (res->r_attrs[x].ra_flags & RA_RECONFIG)
			printf(" reconfig");
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


/* Copied from resrules.c -- _get_actions */
void
_get_actions_ccs(int ccsfd, char *base, resource_t *res)
{
	char xpath[256];
	int idx = 0;
	char *act, *ret;
	int interval, timeout, depth;

	do {
		/* setting these to -1 prevents overwriting with 0 */
		interval = -1;
		depth = -1;
		act = NULL;
		timeout = -1;

		snprintf(xpath, sizeof(xpath),
			 "%s/action[%d]/@name", base, ++idx);

#ifndef NO_CCS
		if (ccs_get(ccsfd, xpath, &act) != 0)
#else
		if (conf_get(xpath, &act) != 0)
#endif
			break;

		snprintf(xpath, sizeof(xpath),
			 "%s/action[%d]/@timeout", base, idx);
#ifndef NO_CCS
		if (ccs_get(ccsfd, xpath, &ret) == 0 && ret) {
#else
		if (conf_get(xpath, &ret) == 0 && ret) {
#endif
			timeout = expand_time(ret);
			if (timeout < 0)
				timeout = 0;
			free(ret);
		}

		snprintf(xpath, sizeof(xpath),
			 "%s/action[%d]/@interval", base, idx);
#ifndef NO_CCS
		if (ccs_get(ccsfd, xpath, &ret) == 0 && ret) {
#else
		if (conf_get(xpath, &ret) == 0 && ret) {
#endif
			interval = expand_time(ret);
			if (interval < 0)
				interval = 0;
			free(ret);
		}

		if (!strcmp(act, "status") || !strcmp(act, "monitor")) {
			snprintf(xpath, sizeof(xpath),
				 "%s/action[%d]/@depth", base, idx);
#ifndef NO_CCS
			if (ccs_get(ccsfd, xpath, &ret) == 0 && ret) {
#else
			if (conf_get(xpath, &ret) == 0 && ret) {
#endif
				depth = atoi(ret);
				if (depth < 0)
					depth = 0;
				
				/* */
				if (ret[0] == '*')
					depth = -1;
				free(ret);
			}
		}

		if (store_action(&res->r_actions, act, depth, timeout,
				 interval) != 0)
			free(act);
	} while (1);
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
				/*
				   If we don't have the inherit flag, see if
				   we have a value anyway.  If we do,
				   this value is the default value, and
				   should be used.
				 */
				if (!rule->rr_attrs[x].ra_value) {
					free(attrname);
					continue;
				}

				/* Copy default value from resource rule */
				attr = strdup(rule->rr_attrs[x].ra_value);
			}
		}

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
		destroy_resource(res);
		return NULL;
	}

	res->r_actions = act_dup(rule->rr_actions);
	_get_actions_ccs(ccsfd, base, res);

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
#ifdef NO_CCS
	       		       printf("Error storing %s resource\n",
				      newres->r_rule->rr_type);
#else
	       		       clulog(LOG_ERR,
				      "Error storing %s resource\n",
				      newres->r_rule->rr_type);
#endif

			       destroy_resource(newres);
		       }

		       /* Just information */
		       newres->r_flags = RF_DEFINED;
		}
	} while (!list_done(rulelist, currule));

	return 0;
}

