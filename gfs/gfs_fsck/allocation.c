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
#include <string.h>
#include "interactive.h"
#include "global.h"

void *zalloc(size_t size){
  void *ptr;

  pp_print(PPD, "Allocating %d bytes.\n", (int)size);
  ptr = malloc(size);

  if(!ptr){
    fprintf(stderr, "Out of memory, exiting...\n");
    exit(EXIT_FAILURE);
  }
  memset(ptr, 0, size);
  return ptr;
}

typedef struct mem_info_s {
  void    *ptr;
  size_t  size;
  char    *filename;
  int     line;

  struct mem_info_s *next;
} mem_info_t;

static mem_info_t *mem_info = NULL;
  

void *zalloc_debug(size_t size, char *filename, int line){
  mem_info_t *tmp;

  tmp = zalloc(sizeof(mem_info_t));
  tmp->ptr = zalloc(size+128)+64;

  tmp->size = size;
  tmp->filename = filename;
  tmp->line = line;

  if(mem_info){
    tmp->next = mem_info;
    mem_info = tmp;
  } else {
    mem_info = tmp;
  }
  return tmp->ptr;
}

void free_debug(void *ptr, char *filename, int line){
  int i,first;
  uint64 *ptr_64;
  mem_info_t *tmp, *prev = NULL;

  for(first = 1, tmp = mem_info;
      tmp && tmp->ptr != ptr;
      tmp = tmp->next, first = 0)
    prev = tmp;
  
  if(!tmp){
    die("Attempting to free memory that is not in use.\n"
	"   File: %s\n"
	"   Line: %d\n", filename, line);
  }

  ptr_64 = (uint64 *)(tmp->ptr - 64);
  for(i=0; i < 8; i++, ptr_64++){
    if(*ptr_64 != 0){
      die("Underflow detected on memory segment.\n"
	  "Allocated at:\n"
	  "   File: %s\n"
	  "   Line: %d\n"
	  "Freed at:\n"
	  "   File: %s\n"
	  "   Line: %d\n", tmp->filename, tmp->line, filename, line);
    }
  }

  ptr_64 = (uint64 *)(tmp->ptr + tmp->size);
  for(i=0; i < 8; i++, ptr_64++){
    if(*ptr_64 != 0){
      die("Overflow detected on memory segment.\n"
	  "Allocated at:\n"
	  "   File: %s\n"
	  "   Line: %d\n"
	  "Freed at:\n"
	  "   File: %s\n"
	  "   Line: %d\n", tmp->filename, tmp->line, filename, line);
    }
  }

  if(first){
    mem_info = tmp->next;
  } else {
    prev->next = tmp->next;
  }
  free(tmp->ptr - 64);
  free(tmp);
}


int check_mem_bounds(void){
  int i, error=0;
  uint64 *ptr_64;
  mem_info_t *tmp;

  for(tmp = mem_info; tmp; tmp = tmp->next){
    ptr_64 = (uint64 *)(tmp->ptr - 64);
    for(i=0; i < 8; i++, ptr_64++){
      if(*ptr_64 != 0){
	pp_print(PPVH, "Underflow detected on memory segment.\n"
		 "Allocated at:\n"
		 "   File: %s\n"
		 "   Line: %d\n"
		 "   Size: %d\n"
		 "   Ptr : 0x%lx\n",
		 tmp->filename, tmp->line, (int)tmp->size, (unsigned long)tmp->ptr);
	error = -1;
	break;
      }
    }

    ptr_64 = (uint64 *)(tmp->ptr + tmp->size);
    for(i=0; i < 8; i++, ptr_64++){
      if(*ptr_64 != 0){
	pp_print(PPVH, "Overflow detected on memory segment.\n"
		 "Allocated at:\n"
		 "   File: %s\n"
		 "   Line: %d\n"
		 "   Size: %d\n"
		 "   Ptr : 0x%lx\n",
		 tmp->filename, tmp->line, (int)tmp->size, (unsigned long)tmp->ptr);
	error = -1;
	break;
      }
    }
  }
  return error;
}
  


void print_meminfo_debug(void){
  int total_entries = 0;
  int total_size = 0;
  mem_info_t *tmp;

  pp_print(PPVH,
	   "  Pointer        | Size     | File           | Line\n"
	   "  ======================================================\n");
  for(tmp = mem_info; tmp; tmp = tmp->next){
    total_size += tmp->size;
    total_entries +=1;
    pp_print(PPVH,
	     "  %-15lu| %-9d| %-15s| %-9d\n",
	     (unsigned long)tmp->ptr, (int)tmp->size, tmp->filename, tmp->line);
  }
  pp_print(PPVH, "Total entries: %d\n", total_entries);
  pp_print(PPVH, "Total unfreed memory: %d kB\n", total_size/1024);
}
