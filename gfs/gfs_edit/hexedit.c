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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "global.h"
#include <linux/gfs_ondisk.h>
#include "copyright.cf"

#define EXTERN
#include "hexedit.h"
#include "gfshex.h"




/******************************************************************************
*******************************************************************************
**
** void print_usage()
**
** Description:
**   This routine prints out the appropriate commands for this application.
**
*******************************************************************************
******************************************************************************/

void print_usage(void)
{
  fprintf(stderr, "\n\nSupported commands:\n");

  fprintf(stderr, "b <bytes>\t- Block size\n");
  fprintf(stderr, "d\t\t- Display GFS structure\n");
  fprintf(stderr, "dx\t\t- Display GFS extended structure\n");
  fprintf(stderr, "e\t\t- Edit mode on and off\n");
  fprintf(stderr, "l\t\t- More Lines\n");
  fprintf(stderr, "n\t\t- Next\n");
  fprintf(stderr, "p\t\t- Previous\n");
  fprintf(stderr, "q\t\t- Quit\n");
  fprintf(stderr, "r\t\t- Reprint\n");
  fprintf(stderr, "s <bn>\t\t- Search\n");
  fprintf(stderr, "sx <bn>\t\t- Search to hex block\n");

  fprintf(stderr, "\nEdit mode operations:\n");
  fprintf(stderr, "set <row> <col> <byte>\n");
  fprintf(stderr, "  A particular byte within the current window is specified\n");
  fprintf(stderr, "  by row and column numbers.  The third argument is the\n");
  fprintf(stderr, "  new value in hex (00 to FF).\n\n");
}


/******************************************************************************
*******************************************************************************
**
** int display()
**
** Description:
**   This routine...
**
** Input(s):
**   start - 
**   end   - 
**
** Returns:
**
**
*******************************************************************************
******************************************************************************/

int display()
{
  char *buf;
  unsigned int row, col;
  uint64 dev_offset;
  unsigned int size, offset;


  /*  Make sure all I/O is 512-byte aligned  */

  dev_offset = (block * bsize + start * SCREEN_WIDTH) >> 9 << 9;
  offset = (block * bsize + start * SCREEN_WIDTH) - dev_offset;
  size = DIV_RU(SCREEN_HEIGHT * SCREEN_WIDTH, 512) * 512;
 

  type_alloc(buf, char, size);

  do_lseek(fd, dev_offset);
  do_read(fd, buf, size);


  printf("\n\nblock size = %u\n", bsize);
  printf("block = %"PRIu64" = 0x%.16"PRIX64"\n",
	 block, block);
  printf("offset in block = %u\n\n", start * SCREEN_WIDTH); 

  if (edit_mode)
  {
    printf("    ");
    for (col = 0; col < SCREEN_WIDTH; col++)
      printf("%.2u ", col);
    printf("\n\n");
  }

  for (row = 0; row < SCREEN_HEIGHT; row++)
  {
    if (edit_mode)
      printf("%.2u  ", row);

    for (col = 0; col < SCREEN_WIDTH; col++)
      printf("%.2X ", (unsigned char)buf[offset + SCREEN_WIDTH * row + col]);

    printf("    ");

    for (col = 0; col < SCREEN_WIDTH; col++)
    {
      if (isprint(buf[offset + SCREEN_WIDTH * row + col]))
	printf("%c", buf[offset + SCREEN_WIDTH * row + col]);
      else
	printf(" ");
    }

    printf("\n");
  }


  free(buf);


  return(0);
}


/******************************************************************************
*******************************************************************************
**
** int run_command()
**
** Description:
**   This routine...
**
** Input(s):
**  *cmd  - 
**  *arg1 - 
**  *arg2 - 
**
** Output(s):
**
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/

int run_command(char *cmd, char *arg1, char *arg2, char *arg3)
{
  int error;

  if (*cmd == 'b')  /*  Change block size  */
  {
    bsize = atoi(arg1);
    if (!bsize)
      bsize = GFS_BASIC_BLOCK;

    block = 0;
    start = 0;

    display();
  }


  else if (!strncmp(cmd, "dx", 2))  /*  Display GFS structure  */
  {
    display_gfs(TRUE);
  }


  else if (*cmd == 'd')  /*  Display GFS structure  */
  {
    display_gfs(FALSE);
  }


  else if (*cmd == 'e')  /*  Edit GFS structure  */
  {
    edit_mode = !edit_mode;
  }


  else if (*cmd == 'l')  /*  Next few lines  */
  {
    start += SCREEN_HEIGHT;
    
    if (start * SCREEN_WIDTH >= bsize)
    {
      start = 0;
      block++;
    }

    display();
  }


  else if (*cmd == 'n')  /*  Next  */
  {
    start = 0;
    block++;
    display();
  }


  else if (*cmd == 'p')  /*  Previous  */
  {
    start = 0;
    block--;
    if (block < 0)
      block = 0;

    display();
  }


  else if (*cmd == 'q')  /*  Quit  */
  {
    printf("\n\n");
    exit(0);
  }


  else if (*cmd == 'r')  /*  Reprint  */ 
  {
    start = 0;

    display();
  }


  else if (!strncmp(cmd, "sx", 2))  /*  Search in Hex  */
  {
    start = 0;
    sscanf(arg1, "%"SCNx64, &block);

    display();
  }


  else if (!strncmp(cmd, "set", 3))  /*  Set byte in current window  */
  {
    if (!edit_mode)
    {
      fprintf(stderr, "%s:  not in edit mode\n", prog_name);
      print_usage();
      return(0);
    }

    error = edit_gfs(arg1, arg2, arg3);

    if (!error)
      display();
  }


  else if (*cmd == 's')  /*  Search  */
  {
    start = 0;
    sscanf(arg1, "%"SCNd64, &block);

    display();
  }


  else
  {
    fprintf(stderr, "%s:  unknown command (%s)\n", prog_name, cmd);
    print_usage();
  }


  return(0);
}


/******************************************************************************
*******************************************************************************
**
** main()
**
** Description:
**   Do everything
**
*******************************************************************************
******************************************************************************/

int main(int argc, char *argv[])
{
  char line[STRLEN];
  char cmd[STRLEN];
  char arg1[STRLEN];
  char arg2[STRLEN];
  char arg3[STRLEN];


  prog_name = argv[0];

  if (argc < 2)
    die("no device specified\n");
  
  if(!strcmp(argv[1], "-V")) {
    printf("%s %s (built %s %s)\n", prog_name, GFS_RELEASE_NAME,
         __DATE__, __TIME__);
    printf("%s\n", REDHAT_COPYRIGHT);
    exit(0);
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0)
    die("can't open %s: %s\n", argv[1], strerror(errno));



  start = 0;
  display();
  
  printf("\n\n> ");


  while (fgets(line, STRLEN - 1, stdin) != NULL)
  {
    sscanf(line, "%s %s %s %s", cmd, arg1, arg2, arg3);
      
    if (!strncmp(cmd, "", 1))
    {
      /* Do nothing */ 
    }
    else
    {
      printf("\n");
      run_command(cmd, arg1, arg2, arg3);
    }

    printf("\n\n> ");
	
    memset(line, 0, STRLEN);
    memset(cmd, 0, STRLEN);
    memset(arg1, 0, STRLEN);
    memset(arg2, 0, STRLEN);
    memset(arg3, 0, STRLEN);
  }


  printf("\n\n");


  exit(EXIT_SUCCESS);
}


