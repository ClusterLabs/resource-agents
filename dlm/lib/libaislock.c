# ifdef _REENTRANT
#  include <pthread.h>
# endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <linux/types.h>
#include <linux/dlm.h>
#define BUILDING_LIBDLM
#include "libdlm.h"
#include "libaislock.h"
#include <linux/dlm_device.h>

enum {
	SA_LCK_GRANT_CB = 1,
	SA_LCK_WAITER_CB = 2,
};

static struct dlm_ls_info *sa_default_ls = NULL;

inline int lkmode_ais2dlm(SaLckLockModeT mode)
{
	switch(mode)
	{
	case SA_LCK_PR_LOCK_MODE:
		return DLM_LOCK_PR;
	case SA_LCK_EX_LOCK_MODE:
		return DLM_LOCK_EX;
	default:
		return -1;
	}
}

inline SaLckLockModeT lkmode_dlm2ais(int mode)
{
	switch(mode)
	{
	case DLM_LOCK_PR:
		return SA_LCK_PR_LOCK_MODE;
	case DLM_LOCK_EX:
		return SA_LCK_EX_LOCK_MODE;
	default:
		return -1;
	}
}

inline unsigned long lkflag_ais2dlm(SaLckLockFlagsT flag)
{
	unsigned long dlm_flag = 0;

	if(flag & SA_LCK_LOCK_NO_QUEUE)
		dlm_flag |= DLM_LKF_NOQUEUE;
	if(flag & SA_LCK_LOCK_ORPHAN)
		dlm_flag |= DLM_LKF_ORPHAN;

	return dlm_flag;
}

inline SaLckLockStatusT lkstatus_dlm2ais(int status)
{
	switch(status)
	{
	case -ENOMEM:
		return SA_LCK_LOCK_NO_MORE;
	case 0:
		return SA_LCK_LOCK_GRANTED;
	case -EAGAIN:
		return SA_LCK_LOCK_NOT_QUEUED;
	default:
		return -1;
	}
}

inline SaErrorT lkerr_dlm2ais(int status)
{
	switch(status)
	{
	case -EINVAL:
		return SA_ERR_INVALID_PARAM;
	case 0:
		return SA_OK;
	default:
		return -1;
	}
}


SaErrorT
saLckInitialize(SaLckHandleT *lckHandle, const SaLckCallbacksT *lckCallbacks,
		const SaVersionT *version)
{
	dlm_lshandle_t ls = NULL;

	if (NULL == lckHandle)
		return SA_ERR_INVALID_PARAM;

	if (lckCallbacks) {
		lckHandle->callback.saLckLockGrantCallback =
			lckCallbacks->saLckLockGrantCallback;
		lckHandle->callback.saLckLockWaiterCallback =
			lckCallbacks->saLckLockWaiterCallback;
		lckHandle->callback.saLckResourceUnlockCallback =
			lckCallbacks->saLckResourceUnlockCallback;
	} else {
		lckHandle->callback.saLckLockGrantCallback = NULL;
		lckHandle->callback.saLckLockWaiterCallback = NULL;
		lckHandle->callback.saLckResourceUnlockCallback = NULL;
	}
	lckHandle->version.releaseCode = version->releaseCode;
	lckHandle->version.major = version->major;
	lckHandle->version.minor = version->minor;

	ls = dlm_create_lockspace("sa_default", 0600);
	if (!ls)
		return SA_ERR_LIBRARY;

	sa_default_ls = (struct dlm_ls_info *)ls;
	return SA_OK;
}


SaErrorT
saLckFinalize(SaLckHandleT *lckHandle)
{
	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	if(!dlm_release_lockspace("sa_default", sa_default_ls, 1)) {
		return SA_OK;
	} else {
		return SA_ERR_LIBRARY;
	}
}

SaErrorT
saLckResourceOpen(const SaLckHandleT *lckHandle, const SaNameT *lockName,
		  SaLckResourceIdT *resourceId)
{
	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	if (lockName->length <= 31 ) /* OpenDLM only support namelen <= 31*/
	{
		resourceId->name.length = lockName->length;
		strncpy((char *)resourceId->name.value, (char *)lockName->value, lockName->length);
	} else {
		return SA_ERR_NO_MEMORY;
	}

	return SA_OK;
}


SaErrorT
saLckResourceClose(SaLckHandleT *lckHandle, SaLckResourceIdT *resourceId)
{
	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	return SA_OK;
}


SaErrorT
saLckSelectionObjectGet(const SaLckHandleT *lckHandle,
			SaSelectionObjectT *selectionObject)
{
	int fd = -1;

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	fd = dlm_ls_get_fd(sa_default_ls);

	if(!fd)
		return SA_ERR_LIBRARY;

	*selectionObject = fd;

	return SA_OK;
}


