#include "util.h"
#include "bio.h"
#include "fs_bits.h"
#include "fs_dir.h"
#include "rgrp.h"
#include "log.h"

#include "fs_inode.h"

#define ST_CREATE 1

/**
 * fs_get_istruct - Get an inode given its number
 * @sdp: The GFS superblock
 * @inum: The inode number
 * @create: Flag to say if we are allowed to create a new struct fsck_inode
 * @ipp: pointer to put the returned inode in
 *
 * Returns: 0 on success, -1 on error
 */
static int fs_get_istruct(struct fsck_sb *sdp, struct gfs_inum *inum,
			  int create, struct fsck_inode **ipp)
{
	struct fsck_inode *ip = NULL;
	int error = 0;

	if (!create){
		/* we are not currently tracking which inodes we already have */
		error = -1;
		goto out;
	}

	if(!(ip = (struct fsck_inode *)malloc(sizeof(struct fsck_inode)))) {
		log_err("Unable to allocate fsck_inode structure\n");
		error = -1;
		goto out;
	}
	if(!memset(ip, 0, sizeof(struct fsck_inode))) {
		log_err("Unable to zero fsck_inode structure\n");
		error = -1;
		goto out;
	}

	ip->i_num = *inum;

	ip->i_sbd = sdp;

	error = fs_copyin_dinode(ip, NULL);
	if (error){
		free(ip);
		ip = NULL;
		goto out;
	}

 out:
	*ipp = ip;

	return error;
}


/*
 * fs_copyin_dinode - read dinode from disk and store in inode
 * @ip: inode, sdp and inum must be set
 *
 * Returns: 0 on success, -1 on error
 */
int fs_copyin_dinode(struct fsck_inode *ip, osi_buf_t *dibh)
{
/*	osi_buf_t *dibh;*/
	int do_relse = 0;
	int error = 0;

	if(!dibh) {
		error = get_and_read_buf(ip->i_sbd,
					 ip->i_num.no_addr, &dibh, 0);
		if (error) {
			stack;
			goto out;
		}

		if(check_meta(dibh, GFS_METATYPE_DI)){
			log_err("Block #%"PRIu64" is not a dinode.\n",
				ip->i_num.no_addr);
			relse_buf(ip->i_sbd, dibh);
			return -1;
		}
		do_relse = 1;
	}
	gfs_dinode_in(&ip->i_di, BH_DATA(dibh));

	if(do_relse)
		relse_buf(ip->i_sbd, dibh);

	

 out:
	return error;
}


/*
 * fs_copyout_dinode - given an inode, copy its dinode data to disk
 * @ip: the inode
 *
 * Returns: 0 on success, -1 on error
 */
int fs_copyout_dinode(struct fsck_inode *ip){
	osi_buf_t *dibh;
	int error;

	error = get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err( "Unable to get a buffer to write dinode to disk.\n");
		return -1;
	}

	gfs_dinode_out(&ip->i_di, BH_DATA(dibh));

	if(write_buf(ip->i_sbd, dibh, 0)){
		log_err( "Unable to commit dinode buffer to disk.\n");
		relse_buf(ip->i_sbd, dibh);
		return -1;
	}

	relse_buf(ip->i_sbd, dibh);
	return 0;
}

/**
 * make_dinode - Fill in a new dinode structure
 * @dip: the directory this inode is being created in
 * @inum: the inode number
 * @type: the file type
 * @mode: the file permissions
 * @cred: a credentials structure
 *
 */

