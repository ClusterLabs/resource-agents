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

from ads_base import *
from resource_ip import ResourceIP
from constants import *


class ADSGeneric(ADSBase):
    
    def __init__(self, ads_utils):
        ADSBase.__init__(self)
        
        self.utils = ads_utils
        
        self.kids = []
        
        self.widget = gtk.VBox(False, 3)
        self.widget.set_border_width(3)
        
        butt = gtk.Button('New Generic Application')
        butt.connect('clicked', self.__add_kid)
        hbox = gtk.HBox(False)
        hbox.pack_start(butt, False)
        hbox.pack_start(gtk.Label())
        self.widget.pack_start(hbox)
        
    
    def get_pretty_name(self):
        return 'Generic Applications'
    
    def get_service_names(self):
        names = []
        for kid in self.kids:
            names.append(kid.get_name())
        return names
    
    def get_config_widget(self):
        return self.widget
    
    def validate(self):
        names = []
        for kid in self.kids:
            kid.validate()
            name = kid.get_name()
            if name in names:
                raise Err('There are two generic applications named ' + name)
            else:
                names.append(name)
    
    def get_rpms(self):
        rpms = []
        for kid in self.kids:
            for rpm in kid.get_rpms():
                rpms.append(rpm)
        return rpms
    
    def generate_xml(self):
        xml = ''
        for kid in self.kids:
            xml += kid.generate_xml().rstrip() + '\n'
        return xml.rstrip()
    
    def configure_nodes(self, nodes):
        return
    
    def get_post_install_messages(self):
        return []
    
    
    def __add_kid(self, *args):
        remove_butt = gtk.Button('Remove generic application')
        name_label = gtk.Label()
        
        kid = Generic(remove_butt, name_label, self.utils)
        
        self.kids.append(kid)
        
        align = gtk.Alignment(0.5, 0.5, 1, 1)
        align.set_padding(0, 0, 20, 0)
        align.add(kid.get_widget())
        
        expander = gtk.Expander()
        expander.set_label_widget(name_label)
        expander.add(align)
        expander.set_expanded(True)
        self.widget.pack_start(expander)
        
        remove_butt.connect('clicked', self.__remove_kid, expander, kid)
        
        self.widget.show_all()
    
    def __remove_kid(self, butt, expander, kid):
        self.widget.remove(expander)
        self.kids.remove(kid)
        kid.destroy()
        
    

class Generic:
    
    def __init__(self, remove_me_butt, name_label, utils):
        glade_path = (INSTALLDIR + '/ADSs/ads_generic.glade')
        self.glade_xml = gtk.glade.XML(glade_path)
        
        self.glade_xml.get_widget('add_remove_me_butt_here').pack_start(remove_me_butt)
        self.name_label = name_label
        
        self.widget = self.glade_xml.get_widget('ads_generic')
        self.widget.unparent()
        
        self.storage_enabled = self.glade_xml.get_widget('storage_enabled')
        self.storage_instance = utils.get_new_storage_config()
        self.storage_instance.set_enabled(False)
        self.storage_instance_widget = self.storage_instance.get_config_widget()
        self.glade_xml.get_widget('insert_storage_config').pack_start(self.storage_instance_widget)
        
        self.ip = ResourceIP()
        ip_widget = self.ip.get_widget()
        ip_widget.set_sensitive(False)
        self.glade_xml.get_widget('insert_ip_config').pack_start(ip_widget)
        self.ip_enabled = self.glade_xml.get_widget('ip_enabled')
        
        self.name_entry = self.glade_xml.get_widget('app_name')
        self.script_entry = self.glade_xml.get_widget('script')
        self.rpms_entry = self.glade_xml.get_widget('rpms')
        
        self.name_entry.connect('key-press-event', self.name_change_key_press)
        self.name_entry.connect('changed', self.name_entry_changed)
        self.rpms_entry.connect('key-press-event', self.rpms_entry_key_press)
        self.storage_enabled.connect('toggled', self.storage_toggled)
        self.ip_enabled.connect('toggled', self.ip_toggled)
        
        pass
    
    def destroy(self):
        self.storage_instance.set_enabled(False)
        self.ip.set_enabled(False)
    
    def get_widget(self):
        return self.widget
    
    def get_name(self):
        return self.name_entry.get_text().strip()
    
    def validate(self):
        name = self.get_name()
        try:
            if name == '' or ' ' in name:
                raise Err('invalid application name')
            elif len(name) >= 16:
                raise Err('application name has to be fewer than 16 characters in length')
            else:
                self.storage_instance.set_name(name)
            if self.storage_enabled.get_active():
                self.storage_instance.validate()
            if self.ip_enabled.get_active():
                self.ip.validate()
            script = self.script_entry.get_text().strip()
            if ' ' in script:
                raise Err('invalid script path')
            if len(script) != 0 and script[0] != '/':
                raise Err('Script has to be an absolute path')
            # nothing to do for rpms
        except Err, e:
            raise Err('Generic application ' + name + ': ' + e.get())
        
    def get_rpms(self):
        rpms = self.rpms_entry.get_text().strip()
        if rpms == '':
            return []
        return rpms.split()
    
    def generate_xml(self):
        name = self.get_name()
        storage_xml_pair = self.storage_instance.generate_xml_local()
        storage_xml = storage_xml_pair[0] + storage_xml_pair[1]
        ip_xml = self.ip.generate_xml()
        script = self.script_entry.get_text().strip()
        script_xml = '<script name="%s" file="%s"/>' % (script.replace('/', '_'), script)
        
        template_main = '\t\t<service name="%s" autostart="1">\n' % name
        if self.storage_enabled.get_active():
            template_main += '\t\t\t' + storage_xml + '\n'
        if self.ip_enabled.get_active():
            template_main += '\t\t\t' + ip_xml + '\n'
        if script != '':
            template_main += '\t\t\t' + script_xml + '\n'
        template_main += '\t\t</service>'
        
        return template_main
    
    
    def storage_toggled(self, butt, *args):
        status = self.storage_enabled.get_active()
        self.storage_instance.set_enabled(status)
    
    def ip_toggled(self, butt, *args):
        status = self.ip_enabled.get_active()
        self.ip.get_widget().set_sensitive(status)
        
    def name_change_key_press(self, obj, event, *args):
        stop_event = False
        ch = event.string
        if ch in ' ` ! @ # $ % ^ & * ( ) | \\ { } [ ] \' " : ; ? < > /' and ch != '':
            stop_event = True
        return stop_event
    def name_entry_changed(self, *args):
        self.name_label.set_text(self.get_name())
        
    def rpms_entry_key_press(self, obj, event, *args):
        stop_event = False
        ch = event.string
        if ch in ' ` ! @ # $ % ^ & * ( ) | \\ { } [ ] \' " : ; ? < > /' and ch != '' and ch != ' ':
            stop_event = True
        return stop_event
