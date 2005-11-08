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



#
# this is a template of Application Deployment Script
#
# to integrate it into cs-deploy-tool,
# copy to /usr/share/cs-deploy-tool/ADSs/ or ~/.cs-deploy-tool/modules/user_ADSs/
#



from ads_base import *
from resource_ip import ResourceIP

import gtk


class ADSExample(ADSBase):
    
    def __init__(self, ads_utils):
        ADSBase.__init__(self)
        
        self.utils = ads_utils
        
        self.service_name = 'sample_ADS'
        
        self.ip = ResourceIP()
        self.storage = self.utils.get_new_storage_config()
        self.storage.set_name(self.service_name) # len(name) < 16
        
        hbox = gtk.HBox(False, 6)
        hbox.pack_start(gtk.Label('Virtual IP address'), False)
        hbox.pack_start(self.ip.get_widget())
        
        self.vbox = gtk.VBox(False, 3)
        self.vbox.pack_start(hbox, False)
        self.vbox.pack_start(self.storage.get_config_widget(), False)
        
    
    # name displayed in applications configuration portion of GUI
    def get_pretty_name(self):
        return 'ADS Implementation Example'
    
    # gtk widget displayed under pretty name in GUI
    def get_config_widget(self):
        return self.vbox
    
    # return names of all services configured by this module
    def get_service_names(self):
        return [self.service_name]
    
    # raise Err on failure
    def validate(self):
        try:
            self.storage.validate()
            self.ip.validate()
        except Err, e:
            raise Err('Sample ADS: ' + e.get())
    
    # return list of rpms to be installed on all nodes
    def get_rpms(self):
        # return ['sendmail', 'some_other_rpm_name']
        return []
    
    # return service tag of cluster.conf
    def generate_xml(self):
        storage_xml_pair = self.storage.generate_xml_local()
        storage_xml = storage_xml_pair[0] + storage_xml_pair[1]
        ip_xml = self.ip.generate_xml()
        
        xml = '\t\t<service name="%s" autostart="1">\n' % self.service_name
        xml += '\t\t\t' + storage_xml + '\n'
        xml += '\t\t\t' + ip_xml + '\n'
        xml += '\t\t</service>'
        
        return xml
    
    # raise Err on failure
    # shared storage is already mounted at self.storage.get_mountpoint() on all nodes
    def configure_nodes(self, nodes):
        # use self.utils.execute_remote() to execute commands on node
        # use self.utils.copy_to_host() to copy local files/dir to node
        return
    
    # messages displayed at the end of installation
    def get_post_install_messages(self):
        #return ['Disable selinux on all nodes']
        return []

    
    
