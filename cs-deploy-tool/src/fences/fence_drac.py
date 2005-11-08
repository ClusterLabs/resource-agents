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


import gtk
import gtk.glade

import err
import fence_base


def get_fence_name():
    return 'Dell Remote Accedd Card (DRAC)'


class FenceDracSharedInfo(fence_base.FenceBaseSharedInfo):
    def __init__(self):
        fence_base.FenceBaseSharedInfo.__init__(self)
        pass
    

class FenceDrac(fence_base.FenceBase, gtk.HBox):

    def __init__(self, node, shared_info):
        fence_base.FenceBase.__init__(self, node, shared_info)
        gtk.HBox.__init__(self)
        
        self.hostname_entry = self.glade_xml.get_widget('fence_drac_hostname')
        self.username_entry = self.glade_xml.get_widget('fence_drac_username')
        self.password_entry = self.glade_xml.get_widget('fence_drac_password')
        
        container = self.glade_xml.get_widget('fence_drac')
        container.unparent()
        self.pack_start(container)
        
    
    def get_widget(self):
        return self
    
    def validate(self):
        hostname, username, password = self.__get_info()
        if hostname == '':
            raise err.Err('node ' + self.node + ' missing fence device hostname')
        if username == '':
            raise err.Err('node ' + self.node + ' missing fence device username')
        if password == '':
            raise err.Err('node ' + self.node + ' missing fence device password')
    
    def get_nodes_fence_tag(self):
        template = '<device name=\"%s-drac\"/>'
        hostname, username, password = self.__get_info()
        return template % self.node
    
    def get_fencedevice_tag(self):
        template = '<fencedevice agent=\"fence_drac\" name=\"%s-drac\" ipaddr=\"%s\" login=\"%s\" passwd=\"%s\"/>'
        hostname, username, password = self.__get_info()
        return template % (self.node, hostname, username, password)
    
    def __get_info(self):
        hostname = self.hostname_entry.get_text().strip()
        username = self.username_entry.get_text().strip()
        password = self.password_entry.get_text().strip()
        return hostname, username, password
