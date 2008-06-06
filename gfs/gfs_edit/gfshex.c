#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>

#include "global.h"
#include "hexedit.h"
#include "linux_endian.h"

#define WANT_GFS_CONVERSION_FUNCTIONS
#include "gfs_ondisk.h"
#include "gfshex.h"

extern int line;
extern struct gfs_sb sb;
extern char *buf;
extern struct gfs_dinode di;
extern uint64 bufsize;

/******************************************************************************
*******************************************************************************
**
** do_dinode_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_dinode_extended(struct gfs_dinode *di, char *buf)
{
	unsigned int x, y, count;
	struct gfs_dirent de;
	uint64 p, last;

	indirect_blocks = 0;
	memset(indirect_block, 0, sizeof(indirect_block));
	if (di->di_height > 0) {
		/* Indirect pointers */
		for (x = sizeof(struct gfs_dinode), y = 0;
			 x < bufsize;
			 x += sizeof(uint64), y++) {
			p = gfs64_to_cpu(*(uint64 *)(buf + x));
			if (p)
				indirect_block[indirect_blocks++] = p;
		}
	}
	else if (di->di_type == GFS_FILE_DIR &&
			 !(di->di_flags & GFS_DIF_EXHASH)) {
		/* Directory Entries: */
		for (x = sizeof(struct gfs_dinode); x < bufsize &&
				 de.de_rec_len > sizeof(struct gfs_dirent) &&
				 de.de_rec_len < 256; x += de.de_rec_len) {
			gfs_dirent_in(&de, buf + x);
			if (de.de_inum.no_formal_ino)
				indirect_block[indirect_blocks++] = de.de_inum.no_formal_ino;
		}
	}
	else if (di->di_type == GFS_FILE_DIR &&
			 (di->di_flags & GFS_DIF_EXHASH) &&
			 di->di_height == 0) {
		/* Leaf Pointers: */
		
		last = gfs64_to_cpu(*(uint64 *)(buf + sizeof(struct gfs_dinode)));
		count = 0;
    
		for (x = sizeof(struct gfs_dinode), y = 0;
			 y < (1 << di->di_depth);
			 x += sizeof(uint64), y++) {
			p = gfs64_to_cpu(*(uint64 *)(buf + x));

			if (p != last) {
				indirect_block[indirect_blocks++] = last;
				/*printf("  %u:  %"PRIu64"\n", count, last);*/
				last = p;
				count = 1;
			}
			else
				count++;

			if ((y + 1) * sizeof(uint64) == di->di_size)
				indirect_block[indirect_blocks++] = last;
				; /*printf("  %u:  %"PRIu64"\n", count, last);*/
		}
	}
}


/******************************************************************************
*******************************************************************************
**
** do_indirect_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_indirect_extended(char *buf)
{
  unsigned int x, y;
  uint64 p;

  printf("\nPointers\n\n");

  for (x = sizeof(struct gfs_indirect), y = 0; x < bufsize; x += 8, y++)
  {
    p = gfs64_to_cpu(*(uint64 *)(buf + x));

    if (p)
      printf("  %u -> %"PRIu64"\n", y, p);
  }
}


/******************************************************************************
*******************************************************************************
**
** do_leaf_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_leaf_extended(char *buf)
{
  struct gfs_dirent de;
  unsigned int x;


  printf("\nDirectory Entries:\n");

  for (x = sizeof(struct gfs_leaf); x < bufsize; x += de.de_rec_len)
  {
    printf("\n");
    gfs_dirent_in(&de, buf + x);
    if (de.de_inum.no_formal_ino)
	gfs_dirent_print(&de, buf + x + sizeof(struct gfs_dirent));
  }
}


/******************************************************************************
*******************************************************************************
**
** do_eattr_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_eattr_extended(char *buf)
{
  struct gfs_ea_header ea;
  unsigned int x;


  printf("\nEattr Entries:\n");

  for (x = sizeof(struct gfs_meta_header); x < bufsize; x += ea.ea_rec_len)
  {
    printf("\n");
    gfs_ea_header_in(&ea, buf + x);
    gfs_ea_header_print(&ea, buf + x + sizeof(struct gfs_ea_header));
  }
}

void gfs_inum_print2(const char *title,struct gfs_inum *no)
{
	move(line,2);
	printw(title);
	pv2(no, no_formal_ino, "%"PRIX64);
	pv2(no, no_addr, "%"PRIX64);
}

/**
 * gfs_sb_print2 - Print out a superblock
 * @sb: the cpu-order buffer
 */
