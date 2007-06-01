###############################################################################
###############################################################################
##
##  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET1= gfs2_fsck
TARGET2= fsck.gfs2

OBJS=	eattr.o \
	fs_recovery.o \
	hash.o \
	initialize.o \
	inode_hash.o \
	link.o \
	lost_n_found.o \
	main.o \
	metawalk.o \
	pass1.o \
	pass1b.o \
	pass1c.o \
	pass2.o \
	pass3.o \
	pass4.o \
	pass5.o \
	rgrepair.o \
	util.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -MMD -Wall -O2
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I../include -I../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfs2 -lgfs2

all: depends ${TARGET1} ${TARGET2}

${TARGET1}: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -fs ${TARGET1} ${TARGET2}	

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../libgfs2 all

test_block_list: log.o test_block_list.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_bitmap: test_bitmap.o log.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f ${TARGET1} ${TARGET2} *.o *~ *.d cscope.* test_inode_list *.orig *.rej test_block_list test_bitmap

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET1} ${sbindir}
	ln -sf ${sbindir}/${TARGET1} ${sbindir}/${TARGET2}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}

${TARGET1}.pot: $(OBJS:.o=.c)
	xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(OBJS:.o=.c) > ${TARGET1}.pot	

-include $(OBJS:.o=.d)

.PHONY: all ${TARGET1}
