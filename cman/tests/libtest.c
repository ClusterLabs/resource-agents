#include <netinet/in.h>
#include <inttypes.h>
#include "libcman.h"

#include <stdlib.h>
#include <stdio.h>

static void cman_callback(cman_handle_t handle, void *private, int reason, int arg)
{
	printf("callback called reason = %d, arg=%d\n", reason, arg);
}

static void print_node(cman_node_t *node)
{
	printf("  node id     %d\n", node->cn_nodeid);
	printf("  node member %d\n", node->cn_member);
	printf("  node name   %s\n", node->cn_name);
	printf("  node incarn %d\n", node->cn_incarnation);
	printf("\n");
}

int main()
{
	cman_handle_t h;
	int num;
	int retnodes;
	cman_node_t *nodes;
	cman_version_t ver;
	cman_cluster_t clinfo;

	h = cman_init(0);
	if (!h)
	{
		perror("cman_init failed");
		exit(1);
	}

	num = cman_get_node_count(h);
	if (num > 0)
		printf("cluster has %d nodes\n", num);
	else
		perror("node count");

	printf("cman is active:    %d\n", cman_is_active(h));
	printf("cman is quorate:   %d\n", cman_is_quorate(h));
	printf("cman is listening: %d\n", cman_is_listening(h, CMAN_NODEID_US, 1)); /* membership! */
	cman_get_version(h, &ver);
	printf("cman version %d.%d.%d (config %d)\n",
	       ver.cv_minor,
	       ver.cv_major,
	       ver.cv_patch,
	       ver.cv_config);

	if (!cman_get_cluster(h, &clinfo))
	{
		printf("Cluster '%s',  number %d\n", clinfo.name, clinfo.number);
	}
	else
		perror("cluster info failed");

	nodes = malloc(num * sizeof(cman_node_t));
	if (!nodes)
	{
		perror("malloc");
		exit(1);
	}

	if (!cman_get_nodes(h, num, &retnodes, nodes))
	{
		int i;
		printf("Getting all nodes:\n");
		for (i=0; i<retnodes; i++)
			print_node(&nodes[i]);
	}
	else
	{
		perror("get_nodes failed");
	}

	if (!cman_get_node(h, CMAN_NODEID_US, &nodes[0]))
	{
		printf("Getting our info:\n");
		print_node(&nodes[0]);
	}
	else
	{
		perror("get_node failed");
	}

	cman_start_notification(h, cman_callback);
	cman_dispatch(h, CMAN_DISPATCH_BLOCKING | CMAN_DISPATCH_ALL);

	cman_finish(h);

	return 0;
}
