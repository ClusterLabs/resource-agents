/*-------------------------------------------------------------------------
 * 
 * Shared Disk File EXclusiveness Control Program(SF-EX)
 *
 * sfex_lib.c --- Libraries for other SF-EX modules.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301, USA.
 *
 * Copyright (c) 2007 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 * $Id$
 *
 *-------------------------------------------------------------------------*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <linux/fs.h>

#include "sfex.h"
#include "sfex_lib.h"

static void *locked_mem;
static int dev_fd;
unsigned long sector_size = 0;

int
prepare_lock (const char *device)
{
  int sec_tmp = 0;

  do {
    dev_fd = open (device, O_RDWR | O_DIRECT | O_SYNC);
    if (dev_fd == -1) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      cl_log(LOG_ERR, "can't open device %s: %s\n",
		    device, strerror (errno));
      exit (3);
    }
    break;
  }
  while (1);

  ioctl(dev_fd, BLKSSZGET, &sec_tmp);
  sector_size = (unsigned long)sec_tmp;
  if (sector_size == 0) {
	  cl_log(LOG_ERR, "Get sector size failed: %s\n", strerror(errno));
	  exit(EXIT_FAILURE);
  }

  if (posix_memalign
      ((void **) (&locked_mem), SFEX_ODIRECT_ALIGNMENT,
       sector_size) != 0) {
    cl_log(LOG_ERR, "Failed to allocate aligned memory\n");
    exit (3);
  }
  memset (locked_mem, 0, sector_size);

  return 0;
}

/*
 * get_progname --- a program name
 *
 * We get program name from directory path. It does not include delimiter 
 * characters. Return value is pointer that point string of program name. 
 * We assume delimiter is '/'.
 */
const char *
get_progname (const char *argv0)
{
  char *p;

  p = strrchr (argv0, '/');
  if (p)
    return p + 1;
  else
    return argv0;
}

/*
 * get_nodename --- get a node name(hostname)
 *
 * We get a node name by using uname(2) and return pointer of it.
 * The error checks are done in this function. The caller does not have 
 * to check return value.
 */
char *
get_nodename (void)
{
  struct utsname u;
  char *n;

  if (uname (&u)) {
    cl_log(LOG_ERR, "%s\n", strerror (errno));
    exit (3);
  }
  if (strlen (u.nodename) > SFEX_MAX_NODENAME) {
    cl_log(LOG_ERR,
      "nodename %s is too long. must be less than %lu byte.\n",
       u.nodename, (unsigned long)SFEX_MAX_NODENAME);
    exit (3);
  }
  n = strdup (&u.nodename[0]);
  if (!n) {
    cl_log(LOG_ERR, "%s\n", strerror (errno));
    exit (3);
  }
  return n;
}

/*
 * init_controldata --- initialize control data
 *
 * We initialize each member of sfex_controldata structure.
 */
void
init_controldata (sfex_controldata * cdata, size_t blocksize, int numlocks)
{
  memcpy (cdata->magic, SFEX_MAGIC, sizeof (cdata->magic));
  cdata->version = SFEX_VERSION;
  cdata->revision = SFEX_REVISION;
  cdata->blocksize = blocksize;
  cdata->numlocks = numlocks;
}

/*
 * init_lockdata --- initialize lock data
 *
 * We initialize each member of sfex_lockdata structure.
 */
void
init_lockdata (sfex_lockdata * ldata)
{
  ldata->status = SFEX_STATUS_UNLOCK;
  ldata->count = 0;
  ldata->nodename[0] = 0;
}

/*
 * write_controldata --- write control data into file
 *
 * We write sfex_controldata struct into file. We open a file with 
 * synchronization mode and write out control data.
 *
 * cdata --- pointer of control data
 *
 * device --- name of target file
 */
