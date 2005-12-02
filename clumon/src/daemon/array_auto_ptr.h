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


#ifndef array_auto_ptr_h
#define array_auto_ptr_h


namespace ClusterMonitoring
{


template <class T>
class array_auto_ptr
{
 public:
  array_auto_ptr(T* array) : _arr(array) {}
  virtual ~array_auto_ptr() { delete[] _arr; }
  
  T& operator [] (unsigned int i) { return _arr[i]; }
  T* get() { return _arr; }
  
 private:
  T* _arr;
  
  array_auto_ptr(const array_auto_ptr&);
  array_auto_ptr& operator= (const array_auto_ptr&);
};  // class array_auto_ptr

 
};  // namespace ClusterMonitoring


#endif  // array_auto_ptr_h
