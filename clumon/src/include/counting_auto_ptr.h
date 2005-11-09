#ifndef counting_auto_ptr_h
#define counting_auto_ptr_h

#include <pthread.h>

template<class X>
class counting_auto_ptr
{
 public:
  explicit counting_auto_ptr(X* ptr = 0);
  counting_auto_ptr(const counting_auto_ptr<X>&);
  counting_auto_ptr<X>& operator=(const counting_auto_ptr<X>&);
  virtual ~counting_auto_ptr();
  
  X& operator*() const;
  X* operator->() const;
  
  X* get();
  
 private:
  X* ptr;
  
  pthread_mutex_t* mutex;
  int* counter;
  
};

#include "counting_auto_ptr.cpp"


#endif
