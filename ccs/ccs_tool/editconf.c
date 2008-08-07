#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <libxml/tree.h>

#ifdef LEGACY_CODE
#include "update.h"
#endif

#define MAX_NODES 256

char *prog_name = "ccs_tool";

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
	exit(EXIT_FAILURE); \
} while (0)


struct option_info
{
	char *name;
	char *altname;
	char *votes;
	char *nodeid;
	char *mcast_addr;
	char *fence_type;
	char *configfile;
	char *outputfile;
	int  do_delete;
	int  tell_ccsd;
	int  force_ccsd;
};

static void config_usage(int rw)
{
	fprintf(stderr, " -c --configfile    Name of configuration file (" DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE ")\n");
	if (rw)
	{
		fprintf(stderr, " -o --outputfile    Name of output file (defaults to same as --configfile)\n");
		fprintf(stderr, " -C --no_ccs        Don't tell CCSD about this change\n");
		fprintf(stderr, "                    default: run \"ccs_tool update\" if file is updated in place)\n");
		fprintf(stderr, " -F --force_ccs     Force \"ccs_tool upgrade\" even if input & output files differ\n");
	}
}

static void help_usage(void)
{
	fprintf(stderr, " -h --help          Display this help text\n");
}

static void list_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [options]\n", prog_name, name);
	fprintf(stderr, " -v --verbose       Print all properties of the item\n");
	config_usage(0);
	help_usage();

	exit(0);
}

static void create_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [-2] <clustername>\n", prog_name, name);
	fprintf(stderr, " -2                 Create a 2-node cman cluster config file\n");
	config_usage(0);
	help_usage();
	fprintf(stderr, "\n"
	  "Note that \"create\" on its own will not create a valid configuration file.\n"
	  "Fence agents and nodes will need to be added to it before handing it over\n"
	  "to ccsd.\n"
	  "\n"
	  "eg:\n"
	  "  ccs_tool create MyCluster\n"
	  "  ccs_tool addfence apc fence_apc ipaddr=apc.domain.net user=apc password=apc\n"
	  "  ccs_tool addnode node1 -n 1 -f apc port=1\n"
	  "  ccs_tool addnode node2 -n 2 -f apc port=2\n"
	  "  ccs_tool addnode node3 -n 3 -f apc port=3\n"
	  "  ccs_tool addnode node4 -n 4 -f apc port=4\n"
          "\n");

	exit(0);
}

static void addfence_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [options] <name> <agent> [param=value]\n", prog_name, name);
	config_usage(1);
	help_usage();

	exit(0);
}

static void delfence_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [options] <name>\n", prog_name, name);
	config_usage(1);
	help_usage();
	fprintf(stderr, "\n");
	fprintf(stderr, "%s will allow you to remove a fence device that is in use by nodes.\n", name);
	fprintf(stderr, "This is to allow changes to be made, but be aware that it may produce an\n");
	fprintf(stderr, "invalid configuration file if you don't add it back in again.\n");

	exit(0);
}

static void delnode_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [options] <name>\n", prog_name, name);
	config_usage(1);
	help_usage();

	exit(0);
}

static void addnodeid_usage(const char *name)
{
	fprintf(stderr, "Add node IDs to all nodes in the config file that don't have them.\n");
	fprintf(stderr, "Nodes with IDs will not be afftected, so you can run this as many times\n");
	fprintf(stderr, "as you like without doing any harm.\n");
	fprintf(stderr, "It will optionally add a multicast address to the cluster config too.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s %s [options] <name>\n", prog_name, name);
	fprintf(stderr, " -n --nodeid        Nodeid to start with (default 1)\n");
	fprintf(stderr, " -m --multicast     Set or change the multicast address\n");
	fprintf(stderr, " -v --verbose       Print nodeids that are assigned\n");
	config_usage(1);
	help_usage();

	exit(0);
}

static void addnode_usage(const char *name)
{
	fprintf(stderr, "Usage: %s %s [options] <nodename> [<fencearg>=<value>]...\n", prog_name, name);
	fprintf(stderr, " -n --nodeid        Nodeid (required)\n");
	fprintf(stderr, " -v --votes         Number of votes for this node (default 1)\n");
	fprintf(stderr, " -a --altname       Alternative name/interface for multihomed hosts\n");
	fprintf(stderr, " -f --fence_type    Type of fencing to use\n");
	config_usage(1);
	help_usage();

	fprintf(stderr, "\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Add a new node to default configuration file:\n");
	fprintf(stderr, "  %s %s -n 1 -f manual ipaddr=newnode\n", prog_name, name);
	fprintf(stderr, "\n");
	fprintf(stderr, "Add a new node and dump config file to stdout rather than save it\n");
	fprintf(stderr, "  %s %s -n 2 -f apc -o- newnode.temp.net port=1\n", prog_name, name);

	exit(0);
}

