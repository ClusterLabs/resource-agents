CFLAGS+=@XMLCFLAGS@ # -DMDEBUG -DFDEBUG
sbin_PROGRAMS = clurgmgrd clurmtabd

clurgmgrd_SOURCES = rg_thread.c rg_locks.c main.c groups.c rg_state.c \
	rg_queue.c members.c rg_forward.c reslist.c resrules.c restree.c \
	fo_domain.c
clurgmgrd_LDADD=@XMLLIB@ @MAGMALIB@ @CCSLIB@ -L../clulib -lclulib

clurmtabd_SOURCES = clurmtabd.c clurmtabd_lib.c
clurmtabd_LDADD=-L../clulib -lclulib
