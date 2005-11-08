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
import os

from constants import *


class FenceBuilder:
    
    def __init__(self):
        self.__node_fence = {} # node -> fence mapping
        
    
    # return False if cancelled
    def configure_fence_type(self):
        fs = FenceSelector()
        fs.prompt()
        fence_class = fs.get_fence_class()
        fence_shared_class = fs.get_fence_shared_class()
        if fence_class == None or fence_shared_class == None:
            return False
        self.__fence_class = fence_class
        self.__fence_shared_info = fence_shared_class()
        return self.__fence_shared_info.configure()
    
    def get_fence_gui(self, node):
        if node in self.__node_fence:
            fence = self.__node_fence[node]
        else:
            fence = self.__fence_class(node, self.__fence_shared_info)
            self.__node_fence[node] = fence
        
        return fence.get_widget()
    
    def validate(self):
        for fence in self.__node_fence.values():
            fence.validate()
            
        
        
    def get_nodes_fence_tag(self, node):
        return self.__node_fence[node].get_nodes_fence_tag()
    def get_fencedevice_tag(self, node):
        return self.__node_fence[node].get_fencedevice_tag()




class FenceSelector:
    
    def __init__(self):
        from module_parser import ModuleParser
        from fence_base import FenceBase, FenceBaseSharedInfo
        import fences
        
        self.available_fences = {}
        
        self.fence_class = None
        self.fence_shared_class = None
        
        mp = ModuleParser()
        mp.import_all_into_module(fences)
        
        for module in mp.get_modules(fences):
            f_base = None
            f_shared = None
            for c in mp.get_classes(module):
                if mp.share_functions(FenceBase, c) and c != FenceBase:
                    f_base = c
                elif mp.share_functions(FenceBaseSharedInfo, c) and c != FenceBaseSharedInfo:
                    f_shared = c
            if f_base != None and f_shared != None:
                try:
                    self.available_fences[module.get_fence_name()] = (f_base, f_shared)
                except:
                    pass
        pass
    
    
    def prompt(self):
        if len(self.available_fences.keys()) == 1:
            fence_name = self.available_fences.keys()[0]
            self.fence_class = self.available_fences[fence_name][0]
            self.fence_shared_class = self.available_fences[fence_name][1]
            return
        
        self.f_combo = gtk.combo_box_new_text()
        selection_text = 'Select Fencing Device Type'
        self.f_combo.append_text(selection_text)
        fence_names = self.available_fences.keys()[:]
        fence_names.sort()
        for name in fence_names:
            self.f_combo.append_text(name)
        model = self.f_combo.get_model()
        iter = model.get_iter_first()
        self.f_combo.set_active_iter(iter)
        
        gladepath = INSTALLDIR + '/fence_builder.glade'
        glade_xml = gtk.glade.XML(gladepath)
        glade_xml.get_widget('insert_combo_here').add(self.f_combo)
        
        prompt = glade_xml.get_widget('fence_sel_win')
        prompt.show_all()
        while True:
            if prompt.run() == gtk.RESPONSE_DELETE_EVENT:
                break
            else:
                if self.__get_fence_name() != selection_text:
                    break
                else:
                    dlg = gtk.MessageDialog(None, 0,
                                            gtk.MESSAGE_ERROR,
                                            gtk.BUTTONS_OK,
                                            'Select Fence Type')
                    dlg.show_all()
                    rc = dlg.run()
                    dlg.destroy()
        
        prompt.hide()
        
        fence_name = self.__get_fence_name()
        try:
            self.fence_class = self.available_fences[fence_name][0]
            self.fence_shared_class = self.available_fences[fence_name][1]
        except:
            pass
        return
    def __get_fence_name(self):
        iter = self.f_combo.get_active_iter()
        return self.f_combo.get_model().get_value(iter, 0)
    
    def get_fence_class(self):
        return self.fence_class
    def get_fence_shared_class(self):
        return self.fence_shared_class
