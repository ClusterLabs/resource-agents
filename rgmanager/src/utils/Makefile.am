INCLUDES=-I${top_srcdir}/include
LDADD=-L../clulib -lclulib
sbin_PROGRAMS=clubufflush clufindhostname clustat clusvcadm clulog

#
# XXX need to include clunfsops again once we start hacking for the NFS
# failover syscalls... grr
#

#
# This should be obsolete, and ideally will go away soon.
#
#cluarp_SOURCES=cluarp.c

clulog_SOURCES=clulog.c

clubufflush_SOURCES=clubufflush.c

clufindhostname_SOURCES=clufindhostname.c

clustat_SOURCES=clustat.c
clustat_LDADD=@MAGMALIB@ @NCURSESLIB@ -L../clulib -lclulib

clusvcadm_SOURCES=clusvcadm.c
clusvcadm_LDADD=@MAGMALIB@ -L../clulib -lclulib
