#include <sys/types.h>
#include <linux/unistd.h>
#include <gettid.h>

_syscall0(pid_t,gettid)
