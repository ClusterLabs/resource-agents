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


from constants import *

from fence_builder import FenceBuilder
from cluster_ops import ClusterOps
from ads_utils import ADSUtils
from storage import Storage
from err import Err



class MainWin:
    
    def __init__(self,
                 cluster_name,
                 environ,
                 rpm_installer,
                 ads_classes):
        
        gladepath = INSTALLDIR + '/main_win.glade'
        self.glade_xml = gtk.glade.XML(gladepath)
        
        self.cluster_name = cluster_name
        self.cluster_alias_entry = self.glade_xml.get_widget('cluster_alias')
        
        self.environ = environ
        self.cluster_ops = ClusterOps(self.environ)
        self.fence_builder = FenceBuilder()
        self.rpm_installer = rpm_installer
        
        self.nodes = []
        
        storage_label = self.glade_xml.get_widget('storage_label')
        self.storage = Storage(self.environ, self.cluster_name, self.glade_xml, storage_label)
        
        self.adss = []
        for ads_class in ads_classes:
            self.adss.append(ads_class(ADSUtils(self.storage, self.environ)))
        
        # display welcome screen
        self.glade_xml.get_widget('welcome_ok').connect('clicked', self.welcome_ok)
        if self.glade_xml.get_widget('welcome_screen').run() == gtk.RESPONSE_DELETE_EVENT:
            self.environ.exit()
        self.glade_xml.get_widget('welcome_screen').hide()
        
        # initialize fencing
        if self.fence_builder.configure_fence_type() == False:
            self.environ.exit()
        
        self.main_win = self.glade_xml.get_widget('setup_win')
        self.nodes_vbox = self.glade_xml.get_widget('nodes_vbox')
        
        self.main_win.connect('delete-event', self.abort)
        self.glade_xml.get_widget('add_node').connect('clicked', self.add_node)
        self.glade_xml.get_widget('install_butt').connect('clicked', self.install)
        
        self.setup_adss()
        
        self.main_win.show()
        
        
    def abort(self, *args):
        self.environ.log('Aborted by user', True)
        self.__unauthorize()
        self.environ.exit(1)
    def exit(self):
        self.__unauthorize()
        self.environ.exit()
    def __unauthorize(self):
        dlg = gtk.MessageDialog(None, 0,
                                gtk.MESSAGE_INFO, gtk.BUTTONS_NONE,
                                'Unauthorizing from nodes')
        dlg.set_modal(True)
        dlg.show_all()
        
        cursor_busy(self.main_win)
        cursor_busy(dlg)
        
        self.environ.unauthorize_to_all()
        
        cursor_regular(self.main_win)
        dlg.destroy()
        
        
    def welcome_ok(self, arg):
        self.glade_xml.get_widget('welcome_screen').response(gtk.RESPONSE_OK)
        
    
    def add_node(self, *args):
        node = self.glade_xml.get_widget('node_entry').get_text().strip()
        if node == '':
            return
        
        dlg = gtk.MessageDialog(None, 0,
                                gtk.MESSAGE_INFO, gtk.BUTTONS_NONE,
                                'Probing ' + node + '. Please wait.')
        dlg.set_modal(True)
        dlg.show_all()
        
        cursor_busy(self.main_win)
        cursor_busy(dlg)
        
        try:
            self.__add_node()
        except Err, e:
            cursor_regular(self.main_win)
            dlg.destroy()
            errorMessage(e.get())
            return
        cursor_regular(self.main_win)
        dlg.destroy()
        
        if len(self.nodes) == 2:
            self.storage.prompt_storage()
        
    
    def __add_node(self):
        node_entry = self.glade_xml.get_widget('node_entry')
        node = node_entry.get_text().strip()
        if node == '':
            node_entry.set_text('')
            return
        if node in self.nodes:
            raise Err(node + ' already added')
        ifconfig_out, e, s = self.environ.execute('/sbin/ifconfig', [])
        o, e, s = self.environ.execute('host', [node])
        words = o.split()
        if len(words) == 4:
            name = words[0]
            address = words[3]
            if address in ifconfig_out:
                raise Err('Installer can only be run from a PC that is not to be node.\nUnable to add.')
        if self.cluster_ops.ping(node) == False:
            raise Err('Unable to ping ' + node)
        # authorize to node
        if self.environ.execute_remote(node, 'echo', [''])[2] != 0:
            raise Err('Unable to login into ' + node)
        if self.rpm_installer.is_os_supported(node) != True:
            raise Err(node + ' doesn\'t contain supported OS')
        o, e, s = self.environ.execute_remote(node, 'cat', ['/proc/cluster/status'])
        if s == 0:
            cluster_name = 'unknown cluster name'
            for line in o.splitlines():
                if 'Cluster name' in line:
                    cluster_name = line.split(':')[1].strip()
            msg = node + ' is already a node of cluster ' + cluster_name + '. '
            msg += 'Unable to add.'
            raise Err(msg)
        
        self.storage.add_node(node)
        self.nodes.append(node)
        
        # add to GUI list
        fence_gui = self.fence_builder.get_fence_gui(node)
        
        node_label = gtk.Label()
        node_label.set_text('<b>' + node + '</b>')
        node_label.set_use_markup(True)
        
        if fence_gui == None:
            new_hbox = gtk.HBox()
            new_hbox.pack_start(node_label, False)
            new_hbox.pack_start(gtk.Label(), True)
            
            self.nodes_vbox.pack_start(new_hbox)
        else:
            align_in = gtk.Alignment(0.5, 0.5, 1, 1)
            align_in.set_padding(0, 0, 20, 0)
            align_in.add(fence_gui)
            
            frame = gtk.Frame('Fencing')
            frame.set_shadow_type(gtk.SHADOW_OUT)
            frame.add(align_in)
            
            align_out = gtk.Alignment(0.5, 0.5, 1, 1)
            align_out.set_padding(0, 0, 20, 0)
            align_out.add(frame)
            
            expander = gtk.Expander()
            expander.set_label_widget(node_label)
            expander.add(align_out)
            expander.set_expanded(True)
            
            self.nodes_vbox.pack_start(expander)
        
        self.nodes_vbox.show_all()
        
        ###  GUI list done  ###
        
        node_entry.set_text('')
        
    
    def setup_adss(self):
        adss_vbox = self.glade_xml.get_widget('adss_vbox')
        
        for ads in self.adss:
            expander = gtk.Expander(ads.get_pretty_name())
            align = gtk.Alignment(0.5, 0.5, 1, 1)
            align.set_padding(0, 0, 20, 0)
            expander.add(align)
            align.add(ads.get_config_widget())
            
            expander.set_expanded(False)
            
            adss_vbox.pack_start(expander)
            
        adss_vbox.show_all()
        for o in adss_vbox.get_children():
            adss_vbox.set_child_packing(o, False, False, 0, gtk.PACK_START)
        
        
        
    def install(self, *args):
        if len(self.nodes) < 2:
            errorMessage('Cluster has to be composed of at least two nodes')
            return
        
        # check for problems in configuration
        try:
            self.fence_builder.validate()
            service_names = []
            for ads in self.adss:
                ads.validate()
                for name in ads.get_service_names():
                    if name in service_names:
                        raise Err('There are two services named ' + name)
                    else:
                        service_names.append(name)
            self.storage.validate()
            cluster_conf = self.__get_cluster_conf(0)
        except Err, e:
            errorMessage(e.get())
            return
        
        
        # data destruction warning
        format_msg = self.storage.get_warning_string()
        if format_msg != '':
            if not questionMessage(format_msg):
                return
        
        
        ## progress ##
        
        dlg = self.glade_xml.get_widget('install_progress_window')
        prog_bar = self.glade_xml.get_widget('progress')
        msgs = self.glade_xml.get_widget('progress_messages_label')
        msgs.set_text('')
        
        dlg.set_modal(True)
        dlg.show_all()
        cursor_busy(self.main_win)
        cursor_busy(dlg)
        
        
        self.__install(msgs, prog_bar)
        
        
        cursor_regular(self.main_win)
        dlg.hide()
        
        msgs = ''
        for ads in self.adss:
            ads_msgs = ads.get_post_install_messages()
            if len(ads_msgs) > 0:
                msgs += 'Application ' + ads.get_pretty_name() + ': \n'
                for msg in ads_msgs:
                    msgs += '\t' + msg + '\n'
        msg = 'Cluster deployment completed successfully'
        if msgs != '':
            msg += '\n\nPost-installation messages:\n\n'
            msg += msgs
        infoMessage(msg)
        
        self.exit()
        
    
    def __install(self, label, prog_bar):
        
        ### installation starts here ###
        
        
        # install needed rpms
        prog_bar.set_fraction(0.00)
        label.set_text('Installing and upgrading software on all nodes')
        for node in self.nodes:
            rpms = []
            for ads in self.adss:
                for rpm in ads.get_rpms():
                    rpms.append(rpm)
            if self.rpm_installer.install(node, rpms) != True:
                msg = 'Failed installation of software on ' + node + '.\n'
                msg += 'Check log file ' + self.environ.get_log_file_path() + '.\n'
                msg += 'Aborting deployment...'
                infoMessage(msg)
                self.abort()
        
        
        # distribute cluster.conf without services
        cluster_conf = self.__get_cluster_conf(1)
        conf_path = './cluster.conf.1'
        open(conf_path, 'w').write(cluster_conf)
        self.__copy_conf(conf_path)
        
        
        # enable cluster
        self.cluster_ops.enable_cluster(self.nodes)
        
        
        # reboot nodes
        prog_bar.set_fraction(0.25)
        label.set_text('Rebooting nodes')
        self.cluster_ops.reboot_nodes(self.nodes)
        
        
        # configure storage
        prog_bar.set_fraction(0.50)
        label.set_text('Configuring shared storage')
        if self.storage.configure_nodes() != True:
            msg = 'Failed configuration of shared storage.\n'
            msg += 'Check log file ' + self.environ.get_log_file_path() + '.\n'
            msg += 'Aborting deployment...'
            infoMessage(msg)
            self.abort()
        
        
        # configure applications
        prog_bar.set_fraction(0.75)
        label.set_text('Configuring applications')
        for ads in self.adss:
            ads.configure_nodes(self.nodes)
        
        
        # distribute complete cluster.conf
        cluster_conf = self.__get_cluster_conf(2)
        conf_path = './cluster.conf.2'
        open(conf_path, 'w').write(cluster_conf)
        self.cluster_ops.distribute_conf(conf_path, 2, self.nodes)
        
        
        # cluster report
        self.cluster_ops.cluster_report(self.nodes)
        
        prog_bar.set_fraction(1.00)
        label.set_text('Completed')
        
    
    def __get_cluster_conf(self, version):
        cluster_name = self.cluster_name
        cluster_alias = self.cluster_alias_entry.get_text().strip()
        include_services = (int(version) > 1)
        if int(version) > 1:
            clean_start = 0
        else:
            clean_start = 1
        
        template_main = '<?xml version="1.0" ?>\n'
        s = '<cluster name="%s" alias="%s" config_version="%s">\n'
        template_main += s % (cluster_name, cluster_alias, str(version))
        template_main += '\t<fence_daemon clean_start="%s" post_fail_delay="0" post_join_delay="3"/>\n' % str(clean_start)
        
	template_main += '\t<clusternodes>\n'
        template_main += self.__get_nodes_str() + '\n'
        template_main += '\t</clusternodes>\n'
        
	template_main += self.__get_cman_str() + '\n'
        
        template_main += '\t<fencedevices>\n'
        template_main += self.__get_fences_str() + '\n'
        template_main += '\t</fencedevices>\n'
        
        template_main += '\t<rm>\n'
        template_main += '\t\t<failoverdomains/>\n'
        template_main += '\t\t<resources>\n'
        if include_services:
            template_main += self.__get_shared_storages_str() + '\n'
        template_main += '\t\t</resources>\n'
        if include_services:
            template_main += self.__get_services_str() + '\n'
        template_main += '\t</rm>\n'
        
        template_main += '</cluster>\n'
        return template_main
    
    def __get_nodes_str(self):
        template_node = '\t\t<clusternode name="%s" votes="1">\n'
        template_node += '\t\t\t<fence>\n'
        template_node += '\t\t\t\t<method name="1">\n'
        template_node += '\t\t\t\t\t%s\n'
        template_node += '\t\t\t\t</method>\n'
        template_node += '\t\t\t</fence>\n'
        template_node += '\t\t</clusternode>'
        
        nodes_str = ''
        for node in self.nodes:
            fence = self.fence_builder.get_nodes_fence_tag(node)
            s = template_node % (node, fence)
            nodes_str += s + '\n'
        
        return nodes_str.rstrip()
    
    def __get_fences_str(self):
        fences = []
        for node in self.nodes:
            fence = '\t\t%s' % self.fence_builder.get_fencedevice_tag(node)
            if fence not in fences:
                fences.append(fence)
        fences_str = ''
        for fence in fences:
            fences_str += fence + '\n'
        return fences_str.rstrip()
    
    def __get_cman_str(self):
        if len(self.nodes) == 2:
            return '\t<cman expected_votes="1" two_node="1"/>'
        else:
            return '\t<cman/>'
        
    def __get_shared_storages_str(self):
        return self.storage.generate_xml().rstrip()
    
    def __get_services_str(self):
        services_str = ''
        for ads in self.adss:
            xml = ads.generate_xml()
            if xml == None:
                continue
            services_str += xml + '\n'
        return services_str.rstrip()
    
    def __copy_conf(self, conf):
        self.environ.log('Copying cluster.conf to all nodes')
        for node in self.nodes:
            self.environ.execute_remote(node, 'mkdir', ['/etc/cluster/'])
            self.environ.copy_to_host(conf, node, '/etc/cluster/cluster.conf')
    
    
    
    
    
    

def cursor_busy(widget):
    cursor = gtk.gdk.Cursor(gtk.gdk.WATCH)
    widget.window.set_cursor(cursor)
def cursor_regular(widget):
    cursor = gtk.gdk.Cursor(gtk.gdk.LEFT_PTR)
    widget.window.set_cursor(cursor)
    

def errorMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_ERROR, gtk.BUTTONS_OK,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    return rc

def infoMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_INFO, gtk.BUTTONS_OK,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    return rc

def questionMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_INFO, gtk.BUTTONS_YES_NO,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    if (rc == gtk.RESPONSE_NO):
        return False
    elif (rc == gtk.RESPONSE_DELETE_EVENT):
        return False
    elif (rc == gtk.RESPONSE_CLOSE):
        return False
    elif (rc == gtk.RESPONSE_CANCEL):
        return False
    else:
        return True
    
