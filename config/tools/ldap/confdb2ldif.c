/*
 * Copyright (c) 2008 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield <ccaulfie@redhat.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openais/saAis.h>
#include <corosync/confdb.h>

confdb_callbacks_t callbacks = {
};

/* This structure maps object parent names to object classes */
struct objectclasses
{
	char *name;
	char *class;
} objectclasses[] =
{
	{ "cluster", "rhcsCluster" },
	{ "cman", "rhcsCman" },
	{ "totem", "rhcsTotem" },
	{ "clusternode", "rhcsNode" },
	{ "device", "rhcsFenceagent" },
	{ "fencedevice", "rhcsFencedevice" },
	{ "method", "rhcsFencemethod" },
	{ "logging", "rhcsLoggersubsys" },
};
/* TODO: Add more here as the schema gets filled in */


static char *ldap_attr_name(char *attrname)
{
	static char newname[1024];
	int i;

	if (strcmp(attrname, "name") == 0)
		return attrname;

	sprintf(newname, "rhcs");
	for (i=0; i<strlen(attrname)+1; i++) {
		if (i == 0)
			newname[4+i] = attrname[i] & 0x5F;
		else
			if (attrname[i] == '_')
				newname[4+i] = '-';
			else
				newname[4+i] = attrname[i];
	}
	return newname;
}


/* Recursively dump the object tree */
static void print_config_tree(confdb_handle_t handle, unsigned int parent_object_handle, char *dn, char *fulldn)
{
	unsigned int object_handle;
	char object_name[1024];
	int object_name_len;
	char key_name[1024];
	int key_name_len;
	char key_value[1024];
	int key_value_len;
	char cumulative_dn[4096];
	int res;
	int i;
	int keycount=0;

	printf("\ndn: %s\n", fulldn);

	/* Show the keys */
	res = confdb_key_iter_start(handle, parent_object_handle);
	if (res != SA_AIS_OK) {
		printf( "error resetting key iterator for object %d: %d\n", parent_object_handle, res);
		return;
	}

	while ( (res = confdb_key_iter(handle, parent_object_handle, key_name, &key_name_len,
				       key_value, &key_value_len)) == SA_AIS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';

		printf("%s: %s\n", ldap_attr_name(key_name), key_value);
		keycount++;
	}
	if (strncmp(fulldn, "cn=", 3) == 0) {
		printf("cn: %s\n", dn);
	}


	/* Determine objectclass... */
	if (keycount == 0) {
		printf("objectclass: nsContainer\n");
	}
	else {
		for (i = 0; i < sizeof(objectclasses)/sizeof(struct objectclasses); i++) {
			if (strcmp(objectclasses[i].name, dn) == 0)
				printf("objectclass: %s\n", objectclasses[i].class);
		}
	}

	/* Show sub-objects */
	res = confdb_object_iter_start(handle, parent_object_handle);
	if (res != SA_AIS_OK) {
		printf( "error resetting object iterator for object %d: %d\n", parent_object_handle, res);
		return;
	}

	while ( (res = confdb_object_iter(handle, parent_object_handle, &object_handle, object_name, &object_name_len)) == SA_AIS_OK)	{
		unsigned int parent;

		res = confdb_object_parent_get(handle, object_handle, &parent);
		if (res != SA_AIS_OK) {
			printf( "error getting parent for object %d: %d\n", object_handle, res);
			return;
		}

		object_name[object_name_len] = '\0';

		/* Check for "name", and create dummy parent object */
		res = confdb_key_get(handle, object_handle, "name", strlen("name"), key_value, &key_value_len);
		if (res == SA_AIS_OK) {
			sprintf(cumulative_dn, "cn=%s,%s", object_name, fulldn);
			printf("\n");
			printf("dn: %s\n", cumulative_dn);
			printf("cn: %s\n", object_name);
			printf("objectclass: %s\n", "nsContainer");

			sprintf(cumulative_dn, "name=%s,cn=%s,%s", key_value, object_name, fulldn);
		}
		else {
			sprintf(cumulative_dn, "cn=%s,%s", object_name, fulldn);
		}

		/* Down we go ... */
		print_config_tree(handle, object_handle, object_name, cumulative_dn);
	}
}


int main(int argc, char *argv[])
{
	confdb_handle_t handle;
	int result;
	unsigned int cluster_handle;
	char *clusterroot = "cluster";
	char basedn[1024];

	if (argc == 1) {
		fprintf(stderr, "usage: \n");
		fprintf(stderr, "    %s <dn> [<objdb root>]\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, " eg: \n");
		fprintf(stderr, "     %s dc=mycompany,dc=com\n", argv[0]);
		fprintf(stderr, "     %s dc=mycompany,dc=com rhcluster\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "objdb root defaults to 'cluster'\n");
		fprintf(stderr, "\n");
		return 0;
	}

	if (argc > 2) {
		clusterroot = argv[2];
	}

	result = confdb_initialize (&handle, &callbacks);
	if (result != SA_AIS_OK) {
		printf ("Could not initialize Cluster Configuration Database API instance error %d\n", result);
		exit (1);
	}

	/* Find the starting object ... this should be a param */

	result = confdb_object_find_start(handle, OBJECT_PARENT_HANDLE);
	if (result != SA_AIS_OK) {
		printf ("Could not start object_find %d\n", result);
		exit (1);
	}

	result = confdb_object_find(handle, OBJECT_PARENT_HANDLE, clusterroot, strlen(clusterroot), &cluster_handle);
	if (result != SA_AIS_OK) {
		printf ("Could not object_find \"cluster\": %d\n", result);
		exit (1);
	}

	sprintf(basedn, "cn=%s,%s", clusterroot, argv[1]);

	/* Print a header */
	printf("# This file was generated by confdb2ldif, from an existing cluster configuration\n");
	printf("#\n");

	/* Print the configuration */
	print_config_tree(handle, cluster_handle, clusterroot, basedn);


	result = confdb_finalize (handle);
	return (0);
}
