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

#include <stdbool.h>
#include <stdint.h>
#include <sys/utsname.h>
#include <string.h>
#include <unistd.h>
#include "dlm.h"
#include "dlm_device.h"

#include <stdio.h>

#if (defined(__s390__) || defined(__sparc__)) && __WORDSIZE == 32
# define BUILD_BIARCH
#endif

extern ssize_t dlm_read(int, struct dlm_lock_result *);
extern ssize_t dlm_read_data(int, struct dlm_lock_result *, size_t);
extern ssize_t dlm_write(int, struct dlm_write_request *, size_t);

#ifdef BUILD_BIARCH
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

static bool check_biarch_convert(void)
{
	static enum { undefined, native, convert } status;
	if (status == undefined)
	{
		struct utsname buf;
		if (uname(&buf) != 0)
			status = native;
		else if (strcmp(buf.machine,
#ifdef __s390__
				"s390x"
#endif
#ifdef __sparc__
				"sparc64"
#endif
			  ) == 0)
			status = convert;
		else
			status = native;
	}
	if (status == convert)
		return true;
	return false;
}

static ssize_t _dlm_write_convert(int fd, struct dlm_write_request *req32, size_t len32)
{
	char buf64[sizeof(struct dlm_write_request64) + DLM_RESNAME_MAXLEN];
	struct dlm_write_request64 *req64;
	int len64;
	int ret;

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
		ret = len32;

	return ret;
}

static ssize_t _dlm_read_convert(bool data, int fd, struct dlm_lock_result *res32, ssize_t len32)
{
	ssize_t ret;
	size_t len64;
	struct dlm_lock_result64 *res64;
	struct dlm_lock_result64 buf64;

	/* There are two types of read done here, the first just gets the structure, for that
	   we need our own buffer because the 64 bit one is larger than the 32bit.
	   When the user wants the extended information it has already been told the full (64bit)
	   size of the buffer by the kernel so we can use that buffer for reading, that
	   also avoids the need to copy the extended data blocks too.
	*/
	if (!data)
	{
		len64 = sizeof(buf64);
		res64 = &buf64;
	}
	else
	{
		len64 = len32;
		res64 = (struct dlm_lock_result64 *)res32;
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

			qinfo64 = (struct dlm_queryinfo64 *)((char*)res32+res32->qinfo_offset);
			qinfo32 = (struct dlm_queryinfo *)((char *)res32+res32->qinfo_offset);
			qinfo32->gqi_lockcount = qinfo64->gqi_lockcount;
		}
	}
	return ret;
}

ssize_t dlm_read(int fd, struct dlm_lock_result *res)
{
	if (check_biarch_convert())
		return _dlm_read_convert(false, fd, res, 0);
	return read(fd, res, sizeof(struct dlm_lock_result));
}

ssize_t dlm_read_data(int fd, struct dlm_lock_result *res, size_t len)
{
	if (check_biarch_convert())
		return _dlm_read_convert(true, fd, res, len);
	return read(fd, res, len);
}

ssize_t dlm_write(int fd, struct dlm_write_request *req, size_t len)
{
	if (check_biarch_convert())
		return _dlm_write_convert(fd, req, len);
	return write(fd, req, len);
}
#else /* BUILD_BIARCH */
ssize_t dlm_read(int fd, struct dlm_lock_result *res)
{
	return read(fd, res, sizeof(struct dlm_lock_result));
}

ssize_t dlm_read_data(int fd, struct dlm_lock_result *res, size_t len)
{
	return read(fd, res, len);
}

ssize_t dlm_write(int fd, struct dlm_write_request *req, size_t len)
{
	return write(fd, req, len);
}
#endif /* BUILD_BIARCH */
