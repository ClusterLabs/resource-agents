#ifndef __MSG_SIMPLE_H
#define __MSG_SIMPLE_H

#include <errno.h>
#include <stdint.h>
#include <resgroup.h>

typedef struct PACKED {
    uint32_t gh_magic; 
    uint32_t gh_length;
    uint32_t gh_command;
    uint32_t gh_arg1;
    uint32_t gh_arg2;
    uint32_t gh_arg3;
} generic_msg_hdr;

#define swab_generic_msg_hdr(ptr)\
{\
	swab32((ptr)->gh_magic);\
	swab32((ptr)->gh_length);\
	swab32((ptr)->gh_command);\
	swab32((ptr)->gh_arg1);\
	swab32((ptr)->gh_arg2);\
}

typedef struct PACKED {
    generic_msg_hdr	sm_hdr;
    struct {
	char 		d_svcName[64];
        uint32_t	d_action;
        uint32_t	d_svcState;
        uint32_t	d_svcOwner;
        int32_t		d_ret;
    } sm_data;
} SmMessageSt;

#define swab_SmMessageSt(ptr) \
{\
	swab_generic_msg_hdr(&((ptr)->sm_hdr));\
	swab32((ptr)->sm_data.d_action);\
	swab32((ptr)->sm_data.d_svcState);\
	swab32((ptr)->sm_data.d_svcOwner);\
	swab32((ptr)->sm_data.d_ret);\
}

typedef struct ALIGNED {
    generic_msg_hdr	rsm_hdr;
    rg_state_t		rsm_state;
} rg_state_msg_t;

#define swab_rg_state_msg_t(ptr) \
{\
	swab_generic_msg_hdr(&((ptr)->rsm_hdr));\
	swab_rg_state_t(&((ptr)->rsm_state));\
}


#define GENERIC_HDR_MAGIC   0x123abc00
#define GENERIC_HDR_MAGICV2 0x123abc02

int msg_send_simple(msgctx_t *ctx, int cmd, int arg1, int arg2);
int msg_receive_simple(msgctx_t *ctx, generic_msg_hdr ** buf, int timeout);

#endif
