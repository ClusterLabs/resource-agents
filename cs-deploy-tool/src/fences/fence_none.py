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


# purposely commented out, so it is not used
#def get_fence_name():
#    return 'No Fencing'


class FenceNoneSharedInfo(fence_base.FenceBaseSharedInfo):
    def __init__(self):
        fence_base.FenceBaseSharedInfo.__init__(self)
        pass
    

class FenceNone(fence_base.FenceBase):
    
    def __init__(self, node, shared_info):
        fence_base.FenceBase.__init__(self, node, shared_info)
        
    
    def get_widget(self):
        return None
    
    def validate(self):
        return True
    
    def get_nodes_fence_tag(self):
        return ''
    def get_fencedevice_tag(self):
        return ''
    
    
    
    
    
