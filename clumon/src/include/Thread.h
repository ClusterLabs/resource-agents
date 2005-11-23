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


#ifndef Thread_h
#define Thread_h

#include <pthread.h>
#include "Mutex.h"


namespace ClusterMonitoring 
{


class Thread
{
 public:
  Thread();
  virtual ~Thread();
  
  // not to be called from run()
  virtual void start();
  virtual void stop();
  virtual bool running();
  
 protected:
  virtual bool shouldStop(); // kids, return from run() if true, check it often
  virtual void run() = 0;  // run in new thread
  
 private:
  pthread_t _thread;
  
  bool _stop;
  Mutex _stop_mutex;
  
  bool _running;
  Mutex _main_mutex;
  
  Thread(const Thread&);
  Thread& operator= (const Thread&);
  friend void* start_thread(void*);
};  // class Thread


};  // namespace ClusterMonitoring 


#endif  // Thread_h
