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


from ads_base import *
from resource_ip import ResourceIP

import gtk


class ADSNFSD(ADSBase):
    
    def __init__(self, ads_utils):
        ADSBase.__init__(self)
        self.utils = ads_utils
        self.pretty_name = 'Network File System Servers'
        
        self.servers = []
        
        self.widget = gtk.VBox(False, 3)
        self.widget.set_border_width(3)
        
        butt = gtk.Button('New NFS Server')
        butt.connect('clicked', self.__add_server)
        hbox = gtk.HBox(False)
        hbox.pack_start(butt, False)
        hbox.pack_start(gtk.Label())
        self.widget.pack_start(hbox)
        
        return
    
    
    # name displayed in applications configuration portion of GUI
    def get_pretty_name(self):
        return self.pretty_name
    
    # gtk widget displayed under pretty name in GUI
    def get_config_widget(self):
        return self.widget
    
    # return names of all services configured by this module
    def get_service_names(self):
        names = []
        for server in self.servers:
            name = server.get_name()
            if name == '':
                msg = 'NFSs: server missing name'
                raise Err(msg)
            elif name in names:
                msg = 'NFSs: there are two NFS servers named ' + name
                msg += '. That is not allowed.'
                raise Err(msg)
            else:
                names.append(name)
        return names
    
    # raise Err on failure
    def validate(self):
        self.get_service_names() # check names
        for server in self.servers:
            server.validate()
    
    # return list of rpms to be installed on all nodes
    def get_rpms(self):
        return []
    
    # return service tag of cluster.conf
    def generate_xml(self):
        xml = ''
        for server in self.servers:
            x = server.generate_xml()
            if x != '':
                xml += x + '\n'
        return xml.rstrip()
    
    # raise Err on failure
    # shared storage is already mounted at self.storage.get_mountpoint() on all nodes
    def configure_nodes(self, nodes):
        # use self.utils.execute_remote() to execute commands on node
        # use self.utils.copy_to_host() to copy local files/dir to node
        if len(self.get_service_names()) != 0:
            for node in nodes:
                # enable nfs daemons
                self.utils.execute_remote(node, 'chkconfig', ['nfs', 'on'])
                self.utils.execute_remote(node, 'chkconfig', ['nfslock', 'on'])
                self.utils.execute_remote(node, 'service', ['nfs', 'start'])
                self.utils.execute_remote(node, 'service', ['nfslock', 'start'])
        return
    
    # messages displayed at the end of installation
    def get_post_install_messages(self):
        return []
    
    
    def __add_server(self, *args):
        remove_butt = gtk.Button('Remove NFS Server')
        name_label = gtk.Label()
        
        server = NFSServer(remove_butt, name_label, self.utils)
        
        self.servers.append(server)
        
        align = gtk.Alignment(0.5, 0.5, 1, 1)
        align.set_padding(0, 0, 20, 0)
        align.add(server.get_widget())
        
        expander = gtk.Expander()
        expander.set_label_widget(name_label)
        expander.add(align)
        expander.set_expanded(True)
        self.widget.pack_start(expander)
        
        remove_butt.connect('clicked', self.__remove_server, expander, server)
        
        self.widget.show_all()
    def __remove_server(self, butt, expander, server):
        self.widget.remove(expander)
        self.servers.remove(server)
        server.destroy()
        