/* Is it really ?
 * Actually, we don't check that this is a valid multicast address(!),
 * merely that it is a valid IP[46] address.
 */
static int valid_mcast_addr(char *mcast)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	int ret;

        memset(&ahints, 0, sizeof(ahints));

        ret = getaddrinfo(mcast, NULL, &ahints, &ainfo);
	if (ret) {
		freeaddrinfo(ainfo);
		return 0;
	}
	return 1;
}

static void save_file(xmlDoc *doc, struct option_info *ninfo)
{
	char tmpfile[strlen(ninfo->outputfile)+5];
	char oldfile[strlen(ninfo->outputfile)+5];
	int using_stdout = 0;
	mode_t old_mode;
	int ret;

	old_mode = umask(026);

	if (strcmp(ninfo->outputfile, "-") == 0)
		using_stdout = 1;

	/*
	 * Save it to a temp file before moving the old one out of the way
	 */
	if (!using_stdout)
	{
		snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", ninfo->outputfile);
		snprintf(oldfile, sizeof(oldfile), "%s.old", ninfo->outputfile);
	}
	else
	{
		strcpy(tmpfile, ninfo->outputfile);
	}

	xmlKeepBlanksDefault(0);
	ret = xmlSaveFormatFile(tmpfile, doc, 1);
	if (ret == -1)
		die("Error writing new config file %s", ninfo->outputfile);

	if (!using_stdout)
	{
		if (rename(ninfo->outputfile, oldfile) == -1 && errno != ENOENT)
			die("Can't move old config file out of the way\n");

		if (rename(tmpfile, ninfo->outputfile))
		{
			perror("Error renaming new file to its real filename");

			/* Drat, that failed, try to put the old one back */
			if (rename(oldfile, ninfo->outputfile))
				die("Can't move old config fileback in place - clean up after me please\n");
		}
	}

#ifdef LEGACY_CODE
	/* Try to tell ccsd if needed */
	if ((strcmp(ninfo->configfile, ninfo->outputfile) == 0 && ninfo->tell_ccsd) ||
	    ninfo->force_ccsd)
	{
		printf("running ccs_tool update...\n");
		update(ninfo->outputfile);
	}
#endif

	/* free the document */
	xmlFreeDoc(doc);

	umask(old_mode);
}

static void validate_int_arg(char argopt, char *arg)
{
	char *tmp;
	int val;

	val = strtol(arg, &tmp, 10);
	if (tmp == arg || tmp != arg + strlen(arg))
		die("argument to %c (%s) is not an integer", argopt, arg);

	if (val < 0)
		die("argument to %c cannot be negative", argopt);
}

/* Get the config_version string from the file */
static xmlChar *find_version(xmlNode *root)
{
	if (xmlHasProp(root, BAD_CAST "config_version"))
	{
		xmlChar *ver;

		ver = xmlGetProp(root, BAD_CAST "config_version");
		return ver;
	}
	return NULL;
}

/* Get the cluster name string from the file */
static xmlChar *cluster_name(xmlNode *root)
{
	if (xmlHasProp(root, BAD_CAST "name"))
	{
		xmlChar *ver;

		ver = xmlGetProp(root, BAD_CAST "name");
		return ver;
	}
	return NULL;
}

static void increment_version(xmlNode *root_element)
{
	int ver;
	unsigned char *version_string;
	char newver[32];

	/* Increment version */
	version_string = find_version(root_element);
	if (!version_string)
		die("Can't find \"config_version\" in config file\n");

	ver = atoi((char *)version_string);
	snprintf(newver, sizeof(newver), "%d", ++ver);
	xmlSetProp(root_element, BAD_CAST "config_version", BAD_CAST newver);
}

static xmlNode *findnode(xmlNode *root, char *name)
{
	xmlNode *cur_node;

	for (cur_node = root->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, name)==0)
		{
			return cur_node;
		}
	}
	return NULL;
}

