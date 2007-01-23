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

top_srcdir=..

include ${top_srcdir}/make/defines.mk

TARGET= gfs2_fsck

INCLUDES= -I${top_srcdir}/include -I${top_srcdir}/config -I${top_srcdir}/libgfs2 -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
LIBS=${top_srcdir}/libgfs2

.PHONY: all clean

sources = main.c initialize.c pass1.c pass1b.c pass1c.c pass2.c pass3.c \
	pass4.c pass5.c util.c fs_recovery.c \
	lost_n_found.c link.c eattr.c hash.c inode_hash.c metawalk.c

CFLAGS+=-D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\" -Wall -O2

all: ${TARGET}

gfs2_fsck: $(sources:.c=.o) $(LIBS)/libgfs2.a
	$(CC) $(CFLAGS) -L$(LIBS) $^ -o $@ -lgfs2

%.o: %.c
	$(CC) -MMD -c $(INCLUDES) $(CFLAGS) $< -o $@


test_block_list: log.o test_block_list.o
	$(CC) $(CFLAGS) $^ -o $@

test_bitmap: test_bitmap.o log.o
	$(CC) $(CFLAGS) $^ -o $@
clean:
	@rm -f gfs2_fsck *.o *~ *.d

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	strip ${TARGET}
	install -m755 ${TARGET} ${sbindir}

distclean: clean
	@rm -f cscope.* gfs2_fsck test_inode_list *.orig *.rej test_block_list test_bitmap


gfs2_fsck.pot: $(sources)
	@xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(sources) > gfs2_fsck.pot	


-include $(sources:.c=.d)