class NFSServer:
    
    def __init__(self, remove_me_butt, name_label, utils):
        self.widget = gtk.VBox(False, 3)
        self.name_label = name_label
        self.utils = utils
        
        self.shares = []
        
        hbox = gtk.HBox(False, 6)
        hbox.pack_start(gtk.Label('Server name'), False)
        self.name_entry = gtk.Entry()
        self.name_entry.connect('key-press-event', self.name_entry_key_press)
        self.name_entry.connect('changed', self.name_entry_changed)
        hbox.pack_start(self.name_entry, False)
        hbox.pack_start(remove_me_butt, False)
        self.widget.pack_start(hbox)
        
        hbox2 = gtk.HBox(False, 6)
        hbox2.pack_start(gtk.Label('Server\'s Virtual IP'), False)
        self.ip = ResourceIP()
        hbox2.pack_start(self.ip.get_widget(), False)
        self.add_share_butt = gtk.Button('New Export')
        self.add_share_butt.connect('clicked', self.__add_share)
        hbox2.pack_start(self.add_share_butt, False)
        self.widget.pack_start(hbox2)
        
        self.__tooltip = gtk.Tooltips()
        tip = 'Name by which a NFS server will be known to a cluster suite (not a hostname)'
        self.__tooltip.set_tip(self.name_entry, tip)
        
        pass
        
    
    def destroy(self):
        self.ip.set_enabled(False)
        for share in self.shares:
            share.destroy()
    
    def validate(self):
        try:
            self.ip.validate()
            names = []
            for share in self.shares:
                name = share.get_name()
                if name in names:
                    msg = 'There are two shares with equal mountpoint. '
                    msg += 'That is not allowed'
                    raise Err(msg)
                else:
                    names.append(name)
                share.validate()
        except Err, e:
            raise Err('NFS server ' + self.get_name() + ': ' + e.get())
    
    def get_widget(self):
        return self.widget
    
    def get_name(self):
        return self.name_entry.get_text().strip()
    
    def name_entry_key_press(self, obj, event, *args):
        stop_event = False
        ch = event.string
        if ch in ' ` ! @ # $ % ^ & * ( ) | \\ { } [ ] \' " : ; ? < > /' and ch != '':
            stop_event = True
        return stop_event
    def name_entry_changed(self, *args):
        self.name_label.set_text(self.get_name())
    
    def __add_share(self, *args):
        remove_butt = gtk.Button('Remove Export')
        name_label = gtk.Label()
        
        share = NFSShare(remove_butt, name_label, self.utils)
        
        self.shares.append(share)
        
        align = gtk.Alignment(0.5, 0.5, 1, 1)
        align.set_padding(0, 0, 20, 0)
        align.add(share.get_widget())
        
        expander = gtk.Expander()
        expander.set_label_widget(name_label)
        expander.add(align)
        expander.set_expanded(True)
        self.widget.pack_start(expander)
        
        remove_butt.connect('clicked', self.__remove_share, expander, share)
        
        self.widget.show_all()
    def __remove_share(self, butt, expander, share):
        self.widget.remove(expander)
        self.shares.remove(share)
        share.destroy()
        
    
    def generate_xml(self):
        shares_xml = ''
        for share in self.shares:
            shares_xml += share.generate_xml() + '\n'
        xml = '\t\t<service name="%s" autostart="1">\n' % self.get_name()
        xml += '\t\t\t%s\n' % self.ip.generate_xml()
        xml += shares_xml
        xml += '\t\t</service>'
        return xml.rstrip()
    
    
    pass



