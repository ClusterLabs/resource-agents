#ifndef __FS_RECOVERY_H__
#define __FS_RECOVERY_H__

#include "fsck_incore.h"

int reconstruct_journals(struct fsck_sb *sdp);

#endif /* __FS_RECOVERY_H__ */