/* Return the fence type name (& node) for a cluster node */
static xmlChar *get_fence_type(xmlNode *clusternode, xmlNode **fencenode)
{
	xmlNode *f;

	f = findnode(clusternode, "fence");
	if (f)
	{
		f = findnode(f, "method");
		if (f)
		{
			f = findnode(f, "device");
			*fencenode = f;
			return xmlGetProp(f, BAD_CAST "name");
		}
	}
	return NULL;
}

/* Check the fence type exists under <fencedevices> */
static xmlNode *valid_fence_type(xmlNode *root, char *fencetype)
{
	xmlNode *devs;
	xmlNode *cur_node;

	devs = findnode(root, "fencedevices");
	if (!devs)
		return NULL;

	for (cur_node = devs->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "fencedevice") == 0)
		{
			xmlChar *name = xmlGetProp(cur_node, BAD_CAST "name");
			if (strcmp((char *)name, fencetype) == 0)
				return cur_node;
		}
	}
	return NULL;
}

/* Check the nodeid is not already in use by another node */
static xmlNode *get_by_nodeid(xmlNode *root, int nodeid)
{
	xmlNode *cnodes;
	xmlNode *cur_node;

	cnodes = findnode(root, "clusternodes");
	if (!cnodes)
		return NULL;

	for (cur_node = cnodes->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "clusternode") == 0)
		{
			xmlChar *idstring = xmlGetProp(cur_node, BAD_CAST "nodeid");
			if (idstring && atoi((char *)idstring) == nodeid)
				return cur_node;
		}
	}
	return NULL;
}


/* Get the multicast address node.
 */
static xmlNode *find_multicast_addr(xmlNode *clusternodes)
{
	xmlNode *clnode = findnode(clusternodes, "cman");
	if (clnode)
	{
		xmlNode *mcast = findnode(clnode, "multicast");
		return mcast;
	}
	return NULL;
}

static xmlNode *find_node(xmlNode *clusternodes, char *nodename)
{
	xmlNode *cur_node;

	for (cur_node = clusternodes->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "clusternode") == 0)
		{
			xmlChar *name = xmlGetProp(cur_node, BAD_CAST "name");
			if (strcmp((char *)name, nodename) == 0)
				return cur_node;
		}
	}
	return NULL;
}

/* Print name=value pairs for a (n XML) node.
 * "ignore" is a string to ignore if present as a property (probably already printed on the main line)
 */
static int print_properties(xmlNode *node, char *prefix, char *ignore, char *ignore2)
{
	xmlAttr *attr;
	int done_prefix = 0;

	for (attr = node->properties; attr; attr = attr->next)
	{
		/* Don't print "name=" */
		if (strcmp((char *)attr->name, "name") &&
		    strcmp((char *)attr->name, ignore) &&
		    strcmp((char *)attr->name, ignore2)
			)
		{
			if (!done_prefix)
			{
				done_prefix = 1;
				printf("%s", prefix);
			}
			printf(" %s=%s", attr->name, xmlGetProp(node, attr->name));
		}
	}
	if (done_prefix)
		printf("\n");
	return done_prefix;
}

/* Add name=value pairs from the commandline as properties to a node */
static void add_fence_args(xmlNode *fencenode, int argc, char **argv, int optind)
{
	int i;

	for (i = optind; i<argc; i++)
	{
		char *prop;
		char *value;
		char *equals;

		prop = strdup(argv[i]);
		equals = strchr(prop, '=');
		if (!equals)
			die("option '%s' is not opt=value pair\n", prop);

		value = equals+1;
		*equals = '\0';

		/* "name" is used for the fence type itself, so this is just
		 *  to protect the user from their own stupidity
		 */
		if (strcmp(prop, "name") == 0)
			die("Can't use \"name\" as a fence argument name\n");

		xmlSetProp(fencenode, BAD_CAST prop, BAD_CAST value);
		free(prop);
	}
}

