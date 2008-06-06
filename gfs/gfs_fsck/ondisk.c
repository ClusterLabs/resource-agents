#ifndef HELPER_PROGRAM

#include "gfs.h"

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);

#else

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "linux_endian.h"
#include "ondisk.h"

#define printk printf
#define pv(struct, member, fmt) printf("  "#member" = "fmt"\n", struct->member);

#endif				/*  !HELPER_PROGRAM  */

#define CPIN_08(s1, s2, member, count) {memcpy((s1->member), (s2->member), (count));}
#define CPOUT_08(s1, s2, member, count) {memcpy((s2->member), (s1->member), (count));}
#define CPIN_16(s1, s2, member) {(s1->member) = gfs16_to_cpu((s2->member));}
#define CPOUT_16(s1, s2, member) {(s2->member) = cpu_to_gfs16((s1->member));}
#define CPIN_32(s1, s2, member) {(s1->member) = gfs32_to_cpu((s2->member));}
#define CPOUT_32(s1, s2, member) {(s2->member) = cpu_to_gfs32((s1->member));}
#define CPIN_64(s1, s2, member) {(s1->member) = gfs64_to_cpu((s2->member));}
#define CPOUT_64(s1, s2, member) {(s2->member) = cpu_to_gfs64((s1->member));}

#define pa(struct, member, count) print_array(#member, struct->member, count, console);

/**
 * print_array - Print out an array of bytes
 * @title: what to print before the array
 * @buf: the array
 * @count: the number of bytes
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

static void
print_array(char *title, char *buf, int count, int console)
{
	int x;

	printk("  %s =\n", title);
	for (x = 0; x < count; x++) {
		printk("%.2X ", (unsigned char) buf[x]);
		if (x % 16 == 15)
			printk("\n");
	}
	if (x % 16)
		printk("\n");

}

/**
 * gfs_inum_in - Read in an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_inum_in(struct gfs_inum *no, char *buf)
{
	struct gfs_inum *str = (struct gfs_inum *) buf;

	CPIN_64(no, str, no_formal_ino);
	CPIN_64(no, str, no_addr);

}

/**
 * gfs_inum_out - Write out an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_inum_out(struct gfs_inum *no, char *buf)
{
	struct gfs_inum *str = (struct gfs_inum *) buf;

	CPOUT_64(no, str, no_formal_ino);
	CPOUT_64(no, str, no_addr);

}

/**
 * gfs_inum_print - Print out a inode number
 * @no: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_inum_print(struct gfs_inum *no, int console)
{

	pv(no, no_formal_ino, "%" PRIu64);
	pv(no, no_addr, "%" PRIu64);

}

/**
 * gfs_meta_header_in - Read in a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_meta_header_in(struct gfs_meta_header *mh, char *buf)
{
	struct gfs_meta_header *str = (struct gfs_meta_header *) buf;

	CPIN_32(mh, str, mh_magic);
	CPIN_32(mh, str, mh_type);
	CPIN_64(mh, str, mh_generation);
	CPIN_32(mh, str, mh_format);
	CPIN_32(mh, str, mh_incarn);

}

/**
 * gfs_meta_header_in - Write out a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 * Don't ever change the generation number in this routine.
 * It's done manually in increment_generation().
 */

void
gfs_meta_header_out(struct gfs_meta_header *mh, char *buf)
{
	struct gfs_meta_header *str = (struct gfs_meta_header *) buf;

	CPOUT_32(mh, str, mh_magic);
	CPOUT_32(mh, str, mh_type);
	/*CPOUT_64(mh, str, mh_generation); */
	CPOUT_32(mh, str, mh_format);
	CPOUT_32(mh, str, mh_incarn);

}

