#ifndef __OSI_USER_DOT_H__
#define __OSI_USER_DOT_H__

/*  Include Files
    These should only be the ones necessary to compile the common code.  */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>

#include "global.h"
#include "osi_list.h"



/*  Memory allocation abstraction  */

static __inline__ void *osi_malloc(unsigned int size)
{
  return malloc(size);
}

static __inline__ void osi_free(void *data, unsigned int size)
{
  free(data);
}



/*  Memory copy abstraction  */

#define osi_copy_to_user(uaddr, kaddr, len) (memcpy((uaddr), (kaddr), (len)) ? 0 : -EFAULT)
#define osi_clear_user(uaddr, len) ((memset((uaddr), 0, (len))) ? 0 : -EFAULT)
#define osi_copy_from_user(kaddr, uaddr, len) ((memcpy((kaddr), (uaddr), (len))) ? 0 : -EFAULT)
#define osi_strncpy_from_user(kaddr, uaddr, len) ((strncpy((kaddr), (uaddr), (len))) ? 0 : -EFAULT)

#define osi_read_access_ok(uaddr, len) (TRUE)
#define osi_write_access_ok(uaddr, len) (TRUE)

#define osi_memset memset
#define osi_memcpy memcpy
#define osi_memcmp memcmp

#define osi_strlen strlen
#define osi_strstr strstr
#define osi_strcmp strcmp
#define osi_strncmp strncmp
#define osi_strtok strtok
#define osi_strchr strchr
#define osi_strcpy strcpy
#define osi_strncpy strncpy
#define osi_strcasecmp strcasecmp
#define osi_strtod strtod
#define osi_strtol strtol



/*  printf() abstraction  */

#define osi_printf printf
#define osi_sprintf sprintf
#define osi_snprintf snprintf
#define osi_vsnprintf vsnprintf

#define osi_sscanf sscanf

#define osi_fprintf fprintf

#define osi_tty_printf printf



/*  panic abstractions  */

#define panic(fmt, args...) \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ## args); \
  exit(-1); \
}

#define osi_panic panic
#define osi_bug(x) \
{ \
  fprintf(stderr, "%s: BUG: %s\n", prog_name, (x)); \
  exit(-1); \
}



/*  GFS to VFS abstraction  */

typedef int osi_vfs_t;
typedef int osi_vnode_t;
typedef int osi_vfile_t;

#define vf2vn(vfile) ((osi_vnode_t *)(vfile))
#define vn2vfs(vnode) ((osi_vfs_t *)(vnode))



/*  I/O abstraction  */

struct buffer_head
{
  unsigned long b_blocknr;
  unsigned long b_size;
  unsigned long b_state;
  char *b_data;
  char *b_pdata;
  osi_list_t b_list;
};
typedef struct buffer_head osi_buf_t;



/*  Device type abstraction  */

typedef unsigned short osi_dev_t;

#define osi_make_dev MKDEV
#define osi_u2k_dev
#define osi_k2u_dev



/*  Atomic Bit Manipulation interface  */

struct osi_bitfield
{
  unsigned long bitfield;
};
typedef struct osi_bitfield osi_bitfield_t;

#define osi_test_bit(nr, addr) ((addr)->bitfield & (1 << nr))
#define osi_set_bit(nr, addr) ((addr)->bitfield |= (1 << nr))
#define osi_clear_bit(nr, addr) ((addr)->bitfield &= ~(1 << nr))
#define osi_test_and_set_bit(nr, addr) ((addr)->bitfield |= (1 << nr))
#define osi_test_and_clear_bit(nr, addr) ((addr)->bitfield &= ~(1 << nr))



/*  Atomic Counter Interface  */

struct osi_atomic
{
  int atomic;
};
typedef struct osi_atomic osi_atomic_t;

#define osi_atomic_inc(x) ((x)->atomic += 1)
#define osi_atomic_dec(x) ((x)->atomic -= 1)
#define osi_atomic_read(x) ((x)->atomic)
#define osi_atomic_dec_and_test(x) ((x)->atomic -= 1)
#define osi_atomic_set(x, y) ((x)->atomic = (y))



/*  Endianness conversion abstraction  */

#include "linux_endian.h"



/*  Filename structure  */

struct osi_filename
{
  unsigned char *name;
  unsigned int len;
};
typedef struct osi_filename osi_filename_t;



/*  Sleeping mutual exclusion primitive abstraction

    All macros take a pointer to a osi_mutex_t structure.
    osi_mutex_down_try() returns TRUE if the down() succeeds 
    */

typedef int osi_mutex_t;

#define osi_mutex_init(x)
#define osi_mutex_init_lkd(x)

#define osi_mutex_lock(x) 
#define osi_mutex_lock_intr(x)
#define osi_mutex_trylock(x) (TRUE)
#define osi_mutex_unlock(x)



/*  Reader/writer Sleeping mutual exclusion primitive abstraction  */

typedef int osi_mutex_rw_t;

#define osi_mutex_rw_init(x)

