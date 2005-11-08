#
#  Copyright Red Hat, Inc. 2005
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.

# Author: Stanko Kupcevic <kupcevic@redhat.com>
#


from IPAddrEntry import IP

from err import Err


class ResourceIP:
    
    def __init__(self):
        self.ip = IP()
        self.set_enabled(True)
        
    
    def validate(self):
        if self.get_enabled():
            if self.ip.isValid() != True:
                raise Err('invalid IP address')
        
    def get_widget(self):
        return self.ip
    
    def generate_xml(self):
        if self.get_enabled():
            addr = self.ip.getAddrAsString()
            return '<ip address="%s" monitor_link="1"/>' % addr
        else:
            return ''
    
    def get_enabled(self):
        return self.get_widget().get_property('sensitive')
    def set_enabled(self, bool):
        self.get_widget().set_sensitive(bool)
        
    
