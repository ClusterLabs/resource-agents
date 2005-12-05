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


#include "XML.h"
#include "Mutex.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>


using namespace ClusterMonitoring;
using namespace std;


XMLObject::XMLObject(const string& elem_name) :
  _tag(elem_name)
{}

XMLObject::~XMLObject()
{}


bool 
XMLObject::operator== (const XMLObject& obj) const
{
  if (children() != obj.children())
    return false;
  if (tag() != obj.tag())
    return false;
  if (attrs() != obj.attrs())
    return false;
  return true;
}

bool 
XMLObject::has_attr(const string& attr_name) const
{
  return _attrs.find(attr_name) != _attrs.end();
}

string 
XMLObject::set_attr(const string& attr_name, const string& value)
{
  string ret = _attrs[attr_name];
  _attrs[attr_name] = value;
  return ret;
}

string 
XMLObject::get_attr(const string& attr_name) const
{
  map<string, string>::const_iterator iter = _attrs.find(attr_name);
  if (iter == _attrs.end())
    return "";
  else
    return iter->second;
}

XMLObject&
XMLObject::add_child(const XMLObject& child)
{
  _kids.push_back(child);
  return _kids.back();
}

bool 
XMLObject::remove_child(const XMLObject& child)
{
  list<XMLObject>::iterator iter = find(_kids.begin(), _kids.end(), child);
  if (iter == _kids.end())
    return false;
  else {
    _kids.erase(iter);
    return true;
  }
}

void
XMLObject::generate_xml(string& xml, const string& indent) const
{
  xml += indent + "<" + _tag;
  for (map<string, string>::const_iterator iter = attrs().begin();
       iter != attrs().end();
       iter++) {
    const string& name = iter->first;
    const string& value = iter->second;
    xml += " " + name + "=\"" + value + "\"";
  }
  xml += ">\n";
  
  for (list<XMLObject>::const_iterator iter = children().begin();
       iter != children().end();
       iter++) {
    iter->generate_xml(xml, indent + "\t");
  }
  
  xml += indent + "</" + _tag + ">\n";
}

void
XMLObject::merge(const XMLObject& obj)
{
  if (tag() != obj.tag())
    throw string("XMLObject::merge(): tag mismatch");
  
  for (map<string, string>::const_iterator iter = obj.attrs().begin();
       iter != obj.attrs().end();
       iter++)
    _attrs[iter->first] = iter->second;
  
  for (list<XMLObject>::const_iterator iter_o = obj.children().begin();
       iter_o != obj.children().end();
       iter_o++) {
    const XMLObject& kid_o = *iter_o;
    bool merged = false;
    for (list<XMLObject>::iterator iter = _kids.begin();
	 iter != _kids.end();
	 iter++) {
      XMLObject& kid = *iter;
      if (kid.tag() == kid_o.tag() &&
	  kid.has_attr("name") && 
	  kid_o.has_attr("name") &&
	  kid.get_attr("name") == kid_o.get_attr("name")) {
	// same tag and name -->> merge
	merged = true;
	kid.merge(kid_o);
      }
    }
    if (!merged)
      _kids.push_back(kid_o);
  }
}




//  ***  GLOBAL FUNCTIONS  ***


static void
_parseXML(XMLObject& parent, xmlNode* children)
{
  for (xmlNode* curr_node = children; 
       curr_node;
       curr_node = curr_node->next) {
    if (curr_node->type == XML_ELEMENT_NODE) {
      
      XMLObject me((const char*) curr_node->name);
      
      // attrs
      for (xmlAttr* curr_attr = curr_node->properties;
	   curr_attr;
	   curr_attr = curr_attr->next) {
	if (curr_attr->type == XML_ATTRIBUTE_NODE) {
	  const xmlChar* name = curr_attr->name;
	  const xmlChar* value = xmlGetProp(curr_node, name);
	  try {
	    me.set_attr((const char*) name, (const char*) value);
	    xmlFree((void*) value);
	  } catch ( ... ) {
	    xmlFree((void*) value);
	    throw;
	  }
	}
      }
      
      // kids
      _parseXML(me, curr_node->children);
      
      parent.add_child(me);
    }
  }
}

XMLObject 
ClusterMonitoring::parseXML(const string& xml)
{
  static Mutex mutex;
  MutexLocker l(mutex);
  static bool initialized = false;
  if (!initialized) {
    LIBXML_TEST_VERSION;
    initialized = true;
  }
  
  xmlDoc* doc = xmlReadMemory(xml.c_str(),
			      xml.size(),
			      "noname.xml",
			      NULL,
			      XML_PARSE_NONET);
  if (!doc)
    throw string("parseXML(): couldn't parse xml");
  
  XMLObject root("if you see this, something wrong happened");
  try {
    _parseXML(root, xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return *(root.children().begin());
  } catch ( ... ) {
    xmlFreeDoc(doc);
    xmlCleanupParser();
    throw string("parseXML(): low memory");
  }
}

string 
ClusterMonitoring::generateXML(const XMLObject& obj)
{
  string xml("<?xml version=\"1.0\"?>\n");
  obj.generate_xml(xml, "");
  return xml;
}