static int make_dinode(struct fsck_inode *dip, struct gfs_inum *inum,
                       unsigned int type, unsigned int mode, osi_cred_t *cred)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct gfs_dinode di;
	osi_buf_t *dibh;
	struct fsck_rgrp *rgd;
	int error;

	error = get_and_read_buf(sdp, inum->no_addr, &dibh, 0);
	if (error)
		goto out;

	if(check_meta(dibh, 0)){
		log_err("make_dinode:  Buffer #%"PRIu64" has no meta header.\n",
			BH_BLKNO(dibh));
		if(query(dip->i_sbd, "Add header? (y/n) ")){
			struct gfs_meta_header mh;
			memset(&mh, 0, sizeof(struct gfs_meta_header));
			mh.mh_magic = GFS_MAGIC;
			mh.mh_type = GFS_METATYPE_NONE;
			gfs_meta_header_out(&mh, BH_DATA(dibh));
			log_warn("meta header added.\n");
		} else {
			log_err("meta header not added.  Failing make_dinode.\n");
			relse_buf(sdp, dibh);
			return -1;
		}
	}

	((struct gfs_meta_header *)BH_DATA(dibh))->mh_type =
		cpu_to_gfs32(GFS_METATYPE_DI);
	((struct gfs_meta_header *)BH_DATA(dibh))->mh_format =
		cpu_to_gfs32(GFS_FORMAT_DI);

	memset(BH_DATA(dibh) + sizeof(struct gfs_dinode), 0,
	       BH_SIZE(dibh) - sizeof(struct gfs_dinode));

	memset(&di, 0, sizeof(struct gfs_dinode));

	gfs_meta_header_in(&di.di_header, BH_DATA(dibh));

	di.di_num = *inum;

	if (dip->i_di.di_mode & 02000)
	{
		di.di_mode = mode | ((type == GFS_FILE_DIR) ? 02000 : 0);
		di.di_gid = dip->i_di.di_gid;
	}
	else
	{
		di.di_mode = mode;
		di.di_gid = osi_cred_to_gid(cred);
	}

	di.di_uid = osi_cred_to_uid(cred);
	di.di_nlink = 1;
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = osi_current_time();

	rgd = fs_blk2rgrpd(sdp, inum->no_addr);
	if(!rgd){
		log_err( "Unable to map block #%"PRIu64" to rgrp\n", inum->no_addr);
		exit(1);
	}

	di.di_rgrp = rgd->rd_ri.ri_addr;
	di.di_goal_rgrp = di.di_rgrp;
	di.di_goal_dblk = di.di_goal_mblk = inum->no_addr - rgd->rd_ri.ri_data1;

	di.di_type = type;

	gfs_dinode_out(&di, BH_DATA(dibh));
	if(write_buf(dip->i_sbd, dibh, 0)){
		log_err( "make_dinode:  bad write_buf()\n");
		error = -EIO;
	}

	relse_buf(dip->i_sbd, dibh);


 out:

	return error;
}

#if 0
/**
 * fs_change_nlink - Change nlink count on inode
 * @ip: The GFS inode
 * @diff: The change in the nlink count required
 *
 * Returns: 0 on success, -EXXXX on failure.
 */
static int fs_change_nlink(struct fsck_inode *ip, int diff)
{
	osi_buf_t *dibh;
	uint32 nlink;
	int error=0;

	nlink = ip->i_di.di_nlink + diff;

	if (diff < 0)
		if(nlink >= ip->i_di.di_nlink)
			log_err( "fs_change_nlink:  Bad link count detected in dinode.\n");

	error = get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
	if (error)
		goto out;

	ip->i_di.di_nlink = nlink;
	ip->i_di.di_ctime = osi_current_time();

	gfs_dinode_out(&ip->i_di, BH_DATA(dibh));
	write_buf(ip->i_sbd, dibh, 0);
	relse_buf(ip->i_sbd,dibh);

 out:
	return error;
}
#endif
/**
 * fs_lookupi - Look up a filename in a directory and return its inode
 * @dip: The directory to search
 * @name: The name of the inode to look for
 * @cred: The caller's credentials
 * @ipp: Used to return the found inode if any
 *
 * Returns: 0 on success, -EXXXX on failure
 */
static int fs_lookupi(struct fsck_inode *dip, osi_filename_t *name,
		      osi_cred_t *cred, struct fsck_inode **ipp)
{
	struct fsck_sb *sdp = dip->i_sbd;
	int error = 0;
	identifier_t id;

	memset(&id, 0, sizeof(identifier_t));
	id.filename = name;
	id.type = ID_FILENAME;

	*ipp = NULL;

	if (!name->len || name->len > GFS_FNAMESIZE)
	{
		error = -ENAMETOOLONG;
		goto out;
	}

	if (fs_filecmp(name, (char *)".", 1))
	{
		*ipp = dip;
		goto out;
	}

	error = fs_dir_search(dip, &id, NULL);
	if (error){
		if (error == -ENOENT)
			error = 0;
		goto out;
	}

	error = fs_get_istruct(sdp, id.inum, ST_CREATE, ipp);

 out:

	if(id.inum) free(id.inum);
	return error;
}

