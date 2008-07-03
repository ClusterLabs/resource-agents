#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include <errno.h>
#include "virt.h"
#include "debug.h"

LOGSYS_DECLARE_SUBSYS("XVM", SYSLOGLEVEL);

static int
_compare_virt(const void *_left, const void *_right)
{
	virt_state_t *left = (virt_state_t *)_left,
		     *right = (virt_state_t *)_right;

	return strcasecmp(left->v_name, right->v_name);
}


virt_list_t *vl_get(virConnectPtr vp, int my_id)
{
	virt_list_t *vl = NULL;
	int *d_ids = NULL;
	int d_count, x;
	char *d_name;
	char d_uuid[MAX_DOMAINNAME_LENGTH];
	virDomainPtr dom;
	virDomainInfo d_info;

	errno = EINVAL;
	if (!vp)
		return NULL;

	d_count = virConnectNumOfDomains(vp);
	if (d_count <= 0) {
		if (d_count == 0) {
			/* Successful, but no domains running */
			errno = 0;
			return NULL;
		}
		goto out_fail;
	}

	vl = malloc(sizeof(uint32_t) + sizeof(virt_state_t) * d_count );
	if (!vl)
		goto out_fail;

	d_ids = malloc(sizeof(int) * d_count);
	if (!d_ids)
		goto out_fail;

	if (virConnectListDomains(vp, d_ids, d_count) < 0)
		goto out_fail;

	vl->vm_count = d_count;

	/* Ok, we have the domain IDs - let's get their names and states */
	for (x = 0; x < d_count; x++) {
		dom = virDomainLookupByID(vp, d_ids[x]);
		if (!dom) {
			/* XXX doom */
			goto out_fail;
		}

		if (!(d_name = (char *)virDomainGetName(dom))) {
			/* XXX no name for the domain?!! */
			virDomainFree(dom);
			goto out_fail;
		}

		if (virDomainGetUUIDString(dom, d_uuid) != 0) {
			virDomainFree(dom);
			goto out_fail;
		}

		if (virDomainGetInfo(dom, &d_info) < 0) {
			/* XXX no info for the domain?!! */
			virDomainFree(dom);
			goto out_fail;
		}

		/* Store the name & state */
		strncpy(vl->vm_states[x].v_name, d_name, MAX_DOMAINNAME_LENGTH);
		strncpy(vl->vm_states[x].v_uuid, d_uuid, MAX_DOMAINNAME_LENGTH);
		vl->vm_states[x].v_state.s_state = d_info.state;
		vl->vm_states[x].v_state.s_owner = my_id;

		virDomainFree(dom);
	}

	/* We have all the locally running domains & states now */
	/* Sort */
	free(d_ids);
	qsort(&vl->vm_states[0], vl->vm_count, sizeof(vl->vm_states[0]),
	      _compare_virt);
	return vl;	

out_fail:
	x = errno;
	if (d_ids)
		free(d_ids);
	if (vl)
		free(vl);
	errno = x;
	return NULL;
}


/* Returns 0 if equal, nonzero if not */
int
vl_cmp(virt_list_t *left, virt_list_t *right)
{
	int x;

	/* Quick checks */
	if (!left->vm_count && !right->vm_count)
		return 1;
	if (left->vm_count != right->vm_count)
		return 0;

	for (x = 0; x < left->vm_count; x++) {
		if (strcmp(left->vm_states[x].v_name,
			   right->vm_states[x].v_name))
			return 1;
		/*
		if (left->vm_states[x].v_state.s_state !=
		    right->vm_states[x].v_state.s_state)
			return 1;
		 */
	}

	return 0;
}


void
vl_print(virt_list_t *vl)
{
	int x;

	/* XXX Once the logsys stderr "tag" goes away, the 21s should go back
	   to 24s. */

	dbg_printf(1, "%-21.21s %-36.36s %-5.5s %-5.5s\n", "Domain", "UUID",
	       "Owner", "State");
	dbg_printf(1, "%-21.21s %-36.36s %-5.5s %-5.5s\n", "------", "----",
	       "-----", "-----");

	if (!vl || !vl->vm_count)
		return;

	for (x = 0; x < vl->vm_count; x++) {
		dbg_printf(1, "%-21.21s %-36.36s %-5.5d %-5.5d\n",
		       vl->vm_states[x].v_name,
		       vl->vm_states[x].v_uuid,
		       vl->vm_states[x].v_state.s_owner,
		       vl->vm_states[x].v_state.s_state);
	}
}


virt_state_t *
vl_find_name(virt_list_t *vl, char *name)
{
	int x;

	if (!vl || !name || !vl->vm_count)
		return NULL;

	for (x = 0; x < vl->vm_count; x++) {
		if (!strcasecmp(vl->vm_states[x].v_name, name))
			return &vl->vm_states[x];
	}

	return NULL;
}


virt_state_t *
vl_find_uuid(virt_list_t *vl, char *uuid)
{
	int x;

	if (!vl || !uuid || !vl->vm_count)
		return NULL;

	for (x = 0; x < vl->vm_count; x++) {
		if (!strcasecmp(vl->vm_states[x].v_uuid, uuid))
			return &vl->vm_states[x];
	}

	return NULL;
}


void
vl_free(virt_list_t *old)
{
	free(old);
}