void gfs_sb_print2(struct gfs_sb *sb)
{
	gfs_meta_header_print(&sb->sb_header);

	pv(sb, sb_fs_format, "%u");
	pv(sb, sb_multihost_format, "%u");
	pv(sb, sb_flags, "%u");

	pv(sb, sb_bsize, "%u");
	pv(sb, sb_bsize_shift, "%u");
	pv(sb, sb_seg_size, "%u");

	gfs_inum_print2("jindex inode",&sb->sb_jindex_di);
	gfs_inum_print2("rindex inode",&sb->sb_rindex_di);
	gfs_inum_print2("root inode",&sb->sb_root_di);

	pv(sb, sb_lockproto, "%s");
	pv(sb, sb_locktable, "%s");

	gfs_inum_print2("quota inode",&sb->sb_quota_di);
	gfs_inum_print2("license inode",&sb->sb_license_di);

	pa(sb, sb_reserved, 96);
}

/******************************************************************************
*******************************************************************************
**
** int display_gfs()
**
** Description:
**   This routine...
**
** Input(s):
**  *buffer   - 
**   extended - 
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/
int display_gfs(void)
{
  struct gfs_meta_header mh;
  struct gfs_rgrp rg;
  struct gfs_leaf lf;
  struct gfs_log_header lh;
  struct gfs_log_descriptor ld;

  uint32 magic;

  magic = gfs32_to_cpu(*(uint32 *)buf);

  switch (magic)
  {
  case GFS_MAGIC:
    gfs_meta_header_in(&mh, buf);

    switch (mh.mh_type)
    {
    case GFS_METATYPE_SB:
      printw("Superblock:\n\n");
      gfs_sb_in(&sb, buf);
      gfs_sb_print2(&sb);
      break;

    case GFS_METATYPE_RG:
      printw("Resource Group Header:\n\n");
      gfs_rgrp_in(&rg, buf);
      gfs_rgrp_print(&rg);
      break;

    case GFS_METATYPE_RB:
      printw("Resource Group Bitmap:\n\n");
      gfs_meta_header_print(&mh);
      break;

    case GFS_METATYPE_DI:
      printw("Dinode:\n\n");
      gfs_dinode_print(&di);
      break;

    case GFS_METATYPE_LF:
      printw("Leaf:\n\n");
      gfs_leaf_in(&lf, buf);
      gfs_leaf_print(&lf);
      break;

    case GFS_METATYPE_IN:
      printw("Indirect Block:\n\n");
      gfs_meta_header_print(&mh);
      break;

    case GFS_METATYPE_JD:
      printf("Journaled File Block:\n\n");
      gfs_meta_header_print(&mh);
      break;

    case GFS_METATYPE_LH:
      printw("Log Header:\n\n");
      gfs_log_header_in(&lh, buf);
      gfs_log_header_print(&lh);
      break;


    case GFS_METATYPE_LD:
      printw("Log Descriptor:\n\n");
      gfs_desc_in(&ld, buf);
      gfs_desc_print(&ld);
      break;

    case GFS_METATYPE_EA:
      printw("Eattr Block:\n\n");
      gfs_meta_header_print(&mh);
      break;

    case GFS_METATYPE_ED:
      printw("Eattr Data Block:\n\n");
      gfs_meta_header_print(&mh);
      break;

    default:
      printw("Unknown metadata type\n");
      break;
    }
    break;

  default:
    printw("Unknown block type\n");
    break;
  };
  return(0);
}
