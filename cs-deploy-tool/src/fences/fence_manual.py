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


import err
import fence_base
import gtk


def get_fence_name():
    return 'Manual Fencing'


class FenceManualSharedInfo(fence_base.FenceBaseSharedInfo):
    def __init__(self):
        fence_base.FenceBaseSharedInfo.__init__(self)
        pass


class FenceManual(fence_base.FenceBase):

    def __init__(self, node, shared_info):
        fence_base.FenceBase.__init__(self, node, shared_info)
        
    
    def get_widget(self):
        return None
    
    def validate(self):
        return True
    
    def get_nodes_fence_tag(self):
        return '<device name="manual" nodename="' + self.node + '"/>'
    def get_fencedevice_tag(self):
        return '<fencedevice agent="fence_manual" name="manual"/>'
    
    
    
    
    
