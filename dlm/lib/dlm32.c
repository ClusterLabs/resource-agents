/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

/* Convert 32 bit userland reads & writes to something suitable for
   a 64 bit kernel */


/* 64 bit versions of the structs */
struct dlm_lock_params64 {
	uint8_t mode;
	uint16_t flags;
	uint32_t lkid;
	uint32_t parent;
	struct dlm_range range;
	uint8_t namelen;
        uint64_t castparam;
	uint64_t castaddr;
	uint64_t bastparam;
        uint64_t bastaddr;
	uint64_t lksb;
	char lvb[DLM_LVB_LEN];
	char name[1];
};

struct dlm_query_params64 {
	uint32_t query;
	uint64_t qinfo;
	uint64_t resinfo;
	uint64_t lockinfo;
	uint64_t castparam;
	uint64_t castaddr;
	uint64_t lksb;
	uint32_t lkid;
	int lockinfo_max;
};


struct dlm_lspace_params64 {
	uint32_t flags;
	uint32_t minor;
	char name[1];
};

struct dlm_write_request64 {
	uint32_t version[3];
	uint8_t cmd;

	union  {
		struct dlm_lock_params64   lock;
		struct dlm_query_params64  query;
		struct dlm_lspace_params64 lspace;
	} i;
};

struct dlm_lksb64
{
        int      sb_status;
        uint32_t sb_lkid;
        char     sb_flags;
        uint64_t sb_lvbptr;
};

/* struct read from the "device" fd,
   consists mainly of userspace pointers for the library to use */
struct dlm_lock_result64 {
	uint32_t length;
	uint64_t user_astaddr;
	uint64_t user_astparam;
	uint64_t user_lksb;
	uint64_t user_qinfo;
	struct dlm_lksb64 lksb;
	uint8_t  bast_mode;
	/* Offsets may be zero if no data is present */
	uint32_t lvb_offset;
	uint32_t qinfo_offset;
	uint32_t qresinfo_offset;
	uint32_t qlockinfo_offset;
};

struct dlm_queryinfo64 {
	uint64_t gqi_resinfo;
	uint64_t gqi_lockinfo;

	int gqi_locksize;	/* input */
	int gqi_lockcount;	/* output */
};


