#include "libgfs.h"

#define ST_CREATE 1

/**
 * fs_get_istruct - Get an inode given its number
 * @sdp: The GFS superblock
 * @inum: The inode number
 * @create: Flag to say if we are allowed to create a new struct gfs_inode
 * @ipp: pointer to put the returned inode in
 *
 * Returns: 0 on success, -1 on error
 */
static int fs_get_istruct(int disk_fd, struct gfs_sbd *sdp,
						  struct gfs_inum *inum, int create,
						  struct gfs_inode **ipp)
{
	struct gfs_inode *ip = NULL;
	int error = 0;

	if (!create){
		/* we are not currently tracking which inodes we already have */
		error = -1;
		goto out;
	}

	if(!(ip = (struct gfs_inode *)malloc(sizeof(struct gfs_inode)))) {
		log_err("Unable to allocate gfs_inode structure\n");
		error = -1;
		goto out;
	}
	if(!memset(ip, 0, sizeof(struct gfs_inode))) {
		log_err("Unable to zero gfs_inode structure\n");
		error = -1;
		goto out;
	}

	ip->i_num = *inum;

	ip->i_sbd = sdp;

	error = fs_copyin_dinode(disk_fd, sdp->sd_sb.sb_bsize, ip, NULL);
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
int fs_copyin_dinode(int disk_fd, uint32_t sb_bsize, struct gfs_inode *ip,
					 osi_buf_t *dibh)
{
/*	osi_buf_t *dibh;*/
	int do_relse = 0;
	int error = 0;

	if(!dibh) {
		error = get_and_read_buf(disk_fd, sb_bsize,
								 ip->i_num.no_addr, &dibh, 0);
		if (error) {
			stack;
			goto out;
		}

		if(check_meta(dibh, GFS_METATYPE_DI)){
			log_err("Block #%"PRIu64" is not a dinode.\n",
				ip->i_num.no_addr);
			relse_buf(dibh);
			return -1;
		}
		do_relse = 1;
	}
	gfs_dinode_in(&ip->i_di, BH_DATA(dibh));

	if(do_relse)
		relse_buf(dibh);

	

 out:
	return error;
}


/*
 * fs_copyout_dinode - given an inode, copy its dinode data to disk
 * @ip: the inode
 *
 * Returns: 0 on success, -1 on error
 */
int fs_copyout_dinode(int disk_fd, uint32_t sb_bsize, struct gfs_inode *ip)
{
	osi_buf_t *dibh;
	int error;

	error = get_and_read_buf(disk_fd, sb_bsize, ip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err( "Unable to get a buffer to write dinode to disk.\n");
		return -1;
	}

	gfs_dinode_out(&ip->i_di, BH_DATA(dibh));

	if(write_buf(disk_fd, dibh, 0)){
		log_err( "Unable to commit dinode buffer to disk.\n");
		relse_buf(dibh);
		return -1;
	}

	relse_buf(dibh);
	return 0;
}

/**
 * fs_lookupi - Look up a filename in a directory and return its inode
 * @dip: The directory to search
 * @name: The name of the inode to look for
 * @cred: The caller's credentials
 * @ipp: Used to return the found inode if any
 *
 * Returns: 0 on success, -EXXXX on failure
 */
static int fs_lookupi(int disk_fd, struct gfs_inode *dip,
					  osi_filename_t *name, osi_cred_t *cred, 
					  struct gfs_inode **ipp)
{
	struct gfs_sbd *sdp = dip->i_sbd;
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

	error = fs_dir_search(disk_fd, dip, &id, NULL);
	if (error){
		if (error == -ENOENT)
			error = 0;
		goto out;
	}

	error = fs_get_istruct(disk_fd, sdp, id.inum, ST_CREATE, ipp);

 out:

	if(id.inum) free(id.inum);
	return error;
}

int fs_createi(int disk_fd, struct gfs_inode *dip, osi_filename_t *name,
	       unsigned int type, unsigned int mode, osi_cred_t *cred,
	       int *new, struct gfs_inode **ipp)
{
	osi_list_t *tmp=NULL;
	struct gfs_sbd *sdp = dip->i_sbd;
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

	error = fs_dir_search(disk_fd, dip, &id, NULL);
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
			error = fs_lookupi(disk_fd, dip, name, cred, ipp);
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
	for (tmp = (osi_list_t *)sdp->sd_rglist.next;
		 tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next){
		uint64 block;
		struct gfs_rgrpd *rgd;

		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		if(fs_rgrp_read(disk_fd, rgd, FALSE))
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

			if(fs_rgrp_recount(disk_fd, rgd)){
				log_err(  "fs_createi:  Unable to recount rgrp blocks.\n");
				fs_rgrp_relse(rgd);
				error = -EIO;
				goto fail;
			}

			/* write out the rgrp */
			gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
			write_buf(disk_fd, rgd->rd_bh[0], 0);
			fs_rgrp_relse(rgd);
			break;
		} else {
			if(allocate){
				if(!clump_alloc(disk_fd, rgd, 0)){
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

					if(fs_rgrp_recount(disk_fd, rgd)){
						log_err( "fs_createi:  Unable to recount rgrp blocks.\n");
						fs_rgrp_relse(rgd);
						error = -EIO;
						goto fail;
					}

					/* write out the rgrp */
					gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
					write_buf(disk_fd, rgd->rd_bh[0], 0);
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

	error = fs_dir_add(disk_fd, dip, name, &inum, type);
	if (error)
		goto fail;

	error = make_dinode(disk_fd, dip, sdp, &inum, type, mode, cred);
	if (error)
		goto fail;


	error = fs_get_istruct(disk_fd, sdp, &inum, ST_CREATE, ipp);
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
int fs_mkdir(int disk_fd, struct gfs_inode *dip, char *new_dir, 
			 int mode, struct gfs_inode **nip){
	int error;
	osi_cred_t creds;
	osi_buf_t *dibh;
	struct gfs_dinode *di;
	struct gfs_dirent *dent;
	struct gfs_inode *ip= NULL;
	struct gfs_sbd *sdp = dip->i_sbd;
	osi_filename_t name;
	int new;

	name.name = new_dir;
	name.len = strlen(new_dir);
	creds.cr_uid = getuid();
	creds.cr_gid = getgid();

	error = fs_createi(disk_fd, dip, &name, GFS_FILE_DIR, mode, &creds,
					   &new, &ip);

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
	ip->i_di.di_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode);
	ip->i_di.di_flags |= GFS_DIF_JDATA;
	ip->i_di.di_payload_format = GFS_FORMAT_DE;
	ip->i_di.di_entries = 2;

	error = get_and_read_buf(disk_fd, ip->i_sbd->sd_sb.sb_bsize,
							 ip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err( "fs_mkdir:  Unable to aquire directory buffer.\n");
		goto fail;
	}

	di = (struct gfs_dinode *)BH_DATA(dibh);

	error = fs_dirent_alloc(disk_fd, ip, dibh, 1, &dent);
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

	error = fs_dirent_alloc(disk_fd, ip, dibh, 2, &dent);
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
	if(write_buf(disk_fd, dibh, 0)){
		log_err( "fs_mkdir:  Bad write_buf()\n");
		error = -EIO;
		goto fail;
	}

	relse_buf(dibh);


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