/**
 * gfs_meta_header_print - Print out a metadata header
 * @mh: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_meta_header_print(struct gfs_meta_header *mh, int console)
{

	pv(mh, mh_magic, "0x%.8X");
	pv(mh, mh_type, "%u");
	pv(mh, mh_generation, "%" PRIu64);
	pv(mh, mh_format, "%u");
	pv(mh, mh_incarn, "%u");

}

/**
 * gfs_sb_in - Read in a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_sb_in(struct gfs_sb *sb, char *buf)
{
	struct gfs_sb *str = (struct gfs_sb *) buf;

	gfs_meta_header_in(&sb->sb_header, buf);

	CPIN_32(sb, str, sb_fs_format);
	CPIN_32(sb, str, sb_multihost_format);
	CPIN_32(sb, str, sb_flags);

	CPIN_32(sb, str, sb_bsize);
	CPIN_32(sb, str, sb_bsize_shift);
	CPIN_32(sb, str, sb_seg_size);

	gfs_inum_in(&sb->sb_jindex_di, (char *) &str->sb_jindex_di);
	gfs_inum_in(&sb->sb_rindex_di, (char *) &str->sb_rindex_di);
	gfs_inum_in(&sb->sb_root_di, (char *) &str->sb_root_di);

	CPIN_08(sb, str, sb_lockproto, GFS_LOCKNAME_LEN);
	CPIN_08(sb, str, sb_locktable, GFS_LOCKNAME_LEN);

	gfs_inum_in(&sb->sb_quota_di, (char *) &str->sb_quota_di);
	gfs_inum_in(&sb->sb_license_di, (char *) &str->sb_license_di);

	CPIN_08(sb, str, sb_reserved, 96);

}

/**
 * gfs_sb_out - Write out a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_sb_out(struct gfs_sb *sb, char *buf)
{
	struct gfs_sb *str = (struct gfs_sb *) buf;

	gfs_meta_header_out(&sb->sb_header, buf);

	CPOUT_32(sb, str, sb_fs_format);
	CPOUT_32(sb, str, sb_multihost_format);
	CPOUT_32(sb, str, sb_flags);

	CPOUT_32(sb, str, sb_bsize);
	CPOUT_32(sb, str, sb_bsize_shift);
	CPOUT_32(sb, str, sb_seg_size);

	gfs_inum_out(&sb->sb_jindex_di, (char *) &str->sb_jindex_di);
	gfs_inum_out(&sb->sb_rindex_di, (char *) &str->sb_rindex_di);
	gfs_inum_out(&sb->sb_root_di, (char *) &str->sb_root_di);

	CPOUT_08(sb, str, sb_lockproto, GFS_LOCKNAME_LEN);
	CPOUT_08(sb, str, sb_locktable, GFS_LOCKNAME_LEN);

	gfs_inum_out(&sb->sb_quota_di, (char *) &str->sb_quota_di);
	gfs_inum_out(&sb->sb_license_di, (char *) &str->sb_license_di);

	CPOUT_08(sb, str, sb_reserved, 96);

}

/**
 * gfs_sb_print - Print out a superblock
 * @sb: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_sb_print(struct gfs_sb *sb, int console)
{

	gfs_meta_header_print(&sb->sb_header, console);

	pv(sb, sb_fs_format, "%u");
	pv(sb, sb_multihost_format, "%u");
	pv(sb, sb_flags, "%u");

	pv(sb, sb_bsize, "%u");
	pv(sb, sb_bsize_shift, "%u");
	pv(sb, sb_seg_size, "%u");

	gfs_inum_print(&sb->sb_jindex_di, console);
	gfs_inum_print(&sb->sb_rindex_di, console);
	gfs_inum_print(&sb->sb_root_di, console);

	pv(sb, sb_lockproto, "%s");
	pv(sb, sb_locktable, "%s");

	gfs_inum_print(&sb->sb_quota_di, console);
	gfs_inum_print(&sb->sb_license_di, console);

	pa(sb, sb_reserved, 96);

}

/**
 * gfs_jindex_in - Read in a journal index structure
 * @jindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_jindex_in(struct gfs_jindex *jindex, char *buf)
{
	struct gfs_jindex *str = (struct gfs_jindex *) buf;

	CPIN_64(jindex, str, ji_addr);
	CPIN_32(jindex, str, ji_nsegment);
	CPIN_32(jindex, str, ji_pad);

	CPIN_08(jindex, str, ji_reserved, 64);

}

/**
 * gfs_jindex_out - Write out a journal index structure
 * @jindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_jindex_out(struct gfs_jindex *jindex, char *buf)
{
	struct gfs_jindex *str = (struct gfs_jindex *) buf;

	CPOUT_64(jindex, str, ji_addr);
	CPOUT_32(jindex, str, ji_nsegment);
	CPOUT_32(jindex, str, ji_pad);

	CPOUT_08(jindex, str, ji_reserved, 64);

}

/**
 * gfs_jindex_print - Print out a journal index structure
 * @ji: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_jindex_print(struct gfs_jindex *ji, int console)
{

	pv(ji, ji_addr, "%" PRIu64);
	pv(ji, ji_nsegment, "%u");
	pv(ji, ji_pad, "%u");

	pa(ji, ji_reserved, 64);

}

/**
 * gfs_rindex_in - Read in a resource index structure
 * @rindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rindex_in(struct gfs_rindex *rindex, char *buf)
{
	struct gfs_rindex *str = (struct gfs_rindex *) buf;

	CPIN_64(rindex, str, ri_addr);
	CPIN_32(rindex, str, ri_length);
	CPIN_32(rindex, str, ri_pad);

	CPIN_64(rindex, str, ri_data1);
	CPIN_32(rindex, str, ri_data);

	CPIN_32(rindex, str, ri_bitbytes);

	CPIN_08(rindex, str, ri_reserved, 64);

}

/**
 * gfs_rindex_out - Write out a resource index structure
 * @rindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rindex_out(struct gfs_rindex *rindex, char *buf)
{
	struct gfs_rindex *str = (struct gfs_rindex *) buf;

	CPOUT_64(rindex, str, ri_addr);
	CPOUT_32(rindex, str, ri_length);
	CPOUT_32(rindex, str, ri_pad);

	CPOUT_64(rindex, str, ri_data1);
	CPOUT_32(rindex, str, ri_data);

	CPOUT_32(rindex, str, ri_bitbytes);

	CPOUT_08(rindex, str, ri_reserved, 64);

}

/**
 * gfs_rindex_print - Print out a resource index structure
 * @ri: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_rindex_print(struct gfs_rindex *ri, int console)
{

	pv(ri, ri_addr, "%" PRIu64);
	pv(ri, ri_length, "%u");
	pv(ri, ri_pad, "%u");

	pv(ri, ri_data1, "%" PRIu64);
	pv(ri, ri_data, "%u");

	pv(ri, ri_bitbytes, "%u");

	pa(ri, ri_reserved, 64);

}

/**
 * gfs_rgrp_in - Read in a resource group header
 * @rgrp: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rgrp_in(struct gfs_rgrp *rgrp, char *buf)
{
	struct gfs_rgrp *str = (struct gfs_rgrp *) buf;

	gfs_meta_header_in(&rgrp->rg_header, buf);

	CPIN_32(rgrp, str, rg_flags);

	CPIN_32(rgrp, str, rg_free);

	CPIN_32(rgrp, str, rg_useddi);
	CPIN_32(rgrp, str, rg_freedi);
	gfs_inum_in(&rgrp->rg_freedi_list, (char *) &str->rg_freedi_list);

	CPIN_32(rgrp, str, rg_usedmeta);
	CPIN_32(rgrp, str, rg_freemeta);

	CPIN_08(rgrp, str, rg_reserved, 64);

}

/**
 * gfs_rgrp_out - Write out a resource group header
 * @rgrp: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rgrp_out(struct gfs_rgrp *rgrp, char *buf)
{
	struct gfs_rgrp *str = (struct gfs_rgrp *) buf;

	gfs_meta_header_out(&rgrp->rg_header, buf);

	CPOUT_32(rgrp, str, rg_flags);

	CPOUT_32(rgrp, str, rg_free);

	CPOUT_32(rgrp, str, rg_useddi);
	CPOUT_32(rgrp, str, rg_freedi);
	gfs_inum_out(&rgrp->rg_freedi_list, (char *) &str->rg_freedi_list);

	CPOUT_32(rgrp, str, rg_usedmeta);
	CPOUT_32(rgrp, str, rg_freemeta);

	CPOUT_08(rgrp, str, rg_reserved, 64);

}

/**
 * gfs_rgrp_print - Print out a resource group header
 * @rg: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_rgrp_print(struct gfs_rgrp *rg, int console)
{

	gfs_meta_header_print(&rg->rg_header, console);

	pv(rg, rg_flags, "%u");

	pv(rg, rg_free, "%u");

	pv(rg, rg_useddi, "%u");
	pv(rg, rg_freedi, "%u");
	gfs_inum_print(&rg->rg_freedi_list, console);

	pv(rg, rg_usedmeta, "%u");
	pv(rg, rg_freemeta, "%u");

	pa(rg, rg_reserved, 64);

}

/**
 * gfs_quota_in - Read in a quota structures
 * @quota: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_in(struct gfs_quota *quota, char *buf)
{
	struct gfs_quota *str = (struct gfs_quota *) buf;

	CPIN_64(quota, str, qu_limit);
	CPIN_64(quota, str, qu_warn);
	CPIN_64(quota, str, qu_value);

	CPIN_08(quota, str, qu_reserved, 64);

}

/**
 * gfs_quota_out - Write out a quota structure
 * @quota: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_out(struct gfs_quota *quota, char *buf)
{
	struct gfs_quota *str = (struct gfs_quota *) buf;

	CPOUT_64(quota, str, qu_limit);
	CPOUT_64(quota, str, qu_warn);
	CPOUT_64(quota, str, qu_value);

	CPOUT_08(quota, str, qu_reserved, 64);

}

/**
 * gfs_quota_print - Print out a quota structure
 * @quota: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_quota_print(struct gfs_quota *quota, int console)
{

	pv(quota, qu_limit, "%" PRIu64);
	pv(quota, qu_warn, "%" PRIu64);
	pv(quota, qu_value, "%" PRId64);

	pa(quota, qu_reserved, 64);

}

/**
 * gfs_dinode_in - Read in a dinode
 * @dinode: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dinode_in(struct gfs_dinode *dinode, char *buf)
{
	struct gfs_dinode *str = (struct gfs_dinode *) buf;

	gfs_meta_header_in(&dinode->di_header, buf);

	gfs_inum_in(&dinode->di_num, (char *) &str->di_num);

	CPIN_32(dinode, str, di_mode);
	CPIN_32(dinode, str, di_uid);
	CPIN_32(dinode, str, di_gid);
	CPIN_32(dinode, str, di_nlink);
	CPIN_64(dinode, str, di_size);
	CPIN_64(dinode, str, di_blocks);
	CPIN_64(dinode, str, di_atime);
	CPIN_64(dinode, str, di_mtime);
	CPIN_64(dinode, str, di_ctime);
	CPIN_32(dinode, str, di_major);
	CPIN_32(dinode, str, di_minor);

	CPIN_64(dinode, str, di_rgrp);
	CPIN_64(dinode, str, di_goal_rgrp);
	CPIN_32(dinode, str, di_goal_dblk);
	CPIN_32(dinode, str, di_goal_mblk);
	CPIN_32(dinode, str, di_flags);
	CPIN_32(dinode, str, di_payload_format);
	CPIN_16(dinode, str, di_type);
	CPIN_16(dinode, str, di_height);
	CPIN_32(dinode, str, di_incarn);
	CPIN_16(dinode, str, di_pad);

	CPIN_16(dinode, str, di_depth);
	CPIN_32(dinode, str, di_entries);

	gfs_inum_in(&dinode->di_next_unused, (char *) &str->di_next_unused);

	CPIN_64(dinode, str, di_eattr);

	CPIN_08(dinode, str, di_reserved, 56);

}

/**
 * gfs_dinode_out - Write out a dinode
 * @dinode: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dinode_out(struct gfs_dinode *dinode, char *buf)
{
	struct gfs_dinode *str = (struct gfs_dinode *) buf;

	gfs_meta_header_out(&dinode->di_header, buf);

	gfs_inum_out(&dinode->di_num, (char *) &str->di_num);

	CPOUT_32(dinode, str, di_mode);
	CPOUT_32(dinode, str, di_uid);
	CPOUT_32(dinode, str, di_gid);
	CPOUT_32(dinode, str, di_nlink);
	CPOUT_64(dinode, str, di_size);
	CPOUT_64(dinode, str, di_blocks);
	CPOUT_64(dinode, str, di_atime);
	CPOUT_64(dinode, str, di_mtime);
	CPOUT_64(dinode, str, di_ctime);
	CPOUT_32(dinode, str, di_major);
	CPOUT_32(dinode, str, di_minor);

	CPOUT_64(dinode, str, di_rgrp);
	CPOUT_64(dinode, str, di_goal_rgrp);
	CPOUT_32(dinode, str, di_goal_dblk);
	CPOUT_32(dinode, str, di_goal_mblk);
	CPOUT_32(dinode, str, di_flags);
	CPOUT_32(dinode, str, di_payload_format);
	CPOUT_16(dinode, str, di_type);
	CPOUT_16(dinode, str, di_height);
	CPOUT_32(dinode, str, di_incarn);
	CPOUT_16(dinode, str, di_pad);

	CPOUT_16(dinode, str, di_depth);
	CPOUT_32(dinode, str, di_entries);

	gfs_inum_out(&dinode->di_next_unused, (char *) &str->di_next_unused);

	CPOUT_64(dinode, str, di_eattr);

	CPOUT_08(dinode, str, di_reserved, 56);

}

/**
 * gfs_dinode_print - Print out a dinode
 * @di: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_dinode_print(struct gfs_dinode *di, int console)
{

	gfs_meta_header_print(&di->di_header, console);

	gfs_inum_print(&di->di_num, console);

	pv(di, di_mode, "0%o");
	pv(di, di_uid, "%u");
	pv(di, di_gid, "%u");
	pv(di, di_nlink, "%u");
	pv(di, di_size, "%" PRIu64);
	pv(di, di_blocks, "%" PRIu64);
	pv(di, di_atime, "%" PRId64);
	pv(di, di_mtime, "%" PRId64);
	pv(di, di_ctime, "%" PRId64);
	pv(di, di_major, "%u");
	pv(di, di_minor, "%u");

	pv(di, di_rgrp, "%" PRIu64);
	pv(di, di_goal_rgrp, "%" PRIu64);
	pv(di, di_goal_dblk, "%u");
	pv(di, di_goal_mblk, "%u");
	pv(di, di_flags, "0x%.8X");
	pv(di, di_payload_format, "%u");
	pv(di, di_type, "%u");
	pv(di, di_height, "%u");
	pv(di, di_incarn, "%u");
	pv(di, di_pad, "%u");

	pv(di, di_depth, "%u");
	pv(di, di_entries, "%u");

	gfs_inum_print(&di->di_next_unused, console);

	pv(di, di_eattr, "%" PRIu64);

	pa(di, di_reserved, 56);

}

/**
 * gfs_indirect_in - copy in the header of an indirect block
 * @indirect: the in memory copy
 * @buf: the buffer copy
 *
 */

