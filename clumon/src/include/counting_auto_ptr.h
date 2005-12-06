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


#ifndef counting_auto_ptr_h
#define counting_auto_ptr_h

#include "Mutex.h"


namespace ClusterMonitoring 
{


template<class X>
class counting_auto_ptr
{
 public:
  explicit counting_auto_ptr(X* ptr = 0);
  counting_auto_ptr(const counting_auto_ptr<X>&);
  counting_auto_ptr<X>& operator= (const counting_auto_ptr<X>&);
  virtual ~counting_auto_ptr();
  
  X& operator*() const;
  X* operator->() const;
  
  bool operator== (const counting_auto_ptr<X>& a) const 
    { return _ptr == a._ptr; }
  
  X* get();
  
 private:
  X* _ptr;
  
  Mutex* _mutex;
  int* _counter;
  
  void decrease_counter();
  
};

#include "counting_auto_ptr.cpp"


};  // namespace ClusterMonitoring 


#endif