void
write_controldata (const sfex_controldata * cdata)
{
  sfex_controldata_ondisk *block;
  int fd;

  block = (sfex_controldata_ondisk *) (locked_mem);

  /* We write control data into the buffer with given format. */
  /* We write the offset value of each field of the control data directly.
   * Because a point using this value is limited to two places, we do not 
   * use macro. If you change the following offset values, you must change 
   * values in the read_controldata() function.
   */
  memset (block, 0, cdata->blocksize);
  memcpy (block->magic, cdata->magic, sizeof (block->magic));
  snprintf ((char *) (block->version), sizeof (block->version), "%d",
	    cdata->version);
  snprintf ((char *) (block->revision), sizeof (block->revision), "%d",
	    cdata->revision);
  snprintf ((char *) (block->blocksize), sizeof (block->blocksize), "%u",
	    (unsigned)cdata->blocksize);
  snprintf ((char *) (block->numlocks), sizeof (block->numlocks), "%d",
	    cdata->numlocks);

  fd = dev_fd;
  if (lseek (fd, 0, SEEK_SET) == -1) {
    cl_log(LOG_ERR, "can't seek file pointer: %s\n",
		  strerror (errno));
    exit (3);
  }

  /* write buffer into a file  */
  do {
	  ssize_t s = write (fd, block, cdata->blocksize);
	  if (s == -1) {
		  if (errno == EINTR || errno == EAGAIN)
			  continue;
		  cl_log(LOG_ERR, "can't write meta-data: %s\n",
				strerror (errno));
		  exit (3);
	  }
	  else
		  break;
  }
  while (1);
}

/*
 * write_lockdata --- write lock data into file
 *
 * We write sfex_lockdata into file and seek file pointer to the given
 * position of lock data.
 *
 * cdata --- pointer for control data
 *
 * ldata --- pointer for lock data
 *
 * device --- file name for write
 *
 * index --- index number for lock data. 1 origine.
 */
int
write_lockdata (const sfex_controldata * cdata, const sfex_lockdata * ldata,
		int index)
{
  sfex_lockdata_ondisk *block;
  int fd;

  block = (sfex_lockdata_ondisk *) locked_mem;
  /* We write lock data into buffer with given format */
  /* We write the offset value of each field of the control data directly.
   * Because a point using this value is limited to two places, we do not 
   * use macro. If you chage the following offset values, you must change 
   * values in the read_lockdata() function.
   */
  memset (block, 0, cdata->blocksize);
  block->status = ldata->status;
  snprintf ((char *) (block->count), sizeof (block->count), "%d",
	    ldata->count);
  snprintf ((char *) (block->nodename), sizeof (block->nodename), "%s",
	    ldata->nodename);

  fd = dev_fd;

  /* seek a file pointer to given position */
  if (lseek (fd, cdata->blocksize * index, SEEK_SET) == -1) {
    cl_log(LOG_ERR, "can't seek file pointer: %s\n",
		  strerror (errno));
    return -1;
  }

  /* write buffer into file */
  do {
    ssize_t s = write (fd, block, cdata->blocksize);
    if (s == -1) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      cl_log(LOG_ERR, "can't write meta-data: %s\n",
		    strerror (errno));
      return -1;
    }
    else if (s != cdata->blocksize) {
      /* if writing atomically failed, this process is error */
      cl_log(LOG_ERR, "can't write meta-data atomically.\n");
      return -1;
    }
    break;
  }
  while (1);
  return 0;
}

/*
 * read_controldata --- read control data from file
 *
 * read sfex_controldata structure from file.
 *
 * cdata --- pointer for control data
 *
 * device --- file name for reading
 */
int
read_controldata (sfex_controldata * cdata)
{
  sfex_controldata_ondisk *block;

  block = (sfex_controldata_ondisk *) (locked_mem);

  if (lseek (dev_fd, 0, SEEK_SET) == -1) {
    cl_log(LOG_ERR, "can't seek file pointer: %s\n",
		  strerror (errno));
    return -1;
  }

  /* read data from file */
  do {
	  ssize_t s = read (dev_fd, block, sector_size);
	  if (s == -1) {
		  if (errno == EINTR || errno == EAGAIN)
			  continue;
		  cl_log(LOG_ERR,
			  "can't read controldata meta-data: %s\n",
			   strerror (errno));
		  return -1;
	  }
	  else
		  break;
  } while (1);

  /* read control data from buffer */
  /* 1. check the magic number.  2. check null terminator of each field 
     3. check the version number.  4. Unmuch of revision number is allowed  */
  /* We write the offset value of each field of the control data directly.
   * Because a point using this value is limited to two places, we do not 
   * use macro. If you chage the following offset values, you must change 
   * values in the write_controldata() function.
   */
  memcpy (cdata->magic, block->magic, 4);
  if (memcmp (cdata->magic, SFEX_MAGIC, sizeof (cdata->magic))) {
    cl_log(LOG_ERR, "magic number mismatched. %c%c%c%c <-> %s\n", block->magic[0], block->magic[1], block->magic[2], block->magic[3], SFEX_MAGIC);
    return -1;
  }
  if (block->version[sizeof (block->version)-1]
      || block->revision[sizeof (block->revision)-1]
      || block->blocksize[sizeof (block->blocksize)-1]
      || block->numlocks[sizeof (block->numlocks)-1]) {
    cl_log(LOG_ERR, "control data format error.\n");
    return -1;
  }
  cdata->version = atoi ((char *) (block->version));
  if (cdata->version != SFEX_VERSION) {
    cl_log(LOG_ERR,
      "version number mismatched. program is %d, data is %d.\n",
       SFEX_VERSION, cdata->version);
    return -1;
  }
  cdata->revision = atoi ((char *) (block->revision));
  cdata->blocksize = atoi ((char *) (block->blocksize));
  cdata->numlocks = atoi ((char *) (block->numlocks));

  return 0;
}

