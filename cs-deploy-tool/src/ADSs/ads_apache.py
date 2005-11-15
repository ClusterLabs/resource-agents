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
import os


class ADSApache(ADSBase):
    
    def __init__(self, ads_utils):
        ADSBase.__init__(self)
        
        self.utils = ads_utils
        
        self.service_name = 'apache_httpd'
        self.pretty_name = 'Apache Web Server'
        
        self.ip = ResourceIP()
        self.storage = self.utils.get_new_storage_config()
        self.storage.set_name(self.service_name)
        
        hbox = gtk.HBox(False, 6)
        hbox.pack_start(gtk.Label('Virtual IP'), False)
        hbox.pack_start(self.ip.get_widget())
        
        vbox_in = gtk.VBox(False, 6)
        vbox_in.set_border_width(3)
        vbox_in.pack_start(hbox, False)
        vbox_in.pack_start(self.storage.get_config_widget(), False)
        
        align_in = gtk.Alignment(0.5, 0.5, 1, 1)
        align_in.set_padding(0, 0, 20, 0)
        align_in.add(vbox_in)
        
        self.frame = gtk.Frame('Configuration')
        self.frame.set_shadow_type(gtk.SHADOW_OUT)
        self.frame.add(align_in)
        
        self.enabled = gtk.CheckButton('Deploy Apache')
        self.enabled.connect('clicked', self.enabled_clicked)
        
        self.__tooltip = gtk.Tooltips()
        tip = 'If deployed:\n'
        tip += ' - Web server will be accessible at <virtual IP>\n'
        tip += ' - Shared LV will be created and mounted on <mountpoint>\n'
        tip += ' - Shared Apache configuration files will be located in <mountpoint>/conf.d/\n'
        tip += ' - Sample web page will be placed into <mountpoint>/www/'
        self.__tooltip.set_tip(self.enabled, tip)
        
        self.vbox = gtk.VBox(False, 3)
        self.vbox.pack_start(self.enabled, False)
        self.vbox.pack_start(self.frame)
        
        self.enabled_clicked()
        
        self.__tmp_files = []
        
    
    # name displayed in applications configuration portion of GUI
    def get_pretty_name(self):
        return self.pretty_name
    
    # gtk widget displayed under pretty name in GUI
    def get_config_widget(self):
        return self.vbox
    
    # return names of all services configured by this module
    def get_service_names(self):
        if self.is_enabled():
            return [self.service_name]
        else:
            return []
    
    # raise Err on failure
    def validate(self):
        if self.is_enabled():
            try:
                self.storage.validate()
                self.ip.validate()
            except Err, e:
                raise Err(self.pretty_name + ': ' + e.get())
    
    # return list of rpms to be installed on all nodes
    def get_rpms(self):
        if self.is_enabled():
            return ['httpd']
        else:
            return []
    
    # return service tag of cluster.conf
    def generate_xml(self):
        if not self.is_enabled():
            return ''
        
        storage_xml_pair = self.storage.generate_xml_local()
        storage_xml = storage_xml_pair[0] + storage_xml_pair[1]
        ip_xml = self.ip.generate_xml()
        script_xml = '<script name="%s" file="/etc/init.d/httpd"/>'
        script_xml = script_xml % self.service_name
        
        xml = '\t\t<service name="%s" autostart="1">\n' % self.service_name
        xml += '\t\t\t' + storage_xml + '\n'
        xml += '\t\t\t' + ip_xml + '\n'
        xml += '\t\t\t' + script_xml + '\n'
        xml += '\t\t</service>'
        
        return xml
    
    # raise Err on failure
    # shared storage is already mounted at self.storage.get_mountpoint() on all nodes
    def configure_nodes(self, nodes):
        # use self.utils.execute_remote() to execute commands on node
        # use self.utils.copy_to_host() to copy local files/dir to node
        
        if not self.is_enabled():
            return
        
        self.utils.log('Configuring service ' + self.service_name)
        
        ins_dir = self.storage.get_mountpoint()
        if ins_dir[len(ins_dir)-1] != '/':
            ins_dir += '/'
        conf_dir = ins_dir + 'conf.d/'
        www_dir = ins_dir + 'www/'
        html_dir = www_dir + 'html/'
        cgi_bin_dir = www_dir + 'cgi-bin/'
        
        
        
        ### shared data ###
        
        node = nodes[0]
        
        # create directory structure
        self.utils.execute_remote(node, 'mkdir', ['-p', conf_dir])
        self.utils.execute_remote(node, 'mkdir', ['-p', www_dir])
        self.utils.execute_remote(node, 'mkdir', ['-p', html_dir])
        self.utils.execute_remote(node, 'mkdir', ['-p', cgi_bin_dir])
        
        # main configuration
        config = '<Directory "%s">\n' % html_dir
        config += '\tAllowOverride None\n'
        config += '\tOptions None\n'
        config += '\tOrder allow,deny\n'
        config += '\tAllow from all\n'
        config += '</Directory>\n'
        config += '\n'
        config += '<Directory "%s">\n' % cgi_bin_dir
        config += '\tAllowOverride None\n'
        config += '\tOptions None\n'
        config += '\tOrder allow,deny\n'
        config += '\tAllow from all\n'
        config += '</Directory>\n'
        config += '\n'
        config += '<VirtualHost *:80>\n'
        config += '\tServerAdmin webmaster@dummy-host.example.com\n'
        config += '\tDocumentRoot %s\n' % html_dir
        config += '\tScriptAlias /cgi-bin/ "%s"\n' %  cgi_bin_dir
        config += '#\tServerName dummy-host.example.com\n'
        config += '#\tErrorLog logs/dummy-host.example.com-error_log\n'
        config += '#\tCustomLog logs/dummy-host.example.com-access_log common\n'
        config += '</VirtualHost>\n'
        conf_path = self.create_tmp_file(config)
        self.utils.copy_to_host(conf_path,
                                node,
                                conf_dir + 'clustered_www.conf')
        
        # config README
        readme = 'This directory holds Apache 2.0 module-specific configuration files;\n'
        readme += 'any files in this directory which have the ".conf" extension will be\n'
        readme += 'processed as Apache configuration files.\n'
        readme += '\n'
        readme += 'Files are processed in alphabetical order, so if using configuration\n'
        readme += 'directives which depend on, say, mod_perl being loaded, ensure that\n'
        readme += 'these are placed in a filename later in the sort order than "perl.conf".\n\n'
        readme_path = self.create_tmp_file(readme)
        self.utils.copy_to_host(readme_path,
                                node,
                                conf_dir + 'README')
        
        # welcome web page
        web_page = '<html>\n'
	web_page += '\t<head><title>Clustered Apache Web Server</title></head>\n'
	web_page += '\t<body>\n'
        web_page += '\t\t<h1>Welcome to the newly installed clustered Apache</h1>\n'
        web_page += '\t\t<br/>\n'
        web_page += '\t\t<br/>\n'
        web_page += '\t\t\tThis is a sample page\n'
        web_page += '\t\t<br/>\n'
        web_page += '\t\tThere is also a cgi <a href="/cgi-bin/sample">script</a>\n'
	web_page += '\t</body>\n'
        web_page += '</html>\n'
        web_page_path = self.create_tmp_file(web_page)
        self.utils.copy_to_host(web_page_path, 
                                node, 
                                html_dir + 'index.html')
        
        # sample cgi-bin
        cgi_bin_script = '#!/bin/bash\n'
        cgi_bin_script += '\n'
        cgi_bin_script += '\n'
        cgi_bin_script += 'echo \'Content-type: text/html\'\n'
        cgi_bin_script += 'echo\n'
        cgi_bin_script += 'echo\n'
        cgi_bin_script += '\n'
        cgi_bin_script += 'echo \n'
        cgi_bin_script += 'echo \'<html>\'\n'
        cgi_bin_script += 'echo \'<head><title>CGI Output</title></head>\'\n'
        cgi_bin_script += 'echo \'<body>\'\n'
        cgi_bin_script += 'echo \'<h1>Sample cgi script</h1>\'\n'
        cgi_bin_script += 'echo \'</body>\'\n'
        cgi_bin_script += 'echo \'</html>\'\n'
        cgi_bin_script += 'echo \n'
        cgi_bin_script += '\n'
        cgi_bin_script += '\n'
        cgi_bin_script_path = self.create_tmp_file(cgi_bin_script)
        self.utils.copy_to_host(cgi_bin_script_path, 
                                node, 
                                cgi_bin_dir + 'sample')
        
        # setup fs rights
        self.utils.execute_remote(node, 'chown', ['-R', 'apache:apache', ins_dir + '*'])
        self.utils.execute_remote(node, 'chmod', ['-R', '744', cgi_bin_dir + '*'])
        
        
        ### configure apache on all nodes ###
        
        include_statement = '# Include shared configuration stored at shared storage\n'
        include_statement += 'Include %s*.conf\n' % conf_dir
        redir_path = self.create_tmp_file(include_statement)
        for node in nodes:
            # conf.d redirection
            remote_path = '/etc/httpd/conf.d/zz_cluster_includes.conf'
            self.utils.copy_to_host(redir_path,
                                    node,
                                    remote_path)
            
            # make sure httpd doesn't start
            self.utils.execute_remote(node, 'chkconfig', ['--del', 'httpd'])
            self.utils.execute_remote(node, 'service', ['httpd', 'stop'])
            
            # FIXME: disable SELinux
            se_conf, e, s = self.utils.execute_remote(node,
                                                      'cat',
                                                      ['/etc/selinux/config'])
            new_se_conf = ''
            for line in se_conf.splitlines():
                l = line.strip()
                if len(l) == 0:
                    new_se_conf += line
                elif l[0] == '#':
                    new_se_conf += line
                else:
                    if 'SELINUX' not in line:
                        new_se_conf += line
                    else:
                        words = l.split('=')
                        if len(words) < 2:
                            new_se_conf += line
                        elif words[0].strip().upper() == 'SELINUX' and words[1].strip().lower() != 'permissive':
                            new_se_conf += '#' + line + '\n'
                            new_se_conf += 'SELINUX=permissive'
                        else:
                            new_se_conf += line
                new_se_conf += '\n'
                pass
            selinux_path = self.create_tmp_file(new_se_conf)
            self.utils.copy_to_host(selinux_path, 
                                    node, 
                                    '/etc/selinux/config')
            self.utils.execute_remote(node, 'setenforce', ['0'])
            pass
        
        
        ### DONE ###
        
        self.remove_tmp_files()
        return
    
    
    # message displayed at the end of installation
    def get_post_install_messages(self):
        if self.is_enabled():
            return ['SELinux has been disabled on all nodes']
        else:
            return []
    
    
    
    
    ### internal ###
    
    def enabled_clicked(self, *args):
        state = self.enabled.get_active()
        self.storage.set_enabled(state)
        self.ip.set_enabled(state)
        self.frame.set_sensitive(state)
    
    def is_enabled(self):
        return self.enabled.get_active()
    
    def create_tmp_file(self, msg=''):
        tmp_file = '/tmp/apache_conf_temp'
        while os.access(tmp_file, os.F_OK):
            tmp_file += '_newer'
        self.__tmp_files.append(tmp_file)
        file = open(tmp_file, 'w')
        file.write(msg)
        file.close()
        return tmp_file
    def remove_tmp_files(self):
        for file in self.__tmp_files:
            os.remove(file)
