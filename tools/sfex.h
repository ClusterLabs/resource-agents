/*-------------------------------------------------------------------------
 *
 * Shared Disk File EXclusiveness Control Program(SF-EX)
 *
 * sfex.h --- Primary include file for SF-EX *.c files.
 *
 * Copyright (c) 2007 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
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
 * $Id$
 *
 *-------------------------------------------------------------------------*/

#ifndef SFEX_H
#define SFEX_H

#include <clplumbing/cl_log.h>
#include <clplumbing/coredumps.h>
#include <clplumbing/realtime.h>

#include <stdint.h>

/*  version, revision */
/*   These numbers are integer and, max number is 999. 
     If these numbers change, version numbers in the configure.ac
     (AC_INIT, AM_INIT_AUTOMAKE) must change together.
 */
#define SFEX_VERSION 1
#define SFEX_REVISION 3

#if 0
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef MIN
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif

/* for Linux >= 2.6, the alignment should be 512
   for Linux < 2.6, the alignment should be sysconf(_SC_PAGESIZE)
   we default to _SC_PAGESIZE
 */
#define SFEX_ODIRECT_ALIGNMENT sysconf(_SC_PAGESIZE)

/*
 * sfex_controldata --- control data
 *
 * This is allocated the head of sfex mata-data area. 
 *
 * magic number --- 4 bytes. This is fixed in {0x01, 0x1f, 0x71, 0x7f}.
 *
 * version number --- 4 bytes. This is printable integer number and 
 * range is from 0 to 999. This must be left-justify, null(0x00) padding, and 
 * make a last byte null.
 *
 * revision number --- 4 bytes. This is printable integer number and 
 * range is from 0 to 999. This must be left-justify, null(0x00) padding, and 
 * make a last byte null.
 *
 * blocksize --- 8bytes. This is printable integer number and range is from 
 * 512 to 9999999. This must be left-justify, null(0x00) padding, and make a 
 * last byte null. This is a size of control data and lock data(one lock data
 * size when there are plural), and it is shown by number of bytes. 
 * For avoiding partial writing, usually block size is set 512 byte etc.
 * If you use direct I/O(if you spacificate --enable-directio for configure 
 * script), note that this value is used for input and output buffer alignment.
 * (In the Linux kernel 2.6, if this value is not 512 multibles, direct I/O 
 * does not work)
 
 * number of locks  --- 4 bytes. This is printable integer number and range 
 * is from 1 to 999. This must be left-justify, null(0x00) padding, and make 
 * a last byte null. This is the number of locks following this control data.
 *
 * padding --- The size of this member depend on blocksize. It is adjusted so 
 * that the whole of the control data including this padding area becomes 
 * blocksize.  The contents of padding area are all 0x00.
 */
typedef struct sfex_controldata {
  char magic[4];		/*  magic number */
  int version;			/*  version number */
  int revision;			/*  revision number */
  size_t blocksize;		/*  block size */
  int numlocks;			/*  number of locks */
} sfex_controldata;

typedef struct sfex_controldata_ondisk {
  uint8_t magic[4];
  uint8_t version[4];
  uint8_t revision[4];
  uint8_t blocksize[8];
  uint8_t numlocks[4];
} sfex_controldata_ondisk;

/*
 * sfex_lockdata --- lock data
 *
 * This data(number is sfex_controldata.numlocks) are allocated behind of 
 * sfex_controldata in the sfex meta-data area. The meaning of each member 
 * and the storage method to mata data area are following;
 *
 * lock status --- 1 byte. printable character. Content is either one of 
 * following;
 *  SFEX_STATUS_UNLOCK: It show the status that no node locks.
 *  SFEX_STATUS_LOCK: It show the status that nodename node is holding lock.
 *  (But there is an exception. Refer to explanation of "count" member.)
 *
 * increment counter --- 4 bytes. This is printable integer number and range 
 * is from 1 to 999. This must be left-justify, null(0x00) padding, and make 
 * a last byte null. The node holding a lock increments this counter  
 * periodically. If this counter does not increment for a certain period of 
 * time, we consider that the lock is invalid. If it overflow, return to 0. 
 * Initial value is 0.
 *
 * node name --- 256bytes. This is printable string. This must be left-justify, 
 * null(0x00) padding, and make a last byte null. This is node name that update 
 * lock data last. The node name must be same to get uname(2). Initial values 
 * are white spaces.
 *
 * padding --- The size of this member depend on blocksize. It is adjusted so 
 * that the whole of the control data including this padding area becomes 
 * blocksize.  The contents of padding area are all 0x00.
 */
typedef struct sfex_lockdata {
  char status;				/* status of lock */
  int count;				/* increment counter */
  char nodename[256];		/* node name */
} sfex_lockdata;

typedef struct sfex_lockdata_ondisk {
	uint8_t status;
	uint8_t count[4];
	uint8_t nodename[256];
} sfex_lockdata_ondisk;

/* character for lock status. This is used in sfex_lockdata.status */
#define SFEX_STATUS_UNLOCK 'u' /* unlock */
#define SFEX_STATUS_LOCK 'l'	/* lock */

/* features of each member of control data and lock data */
#define SFEX_MAGIC "SFEX"
#define SFEX_MIN_NUMLOCKS 1
#define SFEX_MAX_NUMLOCKS 999
#define SFEX_MIN_COUNT 0
#define SFEX_MAX_COUNT 999
#define SFEX_MAX_NODENAME (sizeof(((sfex_lockdata *)0)->nodename) - 1)

/* update macro for increment counter */
#define SFEX_NEXT_COUNT(c) (c >= SFEX_MAX_COUNT ? c - SFEX_MAX_COUNT : c + 1)

/* extern variables */
extern const char *progname;
extern char *nodename;
extern unsigned long sector_size;

#endif /* SFEX_H */
