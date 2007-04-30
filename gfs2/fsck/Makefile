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

include ../../make/defines.mk

TARGET= gfs2_fsck

.PHONY: all clean

sources = main.c initialize.c pass1.c pass1b.c pass1c.c pass2.c pass3.c \
	  pass4.c pass5.c util.c fs_recovery.c lost_n_found.c link.c \
	  eattr.c hash.c inode_hash.c metawalk.c

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -MMD -Wall -O2
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I../include -I../libgfs2

LDFLAGS += -L../libgfs2 -lgfs2

all: ${TARGET}

${TARGET}: $(sources:.c=.o)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

test_block_list: log.o test_block_list.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_bitmap: test_bitmap.o log.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f gfs2_fsck *.o *~ *.d

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

distclean: clean
	@rm -f cscope.* test_inode_list *.orig *.rej test_block_list test_bitmap

gfs2_fsck.pot: $(sources)
	@xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(sources) > gfs2_fsck.pot	

-include $(sources:.c=.d)
