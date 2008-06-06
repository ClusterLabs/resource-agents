TARGET1= fsck.gfs2
TARGET2= gfs2_fsck

SBINDIRT=$(TARGET1)
SBINSYMT=$(TARGET2)

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

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

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfs2 -lgfs2


${TARGET1}: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -fs ${TARGET1} ${TARGET2}	

depends:
	$(MAKE) -C ../libgfs2 all

test_block_list: log.o test_block_list.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_bitmap: test_bitmap.o log.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
	rm -f test_block_list test_bitmap

${TARGET1}.pot: $(OBJS:.o=.c)
	xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(OBJS:.o=.c) > ${TARGET1}.pot	

-include $(OBJS:.o=.d)

.PHONY: all ${TARGET1}
