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

#ifndef __INTERACTIVE_H__
#define __INTERACTIVE_H__

extern int pp_level;
extern int interactive;
extern char query_char;

#define PPD  -3 /* Print priority debug */
#define PPVL -2 /* Print priority very low */
#define PPL  -1 /* Print priority low */
#define PPN   0 /* Print priority normal */
#define PPH   1 /* Print priority high */
#define PPVH  2 /* Print priority very high */
/*********************************************
 ** PPD  - Debugging
 ** PPVL - Everything
 ** PPL  - Fluff
 ** PPN  - Normal
 ** PPH  - Important/Checkpoints
 ** PPVH - Failure notice (non-stderr)
 ** 
 ** For stderr notice, use die macro.
 ********************************************/
#ifdef DEBUG
#define pp_print(level, fmt, args...) \
{ \
  if(level >= pp_level){ \
    printf(fmt, ## args); \
  } \
  fflush(NULL); \
}
#else
#define pp_print(level, fmt, args...) \
{ \
  if(level >= pp_level && level > PPD){ \
    printf(fmt, ## args); \
  } \
  fflush(NULL); \
}
#endif

/* The query macro is used inside of if statements, like this:           **
** if(query("Do you want to remove block #%"PRIu64"? (y/n) ", block)){  **
**   remove_block_function(block);                                       **
** } else {                                                              **
**   printf("Block not removed.\n");                                     **
** }                                                                     */
#define query(fmt, args...) \
( \
  (!interactive || \
  ((printf(fmt "\n" , ## args)) && (!fflush(NULL)) &&\
  (scanf(" %c", &query_char)) && \
  ((query_char == 'y') || (query_char == 'Y')))) \
)


#define die(fmt, args...) \
{ \
  fprintf(stderr, "gfs_fsck: "); \
  fprintf(stderr, fmt, ## args); \
  exit(EXIT_FAILURE); \
}

#endif /* __INTERACTIVE_H__ */