#define osi_mutex_read_lock(x)
#define osi_mutex_write_lock(x)
#define osi_mutex_read_trylock(x) (0)
#define osi_mutex_write_trylock(x) (0)
#define osi_mutex_read_unlock(x)
#define osi_mutex_write_unlock(x)



/*  Spinlock abstraction  */

typedef int osi_spin_t;

#define osi_spin_init(lock)

#define osi_spin_lock(lock)
#define osi_spin_unlock(lock)
#define osi_spin_trylock(lock) (TRUE)



/*  RW-Spinlock abstraction  */

typedef int osi_spin_rw_t;

#define osi_spin_rw_init(lock)

#define osi_spin_read_lock(lock)
#define osi_spin_write_lock(lock)
#define osi_spin_read_unlock(lock)
#define osi_spin_write_unlock(lock)
#define osi_spin_write_trylock(lock) (TRUE)



/*  Process abstraction  */

#define osi_pid() 1
#define osi_pname() ("main")

typedef int osi_task_t;

#define osi_task_is_set(taskp) (*(taskp))
#define osi_task_is_me(taskp) (TRUE)
#define osi_task_is_equal(task1p, task2p) (TRUE)
#define osi_task_to_pid(taskp) (1)
#define osi_task_to_pname(taskp) ("main")

#define osi_task_clear(taskp) do { *(taskp) = 0; } while (0)
#define osi_task_remember_me(taskp) do { *(taskp) = 1; } while (0)
#define osi_task_sleep(taskp, x) do { } while (0)
#define osi_task_wakeup(taskp) do { } while (0)



/*  Kernel Thead code  */

#define osi_daemonize(thread_name)
#define osi_undaemonize()



/*  Time abstraction  */

#define osi_current_time() time(NULL)

static __inline__ uint64 osi_gettimeofday(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64)tv.tv_sec * 1000000 + tv.tv_usec;
}

struct osi_clock_ticks
{
  unsigned long value;
};
typedef struct osi_clock_ticks osi_clock_ticks_t;

static __inline__ void osi_time_stamp(osi_clock_ticks_t *stamp)
{
}

static __inline__ int osi_check_timeout(osi_clock_ticks_t *stamp, int seconds)
{
  return 0;
}

static __inline__ unsigned int osi_time_diff(osi_clock_ticks_t *stamp)
{
  return FALSE;
}

static __inline__ int osi_time_valid(osi_clock_ticks_t *stamp)
{
  return FALSE;
}



/*  Timer abstraction  */


typedef int osi_timer_t;
typedef void (osi_timer_func)(void *);
typedef void (linux_timer_func)(unsigned long);

static __inline__ void osi_init_timer(osi_timer_t *t, osi_timer_func *fp, void *data)
{
}

static __inline__ void osi_del_timer(osi_timer_t *t)
{
}

static __inline__ void osi_set_timer(osi_timer_t *t, unsigned long sec)
{
}

static __inline__ void osi_set_deci_timer(osi_timer_t *t, unsigned long dsec)
{
}

static __inline__ int osi_timer_pending(osi_timer_t *t)
{
  return -ENOSYS;
}



/*  Schedule abstraction - Macro that makes a process temporarily
    give up control of the processor and lets other processes have
    a change to run. 
    Sleep abstraction - Sleep for x number of sections.  */

#define osi_schedule()
#define osi_sleep(x)
#define osi_sleep_intr(x)



/*  Wait queue abstraction  */

typedef int osi_wchan_t;

#define osi_wchan_init(x) do { } while (0)
#define osi_wchan_cond_sleep(chan, sleep_cond) do { } while (0)
#define osi_wchan_cond_sleep_intr(chan, sleep_cond) do { } while (0)
#define osi_wchan_wakeup(x) do { } while (0)



/*  Completion events  */

struct osi_completion
{
};
typedef struct osi_completion osi_completion_t;

#define osi_completion_init(x) do { } while (0)

#define osi_wait_for_completion(x) do { } while (0)
#define osi_complete(x) do { } while (0)



/*  Credentials structure  */

struct osi_cred
{
  uint32 cr_uid;
  uint32 cr_gid;
};
typedef struct osi_cred osi_cred_t;

#define osi_cred_to_uid(credp) ((credp) ? (credp)->cr_uid : 0)
#define osi_cred_to_gid(credp) ((credp) ? (credp)->cr_gid : 0)
#define osi_cred_in_group(credp, gid) ((credp) ? ((cred)->cr_gid == (gid)) : FALSE)



/*  Signals abstraction  */

#define osi_pending_signals() FALSE



/*  Weird errnos  */

#define ERESTARTSYS EINTR



/*  Module stuff  */

typedef int osi_module_t;

#define osi_module_this (NULL)

#define osi_module_init(mod) do { } while (0)
#define osi_module_inc(mod) do { } while (0)
#define osi_module_inc_cond(mod) (FALSE)
#define osi_module_dec(mod) do { } while (0)
#define osi_module_busy(mod) (TRUE)



/*  Other Stuff  */

extern char *prog_name;

#define OSI_CACHE_ALIGNED



#endif  /*  __OSI_USER_DOT_H__  */