int fs_createi(struct fsck_inode *dip, osi_filename_t *name,
	       unsigned int type, unsigned int mode, osi_cred_t *cred,
	       int *new, struct fsck_inode **ipp)
{
	osi_list_t *tmp=NULL;
	struct fsck_sb *sdp = dip->i_sbd;
	struct gfs_inum inum;
	int error;
	int allocate=0;
	identifier_t id;

	memset(&id, 0, sizeof(identifier_t));

	if (!name->len || name->len > GFS_FNAMESIZE){
		error = -ENAMETOOLONG;
		goto fail;
	}

 restart:

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_di.di_nlink){
		error = -EPERM;
		goto fail;
	}

	id.filename = name;
	id.type = ID_FILENAME;

	error = fs_dir_search(dip, &id, NULL);
	if(id.inum) free(id.inum);
	switch (error)
	{
	case -ENOENT:
		break;

	case 0:
		if (!new){
			error = -EEXIST;
			goto fail;
		} else {
			error = fs_lookupi(dip, name, cred, ipp);
			if (error)
				goto fail;

			if (*ipp){
				*new = FALSE;
				return 0;
			} else
				goto restart;
		}
		break;

	default:
		goto fail;
	}

	if (dip->i_di.di_entries == (uint32)-1){
		error = -EFBIG;
		goto fail;
	}
	if (type == GFS_FILE_DIR && dip->i_di.di_nlink == (uint32)-1){
		error = -EMLINK;
		goto fail;
	}

 retry:
	inum.no_addr = inum.no_formal_ino = 0;
	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next){
		uint64 block;
		struct fsck_rgrp *rgd;

		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
		if(fs_rgrp_read(rgd, FALSE))
			return -1;
		if(rgd->rd_rg.rg_freemeta){
			block = fs_blkalloc_internal(rgd, dip->i_num.no_addr,
						     GFS_BLKST_FREEMETA,
						     GFS_BLKST_USEDMETA, 1);
			log_debug("Got block %"PRIu64"\n", block);
			if(block == BFITNOENT) {
				fs_rgrp_relse(rgd);
				continue;
			}
			block += rgd->rd_ri.ri_data1;
			log_debug("Got block #%"PRIu64"\n", block);
			inum.no_addr = inum.no_formal_ino = block;
			rgd->rd_rg.rg_freemeta--;
			rgd->rd_rg.rg_useddi++;

			if(fs_rgrp_recount(rgd)){
				log_err(  "fs_createi:  Unable to recount rgrp blocks.\n");
				fs_rgrp_relse(rgd);
				error = -EIO;
				goto fail;
			}

			/* write out the rgrp */
			gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
			write_buf(sdp, rgd->rd_bh[0], 0);
			fs_rgrp_relse(rgd);
			break;
		} else {
			if(allocate){
				if(!clump_alloc(rgd, 0)){
					block = fs_blkalloc_internal(rgd, dip->i_num.no_addr,
								     GFS_BLKST_FREEMETA,
								     GFS_BLKST_USEDMETA, 1);
					log_debug("Got block %"PRIu64"\n",
						  block);
					if(block == BFITNOENT) {
						fs_rgrp_relse(rgd);
						continue;
					}
					block += rgd->rd_ri.ri_data1;

					inum.no_addr = inum.no_formal_ino = block;
					rgd->rd_rg.rg_freemeta--;
					rgd->rd_rg.rg_useddi++;

					if(fs_rgrp_recount(rgd)){
						log_err( "fs_createi:  Unable to recount rgrp blocks.\n");
						fs_rgrp_relse(rgd);
						error = -EIO;
						goto fail;
					}

					/* write out the rgrp */
					gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
					write_buf(sdp, rgd->rd_bh[0], 0);
					fs_rgrp_relse(rgd);
					break;
				}
			}
			fs_rgrp_relse(rgd);
		}
	}

	if(!inum.no_addr){
		if(allocate){
			log_err( "No space available for new file or directory.\n");
			return -1;
		} else {
			allocate = 1;
			goto retry;
		}
	}

	error = fs_dir_add(dip, name, &inum, type);
	if (error)
		goto fail;

	error = make_dinode(dip, &inum, type, mode, cred);
	if (error)
		goto fail;


	error = fs_get_istruct(sdp, &inum, ST_CREATE, ipp);
	if (error)
		goto fail;

	if (new)
		*new = TRUE;

	return 0;

 fail:
	return error;
}


