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
from constants import *



# define this function in every fence file
def get_fence_name():
    raise 'not implemented'


class FenceBaseSharedInfo:
    
    def __init__(self):
        gladepath = INSTALLDIR + '/fences/fences.glade'
        self.glade_xml = gtk.glade.XML(gladepath)
       	self.gladepath = gladepath
	 
        self.__win = self.glade_xml.get_widget('shared_dialog')
        self.__holder = self.glade_xml.get_widget('add_config_here')
        
        self.__added = False
        
        pass
    
    
    ### override ###
    
    # raise Err if validation failed
    def validate(self):
        raise 'not implemented'
    
    
    ### don't override ###
    
    # return False if cancelled
    def configure(self):
        if not self.__added:
            return True
        self.__win.show_all()
        while True:
            if self.__win.run() == gtk.RESPONSE_DELETE_EVENT:
                return False
            try:
                self.validate()
            except err.Err, e:
                print e.get()
                continue
            break
        self.__win.hide()
        return True
    
    def add(self, gtkobj):
        self.__holder.pack_start(gtkobj)
        self.__added = True
    
    


class FenceBase:
    
    def __init__(self, node, shared_info):
        self.node = node
        self.shared_info = shared_info
        
        gladepath = shared_info.gladepath
        self.glade_xml = gtk.glade.XML(gladepath)
        
    
    def get_widget(self):
        raise 'not implemented'
    
    # raise Err with descriptive message on error in configuration
    def validate(self):
        raise 'not implemented'
    
    def get_nodes_fence_tag(self):
        raise 'not implemented'
    
    def get_fencedevice_tag(self):
        raise 'not implemented'
