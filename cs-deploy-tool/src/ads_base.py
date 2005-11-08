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


from err import Err


class ADSBase:
    
    def __init__(self):
        pass
    
    # name displayed in GUI
    def get_pretty_name(self):
        raise 'not implemented'
    
    # gtk widget displayed under pretty name in GUI
    def get_config_widget(self):
        raise 'not implemented'
    
    # return names of all services configured by this module
    def get_service_names(self):
        raise 'not implemented'
    
    # raise Err on failure
    def validate(self):
        raise 'not implemented'
    
    # return list of rpms to be installed on all nodes
    def get_rpms(self):
        raise 'not implemented'
    
    # return service tag of cluster.conf
    def generate_xml(self):
        raise 'not implemented'
    
    # raise Err on failure
    # shared storage is already mounted at self.storage.get_mountpoint() on all nodes
    def configure_nodes(self, nodes):
        raise 'not implemented'
    
    # messages displayed at the end of installation
    def get_post_install_messages(self):
        raise 'not implemented'