static void add_clusternode(xmlNode *root_element, struct option_info *ninfo,
			    int argc, char **argv, int optind)
{
	xmlNode *clusternodes;
	xmlNode *newnode;

	xmlNode *newfence;
	xmlNode *newfencemethod;
	xmlNode *newfencedevice;

	clusternodes = findnode(root_element, "clusternodes");
	if (!clusternodes)
		die("Can't find \"clusternodes\" in %s\n", ninfo->configfile);

	/* Don't allow duplicate node names */
	if (find_node(clusternodes, ninfo->name))
		die("node %s already exists in %s\n", ninfo->name, ninfo->configfile);

	/* Check for duplicate node ID */
	if (!ninfo->nodeid)
		die("nodeid not specified\n");

	if (get_by_nodeid(root_element, atoi((char *)ninfo->nodeid)))
		die("nodeid %s already in use\n", ninfo->nodeid);

        /* Don't allow random fence types */
	if (!valid_fence_type(root_element, ninfo->fence_type))
		die("fence type '%s' not known\n", ninfo->fence_type);

	/* Add the new node */
	newnode = xmlNewNode(NULL, BAD_CAST "clusternode");
	xmlSetProp(newnode, BAD_CAST "name", BAD_CAST ninfo->name);
	xmlSetProp(newnode, BAD_CAST "votes", BAD_CAST ninfo->votes);
	xmlSetProp(newnode, BAD_CAST "nodeid", BAD_CAST ninfo->nodeid);
	xmlAddChild(clusternodes, newnode);

	if (ninfo->altname)
	{
		xmlNode *altnode;

		altnode = xmlNewNode(NULL, BAD_CAST "altname");
		xmlSetProp(altnode, BAD_CAST "name", BAD_CAST ninfo->altname);
		xmlAddChild(newnode, altnode);
	}

	/* Add the fence attributes */
	newfence = xmlNewNode(NULL, BAD_CAST "fence");
	newfencemethod = xmlNewNode(NULL, BAD_CAST "method");
	xmlSetProp(newfencemethod, BAD_CAST "name", BAD_CAST "single");

	newfencedevice = xmlNewNode(NULL, BAD_CAST "device");
	xmlSetProp(newfencedevice, BAD_CAST "name", BAD_CAST ninfo->fence_type);

	/* Add name=value options */
	add_fence_args(newfencedevice, argc, argv, optind+1);

	xmlAddChild(newnode, newfence);
	xmlAddChild(newfence, newfencemethod);
	xmlAddChild(newfencemethod, newfencedevice);
}

static xmlDoc *open_configfile(struct option_info *ninfo)
{
	xmlDoc *doc;

	/* Init libxml */
	xmlInitParser();
	LIBXML_TEST_VERSION;

	if (!ninfo->configfile)
		ninfo->configfile = DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE;
	if (!ninfo->outputfile)
		ninfo->outputfile = ninfo->configfile;

	/* Load XML document */
	doc = xmlParseFile(ninfo->configfile);
	if (doc == NULL)
		die("Error: unable to parse requested configuration file\n");

	return doc;

}

static void del_clusternode(xmlNode *root_element, struct option_info *ninfo)
{
	xmlNode *clusternodes;
	xmlNode *oldnode;

	clusternodes = findnode(root_element, "clusternodes");
	if (!clusternodes)
	{
		fprintf(stderr, "Can't find \"clusternodes\" in %s\n", ninfo->configfile);
		exit(1);
	}

	oldnode = find_node(clusternodes, ninfo->name);
	if (!oldnode)
	{
		fprintf(stderr, "node %s does not exist in %s\n", ninfo->name, ninfo->configfile);
		exit(1);
	}

	xmlUnlinkNode(oldnode);
}

struct option addnode_options[] =
{
      { "votes", required_argument, NULL, 'v'},
      { "nodeid", required_argument, NULL, 'n'},
      { "altname", required_argument, NULL, 'a'},
      { "fence_type", required_argument, NULL, 'f'},
      { "outputfile", required_argument, NULL, 'o'},
      { "configfile", required_argument, NULL, 'c'},
      { "no_ccs", no_argument, NULL, 'C'},
      { "force_ccs", no_argument, NULL, 'F'},
      { NULL, 0, NULL, 0 },
};

struct option delnode_options[] =
{
      { "outputfile", required_argument, NULL, 'o'},
      { "configfile", required_argument, NULL, 'c'},
      { "no_ccs", no_argument, NULL, 'C'},
      { "force_ccs", no_argument, NULL, 'F'},
      { NULL, 0, NULL, 0 },
};

struct option addfence_options[] =
{
      { "outputfile", required_argument, NULL, 'o'},
      { "configfile", required_argument, NULL, 'c'},
      { "no_ccs", no_argument, NULL, 'C'},
      { "force_ccs", no_argument, NULL, 'F'},
      { NULL, 0, NULL, 0 },
};

