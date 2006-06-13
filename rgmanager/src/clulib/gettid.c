#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <gettid.h>
#include <errno.h>
#include <unistd.h>

/* Patch from Adam Conrad / Ubuntu: Don't use _syscall macro */

#ifdef __NR_gettid
pid_t gettid (void)
{
	return syscall(__NR_gettid);
}
#else

#warn "gettid not available -- substituting with pthread_self()"

#include <pthread.h>
pid_t gettid (void)
{
	return (pid_t)pthread_self();
}
#endif
