#ifndef _PTHREAD_DBG_H
#define _PTHREAD_DBG_H
#include <pthread.h>

#define pthread_mutex_lock(x) \
{\
	printf("pthread_mutex_lock(%s) @ %s:%d in %s\n",\
	       #x, __FILE__, __LINE__, __FUNCTION__); \
	pthread_mutex_lock(x);\
}

#define pthread_mutex_unlock(x) \
{\
	printf("pthread_mutex_unlock(%s) @ %s:%d in %s\n",\
	       #x, __FILE__, __LINE__, __FUNCTION__); \
	pthread_mutex_unlock(x);\
}


#define pthread_rwlock_rdlock(x) \
{\
	printf("pthread_rwlock_rdlock(%s) @ %s:%d in %s\n",\
	       #x, __FILE__, __LINE__, __FUNCTION__); \
	pthread_rwlock_rdlock(x);\
}

#define pthread_rwlock_unlock(x) \
{\
	printf("pthread_rwlock_unlock(%s) @ %s:%d in %s\n",\
	       #x, __FILE__, __LINE__, __FUNCTION__); \
	pthread_rwlock_unlock(x);\
}

#define pthread_rwlock_wrlock(x) \
{\
	printf("pthread_rwlock_wrlock(%s) @ %s:%d in %s\n",\
	       #x, __FILE__, __LINE__, __FUNCTION__); \
	pthread_rwlock_wrlock(x);\
}

#endif