struct option addnodeid_options[] =
{
      { "outputfile", required_argument, NULL, 'o'},
      { "configfile", required_argument, NULL, 'c'},
      { "multicast", required_argument, NULL, 'm'},
      { "nodeid", no_argument, NULL, 'n'},
      { "verbose", no_argument, NULL, 'v'},
      { NULL, 0, NULL, 0 },
};

struct option list_options[] =
{
      { "configfile", required_argument, NULL, 'c'},
      { "verbose", no_argument, NULL, 'v'},
      { NULL, 0, NULL, 0 },
};


static int next_nodeid(int startid, int *nodeids, int nodecount)
{
	int i;
	int nextid = startid;

retry:
	for (i=0; i<nodecount; i++)
	{
		if (nodeids[i] == nextid)
		{
			nextid++;
			goto retry;
		}
	}

	return nextid;
}

void add_nodeids(int argc, char **argv)
{
	struct option_info ninfo;
	unsigned char *nodenames[MAX_NODES];
	xmlDoc *doc;
	xmlNode *root_element;
	xmlNode *clusternodes;
	xmlNode *cur_node;
	xmlNode *mcast;
	int  verbose = 0;
	int  opt;
	int  i;
	int  nodenumbers[MAX_NODES];
	int  nodeidx;
	int  totalnodes;
	int  nextid;

	memset(nodenames, 0, sizeof(nodenames));
	memset(nodenumbers, 0, sizeof(nodenumbers));
	memset(&ninfo, 0, sizeof(ninfo));
	ninfo.nodeid = "1";

	while ( (opt = getopt_long(argc, argv, "n:o:c:m:vh?", addnodeid_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'n':
			validate_int_arg(opt, optarg);
			ninfo.nodeid = strdup(optarg);
			break;

		case 'c':
			ninfo.configfile = strdup(optarg);
			break;

		case 'o':
			ninfo.outputfile = strdup(optarg);
			break;

		case 'm':
			if (!valid_mcast_addr(optarg)) {
				fprintf(stderr, "%s is not a valid multicast address\n", optarg);
				return;
			}
			ninfo.mcast_addr = strdup(optarg);
			break;

		case 'v':
			verbose++;
			break;

		case '?':
		default:
			addnodeid_usage(argv[0]);
		}
	}

	doc = open_configfile(&ninfo);

	root_element = xmlDocGetRootElement(doc);

	increment_version(root_element);

	/* Warn if the cluster doesn't have a multicast address */
	mcast = find_multicast_addr(root_element);
	if (!mcast & !ninfo.mcast_addr) {
		fprintf(stderr, "\nWARNING: The cluster does not have a multicast address.\n");
		fprintf(stderr, "A default will be assigned a run-time which might not suit your installation\n\n");
	}

	if (ninfo.mcast_addr) {
		if (!mcast) {
			xmlNode *cman = xmlNewNode(NULL, BAD_CAST "cman");
			mcast = xmlNewNode(NULL, BAD_CAST "multicast");

			xmlAddChild(cman, mcast);
			xmlAddChild(root_element, cman);
		}
		xmlSetProp(mcast, BAD_CAST "addr", BAD_CAST ninfo.mcast_addr);
	}

	/* Get a list of nodes that /do/ have nodeids so we don't generate
	   any duplicates */
	nodeidx=0;
	clusternodes = findnode(root_element, "clusternodes");
	if (!clusternodes)
		die("Can't find \"clusternodes\" in %s\n", ninfo.configfile);


	for (cur_node = clusternodes->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "clusternode") == 0)
		{
			xmlChar *name   = xmlGetProp(cur_node, BAD_CAST "name");
			xmlChar *nodeid = xmlGetProp(cur_node, BAD_CAST "nodeid");
			nodenames[nodeidx]  = name;
			if (nodeid)
				nodenumbers[nodeidx] = atoi((char*)nodeid);
			nodeidx++;
		}
	}
	totalnodes = nodeidx;

	/* Loop round nodes adding nodeIDs where they don't exist. */
	nextid = next_nodeid(atoi(ninfo.nodeid), nodenumbers, totalnodes);
	for (i=0; i<totalnodes; i++)
	{
		if (nodenumbers[i] == 0)
		{
			nodenumbers[i] = nextid;
			nextid = next_nodeid(nextid, nodenumbers, totalnodes);
			if (verbose)
				fprintf(stderr, "Node %s now has id %d\n", nodenames[i], nodenumbers[i]);
		}
	}

	/* Now write them into the tree */
	nodeidx = 0;
	for (cur_node = clusternodes->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "clusternode") == 0)
		{
			char tmp[80];
			xmlChar *name = xmlGetProp(cur_node, BAD_CAST "name");

			assert(strcmp((char*)nodenames[nodeidx], (char*)name) == 0);

			sprintf(tmp, "%d", nodenumbers[nodeidx]);
			xmlSetProp(cur_node, BAD_CAST "nodeid", BAD_CAST tmp);
			nodeidx++;
		}
	}


	/* Write it out */
	save_file(doc, &ninfo);

	/* Shutdown libxml */
	xmlCleanupParser();
}

