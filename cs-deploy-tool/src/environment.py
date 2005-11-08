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
import sys

from constants import *

from execute import execWithCaptureErrorStatus
from err import Err


class Environment:
    
    def __init__(self, logfile_path):
        self.__ssh_bin    = '/usr/bin/ssh'
        self.__scp_bin    = '/usr/bin/scp'
        self.__ssh_add    = '/usr/bin/ssh-add'
        self.__ssh_keygen = '/usr/bin/ssh-keygen'
        self.__execute_basic = execWithCaptureErrorStatus
        self.__authorized_nodes_list = []
        self.__log_init(logfile_path)
        self.__init_authentication_environ()
    def __del__(self):
        self.unauthorize_to_all()
        
    
    def log(self, msg, stdout=False):
        open(self.__log_file, 'a').write(msg + '\n')
        if stdout:
            print msg
    def get_log_file_path(self):
        if self.__log_file[0] == '/':
            return self.__log_file
        o, e, s = self.execute('pwd', [])
        pwd = o.strip()
        return pwd + '/' + self.__log_file
    
    def execute(self, bin, args):
        l = [bin]
        for arg in args:
            l.append(arg)
        o, e, s = self.__execute_basic(bin, l)
        self.log('exec:\n' + str(l) + ':\nstdout:\n' + o + '\nstderr:\n' + e + '\n')
        return o, e, s
    
    def execute_remote(self, host, bin, args):
        if host not in self.__authorized_nodes_list:
            if self.__authorize_to_node(host) != True:
                raise Err('Unable to authorize to ' + host)
        l = ['root@' + host, bin]
        for arg in args:
            l.append(arg)
        o, e, s = self.execute(self.__ssh_bin, l)
        #self.log('exec on ' + host + ':\n' + str(l) + ':\nstdout:\n' + o + '\nstderr:\n' + e + '\n')
        return o, e, s
    
    def copy_to_host(self, localpath, remote_host, remotepath):
        if remote_host not in self.__authorized_nodes_list:
            if self.__authorize_to_node(remote_host) != True:
                raise Err('Unable to authorize to ' + remote_host)
        l = ['-r', localpath, 'root@' + remote_host + ':' + remotepath]
        o, e, s = self.execute(self.__scp_bin, l)
        self.log('copy to ' + remote_host + ':\n' + str(l) + ':\nout:\n' + o + '\nerror:\n' + e + '\n')
        return e, s
    
    def unauthorize_to_all(self):
        for node in self.__authorized_nodes_list[:]:
            self.__unauthorize_to_node(node)
    
    def exit(self, num=0):
        self.unauthorize_to_all()
        sys.exit(num)
        
    
    ### internal ###
    
    def __init_authentication_environ(self):
        key_path = 'temp_key_dsa'
        while(os.access(key_path, os.F_OK)):
            key_path += '_newer'
        self.execute(self.__ssh_keygen, ['-q', '-t', 'dsa', '-N', '""', '-f', key_path])
        o, e, s = self.execute(self.__ssh_add, [key_path])
        self.__ssh_key_public = open(key_path + '.pub').read().strip()
        os.remove(key_path)
        os.remove(key_path + '.pub')
        if s != 0:
            raise 'Unable to initialize authentication environment'
        
    def __authorize_to_node(self, node):
        if node in self.__authorized_nodes_list:
            return True
        self.log('authorizing to ' + node)
        o, e, s = self.execute(INSTALLDIR + '/authorize_to_node',
                               ['"' + self.__ssh_key_public + '"', node])
        success = (s == 0)
        if success:
            self.__authorized_nodes_list.append(node)
            self.log('success')
        else:
            self.log('failure')
        return success
    def __unauthorize_to_node(self, node):
        if node not in self.__authorized_nodes_list:
            return True
        self.log('unauthorizing to ' + node)
        o, e, s = self.execute(INSTALLDIR + '/unauthorize_to_node',
                               ['"' + self.__ssh_key_public + '"', node])
        success = (s == 0)
        if success:
            self.__authorized_nodes_list.remove(node)
            self.log('success')
        else:
            self.log('failure')
        return success
    
    def __log_init(self, path):
        self.__log_file = path
        self.execute('rm', ['-f', self.__log_file])
        self.log('startup')
    
    