void
gfs_indirect_in(struct gfs_indirect *indirect, char *buf)
{
	struct gfs_indirect *str = (struct gfs_indirect *) buf;

	gfs_meta_header_in(&indirect->in_header, buf);

	CPIN_08(indirect, str, in_reserved, 64);

}

/**
 * gfs_indirect_out - copy out the header of an indirect block
 * @indirect: the in memory copy
 * @buf: the buffer copy
 *
 */

void
gfs_indirect_out(struct gfs_indirect *indirect, char *buf)
{
	struct gfs_indirect *str = (struct gfs_indirect *) buf;

	gfs_meta_header_out(&indirect->in_header, buf);

	CPOUT_08(indirect, str, in_reserved, 64);

}

/**
 * gfs_indirect_print - Print out a indirect block header
 * @indirect: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_indirect_print(struct gfs_indirect *indirect, int console)
{

	gfs_meta_header_print(&indirect->in_header, console);

	pa(indirect, in_reserved, 64);

}

/**
 * gfs_dirent_in - Read in a directory entry
 * @dirent: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dirent_in(struct gfs_dirent *dirent, char *buf)
{
	struct gfs_dirent *str = (struct gfs_dirent *) buf;

	gfs_inum_in(&dirent->de_inum, (char *) &str->de_inum);
	CPIN_32(dirent, str, de_hash);
	CPIN_16(dirent, str, de_rec_len);
	CPIN_16(dirent, str, de_name_len);
	CPIN_16(dirent, str, de_type);

	CPIN_08(dirent, str, de_reserved, 14);

}

/**
 * gfs_dirent_out - Write out a directory entry
 * @dirent: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dirent_out(struct gfs_dirent *dirent, char *buf)
{
	struct gfs_dirent *str = (struct gfs_dirent *) buf;

	gfs_inum_out(&dirent->de_inum, (char *) &str->de_inum);
	CPOUT_32(dirent, str, de_hash);
	CPOUT_16(dirent, str, de_rec_len);
	CPOUT_16(dirent, str, de_name_len);
	CPOUT_16(dirent, str, de_type);

	CPOUT_08(dirent, str, de_reserved, 14);

}

/**
 * gfs_dirent_print - Print out a directory entry
 * @de: the cpu-order buffer
 * @name: the filename
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_dirent_print(struct gfs_dirent *de, char *name, int console)
{
	char buf[GFS_FNAMESIZE + 1];

	gfs_inum_print(&de->de_inum, console);
	pv(de, de_hash, "0x%.8X");
	pv(de, de_rec_len, "%u");
	pv(de, de_name_len, "%u");
	pv(de, de_type, "%u");

	pa(de, de_reserved, 14);

	memset(buf, 0, GFS_FNAMESIZE + 1);
	memcpy(buf, name, de->de_name_len);
	printk("  name = %s\n", buf);

}

/**
 * gfs_leaf_in - Read in a directory leaf header
 * @leaf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_leaf_in(struct gfs_leaf *leaf, char *buf)
{
	struct gfs_leaf *str = (struct gfs_leaf *) buf;

	gfs_meta_header_in(&leaf->lf_header, buf);

	CPIN_16(leaf, str, lf_depth);
	CPIN_16(leaf, str, lf_entries);
	CPIN_32(leaf, str, lf_dirent_format);
	CPIN_64(leaf, str, lf_next);

	CPIN_08(leaf, str, lf_reserved, 64);

}

/**
 * gfs_leaf_out - Write out a directory leaf header
 * @leaf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_leaf_out(struct gfs_leaf *leaf, char *buf)
{
	struct gfs_leaf *str = (struct gfs_leaf *) buf;

	gfs_meta_header_out(&leaf->lf_header, buf);

	CPOUT_16(leaf, str, lf_depth);
	CPOUT_16(leaf, str, lf_entries);
	CPOUT_32(leaf, str, lf_dirent_format);
	CPOUT_64(leaf, str, lf_next);

	CPOUT_08(leaf, str, lf_reserved, 64);

}

/**
 * gfs_leaf_print - Print out a directory leaf header
 * @lf: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_leaf_print(struct gfs_leaf *lf, int console)
{

	gfs_meta_header_print(&lf->lf_header, console);

	pv(lf, lf_depth, "%u");
	pv(lf, lf_entries, "%u");
	pv(lf, lf_dirent_format, "%u");
	pv(lf, lf_next, "%" PRIu64);

	pa(lf, lf_reserved, 64);

}

/**
 * gfs_log_header_in - Read in a log header
 * @head: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_log_header_in(struct gfs_log_header *head, char *buf)
{
	struct gfs_log_header *str = (struct gfs_log_header *) buf;

	gfs_meta_header_in(&head->lh_header, buf);

	CPIN_32(head, str, lh_flags);
	CPIN_32(head, str, lh_pad);

	CPIN_64(head, str, lh_first);
	CPIN_64(head, str, lh_sequence);

	CPIN_64(head, str, lh_tail);
	CPIN_64(head, str, lh_last_dump);

	CPIN_08(head, str, lh_reserved, 64);

}

/**
 * gfs_log_header_out - Write out a log header
 * @head: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_log_header_out(struct gfs_log_header *head, char *buf)
{
	struct gfs_log_header *str = (struct gfs_log_header *) buf;

	gfs_meta_header_out(&head->lh_header, buf);

	CPOUT_32(head, str, lh_flags);
	CPOUT_32(head, str, lh_pad);

	CPOUT_64(head, str, lh_first);
	CPOUT_64(head, str, lh_sequence);

	CPOUT_64(head, str, lh_tail);
	CPOUT_64(head, str, lh_last_dump);

	CPOUT_08(head, str, lh_reserved, 64);

}

/**
 * gfs_log_header_print - Print out a log header
 * @head: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_log_header_print(struct gfs_log_header *lh, int console)
{

	gfs_meta_header_print(&lh->lh_header, console);

	pv(lh, lh_flags, "0x%.8X");
	pv(lh, lh_pad, "%u");

	pv(lh, lh_first, "%" PRIu64);
	pv(lh, lh_sequence, "%" PRIu64);

	pv(lh, lh_tail, "%" PRIu64);
	pv(lh, lh_last_dump, "%" PRIu64);

	pa(lh, lh_reserved, 64);

}

/**
 * gfs_desc_in - Read in a log descriptor
 * @desc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_desc_in(struct gfs_log_descriptor *desc, char *buf)
{
	struct gfs_log_descriptor *str = (struct gfs_log_descriptor *) buf;

	gfs_meta_header_in(&desc->ld_header, buf);

	CPIN_32(desc, str, ld_type);
	CPIN_32(desc, str, ld_length);
	CPIN_32(desc, str, ld_data1);
	CPIN_32(desc, str, ld_data2);

	CPIN_08(desc, str, ld_reserved, 64);

}

/**
 * gfs_desc_out - Write out a log descriptor
 * @desc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_desc_out(struct gfs_log_descriptor *desc, char *buf)
{
	struct gfs_log_descriptor *str = (struct gfs_log_descriptor *) buf;

	gfs_meta_header_out(&desc->ld_header, buf);

	CPOUT_32(desc, str, ld_type);
	CPOUT_32(desc, str, ld_length);
	CPOUT_32(desc, str, ld_data1);
	CPOUT_32(desc, str, ld_data2);

	CPOUT_08(desc, str, ld_reserved, 64);

}

/**
 * gfs_desc_print - Print out a log descriptor
 * @ld: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_desc_print(struct gfs_log_descriptor *ld, int console)
{

	gfs_meta_header_print(&ld->ld_header, console);

	pv(ld, ld_type, "%u");
	pv(ld, ld_length, "%u");
	pv(ld, ld_data1, "%u");
	pv(ld, ld_data2, "%u");

	pa(ld, ld_reserved, 64);

}

/**
 * gfs_block_tag_in - Read in a block tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_block_tag_in(struct gfs_block_tag *tag, char *buf)
{
	struct gfs_block_tag *str = (struct gfs_block_tag *) buf;

	CPIN_64(tag, str, bt_blkno);
	CPIN_32(tag, str, bt_flags);
	CPIN_32(tag, str, bt_pad);

}

/**
 * gfs_block_tag_out - Write out a block tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_block_tag_out(struct gfs_block_tag *tag, char *buf)
{
	struct gfs_block_tag *str = (struct gfs_block_tag *) buf;

	CPOUT_64(tag, str, bt_blkno);
	CPOUT_32(tag, str, bt_flags);
	CPOUT_32(tag, str, bt_pad);

}

/**
 * gfs_block_tag_print - Print out a block tag
 * @tag: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_block_tag_print(struct gfs_block_tag *tag, int console)
{

	pv(tag, bt_blkno, "%" PRIu64);
	pv(tag, bt_flags, "%u");
	pv(tag, bt_pad, "%u");

}

/**
 * gfs_quota_tag_in - Read in a quota tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_tag_in(struct gfs_quota_tag *tag, char *buf)
{
	struct gfs_quota_tag *str = (struct gfs_quota_tag *) buf;

	CPIN_64(tag, str, qt_change);
	CPIN_32(tag, str, qt_flags);
	CPIN_32(tag, str, qt_id);

}

/**
 * gfs_quota_tag_out - Write out a quota tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_tag_out(struct gfs_quota_tag *tag, char *buf)
{
	struct gfs_quota_tag *str = (struct gfs_quota_tag *) buf;

	CPOUT_64(tag, str, qt_change);
	CPOUT_32(tag, str, qt_flags);
	CPOUT_32(tag, str, qt_id);

}

/**
 * gfs_quota_tag_print - Print out a quota tag
 * @tag: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_quota_tag_print(struct gfs_quota_tag *tag, int console)
{

	pv(tag, qt_change, "%" PRId64);
	pv(tag, qt_flags, "0x%.8X");
	pv(tag, qt_id, "%u");

}

/**
 * gfs_ea_header_in - Read in a Extended Attribute header
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_ea_header_in(struct gfs_ea_header *ea, char *buf)
{
	struct gfs_ea_header *str = (struct gfs_ea_header *) buf;

	CPIN_32(ea, str, ea_rec_len);
	CPIN_32(ea, str, ea_data_len);
	ea->ea_name_len = str->ea_name_len;
	ea->ea_type = str->ea_type;
	ea->ea_flags = str->ea_flags;
	ea->ea_num_ptrs = str->ea_num_ptrs;
	CPIN_32(ea, str, ea_pad);

}

/**
 * gfs_ea_header_out - Write out a Extended Attribute header
 * @ea: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_ea_header_out(struct gfs_ea_header *ea, char *buf)
{
	struct gfs_ea_header *str = (struct gfs_ea_header *) buf;

	CPOUT_32(ea, str, ea_rec_len);
	CPOUT_32(ea, str, ea_data_len);
	str->ea_name_len = ea->ea_name_len;
	str->ea_type = ea->ea_type;
	str->ea_flags = ea->ea_flags;
	str->ea_num_ptrs = ea->ea_num_ptrs;
	CPOUT_32(ea, str, ea_pad);

}

/**
 * gfs_ea_header_printt - Print out a Extended Attribute header
 * @ea: the cpu-order buffer
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 *
 */