SaErrorT
saLckDispatch(const SaLckHandleT *lckHandle,
	      const SaDispatchFlagsT dispatchFlags)
{
	int status;
	int fdflags;
	char resultbuf[sizeof(struct dlm_lock_result)];
	struct dlm_lock_result *result = (struct dlm_lock_result *)resultbuf;
	char *fullresult=NULL;
	SaLckLockIdT *lkid;
	SaLckLockModeT lock_mode;
	int fd = -1;

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	fd = dlm_ls_get_fd(sa_default_ls);

	if(!fd)
		return SA_ERR_LIBRARY;

	fdflags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL,  fdflags | O_NONBLOCK);

	do
	{

		/* Just read the header first */
		status = read(fd, result, sizeof(struct dlm_lock_result));
		if (status <= 0)
    			break;

		if (result->length != status)
		{
        		int newstat;

        		fullresult = malloc(result->length);
        		if (!fullresult)
            			break;

        		newstat = read(fd, fullresult, result->length);

			/* If it read OK then use the new data. otherwise we can
			   still deliver the AST, it just might not have all the
			   info in it...hmmm */
			if (newstat == result->length)
				result = (struct dlm_lock_result *)fullresult;
		}

		/* Copy lksb to user's buffer - except the LVB ptr */
		memcpy(result->user_lksb, &result->lksb,
		       sizeof(struct dlm_lksb) - sizeof(char*));

		/* Flip the status. Kernel space likes negative return codes,
		   userspace positive ones */
		result->user_lksb->sb_status = -result->user_lksb->sb_status;

		/* Need not to care LVB*/
#ifdef QUERY
		/* Copy optional items */
		if (result->qinfo_offset)
		{
    			/* Just need the lockcount written out here */
    			struct dlm_queryinfo *qi = (struct dlm_queryinfo *)
				(fullresult+result->qinfo_offset);
    			result->user_qinfo->gqi_lockcount = qi->gqi_lockcount;
		}

		if (result->qresinfo_offset)
			memcpy(result->user_qinfo->gqi_resinfo,
			       fullresult+result->qresinfo_offset,
			       sizeof(struct dlm_resinfo));

		if (result->qlockinfo_offset)
			memcpy(result->user_qinfo->gqi_lockinfo,
			       fullresult+result->qlockinfo_offset,
			       sizeof(struct dlm_lockinfo) *
			       result->user_qinfo->gqi_lockcount);
#endif
 		/* Call AST */
		lkid = (SaLckLockIdT *)result->user_astparam;
		if (lkid->unlock) {
			/* dispatch unlock ast */
			lkid->unlock = 0;
			lkid->held_mode = 0;
			if(lckHandle->callback.saLckResourceUnlockCallback)
				lckHandle->callback.
					saLckResourceUnlockCallback(
						lkid->args, lkid->resource, lkid,
						SA_LCK_LOCK_RELEASED, SA_OK);

		} else if (SA_LCK_GRANT_CB == (int)result->user_astaddr) {
			/* dispatch lock ast */
			if (0 == lkid->lksb.sb_status) {
				lkid->held_mode = lkid->requested_mode;
				lock_mode = lkid->requested_mode;
			} else {
				lock_mode = lkid->held_mode;
			}

			if(lckHandle->callback.saLckLockGrantCallback)
				lckHandle->callback.
					saLckLockGrantCallback(
						lkid->args, lkid->resource,
						lkid, lock_mode,
						lkstatus_dlm2ais(
							lkid->lksb.sb_status),
						SA_OK);
		} else if (SA_LCK_WAITER_CB == (int)result->user_astaddr) {
			/* dispatch waiter ast */
			if(lckHandle->callback.saLckLockWaiterCallback)
				lckHandle->callback.
					saLckLockWaiterCallback(
						lkid->args, lkid->resource,
						lkid, lkid->held_mode, result->bast_mode);
		} else {
			return SA_ERR_LIBRARY;
		}
	} while (0 !=status && SA_DISPATCH_ONE != dispatchFlags);

	/* EAGAIN is not an error */
	if (status < 0 && errno == EAGAIN)
                status = 0;

	fcntl(fd, F_SETFL, fdflags);
    	return SA_OK;
}

