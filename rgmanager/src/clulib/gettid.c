#include <sys/types.h>
#include <linux/unistd.h>
#include <gettid.h>
#include <errno.h>

_syscall0(pid_t,gettid)
