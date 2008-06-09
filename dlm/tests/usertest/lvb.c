/* Simple LVB test prog */

#ifdef VMS
#include starlet
#include psldef
#include ssdef
#include errno
#include descrip
#include rsdmdef
#include lckdef

#include stdio
#include stdlib
#include string

struct lksb
{
    int sb_status;
    int sb_lkid;
    char sb_lvb[16];
};
#endif

#ifdef __linux__
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <libdlm.h>
#define LCK$M_VALBLK LKF_VALBLK
#define LCK$M_CONVERT LKF_CONVERT
#define LCK$K_CRMODE LKM_CRMODE
#define LCK$K_PWMODE LKM_PWMODE
#define PSL$C_USER 0
#define SS$_NORMAL 0
#define SS$_DEADLOCK EDEADLOCK
#define EVMSERR errno
#define sys$exit exit
#define sys$hiber pause


struct lksb {
	int sb_status;
	uint32_t sb_lkid;
	char sb_flags;
	char *sb_lvb;
};

struct dsc$descriptor_s
{
	int dsc$w_length;
	char *dsc$a_pointer;
};

static int sys$enq(int efn, int mode, struct lksb *lksb, int flags,
		   struct dsc$descriptor_s *name, int parent,
		   void *compast, void *astarg, void *blockast,
		   int accmode, int nullarg)
{
	if (name)
		return dlm_lock(mode, (struct dlm_lksb *)lksb, flags,
				name->dsc$a_pointer,
				name->dsc$w_length,
				parent,
				compast, astarg,
				blockast, NULL);
	else
		return dlm_lock(mode, (struct dlm_lksb *)lksb, flags,
				NULL,
				0,
				parent,
				compast, astarg,
				blockast, NULL);
}

static int sys$deq(int lkid, struct lksb *lksb, int accmode, int flags, int c)
{
	return dlm_unlock(lkid, flags, (struct dlm_lksb *)lksb, NULL);
}

static char *linux_strerror(int vmserr, int err)
{
	return strerror(err);
}
#define strerror linux_strerror

#endif


static struct lksb our_lksb;
static int cur_mode;
static void compast_routine(void *arg);
static void blockast_routine(void *arg, int mode);
static char *name = "TESTLOCK";

#ifdef __linux__
static char lksb_lvb[DLM_LVB_LEN];
#endif

static void start_lock(int mode)
{
    struct dsc$descriptor_s name_s;
    int status;

    cur_mode = mode;

/* Make a descriptor of the name */
    memset(&name_s, 0, sizeof(struct dsc$descriptor_s));
    name_s.dsc$w_length = strlen(name);
    name_s.dsc$a_pointer = name;

    /* Lock it */
    status = sys$enq(0,
		      cur_mode,
		      &our_lksb,
		      LCK$M_VALBLK, /* flags */
		      &name_s,
		      0, /* parent */
		      compast_routine,
		      0, /* astp */
		      blockast_routine,
		      PSL$C_USER,
 		      0);

    if (status != SS$_NORMAL)
    {
        printf("lock enq failed : %s\n", strerror(EVMSERR, status));
	sys$exit(status);
    }

    printf("Lock ID is %x\n", our_lksb.sb_lkid);
    return;
}

static int convert_lock(int mode, char *lvb)
{
    int status;

    memset(our_lksb.sb_lvb, 0, 16);
    if (lvb[strlen(lvb)-1] == '\n')
      lvb[strlen(lvb)-1] = '\0';

    cur_mode = mode;
    strcpy(our_lksb.sb_lvb, lvb);
    printf("converting to %d\n", mode);

    /* Lock it */
    status = sys$enq(0,
		     mode,
		     &our_lksb,
		     LCK$M_CONVERT | LCK$M_VALBLK,
		     NULL,
		     0, /* parent */
		     compast_routine,
		     0, /* astp */
		     blockast_routine,
		     PSL$C_USER,
		     0);

    if (status != SS$_NORMAL)
    {
        printf("convert enq failed : %s\n", strerror(EVMSERR, status));
    }

    return status;
}

static void unlock()
{
    int status;

    status = sys$deq(our_lksb.sb_lkid, NULL,0,0,0);
    if (status != SS$_NORMAL)
    {
        printf("denq failed : %s\n", strerror(EVMSERR, status));
    }
}

static void compast_routine(void *arg)
{
#ifdef __linux__
    if (our_lksb.sb_flags & DLM_SBF_VALNOTVALID)
    {
        printf("testlock: LVB not valid\n");
	sys$hiber();
    }
#endif
    if (our_lksb.sb_status == SS$_DEADLOCK)
    {
	printf("testlock: deadlocked\n");
	unlock();
	return;
    }

    if (our_lksb.sb_status == SS$_NORMAL)
    {
	printf("testlock. compast, now at %d, lvb=%s\n", cur_mode, our_lksb.sb_lvb);
    }
    else
    {
	printf("testlock: lock failed (compast): %s\n", strerror(EVMSERR, our_lksb.sb_status));
	sys$hiber();
    }
}

static void blockast_routine(void *arg, int mode)
{
    printf("testlock. blkast.\n");
}

int main(int argc, char *argv[])
{
    char buf[80];

#ifdef __linux__
    our_lksb.sb_lvb = lksb_lvb;
    dlm_pthread_init();
#endif

    if (argc > 1)
      name = argv[1];

    start_lock(LCK$K_CRMODE);

    do {
      fgets(buf, sizeof(buf), stdin);
      char cmd = buf[0] & 0x5F;

      if (cmd == 'W')
	convert_lock(LCK$K_CRMODE, &buf[1]);
      else
	convert_lock(LCK$K_PWMODE, &buf[1]);
    }
    while (buf[0] != 'x');

    return SS$_NORMAL;
}