/*
 * read_lockdata --- read lock data from file
 *
 * read sfex_lockdata from file and seek file pointer to head position of the 
 * file.
 *
 * cdata --- pointer for control data
 *
 * ldata --- pointer for lock data. Read lock data are stored into this 
 * pointed area.
 *
 * device --- file name of source file
 *
 * index --- index number. 1 origin.
 */
int
read_lockdata (const sfex_controldata * cdata, sfex_lockdata * ldata,
	       int index)
{
  sfex_lockdata_ondisk *block;
  int fd;

  block = (sfex_lockdata_ondisk *) (locked_mem);

  fd = dev_fd;

  /* seek a file pointer to given position */
  if (lseek (fd, cdata->blocksize * index, SEEK_SET) == -1) {
    cl_log(LOG_ERR, "can't seek file pointer: %s\n",
		  strerror (errno));
    return -1;
  }

  /* read from file */
  do {
    ssize_t s = read (fd, block, cdata->blocksize);
    if (s == -1) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      cl_log(LOG_ERR, "can't read lockdata meta-data: %s\n",
		    strerror (errno));
      return -1;
    }
    else if (s != cdata->blocksize) {
      cl_log(LOG_ERR, "can't read meta-data atomically.\n");
      return -1;
    }
    break;
  }
  while (1);

  /* read control data form buffer */
  /* 1. check null terminator of each field 2. check the status */
  /* We write the offset value of each field of the control data directly.
   * Because a point using this value is limited to two places, we do not 
   * use macro. If you chage the following offset values, you must change 
   * values in the write_lockdata() function.
   */
  if (block->count[sizeof(block->count)-1] || block->nodename[sizeof(block->nodename)-1]) {
    cl_log(LOG_ERR, "lock data format error.\n");
    return -1;
  }
  ldata->status = block->status;
  if (ldata->status != SFEX_STATUS_UNLOCK
      && ldata->status != SFEX_STATUS_LOCK) {
    cl_log(LOG_ERR, "lock data format error.\n");
    return -1;
  }
  ldata->count = atoi ((char *) (block->count));
  strncpy ((char *) (ldata->nodename), (const char *) (block->nodename), sizeof(block->nodename));

#ifdef SFEX_DEBUG
  cl_log(LOG_INFO, "status: %c\n", ldata->status);
  cl_log(LOG_INFO, "count: %d\n", ldata->count);
  cl_log(LOG_INFO, "nodename: %s\n", ldata->nodename);
#endif
  return 0;
}

/*
 * lock_index_check --- check the value of index
 *
 * The lock_index_check function checks whether the value of index exceeds
 * the number of lock data on the shared disk.
 *
 * cdata --- pointer for control data
 *
 * index --- index number
 */
int
lock_index_check(sfex_controldata * cdata, int index)
{
        if (read_controldata(cdata) == -1) {
                cl_log(LOG_ERR, "%s\n", "read_controldata failed in lock_index_check");
                return -1;
        }
#ifdef SFEX_DEBUG
        cl_log(LOG_INFO, "version: %d\n", cdata->version);
        cl_log(LOG_INFO, "revision: %d\n", cdata->revision);
        cl_log(LOG_INFO, "blocksize: %d\n", cdata->blocksize);
        cl_log(LOG_INFO, "numlocks: %d\n", cdata->numlocks);
#endif

        if (index > cdata->numlocks) {
                cl_log(LOG_ERR, "index %d is too large. %d locks are stored.\n",
                                index, cdata->numlocks);
                return -1;
        }

        if (cdata->blocksize != sector_size) {
                cl_log(LOG_ERR, "sector_size is not the same as the blocksize.\n");
                return -1;
        }
        return 0;
}
