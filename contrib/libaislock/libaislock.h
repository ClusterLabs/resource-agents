typedef char  			SaInt8T;
typedef short 			SaInt16T;
typedef long  			SaInt32T;
typedef long long 		SaInt64T;
typedef unsigned char 		SaUint8T;
typedef unsigned short 		SaUint16T;
typedef unsigned long 		SaUint32T;
typedef unsigned long long 	SaUint64T;
typedef SaInt64T 		SaTimeT;

#define SA_MAX_NAME_LENGTH 256

typedef struct {
    SaUint16T length;
    unsigned char value[SA_MAX_NAME_LENGTH];
} SaNameT;

typedef struct {
    char releaseCode;
    unsigned char major;
    unsigned char minor;
} SaVersionT;

typedef int SaSelectionObjectT;

typedef void *SaInvocationT;

typedef enum {
    SA_DISPATCH_ONE = 1,
    SA_DISPATCH_ALL = 2,
    SA_DISPATCH_BLOCKING = 3
} SaDispatchFlagsT;

typedef enum {
    SA_OK = 1,
    SA_ERR_LIBRARY = 2,
    SA_ERR_VERSION = 3,
    SA_ERR_INIT = 4,
    SA_ERR_TIMEOUT = 5,
    SA_ERR_TRY_AGAIN = 6,
    SA_ERR_INVALID_PARAM = 7,
    SA_ERR_NO_MEMORY = 8,
    SA_ERR_BAD_HANDLE = 9,
    SA_ERR_BUSY = 10,
    SA_ERR_ACCESS = 11,
    SA_ERR_NOT_EXIST = 12,
    SA_ERR_NAME_TOO_LONG = 13,
    SA_ERR_EXIST = 14,
    SA_ERR_NO_SPACE = 15,
    SA_ERR_INTERRUPT =16,
    SA_ERR_SYSTEM = 17,
    SA_ERR_NAME_NOT_FOUND = 18,
    SA_ERR_NO_RESOURCES = 19,
    SA_ERR_NOT_SUPPORTED = 20,
    SA_ERR_BAD_OPERATION = 21,
    SA_ERR_FAILED_OPERATION = 22,
    SA_ERR_MESSAGE_ERROR = 23,
    SA_ERR_NO_MESSAGE = 24,
    SA_ERR_QUEUE_FULL = 25,
    SA_ERR_QUEUE_NOT_AVAILABLE = 26,
    SA_ERR_BAD_CHECKPOINT = 27,
    SA_ERR_BAD_FLAGS = 28
} SaErrorT;

/* Chapter 10 */
typedef enum {
    SA_LCK_PR_LOCK_MODE = 1,
    SA_LCK_EX_LOCK_MODE = 2
} SaLckLockModeT;

typedef struct{
	int site;
	int pid;
} SaLckLockHolderT;

typedef struct {
	SaLckLockHolderT	orphan_holder;
	SaNameT			name;
} SaLckResourceIdT;

typedef struct {
	struct dlm_lksb		lksb;
	SaLckResourceIdT	*resource;
	SaLckLockModeT		held_mode;
	SaLckLockModeT		requested_mode;
	int			unlock;
	SaInvocationT		args;
} SaLckLockIdT;

#define SA_LCK_LOCK_NO_QUEUE 0x1
#define SA_LCK_LOCK_ORPHAN 0x2
#define SA_LCK_LOCK_TIMEOUT 0X4
typedef SaUint32T SaLckLockFlagsT;

typedef enum {
    SA_LCK_LOCK_GRANTED = 1,
    SA_LCK_LOCK_RELEASED = 2,
    SA_LCK_LOCK_DEADLOCK = 3,
    SA_LCK_LOCK_NOT_QUEUED = 4,
    SA_LCK_LOCK_TIMED_OUT = 5,
    SA_LCK_LOCK_ORPHANED = 6,
    SA_LCK_LOCK_NO_MORE = 7
} SaLckLockStatusT;

typedef void 
(*SaLckLockGrantCallbackT)(SaInvocationT invocation,
                           const SaLckResourceIdT *resourceId,
                           const SaLckLockIdT *lockId,
                           SaLckLockModeT lockMode,
                           SaLckLockStatusT lockStatus,
                           SaErrorT error);

typedef void 
(*SaLckLockWaiterCallbackT)(SaInvocationT invocation,
                            const SaLckResourceIdT *resourceId,
                            const SaLckLockIdT *lockId,
                            SaLckLockModeT modeHeld,
                            SaLckLockModeT modeRequested);

typedef void 
(*SaLckResourceUnlockCallbackT)(SaInvocationT invocation,
                                const SaLckResourceIdT *resourceId,
                                const SaLckLockIdT *lockId,
                                SaLckLockStatusT lockStatus,
                                SaErrorT error);
typedef struct SaLckCallbacks {
    SaLckLockGrantCallbackT saLckLockGrantCallback;
    SaLckLockWaiterCallbackT saLckLockWaiterCallback;
    SaLckResourceUnlockCallbackT saLckResourceUnlockCallback;
}SaLckCallbacksT;

typedef struct {
	SaLckCallbacksT		callback;
	SaVersionT		version;
} SaLckHandleT;

    SaErrorT 
saLckInitialize(SaLckHandleT *lckHandle, const SaLckCallbacksT *lckCallbacks,
                const SaVersionT *version);

    SaErrorT 
saLckSelectionObjectGet(const SaLckHandleT *lckHandle,
                        SaSelectionObjectT *selectionObject);

    SaErrorT 
saLckDispatch(const SaLckHandleT *lckHandle,
              const SaDispatchFlagsT dispatchFlags);

    SaErrorT 
saLckFinalize(SaLckHandleT *lckHandle);

    SaErrorT 
saLckResourceOpen(const SaLckHandleT *lckHandle,
                  const SaNameT *lockName,
                  SaLckResourceIdT *resourceId);

    SaErrorT 
saLckResourceClose(SaLckHandleT *lckHandle, SaLckResourceIdT *resourceId);

    SaErrorT 
saLckResourceLock(const SaLckHandleT *lckHandle, SaInvocationT invocation,
                  const SaLckResourceIdT *resourceId,
                  SaLckLockIdT *lockId,
                  SaLckLockModeT lockMode,
                  SaLckLockFlagsT lockFlags,
                  SaTimeT timeout,
                  SaLckLockStatusT *lockStatus);

    SaErrorT
SaLckResourceLockAsync(const SaLckHandleT *lckHandle,
                       SaInvocationT invocation,
                       const SaLckResourceIdT *resourceId,
                       SaLckLockIdT *lockId,
                       SaLckLockModeT lockMode,
                       SaLckLockFlagsT lockFlags,
                       SaTimeT timeout);

    SaErrorT 
saLckResourceUnlock(const SaLckHandleT *lckHandle,
                    SaLckLockIdT *lockId,
                    SaTimeT timeout);

    SaErrorT 
saLckResourceUnlockAsync(const SaLckHandleT *lckHandle,
                         SaInvocationT invocation,
                         SaLckLockIdT *lockId);

    SaErrorT
saLckLockPurge(const SaLckHandleT *lckHandle,
               const SaLckResourceIdT *resourceId);