SaErrorT
SaLckResourceLockAsync(const SaLckHandleT *lckHandle, SaInvocationT invocation,
		       const SaLckResourceIdT *resourceId, SaLckLockIdT *lockId,
		       SaLckLockModeT lockMode, SaLckLockFlagsT lockFlags,
		       SaTimeT timeout)
{
	int ret_val;		/* value to be returned from function */

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	/*FIXME deal with timeout in lock/lockasync/unlock.
	 */
	lockId->resource = (SaLckResourceIdT *)resourceId;
	lockId->requested_mode = lockMode;
	lockId->args = invocation;

	ret_val = dlm_ls_lock(sa_default_ls, lkmode_ais2dlm(lockMode),
			      &(lockId->lksb), lkflag_ais2dlm(lockFlags),
			      (void *)(resourceId->name.value),
			      resourceId->name.length, 0, (void *)SA_LCK_GRANT_CB,
			      lockId, (void *)SA_LCK_WAITER_CB, NULL);

	return lkerr_dlm2ais(ret_val);
}

SaErrorT
saLckResourceLock(const SaLckHandleT *lckHandle, SaInvocationT invocation,
		  const SaLckResourceIdT *resourceId, SaLckLockIdT *lockId,
		  SaLckLockModeT lockMode, SaLckLockFlagsT lockFlags,
		  SaTimeT timeout, SaLckLockStatusT *lockStatus)

{
	int ret_val;		/* value to be returned from function */

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	lockId->resource = (SaLckResourceIdT *)resourceId;
	lockId->requested_mode = lockMode;
	lockId->args = invocation;

	ret_val = dlm_ls_lock_wait(sa_default_ls, lkmode_ais2dlm(lockMode),
				   &(lockId->lksb), lkflag_ais2dlm(lockFlags),
				   (void *)(resourceId->name.value),
				   resourceId->name.length, 0, lockId,
				   (void *)SA_LCK_WAITER_CB, NULL);

	*lockStatus = lkstatus_dlm2ais(lockId->lksb.sb_status);
	lockId->held_mode = lockId->requested_mode;

	return lkerr_dlm2ais(ret_val);
}

SaErrorT
saLckResourceUnlock(const SaLckHandleT *lckHandle, SaLckLockIdT *lockId,
		    SaTimeT timeout)
{
	int ret_val;	/* value to be returned from function */

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	ret_val = dlm_ls_unlock_wait(sa_default_ls, lockId->lksb.sb_lkid, 0,
				     &(lockId->lksb));
	lockId->held_mode = 0;

	return lkerr_dlm2ais(ret_val);
}

SaErrorT
saLckResourceUnlockAsync(const SaLckHandleT *lckHandle,
			 SaInvocationT invocation, SaLckLockIdT *lockId)
{
	int ret_val;	/* value to be returned from function */

	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	lockId->unlock = 1;
	lockId->args = invocation;


	ret_val = dlm_ls_unlock(sa_default_ls, lockId->lksb.sb_lkid, 0, &(lockId->lksb),
				lockId);

	return lkerr_dlm2ais(ret_val);
}


SaErrorT
saLckLockPurge(const SaLckHandleT *lckHandle,
	       const SaLckResourceIdT *resourceId)
{
#ifdef QUERY
	int ret_val;	/* value to be returned from function */
	SaLckLockIdT lockId;
	struct dlm_lksb lksb;
	struct dlm_resinfo resinfo;
	static struct dlm_queryinfo qinfo;
	struct dlm_lockinfo *p;

	qinfo.gqi_resinfo = &resinfo;
	qinfo.gqi_lockinfo = malloc(sizeof(struct dlm_lockinfo) * 10);
	qinfo.gqi_locksize = 10;


	if ( NULL == sa_default_ls ) {
		return SA_ERR_LIBRARY;
	}

	lockId.resource = (SaLckResourceIdT *)resourceId;
	lockId.requested_mode = DLM_LOCK_NL;

	ret_val = dlm_ls_lock_wait(sa_default_ls, DLM_LOCK_NL,
				   &(lockId.lksb), DLM_LKF_EXPEDITE,
				   (void *)(resourceId->name.value),
				   resourceId->name.length, 0, &lockId,
				   (void *)SA_LCK_WAITER_CB, NULL);

	dlm_ls_query_wait(sa_default_ls, &(lockId.lksb),
			  DLM_QUERY_QUEUE_ALL|DLM_QUERY_LOCKS_ORPHAN, &qinfo);

	for ( p = qinfo.gqi_lockinfo; 0 != p->lki_lkid; p++ ) {
		lksb.sb_lkid = p->lki_lkid;
		ret_val = dlm_ls_unlock_wait(sa_default_ls, p->lki_lkid, 0,
					     &lksb);
	}

	ret_val = dlm_ls_unlock_wait(sa_default_ls, lockId.lksb.sb_lkid, 0,
				     &(lockId.lksb));

	return lkerr_dlm2ais(ret_val);
#else
	return -1;
#endif
}

