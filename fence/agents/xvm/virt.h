#ifndef _VIRT_H
#define _VIRT_H
#include <libvirt/libvirt.h>
#include <stdint.h>
#include <netinet/in.h>

#include "xvm.h"

/*
   Owner 0 = no owner.

   checkpoint "xen-vm-states" {
     section "vm-name0" {
       owner_nodeid;
       vm_state;
     }
     section "vm-name1" {
       owner_nodeid;
       vm_state;
     }
     ...
   }
 */

typedef struct {
	uint32_t s_owner;
	int32_t s_state;
} vm_state_t;

typedef struct {
	char v_name[MAX_DOMAINNAME_LENGTH];
	char v_uuid[MAX_DOMAINNAME_LENGTH];
	vm_state_t v_state;
} virt_state_t;

/**
  This is stored in our private checkpoint section.
 */
typedef struct _virt_list {
	uint32_t	vm_count;
	virt_state_t	vm_states[0];
} virt_list_t;

virt_list_t *vl_get(virConnectPtr vp, int my_id);

int vl_cmp(virt_list_t *left, virt_list_t *right);

void vl_print(virt_list_t *vl);
void vl_free(virt_list_t *old);
virt_state_t * vl_find_uuid(virt_list_t *vl, char *name);
virt_state_t * vl_find_name(virt_list_t *vl, char *name);


typedef void ckpt_handle;
int ckpt_read(void *hp, char *secid, void *buf, size_t maxlen);
int ckpt_finish(void *hp);
int ckpt_write(void *hp, char *secid, void *buf, size_t maxlen);
void *ckpt_init(char *ckpt_name, int maxlen, int maxsec, int maxseclen,
		int timeout);


#endif