void add_node(int argc, char **argv)
{
	struct option_info ninfo;
	int opt;
	xmlDoc *doc;
	xmlNode *root_element;

	memset(&ninfo, 0, sizeof(ninfo));
	ninfo.tell_ccsd = 1;
	ninfo.votes = "1";

	while ( (opt = getopt_long(argc, argv, "v:n:a:f:o:c:CFh?", addnode_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'v':
			validate_int_arg(opt, optarg);
			ninfo.votes = optarg;
			break;

		case 'n':
			validate_int_arg(opt, optarg);
			ninfo.nodeid = optarg;
			break;

		case 'a':
			ninfo.altname = strdup(optarg);
			break;

		case 'f':
			ninfo.fence_type = strdup(optarg);
			break;

		case 'c':
			ninfo.configfile = strdup(optarg);
			break;

		case 'o':
			ninfo.outputfile = strdup(optarg);
			break;

		case 'C':
			ninfo.tell_ccsd = 0;
			break;

		case 'F':
			ninfo.force_ccsd = 1;
			break;

		case '?':
		default:
			addnode_usage(argv[0]);
		}
	}

	/* Get node name parameter */
	if (optind < argc)
		ninfo.name = strdup(argv[optind]);
	else
		addnode_usage(argv[0]);

	if (!ninfo.fence_type)
		addnode_usage(argv[0]);


	doc = open_configfile(&ninfo);

	root_element = xmlDocGetRootElement(doc);

	increment_version(root_element);

	add_clusternode(root_element, &ninfo, argc, argv, optind);

	/* Write it out */
	save_file(doc, &ninfo);
	/* Shutdown libxml */
	xmlCleanupParser();

}

void del_node(int argc, char **argv)
{
	struct option_info ninfo;
	int opt;
	xmlDoc *doc;
	xmlNode *root_element;

	memset(&ninfo, 0, sizeof(ninfo));
	ninfo.tell_ccsd = 1;

	while ( (opt = getopt_long(argc, argv, "o:c:CFh?", delnode_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.configfile = strdup(optarg);
			break;

		case 'o':
			ninfo.outputfile = strdup(optarg);
			break;

		case 'C':
			ninfo.tell_ccsd = 0;
			break;

		case 'F':
			ninfo.force_ccsd = 1;
			break;

		case '?':
		default:
			delnode_usage(argv[0]);
		}
	}

	/* Get node name parameter */
	if (optind < argc)
		ninfo.name = strdup(argv[optind]);
	else
		delnode_usage(argv[0]);

	doc = open_configfile(&ninfo);

	root_element = xmlDocGetRootElement(doc);

	increment_version(root_element);

	del_clusternode(root_element, &ninfo);

	/* Write it out */
	save_file(doc, &ninfo);
}

void list_nodes(int argc, char **argv)
{
	xmlNode *cur_node;
	xmlNode *root_element;
	xmlNode *clusternodes;
	xmlNode *fencenode = NULL;
	xmlDocPtr doc;
	xmlNode *mcast;
	struct option_info ninfo;
	int opt;
	int verbose = 0;

	memset(&ninfo, 0, sizeof(ninfo));

	while ( (opt = getopt_long(argc, argv, "c:vh?", list_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.configfile = strdup(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case '?':
		default:
			list_usage(argv[0]);
		}
	}
	doc = open_configfile(&ninfo);

	root_element = xmlDocGetRootElement(doc);


	printf("\nCluster name: %s, config_version: %s\n\n",
	       (char *)cluster_name(root_element),
	       (char *)find_version(root_element));

	clusternodes = findnode(root_element, "clusternodes");
	if (!clusternodes)
		die("Can't find \"clusternodes\" in %s\n", ninfo.configfile);

	mcast = find_multicast_addr(root_element);
	if (mcast)
		printf("Multicast address for cluster: %s\n\n", xmlGetProp(mcast, BAD_CAST "addr"));

	printf("Nodename                        Votes Nodeid Fencetype\n");
	for (cur_node = clusternodes->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "clusternode") == 0)
		{
			xmlChar *name   = xmlGetProp(cur_node, BAD_CAST "name");
			xmlChar *votes  = xmlGetProp(cur_node, BAD_CAST "votes");
			xmlChar *nodeid = xmlGetProp(cur_node, BAD_CAST "nodeid");
			xmlChar *ftype  = get_fence_type(cur_node, &fencenode);

			if (!nodeid)
				nodeid=(unsigned char *)"0";
			if (!votes)
				votes = (unsigned char *)"1";

			printf("%-32s %3d  %3d    %s\n", name, atoi((char *)votes),
			       atoi((char *)nodeid),
			       ftype?ftype:(xmlChar *)"");
			if (verbose)
			{
				xmlNode *a = findnode(cur_node, "altname");
				if (a)
				{
					printf(" altname %s=%s", "name", xmlGetProp(a, BAD_CAST "name"));
					if (!print_properties(a, "","",""))
						printf("\n");
				}
				print_properties(cur_node, "  Node properties: ", "votes", "nodeid");
				print_properties(fencenode, "  Fence properties: ", "agent", "");
			}

		}
	}
}

void create_skeleton(int argc, char **argv)
{
	xmlNode *root_element;
	xmlNode *fencedevices;
	xmlNode *clusternodes;
	xmlNode *rm;
	xmlNode *rm1;
	xmlNode *rm2;
	xmlDocPtr doc;
	char *clustername;
	struct option_info ninfo;
	struct stat st;
	int twonode = 0;
	int opt;

	memset(&ninfo, 0, sizeof(ninfo));

	while ( (opt = getopt_long(argc, argv, "c:2h?", list_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.outputfile = strdup(optarg);
			break;

		case '2':
			twonode = 1;
			break;

		case '?':
		default:
			create_usage(argv[0]);
		}
	}
	if (!ninfo.outputfile)
		ninfo.outputfile = DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE;
	ninfo.configfile = "-";

	if (argc - optind < 1)
		create_usage(argv[0]);

	clustername = argv[optind];

	if (stat(ninfo.outputfile, &st) == 0)
		die("%s already exists", ninfo.outputfile);

	/* Init libxml */
	xmlInitParser();
	LIBXML_TEST_VERSION;

	doc = xmlNewDoc(BAD_CAST "1.0");
	root_element = xmlNewNode(NULL, BAD_CAST "cluster");
	xmlDocSetRootElement(doc, root_element);

	xmlSetProp(root_element, BAD_CAST "name", BAD_CAST clustername);
	xmlSetProp(root_element, BAD_CAST "config_version", BAD_CAST "1");

	/* Generate extra bits for a 2node cman cluster */
	if (twonode) {

		xmlNode *cman = xmlNewNode(NULL, BAD_CAST "cman");
		xmlSetProp(cman, BAD_CAST "two_node", BAD_CAST "1");
		xmlSetProp(cman, BAD_CAST "expected_votes", BAD_CAST "1");
		xmlAddChild(root_element, cman);
	}

	clusternodes = xmlNewNode(NULL, BAD_CAST "clusternodes");
	fencedevices = xmlNewNode(NULL, BAD_CAST "fencedevices");
	rm = xmlNewNode(NULL, BAD_CAST "rm");
	rm1 = xmlNewNode(NULL, BAD_CAST "failoverdomains");

	xmlAddChild(root_element, clusternodes);
	xmlAddChild(root_element, fencedevices);
	xmlAddChild(root_element, rm);

	/* Create empty resource manager sections to keep GUI happy */
	rm2 = xmlNewNode(NULL, BAD_CAST "resources");
	xmlAddChild(rm, rm1);
	xmlAddChild(rm, rm2);

	save_file(doc, &ninfo);

}

void add_fence(int argc, char **argv)
{
	xmlNode *root_element;
	xmlNode *fencedevices;
	xmlNode *fencenode = NULL;
	xmlDocPtr doc;
	char *fencename;
	char *agentname;
	struct option_info ninfo;
	int opt;

	memset(&ninfo, 0, sizeof(ninfo));
	ninfo.tell_ccsd = 1;

	while ( (opt = getopt_long(argc, argv, "c:o:CFh?", list_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.configfile = strdup(optarg);
			break;
		case 'o':
			ninfo.outputfile = strdup(optarg);
			break;

		case 'C':
			ninfo.tell_ccsd = 0;
			break;

		case 'F':
			ninfo.force_ccsd = 1;
			break;

		case '?':
		default:
			addfence_usage(argv[0]);
		}
	}

	if (argc - optind < 2)
		addfence_usage(argv[0]);

	doc = open_configfile(&ninfo);
	root_element = xmlDocGetRootElement(doc);

	increment_version(root_element);

	fencedevices = findnode(root_element, "fencedevices");
	if (!fencedevices)
		die("Can't find \"fencedevices\" %s\n", ninfo.configfile);

	/* First param is the fence name - check it doesn't already exist */
	fencename = argv[optind++];

	if (valid_fence_type(root_element, fencename))
		die("fence type %s already exists\n", fencename);

	agentname = argv[optind++];

	/* Add it */
	fencenode = xmlNewNode(NULL, BAD_CAST "fencedevice");
	xmlSetProp(fencenode, BAD_CAST "name", BAD_CAST fencename);
	xmlSetProp(fencenode, BAD_CAST "agent", BAD_CAST agentname);

	/* Add name=value options */
	add_fence_args(fencenode, argc, argv, optind);

	xmlAddChild(fencedevices, fencenode);

	save_file(doc, &ninfo);
}

void del_fence(int argc, char **argv)
{
	xmlNode *root_element;
	xmlNode *fencedevices;
	xmlNode *fencenode;
	xmlDocPtr doc;
	char *fencename;
	struct option_info ninfo;
	int opt;

	memset(&ninfo, 0, sizeof(ninfo));
	ninfo.tell_ccsd = 1;

	while ( (opt = getopt_long(argc, argv, "c:o:CFhv?", list_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.configfile = strdup(optarg);
			break;
		case 'o':
			ninfo.outputfile = strdup(optarg);
			break;

		case 'C':
			ninfo.tell_ccsd = 0;
			break;

		case 'F':
			ninfo.force_ccsd = 1;
			break;

		case '?':
		default:
			delfence_usage(argv[0]);
		}
	}

	if (argc - optind < 1)
		delfence_usage(argv[0]);

	fencename = argv[optind];

	doc = open_configfile(&ninfo);
	root_element = xmlDocGetRootElement(doc);
	increment_version(root_element);

	fencedevices = findnode(root_element, "fencedevices");
	if (!fencedevices)
		die("Can't find \"fencedevices\" in %s\n", ninfo.configfile);

	fencenode = valid_fence_type(root_element, fencename);
	if (!fencenode)
		die("fence type %s does not exist\n", fencename);

	xmlUnlinkNode(fencenode);

	save_file(doc, &ninfo);
}

void list_fences(int argc, char **argv)
{
	xmlNode *cur_node;
	xmlNode *root_element;
	xmlNode *fencedevices;
	xmlDocPtr doc;
	struct option_info ninfo;
	int opt;
	int verbose=0;

	memset(&ninfo, 0, sizeof(ninfo));

	while ( (opt = getopt_long(argc, argv, "c:hv?", list_options, NULL)) != EOF)
	{
		switch(opt)
		{
		case 'c':
			ninfo.configfile = strdup(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case '?':
		default:
			list_usage(argv[0]);
		}
	}
	doc = open_configfile(&ninfo);
	root_element = xmlDocGetRootElement(doc);

	fencedevices = findnode(root_element, "fencedevices");
	if (!fencedevices)
		die("Can't find \"fencedevices\" in %s\n", ninfo.configfile);


	printf("Name             Agent\n");
	for (cur_node = fencedevices->children; cur_node; cur_node = cur_node->next)
	{
		if (cur_node->type == XML_ELEMENT_NODE && strcmp((char *)cur_node->name, "fencedevice") == 0)
		{
			xmlChar *name  = xmlGetProp(cur_node, BAD_CAST "name");
			xmlChar *agent = xmlGetProp(cur_node, BAD_CAST "agent");

			printf("%-16s %s\n", name, agent);
			if (verbose)
				print_properties(cur_node, "  Properties: ", "agent", "");
		}
	}
}

