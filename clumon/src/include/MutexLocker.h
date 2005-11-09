#ifndef MutexLocker_h
#define MutexLocker_h

#include <pthread.h>


class MutexLocker
{
public:
  
  MutexLocker(pthread_mutex_t* mutex) : 
    mutex(mutex)
  {
    pthread_mutex_lock(mutex);
  }
  MutexLocker(const MutexLocker&);
  MutexLocker& operator=(const MutexLocker&);
  virtual ~MutexLocker()
  {
    pthread_mutex_unlock(mutex);
  }
  
private:
  pthread_mutex_t* mutex;
  
};





#endif
