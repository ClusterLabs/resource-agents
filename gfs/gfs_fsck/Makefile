###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir= ..
 
include ${top_srcdir}/make/defines.mk

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config

.PHONY: all clean

sources = main.c initialize.c pass1.c pass1b.c pass1c.c \
	  pass2.c pass3.c pass4.c pass5.c \
	  block_list.c super.c bio.c ondisk.c file.c rgrp.c fs_bits.c util.c \
	  fs_bmap.c fs_inode.c fs_dir.c fs_recovery.c log.c hash.c \
	  inode_hash.c bitmap.c lost_n_found.c inode.c link.c metawalk.c \
	  eattr.c

CFLAGS+=-D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\" -Wall -g

all: gfs_fsck

gfs_fsck: $(sources:.c=.o)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) -MMD -c $(INCLUDES) $(CFLAGS) $< -o $@


test_block_list: block_list.o bitmap.o log.o test_block_list.o
	$(CC) $(CFLAGS) $^ -o $@

test_bitmap: bitmap.o test_bitmap.o log.o
	$(CC) $(CFLAGS) $^ -o $@
clean:
	@rm -f *.o *~

copytobin: gfs_fsck
	cp gfs_fsck ${top_srcdir}/bin

distclean: clean
	@rm -f *.d cscope.* gfs_fsck test_inode_list *.orig *.rej test_block_list test_bitmap


gfs_fsck.pot: $(sources)
	@xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(sources) > gfs_fsck.pot	


-include $(sources:.c=.d)
