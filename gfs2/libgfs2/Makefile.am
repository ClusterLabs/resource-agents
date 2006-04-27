# Use this to build static library libgfs2.a:
     noinst_LIBRARIES = libgfs2.a
     libgfs2_a_SOURCES = buf.c device_geometry.c fs_geometry.c fs_ops.c iddev.c locking.c misc.c ondisk.c size.c structures.c
     libgfs2_a_CFLAGS = -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\" -Wall -g
# Use this to build dynamic library .libs/libgfs2.so.0.0.0
#     lib_LTLIBRARIES = libgfs2.la
#     lib_LTLIBRARIES = libgfs2.la
#     libgfs2_la_SOURCES = buf.c  device_geometry.c  fs_geometry.c  fs_ops.c  iddev.c  locking.c  misc.c  ondisk.c  size.c  structures.c
#     libgfs2_la_CFLAGS = -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"${RELEASE}\" -Wall -g
#     libgfs2_la_LIBADD = $(INTLLIBS) $(ALLOCA)
     localedir = $(datadir)/locale
     INCLUDES = -I../include -I../config -I/home/devel/gfs2-2.6/fs/gfs2/ -I/home/devel/gfs2-2.6/include/
     include_HEADERS = gfs2_disk_hash.h libgfs2.h iddev.h linux_endian.h ondisk.h osi_list.h
#     LIBTOOL_DEPS = @LIBTOOL_DEPS@
#     libtool: $(LIBTOOL_DEPS)