/*
 * fs_mkdir - make a directory
 * @dip - dir inode that is the parent of the new dir
 * @new_dir - name of the new dir
 * @mode - mode of new dir
 * @nip - returned inode ptr to the new directory
 *
 * This function has one main difference from the way a normal mkdir
 * works.  It will not return an error if the directory already
 * exists.  Instead it will return success and nip will point to the
 * inode that exists with the same name as new_dir.
 *
 * Returns: 0 on success, -1 on failure.
 */
int fs_mkdir(struct fsck_inode *dip, char *new_dir, int mode, struct fsck_inode **nip){
	int error;
	osi_cred_t creds;
	osi_buf_t *dibh;
	struct gfs_dinode *di;
	struct gfs_dirent *dent;
	struct fsck_inode *ip= NULL;
	struct fsck_sb *sdp = dip->i_sbd;
	osi_filename_t name;
	int new;

	name.name = new_dir;
	name.len = strlen(new_dir);
	creds.cr_uid = getuid();
	creds.cr_gid = getgid();

	error = fs_createi(dip, &name, GFS_FILE_DIR, mode, &creds, &new, &ip);

	if (error)
		goto fail;

	if(!new){
		goto out;
	}

	if(!ip){
		log_err(  "fs_mkdir:  fs_createi() failed.\n");
		error = -1;
		goto fail;
	}

	ip->i_di.di_nlink = 2;
	ip->i_di.di_size = sdp->sb.sb_bsize - sizeof(struct gfs_dinode);
	ip->i_di.di_flags |= GFS_DIF_JDATA;
	ip->i_di.di_payload_format = GFS_FORMAT_DE;
	ip->i_di.di_entries = 2;

	error = get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err( "fs_mkdir:  Unable to aquire directory buffer.\n");
		goto fail;
	}

	di = (struct gfs_dinode *)BH_DATA(dibh);

	error = fs_dirent_alloc(ip, dibh, 1, &dent);
	if(error){  /*  This should never fail  */
		log_err( "fs_mkdir:  fs_dirent_alloc() failed for \".\" entry.\n");
		goto fail;
	}

	dent->de_inum = di->di_num;  /*  already GFS endian  */
	dent->de_hash = gfs_dir_hash(".", 1);
	dent->de_hash = cpu_to_gfs32(dent->de_hash);
	dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
	memcpy((char *)(dent + 1), ".", 1);
	di->di_entries = cpu_to_gfs32(1);

	error = fs_dirent_alloc(ip, dibh, 2, &dent);
	if(error){  /*  This should never fail  */
		log_err( "fs_mkdir:  fs_dirent_alloc() failed for \"..\" entry.\n");
		goto fail;
	}
	gfs_inum_out(&dip->i_num, (char *)&dent->de_inum);
	dent->de_hash = gfs_dir_hash("..", 2);
	dent->de_hash = cpu_to_gfs32(dent->de_hash);
	dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
	memcpy((char *)(dent + 1), "..", 2);

	gfs_dinode_out(&ip->i_di, (char *)di);
	if(write_buf(ip->i_sbd, dibh, 0)){
		log_err( "fs_mkdir:  Bad write_buf()\n");
		error = -EIO;
		goto fail;
	}

	relse_buf(ip->i_sbd, dibh);


	/* FIXME: this may break stuff elsewhere, but since I'm
	 * keeping track of the linkcount in-core, we shouldn't need
	 * to do this...

	error = fs_change_nlink(dip, +1);
	if(error){
		log_err( "fs_mkdir:  fs_change_nlink() failed.\n");
		goto fail;
		} */

 out:
	error=0;
	if(nip) {
		*nip = ip;
	}
	else if(ip) {
		free(ip);
		ip = NULL;
	}
	return 0;
 fail:
	if(ip)
		free(ip);
	return error;
}


