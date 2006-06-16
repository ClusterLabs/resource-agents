#include <sys/types.h>
#include <linux/unistd.h>
#include <gettid.h>
#include <unistd.h>
#include <errno.h>

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