class NFSShare(gtk.VBox):
    
    def __init__(self, remove_me_butt, name_label, utils):
        self.widget = gtk.VBox(False, 3)
        self.name_label = name_label
        self.utils = utils
        
        self.storage = self.utils.get_new_storage_config()
        self.storage.register_mountpoint_changed(self.mountpoint_changed)
        
        self.storage.set_size_label_text('Export\'s size (GB)')
        self.storage.set_mountpoint_label_text('Export path')
        path_tooltip = self.storage.get_mountpoint_tooltip()
        path_tooltip += '\nIt is also the path that clients will mount from.'
        self.storage.set_mountpoint_tooltip(path_tooltip)
        
        hbox = gtk.HBox(False, 3)
        hbox.pack_start(self.storage.get_config_widget(), False)
        hbox.pack_start(remove_me_butt, False)
        self.widget.pack_start(hbox)
        
        ## clients ##
        
        frame = gtk.Frame('Clients')
        align = gtk.Alignment(0.5, 0.5, 1, 1)
        align.set_padding(0, 0, 12, 0)
        align.set_border_width(3)
        in_vbox = gtk.VBox(False, 3)
        align.add(in_vbox)
        frame.add(align)
        self.widget.pack_start(frame)
        
        hbox = gtk.HBox(False, 3)
        self.new_client_entry = gtk.Entry()
        self.new_client_entry.connect('key-press-event', self.new_client_entry_press_event)
        hbox.pack_start(self.new_client_entry, True)
        add_butt = gtk.Button('Add')
        add_butt.connect('clicked', self.__add_client)
        hbox.pack_start(add_butt, False)
        in_vbox.pack_start(hbox)
        
        self.clients_vbox = gtk.VBox(False, 3)
        in_vbox.pack_start(self.clients_vbox)
        
        self.__tooltip = gtk.Tooltips()
        tip = 'A hostname, a wildcard (IP address or hostname based), or a netgroup'
        self.__tooltip.set_tip(self.new_client_entry, tip)
        
        pass
    
    
    def destroy(self):
        self.storage.set_enabled(False)
    
    def get_widget(self):
        return self.widget
    
    def get_name(self):
        return self.storage.get_mountpoint()
    
    def __add_client(self, *args):
        client = self.new_client_entry.get_text().strip()
        
        if client == '':
            self.new_client_entry.set_text('')
            return
        if client in self.get_client_names():
            errorMessage('Client \'' + client + '\' already added')
            return
        
        hbox = gtk.HBox(False, 3)
        hbox.pack_start(gtk.Label(client), False)
        hbox.pack_start(gtk.Label(), True)
        hbox.pack_start(gtk.CheckButton('Read Only'), False)
        rem_me = gtk.Button('Remove')
        rem_me.connect('clicked', self.__remove_client, hbox)
        hbox.pack_start(rem_me, False)
        self.clients_vbox.pack_start(hbox)
        
        self.widget.show_all()
        
        self.new_client_entry.set_text('')
    def __remove_client(self, butt, hbox):
        self.clients_vbox.remove(hbox)
    
    def get_client_names(self):
        cls = []
        for c in self.clients_vbox.get_children():
            cls.append(c.get_children()[0].get_text())
        return cls
    
    def get_clients(self):
        # return (name, options_str)
        cls = []
        for c in self.clients_vbox.get_children():
            name = c.get_children()[0].get_text()
            opts = ''
            if c.get_children()[2].get_active():
                opts += 'ro'
            else:
                opts += 'rw'
            cls.append((name, opts))
        return cls
        
    def mountpoint_changed(self, mountpoint):
        self.name_label.set_text(mountpoint)
        name = mountpoint.replace('/', '_')
        if len(name) > 15:
            l = len(name)
            name = name[l-15:]
        self.storage.set_name(name) # len(name) < 16
        
    def validate(self):
        # TODO: validate clients
        self.storage.validate()
    
    def generate_xml(self):
        storage_xml_pair = self.storage.generate_xml_local()
        
        clients_xml = ''
        for cl in self.get_clients():
            name = cl[0]
            opts = cl[1]
            template_client = '<nfsclient name="%s" target="%s" options="%s"/>'
            clients_xml += '\t\t\t\t\t' + template_client % (name, name, opts)
            clients_xml += '\n'
        
        xml = '\t\t\t%s\n' % storage_xml_pair[0]
        xml += '\t\t\t\t<nfsexport name="%s">\n' % self.get_name()
        xml += clients_xml
        xml += '\t\t\t\t</nfsexport>\n'
	xml += '\t\t\t%s\n' % storage_xml_pair[1]
        return xml.rstrip()
        
    def new_client_entry_press_event(self, obj, event, *args):
        stop_event = False
        ch = event.string
        if ch in ' ~ = + ` ! @ # $ % ^ & ( ) | \\ { } [ ] \' " , : ; ? < >' and ch != '':
            stop_event = True
        return stop_event


def errorMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_ERROR, gtk.BUTTONS_OK,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    return rc
