/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

// CC: temp until I tame SASL ... is this necessary?
#define LDAP_DEPRECATED 1
#include <ldap.h>

/* corosync headers */
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>

/* These are defaults. they can be overridden with environment variables
 *  LDAP_URL & LDAP_BASEDN
 */
#define DEFAULT_LDAP_URL "ldap:///"
#define DEFAULT_LDAP_BASEDN "dc=chrissie,dc=net"

static int ldap_readconfig(struct objdb_iface_ver0 *objdb, char **error_string);
static int init_config(struct objdb_iface_ver0 *objdb);
static char error_reason[1024];
static char *ldap_url = DEFAULT_LDAP_URL;
static char *ldap_basedn = DEFAULT_LDAP_BASEDN;

/*
 * Exports the interface for the service
 */

static struct config_iface_ver0 ldapconfig_iface_ver0 = {
	.config_readconfig        = ldap_readconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
		.name		       	= "ldapconfig",
		.version	       	= 0,
		.versions_replace      	= 0,
		.versions_replace_count	= 0,
		.dependencies	       	= 0,
		.dependency_count      	= 0,
		.constructor	       	= NULL,
		.destructor	       	= NULL,
		.interfaces	       	= NULL,
	}
};

static struct lcr_comp ldap_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};



__attribute__ ((constructor)) static void ldap_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &ldapconfig_iface_ver0);
	lcr_component_register(&ldap_comp_ver0);
};

static int ldap_readconfig(struct objdb_iface_ver0 *objdb, char **error_string)
{
	int ret;

	/* Read config tree from LDAP */
	if (!(ret = init_config(objdb)))
	    sprintf(error_reason, "%s", "Successfully read config from LDAP\n");

        *error_string = error_reason;

	return ret;
}

/*
 * Convert hyphens to underscores in all attribute names
 */
static void convert_underscores(char *s, int len)
{
	int j;

	for (j=0; j < len; j++) {
		if (s[j] == '-')
			s[j] = '_';
	}
}

static void convert_dn_underscores(LDAPDN dn)
{
	int i=0;

	while (dn[i]) {
		convert_underscores(dn[i][0][0].la_attr.bv_val, dn[i][0][0].la_attr.bv_len);
		i++;
	}
}

/*
 * Return the parent object of a DN.
 * Actually, this returns the LAST parent with that name. which should (!) be correct.
 */
static unsigned int find_parent(struct objdb_iface_ver0 *objdb, LDAPDN dn, int startdn, char *parent)
{
	int i=startdn;
	int gotstart=0;
	int start=0, end=startdn;
	unsigned int parent_handle = OBJECT_PARENT_HANDLE;
	unsigned int object_handle=0;
	unsigned int find_handle;

	/*
	 * Find the start and end positions first.
	 * start is where the 'parent' entry is.
	 * end   is the end of the list
	 */
	do {
		if (!gotstart && dn[i][0][0].la_value.bv_len == 7 &&
		    !strncmp(parent, dn[i][0][0].la_value.bv_val, 7)) {
			gotstart = 1;
			start = i;
		}
		i++;
	} while (dn[i]);
	if (start <= 0)
		return parent_handle;

	for (i=start; i>=end; i--) {
		objdb->object_find_create(parent_handle,
					     dn[i][0][0].la_value.bv_val, dn[i][0][0].la_value.bv_len,
					     &find_handle);
		if (!objdb->object_find_next(find_handle, &object_handle)) {
			parent_handle = object_handle;
		}
		objdb->object_find_destroy(find_handle);
	}
	return object_handle;
}



