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


#ifndef XML_h
#define XML_h

#include <string>
#include <map>
#include <list>


namespace ClusterMonitoring 
{


class XMLObject
{
 public:
  XMLObject(const std::string& elem_name = "TagName");
  virtual ~XMLObject();
  
  std::string tag() const 
    { return _tag; };
  
  // attributes
  bool has_attr(const std::string& attr_name) const;
  std::string set_attr(const std::string& attr_name, 
		       const std::string& value); // return old value
  std::string get_attr(const std::string& attr_name) const;
  const std::map<std::string, std::string>& attrs() const
    { return _attrs; }
  
  // kids
  XMLObject& add_child(const XMLObject& child);
  bool remove_child(const XMLObject& child);
  const std::list<XMLObject>& children() const
    { return _kids; }
  
  void merge(const XMLObject&);
  
  bool operator== (const XMLObject&) const;
  
 private:
  std::string _tag;
  std::list<XMLObject> _kids;
  std::map<std::string, std::string> _attrs;
  void generate_xml(std::string& xml, const std::string& indent) const;
  friend std::string generateXML(const XMLObject& obj);
};


XMLObject parseXML(const std::string& xml);
std::string generateXML(const XMLObject& obj);


};  // namespace ClusterMonitoring 


#endif  // XML_h
