#ifndef _SUPER_H
#define _SUPER_H

#include "fsck_incore.h"

int read_sb(struct fsck_sb *sdp);
int ji_update(struct fsck_sb *sdp);
int ri_update(struct fsck_sb *sdp);
int write_sb(struct fsck_sb *sdp);

#endif /* _SUPER_H */
