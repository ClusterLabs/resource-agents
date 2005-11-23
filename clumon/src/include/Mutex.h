/*
  Copyright Red Hat, Inc. 2005

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/*
 * Author: Stanko Kupcevic <kupcevic@redhat.com>
 */


#ifndef Mutex_h
#define Mutex_h

#include <pthread.h>


namespace ClusterMonitoring 
{


class Mutex
{
 public:
  Mutex()
    {
      pthread_mutexattr_t attr;
      pthread_mutexattr_init(&attr);
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
      pthread_mutex_init(&_mutex, &attr);
      pthread_mutexattr_destroy(&attr);
    }
  virtual ~Mutex()
    { pthread_mutex_destroy(&_mutex); }
  
  void lock()
    { pthread_mutex_lock(&_mutex); }
  void unlock()
    { pthread_mutex_unlock(&_mutex); }
  
 private:
  pthread_mutex_t _mutex;
  
  Mutex(const Mutex&);
  Mutex& operator= (const Mutex&);
};  // class Mutex


class MutexLocker
{
 public:
  MutexLocker(Mutex& m) :
    _mutex(m) { _mutex.lock(); }
  virtual ~MutexLocker() { _mutex.unlock(); }
  
 private:
  Mutex& _mutex;
  
  MutexLocker(const MutexLocker&);
  MutexLocker& operator= (const MutexLocker&);
};  // class MutexLocker


};  // namespace ClusterMonitoring 


#endif  // Mutex_h
