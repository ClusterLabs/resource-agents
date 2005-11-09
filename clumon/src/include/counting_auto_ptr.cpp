
//#include "counting_auto_ptr.h"
#include <MutexLocker.h>



template<class X>
counting_auto_ptr<X>::counting_auto_ptr(X* ptr) : ptr(ptr)
{
  try {
    counter = new int(1);
  } catch ( ... ) {
    delete ptr;
    throw;
  }
  
  try {
    mutex = new pthread_mutex_t;
  } catch ( ... ) {
    delete counter;
    delete ptr;
    throw;
  }
  
  // init mutex
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init(mutex, &attr);
  pthread_mutexattr_destroy(&attr);
};

template<class X>
counting_auto_ptr<X>::counting_auto_ptr(const counting_auto_ptr<X>& o)
{
  MutexLocker lock(o.mutex);
  ptr = o.ptr;
  mutex = o.mutex;
  counter = o.counter;
  *counter += 1;
};

template<class X> 
counting_auto_ptr<X>&
counting_auto_ptr<X>::operator=(const counting_auto_ptr<X>& o)
{
  if (&o == this)
    return *this;
  
  this->~counting_auto_ptr();
  
  MutexLocker lock(o.mutex);
  ptr = o.ptr;
  mutex = o.mutex;
  counter = o.counter;
  *counter += 1;
  return *this;
};

template<class X>
counting_auto_ptr<X>::~counting_auto_ptr()
{
  bool last = false;
  {
    MutexLocker lock(mutex);
    last = (--(*counter) == 0);
  }
  if (last) {
    delete counter;
    delete ptr;
    pthread_mutex_destroy(mutex);
    delete mutex;
  }
};


template<class X>
X&
counting_auto_ptr<X>::operator*() const
{
  return *ptr;
};

template<class X>
X*
counting_auto_ptr<X>::operator->() const
{
  return ptr;
};

template<class X>
X*
counting_auto_ptr<X>::get()
{
  return ptr;
};

