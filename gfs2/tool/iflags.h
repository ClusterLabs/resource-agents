#ifndef __IFLAGS_DOT_H__
#define __IFLAGS_DOT_H__

#define FS_IOC_GETFLAGS                 _IOR('f', 1, long)
#define FS_IOC_SETFLAGS                 _IOW('f', 2, long)
#define FS_IOC32_GETFLAGS               _IOR('f', 1, int)
#define FS_IOC32_SETFLAGS               _IOW('f', 2, int)

/*
 * Inode flags (FS_IOC_GETFLAGS / FS_IOC_SETFLAGS)
 */
#define FS_SECRM_FL                     0x00000001 /* Secure deletion */
#define FS_UNRM_FL                      0x00000002 /* Undelete */
#define FS_COMPR_FL                     0x00000004 /* Compress file */
#define FS_SYNC_FL                      0x00000008 /* Synchronous updates */
#define FS_IMMUTABLE_FL                 0x00000010 /* Immutable file */
#define FS_APPEND_FL                    0x00000020 /* writes to file may only append */
#define FS_NODUMP_FL                    0x00000040 /* do not dump file */
#define FS_NOATIME_FL                   0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define FS_DIRTY_FL                     0x00000100
#define FS_COMPRBLK_FL                  0x00000200 /* One or more compressed clusters */
#define FS_NOCOMP_FL                    0x00000400 /* Don't compress */
#define FS_ECOMPR_FL                    0x00000800 /* Compression error */
/* End compression flags --- maybe not all used */
#define FS_BTREE_FL                     0x00001000 /* btree format dir */
#define FS_INDEX_FL                     0x00001000 /* hash-indexed directory */
#define FS_IMAGIC_FL                    0x00002000 /* AFS directory */
#define FS_JOURNAL_DATA_FL              0x00004000 /* Reserved for ext3 */
#define FS_NOTAIL_FL                    0x00008000 /* file tail should not be merged */
#define FS_DIRSYNC_FL                   0x00010000 /* dirsync behaviour (directories only) */
#define FS_TOPDIR_FL                    0x00020000 /* Top of directory hierarchies*/
#define FS_EXTENT_FL                    0x00080000 /* Extents */
#define FS_DIRECTIO_FL                  0x00100000 /* Use direct i/o */
#define FS_RESERVED_FL                  0x80000000 /* reserved for ext2 lib */

#define FS_FL_USER_VISIBLE              0x0003DFFF /* User visible flags */
#define FS_FL_USER_MODIFIABLE           0x000380FF /* User modifiable flags */

#endif /* __IFLAGS_DOT_H__ */