static int read_config_for(LDAP *ld, struct objdb_iface_ver0 *objdb, unsigned int parent,
			   char *object, char *sub_dn, int always_create)
{
	char search_dn[4096];
	int rc;
	char *dn;
	LDAPMessage *result, *e;
	unsigned int parent_handle = OBJECT_PARENT_HANDLE;
	unsigned int object_handle;

	sprintf(search_dn, "%s,%s", sub_dn, ldap_basedn);

	/* Search the whole tree from the base DN provided */
	rc = ldap_search_ext_s(ld, search_dn, LDAP_SCOPE_SUBTREE, "(objectClass=*)", NULL, 0,
			       NULL, NULL, NULL, 0, &result);
	if (rc != LDAP_SUCCESS) {
		sprintf(error_reason, "ldap_search_ext_s: %s\n", ldap_err2string(rc));
		if (rc == LDAP_NO_SUCH_OBJECT)
			return 0;
		else
			return -1;
	}
	for (e = ldap_first_entry(ld, result); e != NULL;
	     e = ldap_next_entry(ld, e)) {
		if ((dn = ldap_get_dn(ld, e)) != NULL) {
			char *attr;
			BerElement *attr_ber;
			LDAPDN parsed_dn;

			/* Make it parsable so we can discern the hierarchy */
			if (ldap_str2dn(dn, &parsed_dn, LDAP_DN_PEDANTIC)) {
				sprintf(error_reason, "ldap_str2dn failed: %s\n", ldap_err2string(rc));
				return -1;
			}

			/*
			 * LDAP doesn't allow underscores in dn names so we replace hypens with
			 * underscores so we can have thing like config_version, appear as
			 * config-version in ldap
			 */
			convert_dn_underscores(parsed_dn);

			/* Create a new object if the top-level is NOT name= */
			if (strncmp(parsed_dn[0][0][0].la_attr.bv_val, "name", 4)) {
				parent_handle = find_parent(objdb, parsed_dn, 0, object);

				objdb->object_create(parent_handle, &object_handle, parsed_dn[0][0][0].la_value.bv_val,
						     parsed_dn[0][0][0].la_value.bv_len);
			}
			else {
				parent_handle = find_parent(objdb, parsed_dn, 2, object);
				/* Create a new object with the same name as the current one */
				objdb->object_create(parent_handle, &object_handle, parsed_dn[1][0][0].la_value.bv_val,
						     parsed_dn[1][0][0].la_value.bv_len);

			}

			/* Finished with the text representation */
			ldap_memfree(dn);

			/* Store the attributes as keys */
			attr = ldap_first_attribute(ld, e, &attr_ber);
			while (attr) {
				int i;
				struct berval **val_ber;

				val_ber = ldap_get_values_len(ld, e, attr);
				i=0;
				while (val_ber[i]) {
					/*
					 * If the attribute starts "rhcs" then remove that bit
					 * and make the first letter lower case so it matches the
					 * cluster.conf entry.
					 * so, after the above underscore change too:
					 *   eg 'rhcsConfig-version' becomes 'config_version'. magic!
					 */
					if (strncmp(attr, "rhcs", 4) == 0) {
						memmove(attr, attr+4, strlen(attr+4)+1);
						attr[0] |= 0x60;
					}
					convert_underscores(attr, strlen(attr));

					/*
					 * Add a key - but ignore "objectClass" & "cn" attributes
					 * as they don't provide anything we can use
					 */
					if (strcmp("objectClass", attr) &&
					    strcmp("cn", attr))
						objdb->object_key_create(object_handle, attr, strlen(attr),
									 val_ber[i]->bv_val,
									 val_ber[i]->bv_len+1);
					i++;
				}
				ldap_memfree(attr);
				attr = ldap_next_attribute(ld, e, attr_ber);
				ldap_value_free_len(val_ber);
			}
			ldap_memfree(attr);
			ber_free(attr_ber, 0);
		}
	}
	ldap_msgfree(result);

	return 0;
}

/* The real work starts here */
static int init_config(struct objdb_iface_ver0 *objdb)
{
	LDAP *ld;
	int version, rc;

	if (getenv("COROSYNC_LDAP_URL"))
		ldap_url = getenv("COROSYNC_LDAP_URL");
	if (getenv("COROSYNC_LDAP_BASEDN"))
		ldap_basedn = getenv("COROSYNC_LDAP_BASEDN");

	/* Connect to the LDAP server */
	if (ldap_initialize(&ld, ldap_url)) {
		sprintf(error_reason, "ldap_simple_bind failed: %s\n", strerror(errno));
		return -1;
	}
	version = LDAP_VERSION3;
	ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

	/*
	 * CC: Do I need to use sasl ?!
	 */
	rc = ldap_simple_bind_s(ld, getenv("COROSYNC_LDAP_BINDDN"), getenv("COROSYNC_LDAP_BINDPWD"));
	if (rc != LDAP_SUCCESS) {
		sprintf(error_reason, "ldap_simple_bind failed: %s\n", ldap_err2string(rc));
		return -1;
	}

	rc = read_config_for(ld, objdb, OBJECT_PARENT_HANDLE, "cluster", "cn=cluster", 1);
	if (!rc)
		rc = read_config_for(ld, objdb, OBJECT_PARENT_HANDLE, "totem", "cn=totem,cn=cluster", 1);
	if (!rc)
		rc = read_config_for(ld, objdb, OBJECT_PARENT_HANDLE, "logging", "cn=logging,cn=cluster", 1);

	ldap_unbind(ld);
	return 0;
}