void
gfs_ea_header_print(struct gfs_ea_header *ea, int console)
{

	pv(ea, ea_rec_len, "%u");
	pv(ea, ea_data_len, "%u");
	pv(ea, ea_name_len, "%u");
	pv(ea, ea_type, "%u");
	pv(ea, ea_flags, "%u");
	pv(ea, ea_num_ptrs, "%u");
	pv(ea, ea_pad, "%u");

}

static const uint32_t crc_32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd,
	0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb,
	0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447,
	0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75,
	0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11,
	0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
	0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b,
	0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49,
	0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
	0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3,
	0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af,
	0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d,
	0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9,
	0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767,
	0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703,
	0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
	0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d,
	0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b,
	0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
	0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5,
	0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1,
	0x5a05df1b, 0x2d02ef8d
};

/**
 * gfs_dir_hash - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * The hash function is a 32-bit CRC of the data.  The algorithm uses
 * the crc_32_tab table above.
 *
 * This may not be the fastest hash function, but it does a fair bit better
 * at providing uniform results than the others I've looked at.  That's
 * really important for efficient directories.
 *
 * Returns: the hash
 */

uint32_t
gfs_dir_hash(const char *data, int len)
{
	uint32_t hash = 0xFFFFFFFF;

	for (; len--; data++)
		hash = crc_32_tab[(hash ^ *data) & 0xFF] ^ (hash >> 8);

	hash = ~hash;

	return hash;
}
