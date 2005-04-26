/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __HEXVIEW_DOT_H__
#define __HEXVIEW_DOT_H__

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X
#endif

EXTERN char *prog_name;
EXTERN int fd;
EXTERN unsigned int bsize INIT(GFS2_BASIC_BLOCK);
EXTERN uint64_t block INIT(0);
EXTERN unsigned int start INIT(0);
EXTERN int edit_mode INIT(0);

#define STRLEN (256)
#define SCREEN_HEIGHT   (16)
#define SCREEN_WIDTH    (16)

#define die(fmt, args...) \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ## args); \
  exit(EXIT_FAILURE); \
}

/*  I/O macros  */

#define do_lseek(fd, off) \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
	strerror(errno),__LINE__, __FILE__); \
}

#define do_read(fd, buff, len) \
{ \
  if (read((fd), (buff), (len)) != (len)) \
    die("bad read: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define do_write(fd, buff, len) \
{ \
  if (write((fd), (buff), (len)) != (len)) \
    die("bad write: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

/*  Memory macros  */

#define type_zalloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if ((ptr)) \
    memset((char *)(ptr), 0, sizeof(type) * (count)); \
  else \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define type_alloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if (!(ptr)) \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

/*  Divide x by y.  Round up if there is a remainder.  */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

#endif /* __HEXVIEW_DOT_H__ */
