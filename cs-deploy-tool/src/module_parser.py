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


import os


class ModuleParser:
    def __init__(self):
        pass
    
    
    def import_all_into_module(self, module):
        ext = '.py'
        files = []
        for file in os.listdir(module.__path__[0]):
            if file.endswith(ext):
                file_name = file[:len(file)-len(ext)]
                files.append(file_name)
        self.import_into_module(module, files)
    
    def import_into_module(self, module, kids):
        for name in kids:
            com = 'import ' + module.__name__ + '.' + name
            exec com
    
    def get_modules(self, module):
        exec 'import ' + module.__name__
        l = []
        for m in dir(module):
            if m.endswith('__'):
                continue
            com = 'o = ' + module.__name__ +'.' + m
            exec com
            if '__file__' in dir(o):
                l.append(o)
        return l
    
    def get_classes(self, module):
        exec 'import ' + module.__name__
        l = []
        for m in dir(module):
            if m.endswith('__'):
                continue
            com = 'o = ' + module.__name__ +'.' + m
            exec com
            if '__init__' in dir(o):
                l.append(o)
        return l
    
    def share_functions(self, subset, superset):
        subset_l = dir(subset)
        superset_l = dir(superset)
        for t in subset_l:
            if t not in superset_l:
                return False
        return True