int dlm_write(int fd, void *buf, int len)
{
	char buf64[sizeof(struct dlm_write_request64) + DLM_RESNAME_MAXLEN];
	struct dlm_write_request64 *req64;
	struct dlm_write_request   *req32;
	int len64;
	int ret;

	req32 = (struct dlm_write_request *)buf;
	req64 = (struct dlm_write_request64 *)buf64;
	len64 = sizeof(struct dlm_write_request64);

	req64->version[0] = req32->version[0];
	req64->version[1] = req32->version[1];
	req64->version[2] = req32->version[2];
	req64->cmd = req32->cmd;
	switch (req32->cmd)
	{
	case DLM_USER_LOCK:
	case DLM_USER_UNLOCK:
		req64->i.lock.mode = req32->i.lock.mode;
		req64->i.lock.flags = req32->i.lock.flags;
		req64->i.lock.lkid = req32->i.lock.lkid;
		req64->i.lock.parent = req32->i.lock.parent;
		req64->i.lock.range.ra_start = req32->i.lock.range.ra_start;
		req64->i.lock.range.ra_end = req32->i.lock.range.ra_end;
		req64->i.lock.namelen = req32->i.lock.namelen;
		req64->i.lock.castparam = (uint64_t)(uint32_t)req32->i.lock.castparam;
		req64->i.lock.castaddr = (uint64_t)(uint32_t)req32->i.lock.castaddr;
		req64->i.lock.bastparam = (uint64_t)(uint32_t)req32->i.lock.bastparam;
		req64->i.lock.bastaddr = (uint64_t)(uint32_t)req32->i.lock.bastaddr;
		req64->i.lock.lksb = (uint64_t)(uint32_t)req32->i.lock.lksb;
		memcpy(req64->i.lock.lvb, req32->i.lock.lvb, DLM_LVB_LEN);
		memcpy(req64->i.lock.name, req32->i.lock.name, req64->i.lock.namelen);
		len64 = sizeof(struct dlm_write_request64) + req64->i.lock.namelen;
		break;

	case DLM_USER_QUERY:
		req64->i.query.query = req32->i.query.query;
		req64->i.query.qinfo = (uint64_t)(uint32_t)req32->i.query.qinfo;
		req64->i.query.resinfo = (uint64_t)(uint32_t)req32->i.query.resinfo;
		req64->i.query.lockinfo = (uint64_t)(uint32_t)req32->i.query.lockinfo;
		req64->i.query.castparam = (uint64_t)(uint32_t)req32->i.query.castparam;
		req64->i.query.castaddr = (uint64_t)(uint32_t)req32->i.query.castaddr;
		req64->i.query.lksb = (uint64_t)(uint32_t)req32->i.query.lksb;
		req64->i.query.lkid = req32->i.query.lkid;
		req64->i.query.lockinfo_max = req32->i.query.lockinfo_max;
		break;

	case DLM_USER_CREATE_LOCKSPACE:
	case DLM_USER_REMOVE_LOCKSPACE:
		req64->i.lspace.flags = req32->i.lspace.flags;
		req64->i.lspace.minor = req32->i.lspace.minor;
		strcpy(req64->i.lspace.name, req32->i.lspace.name);
		len64 = sizeof(struct dlm_write_request64) + strlen(req64->i.lspace.name);
		break;
	}

	ret = write(fd, buf64, len64);

	/* Fake the return length */
	if (ret == len64)
		ret = len;

	return ret;
}


int dlm_read(int fd, void *buf, int len)
{
	int ret;
	int len64;
	struct dlm_lock_result *res32;
	struct dlm_lock_result64 *res64;
	struct dlm_lock_result64 buf64;

	res32 = (struct dlm_lock_result *)buf;

	/* There are two types of read done here, the first just gets the structure, for that
	   we need our own buffer because the 64 bit one is larger than the 32bit.
	   When the user wants the extended information it has already been told the full (64bit)
	   size of the buffer by the kernel so we can use that buffer for reading, that
	   also avoids the need to copy the extended data blocks too.
	*/
	if (len == sizeof(struct dlm_lock_result))
	{
		len64 = sizeof(struct dlm_lock_result64);
		res64 = &buf64;
	}
	else
	{
		len64 = len;
		res64 = (struct dlm_lock_result64 *)buf;
	}

	ret = read(fd, res64, len64);
	if (ret > 0)
	{
		res32->length = res64->length;
		res32->user_astaddr = (void *)(long)res64->user_astaddr;
		res32->user_astparam = (void *)(long)res64->user_astparam;
		res32->user_lksb  = (void *)(long)res64->user_lksb;
		res32->user_qinfo = (void *)(long)res64->user_qinfo;
		res32->lksb.sb_status = res64->lksb.sb_status;
		res32->lksb.sb_flags = res64->lksb.sb_flags;
		res32->lksb.sb_lkid = res64->lksb.sb_lkid;
		res32->bast_mode = res64->bast_mode;
		res32->lvb_offset = res64->lvb_offset;
		res32->qinfo_offset = res64->qinfo_offset;
		res32->qresinfo_offset = res64->qresinfo_offset;
		res32->qlockinfo_offset = res64->qlockinfo_offset;

		if (res32->qinfo_offset)
		{
			struct dlm_queryinfo64 *qinfo64;
			struct dlm_queryinfo *qinfo32;

			qinfo64 = (struct dlm_queryinfo64 *)(buf+res32->qinfo_offset);
			qinfo32 = (struct dlm_queryinfo *)(buf+res32->qinfo_offset);
			qinfo32->gqi_lockcount = qinfo64->gqi_lockcount;
		}
	}
	return ret;
}
