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


class StorageConfig:
    def __init__(self):
        pass
    
    # returns storage configuration widget,
    # that should be integrated in application's configuration GUI
    def get_config_widget(self):
        raise 'not implemented'
    
    # raises Err with descriptive message on errors
    def validate(self):
        raise 'not implemented'
    
    # enable/disable storage
    def get_enabled(self):
        raise 'not implemented'
    def set_enabled(self, bool):
        raise 'not implemented'
    
    # returns size in bytes
    def get_size(self):
        raise 'not implemented'
    
    def get_mountpoint(self):
        raise 'not implemented'
    
    # callback accepts a single argument (mountpoint string)
    # and is called on every mountpoint change
    def register_mountpoint_changed(self, callback):
        raise 'not implemented'
    
    # name of shared storage, has to be unique,
    # and less than 16 chars long
    def set_name(self, name):
        raise 'not implemented'
    def get_name(self):
        raise 'not implemented'
    
    # returns clusterfs tag, (beggining, end)
    def generate_xml_local(self):
        raise 'not implemented'
    
    ### cosmetics ###
    
    def set_size_label_text(self, text):
        raise 'not implemented'
    def set_mountpoint_label_text(self, text):
        raise 'not implemented'
    
    def get_size_tooltip(self):
        raise 'not implemented'
    def set_size_tooltip(self, text):
        raise 'not implemented'
    def get_mountpoint_tooltip(self):
        raise 'not implemented'
    def set_mountpoint_tooltip(self, text):
        raise 'not implemented'
